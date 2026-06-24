// Copyright Epic Games, Inc. All Rights Reserved.

#include "GenericPlatform/GenericPlatformMisc.h"
#include "Misc/AssertionMacros.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/CriticalSection.h"
#include "Misc/ScopeRWLock.h"
#include "Math/UnrealMathUtility.h"
#include "HAL/UnrealMemory.h"
#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Logging/LogMacros.h"
#include "CoreGlobals.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Internationalization/Text.h"
#include "Internationalization/Internationalization.h"
#include "Misc/Guid.h"
#include "Math/Color.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/App.h"
#include "GenericPlatform/GenericPlatformChunkInstall.h"
#include "HAL/FileManagerGeneric.h"
#include "Misc/VarargsHelper.h"
#include "Misc/SecureHash.h"
#include "HAL/ExceptionHandling.h"
#include "GenericPlatform/GenericPlatformCrashContext.h"
#include "GenericPlatform/GenericPlatformDriver.h"
#include "ProfilingDebugging/ExternalProfiler.h"
#include "HAL/LowLevelMemTracker.h"
#include "Templates/Function.h"
#include "Modules/ModuleManager.h"
#include "Misc/LazySingleton.h"

#include "Misc/UProjectInfo.h"
#include "Internationalization/Culture.h"

#if UE_ENABLE_ICU
	THIRD_PARTY_INCLUDES_START
		#include <unicode/locid.h>
	THIRD_PARTY_INCLUDES_END
#endif

DEFINE_LOG_CATEGORY_STATIC(LogGenericPlatformMisc, Log, All);

/** Holds an override path if a program has special needs */
FString OverrideProjectDir;

/** Hooks for moving ClipboardCopy and ClipboardPaste into FPlatformApplicationMisc */
CORE_API void (*ClipboardCopyShim)(const TCHAR* Text) = nullptr;
CORE_API void (*ClipboardPasteShim)(FString& Dest) = nullptr;

/* EBuildConfigurations interface
 *****************************************************************************/

bool LexTryParseString(EBuildConfiguration& OutConfiguration, const TCHAR* Configuration)
{
	if (FCString::Stricmp(Configuration, TEXT("Debug")) == 0)
	{
		OutConfiguration = EBuildConfiguration::Debug;
		return true;
	}
	else if (FCString::Stricmp(Configuration, TEXT("DebugGame")) == 0)
		{
		OutConfiguration = EBuildConfiguration::DebugGame;
		return true;
		}
	else if (FCString::Stricmp(Configuration, TEXT("Development")) == 0)
		{
		OutConfiguration = EBuildConfiguration::Development;
		return true;
		}
	else if (FCString::Stricmp(Configuration, TEXT("Shipping")) == 0)
		{
		OutConfiguration = EBuildConfiguration::Shipping;
		return true;
		}
	else if(FCString::Stricmp(Configuration, TEXT("Test")) == 0)
		{
		OutConfiguration = EBuildConfiguration::Test;
		return true;
		}
	else if(FCString::Stricmp(Configuration, TEXT("Unknown")) == 0)
		{
		OutConfiguration = EBuildConfiguration::Unknown;
		return true;
	}
	else
	{
		OutConfiguration = EBuildConfiguration::Unknown;
		return false;
		}
	}

const TCHAR* LexToString( EBuildConfiguration Configuration )
	{
		switch (Configuration)
		{
	case EBuildConfiguration::Debug:
				return TEXT("Debug");
	case EBuildConfiguration::DebugGame:
				return TEXT("DebugGame");
	case EBuildConfiguration::Development:
				return TEXT("Development");
	case EBuildConfiguration::Shipping:
				return TEXT("Shipping");
	case EBuildConfiguration::Test:
				return TEXT("Test");
			default:
				return TEXT("Unknown");
		}
	}

namespace EBuildConfigurations
{
	EBuildConfiguration FromString( const FString& Configuration )
	{
		EBuildConfiguration Result;
		LexTryParseString(Result, *Configuration);
		return Result;
	}

	const TCHAR* ToString( EBuildConfiguration Configuration )
	{
		return LexToString(Configuration);
	}

	FText ToText( EBuildConfiguration Configuration )
	{
		switch (Configuration)
		{
		case EBuildConfiguration::Debug:
			return NSLOCTEXT("UnrealBuildConfigurations", "DebugName", "Debug");

		case EBuildConfiguration::DebugGame:
			return NSLOCTEXT("UnrealBuildConfigurations", "DebugGameName", "DebugGame");

		case EBuildConfiguration::Development:
			return NSLOCTEXT("UnrealBuildConfigurations", "DevelopmentName", "Development");

		case EBuildConfiguration::Shipping:
			return NSLOCTEXT("UnrealBuildConfigurations", "ShippingName", "Shipping");

		case EBuildConfiguration::Test:
			return NSLOCTEXT("UnrealBuildConfigurations", "TestName", "Test");

		default:
			return NSLOCTEXT("UnrealBuildConfigurations", "UnknownName", "Unknown");
		}
	}
}


/* EBuildTargetType functions
 *****************************************************************************/

bool LexTryParseString(EBuildTargetType& OutType, const TCHAR* Type)
{
	if (FCString::Strcmp(Type, TEXT("Editor")) == 0)
	{
		OutType = EBuildTargetType::Editor;
		return true;
	}
	else if (FCString::Strcmp(Type, TEXT("Game")) == 0)
	{
		OutType = EBuildTargetType::Game;
		return true;
	}
	else if (FCString::Strcmp(Type, TEXT("Server")) == 0)
{
		OutType = EBuildTargetType::Server;
		return true;
	}
	else if (FCString::Strcmp(Type, TEXT("Client")) == 0)
	{
		OutType = EBuildTargetType::Client;
		return true;
	}
	else if (FCString::Strcmp(Type, TEXT("Program")) == 0)
		{
		OutType = EBuildTargetType::Program;
		return true;
		}
	else if (FCString::Strcmp(Type, TEXT("Unknown")) == 0)
		{
		OutType = EBuildTargetType::Unknown;
		return true;
		}
	else
		{
		OutType = EBuildTargetType::Unknown;
		return false;
		}
	}

const TCHAR* LexToString(EBuildTargetType Type)
	{
	switch (Type)
		{
	case EBuildTargetType::Editor:
				return TEXT("Editor");
	case EBuildTargetType::Game:
				return TEXT("Game");
	case EBuildTargetType::Server:
				return TEXT("Server");
	case EBuildTargetType::Client:
		return TEXT("Client");
	case EBuildTargetType::Program:
		return TEXT("Program");
			default:
				return TEXT("Unknown");
		}
	}

EBuildTargetType EBuildTargets::FromString(const FString& Target)
{
	EBuildTargetType Type;
	LexTryParseString(Type, *Target);
	return Type;
}

const TCHAR* EBuildTargets::ToString(EBuildTargetType Target)
{
	return LexToString(Target);
}

FString FSHA256Signature::ToString() const
{
	FString LocalHashStr;
	for (int Idx = 0; Idx < 32; Idx++)
	{
		LocalHashStr += FString::Printf(TEXT("%02x"), Signature[Idx]);
	}
	return LocalHashStr;
}

/* ENetworkConnectionType interface
 *****************************************************************************/

    const TCHAR* LexToString( ENetworkConnectionType Target )
    {
        switch (Target)
        {
            case ENetworkConnectionType::None:
                return TEXT("None");
                
            case ENetworkConnectionType::AirplaneMode:
                return TEXT("AirplaneMode");
                
            case ENetworkConnectionType::Cell:
                return TEXT("Cell");
                
            case ENetworkConnectionType::WiFi:
                return TEXT("WiFi");
                
            case ENetworkConnectionType::Ethernet:
                return TEXT("Ethernet");
                
			case ENetworkConnectionType::Bluetooth:
				return TEXT("Bluetooth");

			case ENetworkConnectionType::WiMAX:
				return TEXT("WiMAX");

			default:
                return TEXT("Unknown");
        }
    }

/* FGenericPlatformMisc interface
 *****************************************************************************/

#if !UE_BUILD_SHIPPING
	bool FGenericPlatformMisc::bShouldPromptForRemoteDebugging = false;
	bool FGenericPlatformMisc::bPromptForRemoteDebugOnEnsure = false;
#endif	//#if !UE_BUILD_SHIPPING

struct FGenericPlatformMisc::FStaticData
{
	FString         RootDir;
	TArray<FString> AdditionalRootDirectories;
	FRWLock         AdditionalRootDirectoriesLock;
	FString         EngineDirectory;
	FString         LaunchDir;
	FString         ProjectDir;
	FString         GamePersistentDownloadDir;
};

FString FGenericPlatformMisc::GetEnvironmentVariable(const TCHAR* VariableName)
{
	return FString();
}

void FGenericPlatformMisc::SetEnvironmentVar(const TCHAR* VariableName, const TCHAR* Value)
{
	UE_LOG(LogGenericPlatformMisc, Error, TEXT("SetEnvironmentVar not implemented for this platform: %s = %s"), VariableName, Value);
}

const TCHAR* FGenericPlatformMisc::GetPathVarDelimiter()
{
	return TEXT(";");
}

TArray<uint8> FGenericPlatformMisc::GetMacAddress()
{
	return TArray<uint8>();
}

FString FGenericPlatformMisc::GetMacAddressString()
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	TArray<uint8> MacAddr = FPlatformMisc::GetMacAddress();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	FString Result;
	for (TArray<uint8>::TConstIterator it(MacAddr);it;++it)
	{
		Result += FString::Printf(TEXT("%02x"),*it);
	}
	return Result;
}

FString FGenericPlatformMisc::GetHashedMacAddressString()
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	// ensure empty MAC addresses don't return a hash of zero bytes.
	FString MacAddr = FPlatformMisc::GetMacAddressString();
	if (!MacAddr.IsEmpty())
	{
		return FMD5::HashAnsiString(*MacAddr);
	}
	else
	{
		return FString();
	}
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

FString FGenericPlatformMisc::GetDeviceId()
{
	// not implemented at the base level. Each platform must decide how to implement this, if possible.
	return FString();
}

FString FGenericPlatformMisc::GetUniqueAdvertisingId()
{
	// this has no meaning generically, primarily used for attribution on mobile platforms
	return FString();
}

void FGenericPlatformMisc::SubmitErrorReport( const TCHAR* InErrorHist, EErrorReportMode::Type InMode )
{
	if ((!FPlatformMisc::IsDebuggerPresent() || GAlwaysReportCrash) && !FParse::Param(FCommandLine::Get(), TEXT("CrashForUAT")))
	{
		if ( GUseCrashReportClient )
		{
			int32 FromCommandLine = 0;
			FParse::Value( FCommandLine::Get(), TEXT("AutomatedPerfTesting="), FromCommandLine );
			if (FApp::IsUnattended() && FromCommandLine != 0 && FParse::Param(FCommandLine::Get(), TEXT("KillAllPopUpBlockingWindows")))
			{
				UE_LOG(LogGenericPlatformMisc, Error, TEXT("This platform does not implement KillAllPopUpBlockingWindows"));
			}
		}
		else
		{
			UE_LOG(LogGenericPlatformMisc, Error, TEXT("This platform cannot submit a crash report. Report was:\n%s"), InErrorHist);
		}
	}
}


FString FGenericPlatformMisc::GetCPUVendor()
{
	// Not implemented cross-platform. Each platform may or may not choose to implement this.
	return FString( TEXT( "GenericCPUVendor" ) );
}

FString FGenericPlatformMisc::GetCPUBrand()
{
	// Not implemented cross-platform. Each platform may or may not choose to implement this.
	return FString( TEXT( "GenericCPUBrand" ) );
}

FString FGenericPlatformMisc::GetCPUChipset()
{
	return FString( TEXT( "Unknown" ) );
}

uint32 FGenericPlatformMisc::GetCPUInfo()
{
	// Not implemented cross-platform. Each platform may or may not choose to implement this.
	return 0;
}

bool FGenericPlatformMisc::HasNonoptionalCPUFeatures()
{
	// Not implemented cross-platform. Each platform may or may not choose to implement this.
	return false;
}

bool FGenericPlatformMisc::NeedsNonoptionalCPUFeaturesCheck()
{
	// This is opt in on a per platform basis
	return false;
}

FString FGenericPlatformMisc::GetPrimaryGPUBrand()
{
	// Not implemented cross-platform. Each platform may or may not choose to implement this.
	return FString( TEXT( "GenericGPUBrand" ) );
}

FString FGenericPlatformMisc::GetDeviceMakeAndModel()
{
	const FString CPUVendor = FPlatformMisc::GetCPUVendor().TrimStartAndEnd();
	const FString CPUBrand = FPlatformMisc::GetCPUBrand().TrimStartAndEnd();
	const FString CPUChipset = FPlatformMisc::GetCPUChipset().TrimStartAndEnd();
	if (!CPUChipset.Equals(TEXT("Unknown")))
	{
		if (CPUBrand.Contains(TEXT("|")))
		{
			FString FixedCPUBrand = CPUBrand.Replace(TEXT("|"), TEXT("_"));
			return FString::Printf(TEXT("%s|%s|%s"), *CPUVendor, *FixedCPUBrand, *CPUChipset);
		}
		return FString::Printf(TEXT("%s|%s|%s"), *CPUVendor, *CPUBrand, *CPUChipset);
	}
	return FString::Printf(TEXT("%s|%s"), *CPUVendor, *CPUBrand);
}

FGPUDriverInfo FGenericPlatformMisc::GetGPUDriverInfo(const FString& DeviceDescription)
{
	return FGPUDriverInfo();
}

void FGenericPlatformMisc::GetOSVersions( FString& out_OSVersionLabel, FString& out_OSSubVersionLabel )
{
	// Not implemented cross-platform. Each platform may or may not choose to implement this.
	out_OSVersionLabel = FString( TEXT( "GenericOSVersionLabel" ) );
	out_OSSubVersionLabel = FString( TEXT( "GenericOSSubVersionLabel" ) );
}


FString FGenericPlatformMisc::GetOSVersion()
{
	return FString();
}

bool FGenericPlatformMisc::GetDiskTotalAndFreeSpace( const FString& InPath, uint64& TotalNumberOfBytes, uint64& NumberOfFreeBytes )
{
	// Not implemented cross-platform. Each platform may or may not choose to implement this.
	TotalNumberOfBytes = 0;
	NumberOfFreeBytes = 0;
	return false;
}


void FGenericPlatformMisc::MemoryBarrier()
{
}

void FGenericPlatformMisc::RaiseException(uint32 ExceptionCode)
{
	/** This is the last place to gather memory stats before exception. */
	FGenericCrashContext::SetMemoryStats(FPlatformMemory::GetStats());

#if HACK_HEADER_GENERATOR && !PLATFORM_EXCEPTIONS_DISABLED
	// We want Unreal Header Tool to throw an exception but in normal runtime code 
	// we don't support exception handling
	throw(ExceptionCode);
#else	
	*((uint32*)3) = ExceptionCode;
#endif
}

void FGenericPlatformMisc::BeginNamedEvent(const struct FColor& Color, const ANSICHAR* Text)
{
#if UE_EXTERNAL_PROFILING_ENABLED
	//If there's an external profiler attached, trigger its scoped event.
	FExternalProfiler* CurrentProfiler = FActiveExternalProfilerBase::GetActiveProfiler();
	if (CurrentProfiler != NULL)
	{
		CurrentProfiler->StartScopedEvent(ANSI_TO_TCHAR(Text));
	}
#endif
}

void FGenericPlatformMisc::BeginNamedEvent(const struct FColor& Color, const TCHAR* Text)
{
#if UE_EXTERNAL_PROFILING_ENABLED
	//If there's an external profiler attached, trigger its scoped event.
	FExternalProfiler* CurrentProfiler = FActiveExternalProfilerBase::GetActiveProfiler();

	if (CurrentProfiler != NULL)
	{
		CurrentProfiler->StartScopedEvent(Text);
	}
#endif
}

void FGenericPlatformMisc::EndNamedEvent()
{
#if UE_EXTERNAL_PROFILING_ENABLED
	//If there's an external profiler attached, trigger its scoped event.
	FExternalProfiler* CurrentProfiler = FActiveExternalProfilerBase::GetActiveProfiler();

	if (CurrentProfiler != NULL)
	{
		CurrentProfiler->EndScopedEvent();
	}
#endif
}

bool FGenericPlatformMisc::SetStoredValues(const FString& InStoreId, const FString& InSectionName, const TMap<FString, FString>& InKeyValues)
{
	for (const TPair<FString, FString>& InKeyValue : InKeyValues)
	{
		if (!FPlatformMisc::SetStoredValue(InStoreId, InSectionName, InKeyValue.Key, InKeyValue.Value))
		{
			return false;
		}
	}

	return true;
}

bool FGenericPlatformMisc::SetStoredValue(const FString& InStoreId, const FString& InSectionName, const FString& InKeyName, const FString& InValue)
{
	check(!InStoreId.IsEmpty());
	check(!InSectionName.IsEmpty());
	check(!InKeyName.IsEmpty());

	// This assumes that FPlatformProcess::ApplicationSettingsDir() returns a user-specific directory; it doesn't on Windows, but Windows overrides this behavior to use the registry
	const FString ConfigPath = FString(FPlatformProcess::ApplicationSettingsDir()) / InStoreId / FString(TEXT("KeyValueStore.ini"));
		
	FConfigFile ConfigFile;
	ConfigFile.Read(ConfigPath);

	FConfigSection& Section = ConfigFile.FindOrAdd(InSectionName);

	FConfigValue& KeyValue = Section.FindOrAdd(*InKeyName);
	KeyValue = FConfigValue(InValue);

	ConfigFile.Dirty = true;
	return ConfigFile.Write(ConfigPath);
}

bool FGenericPlatformMisc::GetStoredValue(const FString& InStoreId, const FString& InSectionName, const FString& InKeyName, FString& OutValue)
{
	check(!InStoreId.IsEmpty());
	check(!InSectionName.IsEmpty());
	check(!InKeyName.IsEmpty());

	// This assumes that FPlatformProcess::ApplicationSettingsDir() returns a user-specific directory; it doesn't on Windows, but Windows overrides this behavior to use the registry
	const FString ConfigPath = FString(FPlatformProcess::ApplicationSettingsDir()) / InStoreId / FString(TEXT("KeyValueStore.ini"));
		
	FConfigFile ConfigFile;
	ConfigFile.Read(ConfigPath);

	const FConfigSection* const Section = ConfigFile.Find(InSectionName);
	if(Section)
	{
		const FConfigValue* const KeyValue = Section->Find(*InKeyName);
		if(KeyValue)
		{
			OutValue = KeyValue->GetValue();
			return true;
		}
	}

	return false;
}

bool FGenericPlatformMisc::DeleteStoredValue(const FString& InStoreId, const FString& InSectionName, const FString& InKeyName)
{
	check(!InStoreId.IsEmpty());
	check(!InSectionName.IsEmpty());
	check(!InKeyName.IsEmpty());

	// This assumes that FPlatformProcess::ApplicationSettingsDir() returns a user-specific directory; it doesn't on Windows, but Windows overrides this behavior to use the registry
	const FString ConfigPath = FString(FPlatformProcess::ApplicationSettingsDir()) / InStoreId / FString(TEXT("KeyValueStore.ini"));

	FConfigFile ConfigFile;
	ConfigFile.Read(ConfigPath);

	FConfigSection* const Section = ConfigFile.Find(InSectionName);
	if (Section)
	{
		int32 RemovedNum = Section->Remove(*InKeyName);

		ConfigFile.Dirty = true;
		return ConfigFile.Write(ConfigPath) && RemovedNum == 1;
	}

	return false;
}

bool FGenericPlatformMisc::DeleteStoredSection(const FString& InStoreId, const FString& InSectionName)
{
	check(!InStoreId.IsEmpty());
	check(!InSectionName.IsEmpty());

	// This assumes that FPlatformProcess::ApplicationSettingsDir() returns a user-specific directory; it doesn't on Windows, but Windows overrides this behavior to use the registry
	const FString ConfigPath = FString(FPlatformProcess::ApplicationSettingsDir()) / InStoreId / FString(TEXT("KeyValueStore.ini"));

	FConfigFile ConfigFile;
	ConfigFile.Read(ConfigPath);

	if (ConfigFile.Remove(InSectionName) != 0)
	{
		ConfigFile.Dirty = true;
		return ConfigFile.Write(ConfigPath);
	}

	return false;
}

void FGenericPlatformMisc::LowLevelOutputDebugString( const TCHAR *Message )
{
	FPlatformMisc::LocalPrint( Message );
}

void FGenericPlatformMisc::LowLevelOutputDebugStringf(const TCHAR *Fmt, ... )
{
	GROWABLE_LOGF(
		FPlatformMisc::LowLevelOutputDebugString( Buffer );
	);
}

void FGenericPlatformMisc::SetUTF8Output()
{
	// assume that UTF-8 is possible by default anyway
}

void FGenericPlatformMisc::LocalPrint( const TCHAR* Str )
{
#if PLATFORM_TCHAR_IS_CHAR16
	printf("%s", TCHAR_TO_UTF8(Str));
#elif PLATFORM_USE_LS_SPEC_FOR_WIDECHAR
	printf("%ls", Str);
#else
	wprintf(TEXT("%s"), Str);
#endif
}

bool FGenericPlatformMisc::HasSeparateChannelForDebugOutput()
{
	return true;
}

void FGenericPlatformMisc::RequestExit( bool Force )
{
	UE_LOG(LogGenericPlatformMisc, Log,  TEXT("FPlatformMisc::RequestExit(%i)"), Force );
	if( Force )
	{
		// Force immediate exit.
		// Dangerous because config code isn't flushed, global destructors aren't called, etc.
		// Suppress abort message and MS reports.
		abort();
	}
	else
	{
		// Tell the platform specific code we want to exit cleanly from the main loop.
		RequestEngineExit(TEXT("GenericPlatform RequestExit"));
	}
}

bool FGenericPlatformMisc::RestartApplication()
{
	UE_LOG(LogInit, Display, TEXT("Restart application is not supported or implemented in current platform"));
	return false;
}

void FGenericPlatformMisc::RequestExitWithStatus(bool Force, uint8 ReturnCode)
{
	// Generic implementation will ignore the return code - this may be important, so warn.
	UE_LOG(LogGenericPlatformMisc, Warning, TEXT("FPlatformMisc::RequestExitWithStatus(%i, %d) - return code will be ignored by the generic implementation."), Force, ReturnCode);

	return FPlatformMisc::RequestExit(Force);
}

const TCHAR* FGenericPlatformMisc::GetSystemErrorMessage(TCHAR* OutBuffer, int32 BufferCount, int32 Error)
{
	const TCHAR* Message = TEXT("No system errors available on this platform.");
	check(OutBuffer && BufferCount > 80);
	Error = 0;
	FCString::Strcpy(OutBuffer, BufferCount - 1, Message);
	return OutBuffer;
}

void FGenericPlatformMisc::ClipboardCopy(const TCHAR* Str)
{
	if(ClipboardCopyShim == nullptr)
	{
		UE_LOG(LogGenericPlatformMisc, Warning, TEXT("ClipboardCopyShim() is not bound; ignoring."));
	}
	else
	{
		ClipboardCopyShim(Str);
	}
}

void FGenericPlatformMisc:: ClipboardPaste(class FString& Dest)
{
	if(ClipboardPasteShim == nullptr)
	{
		UE_LOG(LogGenericPlatformMisc, Warning, TEXT("ClipboardPasteShim() is not bound; ignoring."));
	}
	else
	{
		ClipboardPasteShim(Dest);
	}
}

void FGenericPlatformMisc::CreateGuid(FGuid& Guid)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FGenericPlatformMisc_CreateGuid);

	static uint16 IncrementCounter = 0; 

	static FDateTime InitialDateTime;
	static uint64 InitialCycleCounter;

	FDateTime EstimatedCurrentDateTime;

	if (IncrementCounter == 0)
	{
		// Hack: First Guid can be created prior to FPlatformTime::InitTiming(), so do it here.
		FPlatformTime::InitTiming();

		// uses FPlatformTime::SystemTime()
		InitialDateTime = FDateTime::Now();
		InitialCycleCounter = FPlatformTime::Cycles64();
		
		EstimatedCurrentDateTime = InitialDateTime;
	}
	else
	{
		FTimespan ElapsedTime = FTimespan::FromSeconds(FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - InitialCycleCounter));
		
		EstimatedCurrentDateTime = InitialDateTime + ElapsedTime;
	}

	uint32 SequentialBits = static_cast<uint32>(IncrementCounter++); // Add sequential bits to ensure sequentially generated guids are unique even if Cycles is wrong
	uint32 RandBits = FMath::Rand() & 0xFFFF; // Add randomness to improve uniqueness across machines

	Guid = FGuid(RandBits | (SequentialBits << 16), EstimatedCurrentDateTime.GetTicks() >> 32, EstimatedCurrentDateTime.GetTicks() & 0xffffffff, FPlatformTime::Cycles());
}

const TCHAR* LexToString( EAppReturnType::Type Value )
{
	switch (Value)
	{
	case EAppReturnType::No:
		return TEXT("No");
	case EAppReturnType::Yes:
		return TEXT("Yes");
	case EAppReturnType::YesAll:
		return TEXT("YesAll");
	case EAppReturnType::NoAll:
		return TEXT("NoAll");
	case EAppReturnType::Cancel:
		return TEXT("Cancel");
	case EAppReturnType::Ok:
		return TEXT("Ok");
	case EAppReturnType::Retry:
		return TEXT("Retry");
	case EAppReturnType::Continue:
		return TEXT("Continue");
	default:
		return TEXT("Unknown");
	}
}

EAppReturnType::Type FGenericPlatformMisc::MessageBoxExt( EAppMsgType::Type MsgType, const TCHAR* Text, const TCHAR* Caption )
{
	if (GWarn)
	{
		UE_LOG(LogGenericPlatformMisc, Warning, TEXT("MessageBox: %s : %s"), Caption, Text);
	}

	switch(MsgType)
	{
	case EAppMsgType::Ok:
		return EAppReturnType::Ok; // Ok
	case EAppMsgType::YesNo:
		return EAppReturnType::No; // No
	case EAppMsgType::OkCancel:
		return EAppReturnType::Cancel; // Cancel
	case EAppMsgType::YesNoCancel:
		return EAppReturnType::Cancel; // Cancel
	case EAppMsgType::CancelRetryContinue:
		return EAppReturnType::Cancel; // Cancel
	case EAppMsgType::YesNoYesAllNoAll:
		return EAppReturnType::No; // No
	case EAppMsgType::YesNoYesAllNoAllCancel:
		return EAppReturnType::Yes; // Yes
	default:
		check(0);
	}
	return EAppReturnType::Cancel; // Cancel
}

const TCHAR* FGenericPlatformMisc::RootDir()
{
	FString& Path = TLazySingleton<FStaticData>::Get().RootDir;
	if (Path.Len() == 0)
	{
		FString TempPath = FPaths::EngineDir();
		int32 chopPos = TempPath.Find(TEXT("/Engine"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		if (chopPos != INDEX_NONE)
		{
			TempPath.LeftInline(chopPos + 1, false);
		}
		else
		{
			TempPath = FPlatformProcess::BaseDir();

			// if the path ends in a separator, remove it
			if (TempPath.Right(1) == TEXT("/"))
			{
				TempPath.LeftChopInline(1, false);
			}

			// keep going until we've removed Binaries
#if IS_MONOLITHIC && !IS_PROGRAM
			int32 pos = TempPath.Find(*FString::Printf(TEXT("/%s/Binaries"), FApp::GetProjectName()));
#else
			int32 pos = TempPath.Find(TEXT("/Engine/Binaries"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
#endif
			if (pos != INDEX_NONE)
			{
				TempPath.LeftInline(pos + 1, false);
			}
			else
			{
				pos = TempPath.Find(TEXT("/../Binaries"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
				if (pos != INDEX_NONE)
				{
					TempPath = TempPath.Left(pos + 1) + TEXT("../../");
				}
				else
				{
					while (TempPath.Len() && TempPath.Right(1) != TEXT("/"))
					{
						TempPath.LeftChopInline(1, false);
					}
				}
			}
		}

		Path = FPaths::ConvertRelativePathToFull(TempPath);
		FPaths::RemoveDuplicateSlashes(Path);
	}
	return *Path;
}

TArray<FString> FGenericPlatformMisc::GetAdditionalRootDirectories()
{
	FRWScopeLock Lock(TLazySingleton<FStaticData>::Get().AdditionalRootDirectoriesLock, SLT_ReadOnly);

	return TLazySingleton<FStaticData>::Get().AdditionalRootDirectories;
}

void FGenericPlatformMisc::AddAdditionalRootDirectory(const FString& RootDir)
{
	FRWScopeLock Lock(TLazySingleton<FStaticData>::Get().AdditionalRootDirectoriesLock, SLT_Write);

	TArray<FString>& RootDirectories = TLazySingleton<FStaticData>::Get().AdditionalRootDirectories;
	FString NewRootDirectory = RootDir;
	FPaths::MakePlatformFilename(NewRootDirectory);
	RootDirectories.Add(NewRootDirectory);
}

static void MakeEngineDir(FString& OutEngineDir)
{
	// See if we are a root-level project
	FString DefaultEngineDir = TEXT("../../../Engine/");
#if PLATFORM_DESKTOP
#if !defined(DISABLE_CWD_CHANGES) || DISABLE_CWD_CHANGES == 0
	FPlatformProcess::SetCurrentWorkingDirectoryToBaseDir();
#endif

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	const TCHAR* BaseDir = FPlatformProcess::BaseDir();

	//@todo. Need to have a define specific for this scenario??
	FString DirToTry = BaseDir / DefaultEngineDir / TEXT("Binaries");
	if (PlatformFile.DirectoryExists(*DirToTry))
	{
		OutEngineDir = MoveTemp(DefaultEngineDir);
		return;
	}

	if (GForeignEngineDir)
	{
		DirToTry = FString(GForeignEngineDir) / TEXT("Binaries");
		if (PlatformFile.DirectoryExists(*DirToTry))
		{
			OutEngineDir = GForeignEngineDir;
			return;
		}
	}

	// Temporary work-around for legacy dependency on ../../../ (re Lightmass)
	UE_LOG(LogGenericPlatformMisc, Warning, TEXT("Failed to determine engine directory: Defaulting to %s"), *OutEngineDir);
#endif

	OutEngineDir = MoveTemp(DefaultEngineDir);
}

const TCHAR* FGenericPlatformMisc::EngineDir()
{
	FString& EngineDirectory = TLazySingleton<FStaticData>::Get().EngineDirectory;
	if (EngineDirectory.Len() == 0)
	{
		MakeEngineDir(EngineDirectory);
	}
	return *EngineDirectory;
}

void FGenericPlatformMisc::CacheLaunchDir()
{
	FString& LaunchDir = TLazySingleton<FStaticData>::Get().LaunchDir;

	// we can only cache this ONCE
	if (LaunchDir.Len() != 0)
	{
		return;
	}
	
	LaunchDir = FPlatformProcess::GetCurrentWorkingDirectory() + TEXT("/");
}

const TCHAR* FGenericPlatformMisc::LaunchDir()
{
	return *TLazySingleton<FStaticData>::Get().LaunchDir;
}


const TCHAR* FGenericPlatformMisc::GetNullRHIShaderFormat()
{
	return TEXT("PCD3D_SM5");
}

IPlatformChunkInstall* FGenericPlatformMisc::GetPlatformChunkInstall()
{
	static FGenericPlatformChunkInstall Singleton;
	return &Singleton;
}

void GenericPlatformMisc_GetProjectFilePathProjectDir(FString& OutGameDir)
{
	// Here we derive the game path from the project file location.
	FString BasePath = FPaths::GetPath(FPaths::GetProjectFilePath());
	FPaths::NormalizeFilename(BasePath);
	BasePath = FFileManagerGeneric::DefaultConvertToRelativePath(*BasePath);
	if(!BasePath.EndsWith("/")) BasePath += TEXT("/");
	OutGameDir = BasePath;
}

const TCHAR* FGenericPlatformMisc::ProjectDir()
{
	FString& ProjectDir = TLazySingleton<FStaticData>::Get().ProjectDir;

	// track if last time we called this function the .ini was ready and had fixed the GameName case
	static bool bWasIniReady = false;
	bool bIsIniReady = GConfig && GConfig->IsReadyForUse();
	if (bWasIniReady != bIsIniReady)
	{
		ProjectDir.Reset();
		bWasIniReady = bIsIniReady;
	}

	// track if last time we called this function the project file path was set
	static bool bWasProjectFilePathReady = false;
	if (!bWasProjectFilePathReady && FPaths::IsProjectFilePathSet())
	{
		ProjectDir.Reset();
		bWasProjectFilePathReady = true;
	}

	// try using the override game dir if it exists, which will override all below logic
	if (ProjectDir.Len() == 0)
	{
		ProjectDir.Reserve(FPlatformMisc::GetMaxPathLength());
		ProjectDir = OverrideProjectDir;
	}

	if (ProjectDir.Len() == 0)
	{
		ProjectDir.Reserve(FPlatformMisc::GetMaxPathLength());
		if (FPlatformProperties::IsProgram())
		{
			// monolithic, game-agnostic executables, the ini is in Engine/Config/Platform
			ProjectDir = FString::Printf(TEXT("../../../Engine/Programs/%s/"), FApp::GetProjectName());
		}
		else
		{
			if (FPaths::IsProjectFilePathSet())
			{
				GenericPlatformMisc_GetProjectFilePathProjectDir(ProjectDir);
			}
			else if ( FApp::HasProjectName() )
			{
				if (FPlatformProperties::IsMonolithicBuild() == false)
				{
					// No game project file, but has a game name, use the game folder next to the working directory
					ProjectDir = FString::Printf(TEXT("../../../%s/"), FApp::GetProjectName());
					FString GameBinariesDir = ProjectDir / TEXT("Binaries/");
					if (FPlatformFileManager::Get().GetPlatformFile().DirectoryExists(*GameBinariesDir) == false)
					{
						// The game binaries folder was *not* found
						// 
						FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Failed to find game directory: %s\n"), *ProjectDir);

						// Use the uprojectdirs
						FString GameProjectFile = FUProjectDictionary::GetDefault().GetRelativeProjectPathForGame(FApp::GetProjectName(), FPlatformProcess::BaseDir());
						if (GameProjectFile.IsEmpty() == false)
						{
							// We found a project folder for the game
							FPaths::SetProjectFilePath(GameProjectFile);
							ProjectDir = FPaths::GetPath(GameProjectFile);
							if (ProjectDir.EndsWith(TEXT("/")) == false)
							{
								ProjectDir += TEXT("/");
							}
						}
					}
				}
				else
				{
#if !PLATFORM_DESKTOP
					ProjectDir = FString::Printf(TEXT("../../../%s/"), FApp::GetProjectName());
#else
					// This assumes the game executable is in <GAME>/Binaries/<PLATFORM>
					ProjectDir = TEXT("../../");

					// Determine a relative path that includes the game folder itself, if possible...
					FString LocalBaseDir = FString(FPlatformProcess::BaseDir());
					FString LocalRootDir = FPaths::RootDir();
					FString BaseToRoot = LocalRootDir;
					FPaths::MakePathRelativeTo(BaseToRoot, *LocalBaseDir);
					FString LocalProjectDir = LocalBaseDir / TEXT("../../");
					FPaths::CollapseRelativeDirectories(LocalProjectDir);
					FPaths::MakePathRelativeTo(LocalProjectDir, *(FPaths::RootDir()));
					LocalProjectDir = BaseToRoot / LocalProjectDir;
					if (LocalProjectDir.EndsWith(TEXT("/")) == false)
					{
						LocalProjectDir += TEXT("/");
					}

					FString CheckLocal = FPaths::ConvertRelativePathToFull(LocalProjectDir);
					FString CheckGame = FPaths::ConvertRelativePathToFull(ProjectDir);
					if (CheckLocal == CheckGame)
					{
						ProjectDir = LocalProjectDir;
					}

					if (ProjectDir.EndsWith(TEXT("/")) == false)
					{
						ProjectDir += TEXT("/");
					}
#endif
				}
			}
			else
			{
				// Get a writable engine directory
				ProjectDir = FPaths::EngineUserDir();
				FPaths::NormalizeFilename(ProjectDir);
				ProjectDir = FFileManagerGeneric::DefaultConvertToRelativePath(*ProjectDir);
				if(!ProjectDir.EndsWith(TEXT("/"))) ProjectDir += TEXT("/");
			}
		}
	}

	return *ProjectDir;
}

FString FGenericPlatformMisc::CloudDir()
{
	return FPaths::ProjectSavedDir() + TEXT("Cloud/");
}

const TCHAR* FGenericPlatformMisc::GamePersistentDownloadDir()
{
	FString& GamePersistentDownloadDir = TLazySingleton<FStaticData>::Get().GamePersistentDownloadDir;

	if (GamePersistentDownloadDir.Len() == 0)
	{
		GamePersistentDownloadDir = FPaths::ProjectSavedDir() / TEXT("PersistentDownloadDir");
	}
	return *GamePersistentDownloadDir;
}

const TCHAR* FGenericPlatformMisc::GeneratedConfigDir()
{
	static FString Dir = FPaths::ProjectSavedDir() / TEXT("Config/");
	return *Dir;
}

const TCHAR* FGenericPlatformMisc::GetUBTPlatform()
{
	return TEXT(PREPROCESSOR_TO_STRING(UBT_COMPILED_PLATFORM));
}

const TCHAR* FGenericPlatformMisc::GetUBTTarget()
{
	return TEXT(PREPROCESSOR_TO_STRING(UBT_COMPILED_TARGET));
}

/** The name of the UBT target that the current executable was built from. Defaults to the UE4 default target for this type to make content only projects work, 
	but will be overridden by the primary game module if it exists */
TCHAR GUBTTargetName[128] = TEXT("UE4" PREPROCESSOR_TO_STRING(UBT_COMPILED_TARGET));

void FGenericPlatformMisc::SetUBTTargetName(const TCHAR* InTargetName)
{
	check(FCString::Strlen(InTargetName) < (UE_ARRAY_COUNT(GUBTTargetName) - 1));
	FCString::Strcpy(GUBTTargetName, InTargetName);
}

const TCHAR* FGenericPlatformMisc::GetUBTTargetName()
{
	return GUBTTargetName;
}

const TCHAR* FGenericPlatformMisc::GetDefaultDeviceProfileName()
{
	return TEXT("Default");
}

float FGenericPlatformMisc::GetDeviceTemperatureLevel()
{
	return -1.0f;
}

void FGenericPlatformMisc::SetOverrideProjectDir(const FString& InOverrideDir)
{
	OverrideProjectDir = InOverrideDir;
}

bool FGenericPlatformMisc::UseRenderThread()
{
	// look for disabling commandline options (-onethread is old-school, here for compatibility with people's brains)
	if (FParse::Param(FCommandLine::Get(), TEXT("norenderthread")) || FParse::Param(FCommandLine::Get(), TEXT("onethread")))
	{
		return false;
	}

	// single core devices shouldn't use it (unless that platform overrides this function - maybe RT could be required?)
	if (FPlatformMisc::NumberOfCoresIncludingHyperthreads() < 2)
	{
		return false;
	}

	// if the platform doesn't allow threading at all, we really can't use it
	if (FPlatformProcess::SupportsMultithreading() == false)
	{
		return false;
	}

	// dedicated servers should not use a rendering thread
	if (IsRunningDedicatedServer())
	{
		return false;
	}


#if ENABLE_LOW_LEVEL_MEM_TRACKER
	// disable rendering thread when LLM wants to so that memory is attributer better
	if (FLowLevelMemTracker::Get().ShouldReduceThreads())
	{
		return false;
	}
#endif

	// allow if not overridden
	return true;
}

bool FGenericPlatformMisc::AllowThreadHeartBeat()
{
	static bool bHeartbeat = !FParse::Param(FCommandLine::Get(), TEXT("noheartbeatthread"));
	return bHeartbeat;
}

int32 FGenericPlatformMisc::NumberOfCoresIncludingHyperthreads()
{
	return FPlatformMisc::NumberOfCores();
}

int32 FGenericPlatformMisc::NumberOfWorkerThreadsToSpawn()
{
	static int32 MaxGameThreads = 4;
	static int32 MaxThreads = 16;

	int32 NumberOfCores = FPlatformMisc::NumberOfCores();
	int32 MaxWorkerThreadsWanted = (IsRunningGame() || IsRunningDedicatedServer() || IsRunningClientOnly()) ? MaxGameThreads : MaxThreads;
	// need to spawn at least two worker thread (see FTaskGraphImplementation)
	return FMath::Max(FMath::Min(NumberOfCores - 1, MaxWorkerThreadsWanted), 2);
}

int32 FGenericPlatformMisc::NumberOfIOWorkerThreadsToSpawn()
{
	return 4;
}

void FGenericPlatformMisc::GetValidTargetPlatforms(class TArray<class FString>& TargetPlatformNames)
{
	// by default, just return the running PlatformName as the only TargetPlatform we support
	TargetPlatformNames.Add(FPlatformProperties::PlatformName());
}

TArray<uint8> FGenericPlatformMisc::GetSystemFontBytes()
{
	return TArray<uint8>();
}

const TCHAR* FGenericPlatformMisc::GetDefaultPathSeparator()
{
	return TEXT( "/" );
}

bool FGenericPlatformMisc::GetSHA256Signature(const void* Data, uint32 ByteSize, FSHA256Signature& OutSignature)
{
	checkf(false, TEXT("No SHA256 Platform implementation"));
	FMemory::Memzero(OutSignature.Signature);
	return false;
}

FString FGenericPlatformMisc::GetDefaultLanguage()
{
	return FPlatformMisc::GetDefaultLocale();
}

FString FGenericPlatformMisc::GetDefaultLocale()
{
#if UE_ENABLE_ICU
	icu::Locale ICUDefaultLocale = icu::Locale::getDefault();
	return FString(ICUDefaultLocale.getName());
#else
	return TEXT("en");
#endif
}

FString FGenericPlatformMisc::GetTimeZoneId()
{
	// ICU will calculate this correctly for most platforms (if enabled)
	return FString();
}

#if DO_ENSURE
namespace GenericPlatformMisc
{
	/** Chances for handling an ensure (0.0 - never, 1.0 - always). */
	float GEnsureChance = 1.0f;

	/** Checks if we ever updated ensure settings. */
	bool GEnsureSettingsEverUpdated = false;
}

bool FGenericPlatformMisc::IsEnsureAllowed()
{
	// not all targets call FEngineLoop::Tick() or we might be here early
	if (!GenericPlatformMisc::GEnsureSettingsEverUpdated)
	{
		FPlatformMisc::UpdateHotfixableEnsureSettings();
	}

	// using random makes it less deterministic between runs and multiple processes
	return FMath::FRand() < GenericPlatformMisc::GEnsureChance;
}

void FGenericPlatformMisc::UpdateHotfixableEnsureSettings()
{
	// config (which is hotfixable) makes priority over the commandline
	float HandleEnsurePercentInConfig = 100.0f;
	if (GConfig && GConfig->GetFloat(TEXT("Core.System"), TEXT("HandleEnsurePercent"), HandleEnsurePercentInConfig, GEngineIni))
	{
		GenericPlatformMisc::GEnsureChance = HandleEnsurePercentInConfig / 100.0f;
	}
	else
	{
		float HandleEnsurePercentOnCmdLine = 100.0f;
		if (FCommandLine::IsInitialized() && FParse::Value(FCommandLine::Get(), TEXT("handleensurepercent="), HandleEnsurePercentOnCmdLine))
		{
			GenericPlatformMisc::GEnsureChance = HandleEnsurePercentOnCmdLine / 100.0f;
		}
	}

	// to compensate for FRand() being able to return 1.0 (argh!), extra check for 100 
	if (GenericPlatformMisc::GEnsureChance >= 1.00f)
	{
		GenericPlatformMisc::GEnsureChance = 1.01f;
	}

	GenericPlatformMisc::GEnsureSettingsEverUpdated = true;
}
#endif // #if DO_ENSURE

void FGenericPlatformMisc::TickHotfixables()
{
	UpdateHotfixableEnsureSettings();
}

FText FGenericPlatformMisc::GetFileManagerName()
{
	return NSLOCTEXT("GenericPlatform", "FileManagerName", "File Manager");
}

bool FGenericPlatformMisc::IsRunningOnBattery()
{
	return false;
}

EDeviceScreenOrientation FGenericPlatformMisc::GetDeviceOrientation()
{
	return EDeviceScreenOrientation::Unknown;
}

void FGenericPlatformMisc::SetDeviceOrientation(EDeviceScreenOrientation NewDeviceOrientation)
{
	// not implemented by default
}

int32 FGenericPlatformMisc::GetDeviceVolume()
{
	return -1;
}

FGuid FGenericPlatformMisc::GetMachineId()
{
	static FGuid MachineId;
	FString MachineIdString;

	// Check to see if we already have a valid machine ID to use
	if( !MachineId.IsValid() && (!FPlatformMisc::GetStoredValue( TEXT( "Epic Games" ), TEXT( "Unreal Engine/Identifiers" ), TEXT( "MachineId" ), MachineIdString ) || !FGuid::Parse( MachineIdString, MachineId )) )
	{
		// No valid machine ID, generate and save a new one
		MachineId = FGuid::NewGuid();
		MachineIdString = MachineId.ToString( EGuidFormats::Digits );

		if( !FPlatformMisc::SetStoredValue( TEXT( "Epic Games" ), TEXT( "Unreal Engine/Identifiers" ), TEXT( "MachineId" ), MachineIdString ) )
		{
			// Failed to persist the machine ID - reset it to zero to avoid returning a transient value
			MachineId = FGuid();
		}
	}

	return MachineId;
}

FString FGenericPlatformMisc::GetLoginId()
{
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FGuid Id = FPlatformMisc::GetMachineId();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	// force an empty string if we cannot determine an ID.
	if (Id == FGuid())
	{
		return FString();
	}
	return Id.ToString(EGuidFormats::Digits).ToLower();
}


FString FGenericPlatformMisc::GetEpicAccountId()
{
	FString AccountId;
	FPlatformMisc::GetStoredValue( TEXT( "Epic Games" ), TEXT( "Unreal Engine/Identifiers" ), TEXT( "AccountId" ), AccountId );
	return AccountId;
}

bool FGenericPlatformMisc::SetEpicAccountId( const FString& AccountId )
{
	checkf(false, TEXT("FPlatformMisc::SetEpicAccountId should not be called"));
	return false;
}

EConvertibleLaptopMode FGenericPlatformMisc::GetConvertibleLaptopMode()
{
	return EConvertibleLaptopMode::NotSupported;
}

const TCHAR* FGenericPlatformMisc::GetEngineMode()
{
	return	
		IsRunningCommandlet() ? TEXT( "Commandlet" ) :
		GIsEditor ? TEXT( "Editor" ) :
		IsRunningDedicatedServer() ? TEXT( "Server" ) :
		TEXT( "Game" );
}

TArray<FString> FGenericPlatformMisc::GetPreferredLanguages()
{
	// Determine what out current culture is, and grab the most appropriate set of subtitles for it
	FInternationalization& Internationalization = FInternationalization::Get();

	TArray<FString> PrioritizedCultureNames = Internationalization.GetPrioritizedCultureNames(Internationalization.GetCurrentCulture()->GetName());
	return PrioritizedCultureNames;
}

FString FGenericPlatformMisc::GetLocalCurrencyCode()
{
	// not implemented by default
	return FString();
}

FString FGenericPlatformMisc::GetLocalCurrencySymbol()
{
	// not implemented by default
	return FString();
}

void FGenericPlatformMisc::PlatformPreInit()
{
	FGenericCrashContext::Initialize();
}

FString FGenericPlatformMisc::GetOperatingSystemId()
{
	// not implemented by default.
	return FString();
}

void FGenericPlatformMisc::RegisterForRemoteNotifications()
{
	// not implemented by default
}

bool FGenericPlatformMisc::IsRegisteredForRemoteNotifications()
{
	// not implemented by default
	return false;
}

void FGenericPlatformMisc::UnregisterForRemoteNotifications()
{
	// not implemented by default
}

bool FGenericPlatformMisc::RequestDeviceCheckToken(TFunction<void(const TArray<uint8>&)> QuerySucceededFunc, TFunction<void(const FString&, const FString&)> QueryFailedFunc)
{
	// not implemented by default
	return false;
}

TArray<FCustomChunk> FGenericPlatformMisc::GetOnDemandChunksForPakchunkIndices(const TArray<int32>& PakchunkIndices)
{
	return TArray<FCustomChunk>();
}

TArray<FCustomChunk> FGenericPlatformMisc::GetAllOnDemandChunks()
{
	return TArray<FCustomChunk>();
}

TArray<FCustomChunk> FGenericPlatformMisc::GetAllLanguageChunks()
{
	return TArray<FCustomChunk>();
}

TArray<FCustomChunk> FGenericPlatformMisc::GetCustomChunksByType(ECustomChunkType DesiredChunkType)
{
	if (DesiredChunkType == ECustomChunkType::OnDemandChunk)
	{
		return GetAllOnDemandChunks();
	}
	else
	{
		return GetAllLanguageChunks();
	}
}

FString FGenericPlatformMisc::LoadTextFileFromPlatformPackage(const FString& RelativePath)
{
	FString Path = RootDir() / RelativePath;
	FString Result;
	if (FFileHelper::LoadFileToString(Result, &IPlatformFile::GetPlatformPhysical(), *Path))
	{
		return Result;
	}

	Result.Empty();
	return Result;
}

bool FGenericPlatformMisc::FileExistsInPlatformPackage(const FString& RelativePath)
{
	FString Path = RootDir() / RelativePath;
	return IPlatformFile::GetPlatformPhysical().FileExists(*Path);
}

void FGenericPlatformMisc::TearDown()
{
	TLazySingleton<FStaticData>::TearDown();
}

void FGenericPlatformMisc::ParseChunkIdPakchunkIndexMapping(TArray<FString> ChunkIndexMappingData, TMap<int32, int32>& OutMapping)
{
	OutMapping.Empty();

	const TCHAR* PropertyOldChunkIndex = TEXT("Old=");
	const TCHAR* PropertyNewChunkIndex = TEXT("New=");
	for (FString& Entry : ChunkIndexMappingData)
	{
		// Remove parentheses
		Entry.TrimStartAndEndInline();
		Entry.ReplaceInline(TEXT("("), TEXT(""));
		Entry.ReplaceInline(TEXT(")"), TEXT(""));

		int32 ChunkId = -1;
		int32 PakchunkIndex = -1;
		FParse::Value(*Entry, PropertyOldChunkIndex, ChunkId);
		FParse::Value(*Entry, PropertyNewChunkIndex, PakchunkIndex);

		if (ChunkId != -1 && PakchunkIndex != -1 && ChunkId != PakchunkIndex && !OutMapping.Contains(ChunkId))
		{
			OutMapping.Add(ChunkId, PakchunkIndex);
		}
	}
}

int32 FGenericPlatformMisc::GetPakchunkIndexFromPakFile(const FString& InFilename)
{
	FString ChunkIdentifier(TEXT("pakchunk"));
	FString BaseFilename = FPaths::GetBaseFilename(InFilename);
	int32 ChunkNumber = INDEX_NONE;

	if (BaseFilename.StartsWith(ChunkIdentifier))
	{
		int32 StartOfNumber = ChunkIdentifier.Len();
		int32 DigitCount = 0;
		if (FChar::IsDigit(BaseFilename[StartOfNumber]))
		{
			while ((DigitCount + StartOfNumber) < BaseFilename.Len() && FChar::IsDigit(BaseFilename[StartOfNumber + DigitCount]))
			{
				DigitCount++;
			}

			if ((StartOfNumber + DigitCount) < BaseFilename.Len())
			{
				FString ChunkNumberString = BaseFilename.Mid(StartOfNumber, DigitCount);
				check(ChunkNumberString.IsNumeric());
				TTypeFromString<int32>::FromString(ChunkNumber, *ChunkNumberString);
			}
		}
	}

	return ChunkNumber;
}

bool FGenericPlatformMisc::IsPGOEnabled()
{
	return PLATFORM_COMPILER_OPTIMIZATION_PG;
}
