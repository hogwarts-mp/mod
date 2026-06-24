// Copyright Epic Games, Inc. All Rights Reserved.

#include "ProfilingDebugging/TraceAuxiliary.h"
#include "Trace/Trace.h"

#if UE_TRACE_ENABLED

#include "BuildSettings.h"
#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/StringView.h"
#include "Containers/UnrealString.h"
#include "CoreGlobals.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/App.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/CString.h"
#include "Misc/DateTime.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "ProfilingDebugging/PlatformFileTrace.h"
#include "String/ParseTokens.h"
#include "Templates/UnrealTemplate.h"
#include "Trace/Trace.inl"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"
#endif

////////////////////////////////////////////////////////////////////////////////
enum class ETraceConnectType
{
	Network,
	File
};

////////////////////////////////////////////////////////////////////////////////
class FTraceAuxiliaryImpl
{
public:
	const TCHAR*			GetDest() const;
	template <class T> void	ReadChannels(T&& Callback) const;
	void					AddChannels(const TCHAR* ChannelList);
	bool					Connect(ETraceConnectType Type, const TCHAR* Parameter);
	void					EnableChannels();
	void					DisableChannels();

private:
	enum class EState : uint8
	{
		None,
		Tracing,
		Stopped,
	};

	struct FChannel
	{
		FString				Name;
		bool				bActive = false;
	};

	void					AddChannel(const TCHAR* Name);
	void					AddChannels(const TCHAR* Name, bool bResolvePresets);
	void					EnableChannel(FChannel& Channel);
	bool					SendToHost(const TCHAR* Host);
	bool					WriteToFile(const TCHAR* Path=nullptr);

	TMap<uint32, FChannel>	Channels;
	FString					TraceDest;
	EState					State = EState::None;
};

static FTraceAuxiliaryImpl GTraceAuxiliary;

////////////////////////////////////////////////////////////////////////////////
void FTraceAuxiliaryImpl::AddChannels(const TCHAR* ChannelList)
{
	AddChannels(ChannelList, true);
}

////////////////////////////////////////////////////////////////////////////////
void FTraceAuxiliaryImpl::AddChannels(const TCHAR* ChannelList, bool bResolvePresets)
{
	UE::String::ParseTokens(ChannelList, TEXT(","), [this, bResolvePresets] (const FStringView& Token)
	{
		TCHAR Name[80];
		const size_t ChannelNameSize = Token.CopyString(Name, UE_ARRAY_COUNT(Name) - 1);
		Name[ChannelNameSize] = '\0';

		if (bResolvePresets)
		{
			FString Value;
			if (GConfig->GetString(TEXT("Trace.ChannelPresets"), Name, Value, GEngineIni))
			{
				AddChannels(*Value, false);
				return;
			}
		}

		AddChannel(Name);
	});
}

////////////////////////////////////////////////////////////////////////////////
void FTraceAuxiliaryImpl::AddChannel(const TCHAR* Name)
{
	uint32 Hash = 5381;
	for (const TCHAR* c = Name; *c; ++c)
	{
		uint32 LowerC = *c | 0x20;
        Hash = ((Hash << 5) + Hash) + LowerC;
	}

	if (Channels.Find(Hash) != nullptr)
	{
		return;
	}

	FChannel& Value = Channels.Add(Hash, {});
	Value.Name = Name;

	if (State >= EState::Tracing)
	{
		EnableChannel(Value);
	}
}

////////////////////////////////////////////////////////////////////////////////
bool FTraceAuxiliaryImpl::Connect(ETraceConnectType Type, const TCHAR* Parameter)
{
	// Connect/write to file. But only if we're not already sending/writing
	bool bConnected = Trace::IsTracing();
	if (!bConnected)
	{
		if (Type == ETraceConnectType::Network)
		{
			bConnected = SendToHost(Parameter);
		}

		else if (Type == ETraceConnectType::File)
		{
			bConnected = WriteToFile(Parameter);
		}
	}

	if (!bConnected)
	{
		return false;
	}

	// We're now connected. If we don't appear to have any channels we'll set
	// some defaults for the user. Less futzing.
	if (!Channels.Num())
	{
		FString Value;
		if (!GConfig->GetString(TEXT("Trace.ChannelPresets"), TEXT("Default"), Value, GEngineIni))
		{
			Value = TEXT("cpu,frame,log,bookmark");
		}

		AddChannels(*Value);
	}

	EnableChannels();

	State = EState::Tracing;
	return true;
}

////////////////////////////////////////////////////////////////////////////////
void FTraceAuxiliaryImpl::EnableChannel(FChannel& Channel)
{
	if (Channel.bActive)
	{
		return;
	}

	// Channel names have been provided by the user and may not exist yet. As
	// we want to maintain bActive accurately (channels toggles are reference
	// counted), we will first check Trace knows of the channel.
	if (!Trace::IsChannel(*Channel.Name))
	{
		return;
	}

	Trace::ToggleChannel(*Channel.Name, true);
	Channel.bActive = true;
}

////////////////////////////////////////////////////////////////////////////////
void FTraceAuxiliaryImpl::EnableChannels()
{
	for (auto& ChannelPair : Channels)
	{
		EnableChannel(ChannelPair.Value);
	}
}

////////////////////////////////////////////////////////////////////////////////
void FTraceAuxiliaryImpl::DisableChannels()
{
	for (auto& ChannelPair : Channels)
	{
		FChannel& Channel = ChannelPair.Value;
		if (Channel.bActive)
		{
			Trace::ToggleChannel(*Channel.Name, false);
			Channel.bActive = false;
		}
	}
}

////////////////////////////////////////////////////////////////////////////////
bool FTraceAuxiliaryImpl::SendToHost(const TCHAR* Host)
{
	if (!Trace::SendTo(Host))
	{
		UE_LOG(LogCore, Warning, TEXT("Unable to trace to host '%s'"), Host);
		return false;
	}

	TraceDest = Host;
	return true;
}

////////////////////////////////////////////////////////////////////////////////
bool FTraceAuxiliaryImpl::WriteToFile(const TCHAR* Path)
{
	if (Path == nullptr || *Path == '\0')
	{
		FString Name = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S.utrace"));
		return WriteToFile(*Name);
	}

	FString WritePath;

	// If there's no slash in the path, we'll put it in the profiling directory
	if (FCString::Strchr(Path, '\\') == nullptr && FCString::Strchr(Path, '/') == nullptr)
	{
		WritePath = FPaths::ProfilingDir();
		WritePath += Path;
	}
	else
	{
		WritePath = Path;
	}

	// The user may not have provided a suitable extension
	if (!WritePath.EndsWith(".utrace"))
	{
		WritePath += ".utrace";
	}

	IFileManager& FileManager = IFileManager::Get();

	// Ensure we can write the trace file appropriately
	FString WriteDir = FPaths::GetPath(WritePath);
	if (!FileManager.MakeDirectory(*WriteDir, true))
	{
		UE_LOG(LogCore, Warning, TEXT("Failed to create directory '%s'"), *WriteDir);
		return false;
	}

	if (FileManager.FileExists(*WritePath))
	{
		UE_LOG(LogCore, Warning, TEXT("Trace file '%s' already exists"), *WritePath);
		return false;
	}

	// Finally, tell trace to write the trace to a file.
	FString NativePath = FileManager.ConvertToAbsolutePathForExternalAppForWrite(*WritePath);
	if (!Trace::WriteTo(*NativePath))
	{
		UE_LOG(LogCore, Warning, TEXT("Unable to trace to file '%s'"), *WritePath);
		return false;
	}

	TraceDest = MoveTemp(NativePath);
	return true;
}

////////////////////////////////////////////////////////////////////////////////
const TCHAR* FTraceAuxiliaryImpl::GetDest() const
{
	return *TraceDest;
}

////////////////////////////////////////////////////////////////////////////////
template <class T>
void FTraceAuxiliaryImpl::ReadChannels(T&& Callback) const
{
	for (const auto& ChannelPair : Channels)
	{
		Callback(*(ChannelPair.Value.Name));
	}
}



////////////////////////////////////////////////////////////////////////////////
static void TraceAuxiliaryStart(const TArray<FString>& Args)
{
	if (Args.Num() > 0)
	{
		GTraceAuxiliary.AddChannels(*(Args[0]));
	}

	if (!GTraceAuxiliary.Connect(ETraceConnectType::File, nullptr))
	{
		UE_LOG(LogConsoleResponse, Warning, TEXT("Failed to start tracing to a file"));
		return;
	}

	// Give the user some feedback that everything's underway.
	FString Channels;
	GTraceAuxiliary.ReadChannels([&Channels] (const TCHAR* Channel)
	{
		if (Channels.Len())
		{
			Channels += TEXT(",");
		}

		Channels += Channel;
	});
	UE_LOG(LogConsoleResponse, Log, TEXT("Tracing to; %s"), GTraceAuxiliary.GetDest());
	UE_LOG(LogConsoleResponse, Log, TEXT("Trace channels; %s"), *Channels);
}

////////////////////////////////////////////////////////////////////////////////
static void TraceAuxiliaryStop()
{
	GTraceAuxiliary.DisableChannels();
	UE_LOG(LogConsoleResponse, Log, TEXT("Tracing stopped. Use 'Trace.Start' to resume"));
}

////////////////////////////////////////////////////////////////////////////////
static FAutoConsoleCommand TraceAuxiliaryStartCmd(
	TEXT("Trace.Start"),
	TEXT(
		"Begin tracing profiling events to a file; Trace.Start [ChannelSet]"
		" where ChannelSet is either comma-separated list of trace channels, a Config/Trace.ChannelPresets key, or optional."
	),
	FConsoleCommandWithArgsDelegate::CreateStatic(TraceAuxiliaryStart)
);

////////////////////////////////////////////////////////////////////////////////
static FAutoConsoleCommand TraceAuxiliaryStopCmd(
	TEXT("Trace.Stop"),
	TEXT("Stops tracing profiling events"),
	FConsoleCommandDelegate::CreateStatic(TraceAuxiliaryStop)
);

#endif // UE_TRACE_ENABLED



////////////////////////////////////////////////////////////////////////////////
UE_TRACE_EVENT_BEGIN(Diagnostics, Session2, Important)
	UE_TRACE_EVENT_FIELD(Trace::AnsiString, Platform)
	UE_TRACE_EVENT_FIELD(Trace::AnsiString, AppName)
	UE_TRACE_EVENT_FIELD(Trace::WideString, CommandLine)
	UE_TRACE_EVENT_FIELD(Trace::WideString, Branch)
	UE_TRACE_EVENT_FIELD(Trace::WideString, BuildVersion)
	UE_TRACE_EVENT_FIELD(uint32, Changelist)
	UE_TRACE_EVENT_FIELD(uint8, ConfigurationType)
	UE_TRACE_EVENT_FIELD(uint8, TargetType)
UE_TRACE_EVENT_END()

////////////////////////////////////////////////////////////////////////////////
void FTraceAuxiliary::Initialize(const TCHAR* CommandLine)
{
#if UE_TRACE_ENABLED
	// Trace out information about this session. This is done before initialisation
	// so that it is always sent (all channels are enabled prior to initialisation)
	UE_TRACE_LOG(Diagnostics, Session2, Trace::TraceLogChannel)
		<< Session2.Platform(PREPROCESSOR_TO_STRING(UBT_COMPILED_PLATFORM))
		<< Session2.AppName(UE_APP_NAME)
		<< Session2.CommandLine(CommandLine)
		<< Session2.Branch(BuildSettings::GetBranchName())
		<< Session2.BuildVersion(BuildSettings::GetBuildVersion())
		<< Session2.Changelist(BuildSettings::GetCurrentChangelist())
		<< Session2.ConfigurationType(uint8(FApp::GetBuildConfiguration()))
		<< Session2.TargetType(uint8(FApp::GetBuildTargetType()));

	// Initialize Trace
	Trace::FInitializeDesc Desc;
	Desc.bUseWorkerThread = FPlatformProcess::SupportsMultithreading();
	Trace::Initialize(Desc);

	FCoreDelegates::OnEndFrame.AddStatic(Trace::Update);
	FModuleManager::Get().OnModulesChanged().AddLambda([](FName Name, EModuleChangeReason Reason){
		if (Reason == EModuleChangeReason::ModuleLoaded)
		{
			EnableChannels();
		}
	});

	// Extract an explicit channel set from the command line.
	FString Parameter;
	if (FParse::Value(CommandLine, TEXT("-trace="), Parameter, false))
	{
		GTraceAuxiliary.AddChannels(*Parameter);
		GTraceAuxiliary.EnableChannels();
	}

	// Attempt to send trace data somewhere from the command line
	if (FParse::Value(CommandLine, TEXT("-tracehost="), Parameter))
	{
		GTraceAuxiliary.Connect(ETraceConnectType::Network, *Parameter);
	}
	else if (FParse::Value(CommandLine, TEXT("-tracefile="), Parameter))
	{
		GTraceAuxiliary.Connect(ETraceConnectType::File, *Parameter);
	}
	else if (FParse::Param(CommandLine, TEXT("tracefile")))
	{
		GTraceAuxiliary.Connect(ETraceConnectType::File, nullptr);
	}

	Trace::ThreadRegister(TEXT("GameThread"), FPlatformTLS::GetCurrentThreadId(), -1);

#endif
}

////////////////////////////////////////////////////////////////////////////////
void FTraceAuxiliary::EnableChannels()
{
#if UE_TRACE_ENABLED
	GTraceAuxiliary.EnableChannels();
#endif
}

////////////////////////////////////////////////////////////////////////////////
void FTraceAuxiliary::TryAutoConnect()
{
#if UE_TRACE_ENABLED
	#if PLATFORM_WINDOWS
	// If we can detect a named event then we can try and auto-connect to UnrealInsights.
	HANDLE KnownEvent = ::OpenEvent(EVENT_ALL_ACCESS, false, TEXT("Local\\UnrealInsightsRecorder"));
	if (KnownEvent != nullptr)
	{
		GTraceAuxiliary.Connect(ETraceConnectType::Network, TEXT("127.0.0.1"));
		::CloseHandle(KnownEvent);
	}
	#endif
#endif
}
