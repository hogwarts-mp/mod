// Copyright Epic Games, Inc. All Rights Reserved.

#include "GenericPlatform/GenericPlatformCrashContext.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformStackWalk.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Internationalization/Culture.h"
#include "Misc/Optional.h"
#include "Internationalization/Internationalization.h"
#include "Misc/Guid.h"
#include "Misc/SecureHash.h"
#include "Containers/Ticker.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/App.h"
#include "Misc/EngineVersion.h"
#include "Misc/EngineBuildSettings.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/ScopeLock.h"
#include "Stats/Stats.h"
#include "Internationalization/Regex.h"
#include "Internationalization/TextLocalizationManager.h"
#include "Logging/LogScopedCategoryAndVerbosityOverride.h"
#include "HAL/PlatformOutputDevices.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/OutputDeviceArchiveWrapper.h"

#ifndef NOINITCRASHREPORTER
#define NOINITCRASHREPORTER 0
#endif

DEFINE_LOG_CATEGORY_STATIC(LogCrashContext, Display, All);

extern CORE_API bool GIsGPUCrashed;

/*-----------------------------------------------------------------------------
	FGenericCrashContext
-----------------------------------------------------------------------------*/

const ANSICHAR* const FGenericCrashContext::CrashContextRuntimeXMLNameA = "CrashContext.runtime-xml";
const TCHAR* const FGenericCrashContext::CrashContextRuntimeXMLNameW = TEXT( "CrashContext.runtime-xml" );

const ANSICHAR* const FGenericCrashContext::CrashConfigFileNameA = "CrashReportClient.ini";
const TCHAR* const FGenericCrashContext::CrashConfigFileNameW = TEXT("CrashReportClient.ini");
const TCHAR* const FGenericCrashContext::CrashConfigExtension = TEXT(".ini");
const TCHAR* const FGenericCrashContext::ConfigSectionName = TEXT("CrashReportClient");
const TCHAR* const FGenericCrashContext::CrashConfigPurgeDays = TEXT("CrashConfigPurgeDays");
const TCHAR* const FGenericCrashContext::CrashGUIDRootPrefix = TEXT("UE4CC-");

const TCHAR* const FGenericCrashContext::CrashContextExtension = TEXT(".runtime-xml");
const TCHAR* const FGenericCrashContext::RuntimePropertiesTag = TEXT( "RuntimeProperties" );
const TCHAR* const FGenericCrashContext::PlatformPropertiesTag = TEXT( "PlatformProperties" );
const TCHAR* const FGenericCrashContext::EngineDataTag = TEXT( "EngineData" );
const TCHAR* const FGenericCrashContext::GameDataTag = TEXT( "GameData" );
const TCHAR* const FGenericCrashContext::EnabledPluginsTag = TEXT("EnabledPlugins");
const TCHAR* const FGenericCrashContext::UE4MinidumpName = TEXT( "UE4Minidump.dmp" );
const TCHAR* const FGenericCrashContext::NewLineTag = TEXT( "&nl;" );

const TCHAR* const FGenericCrashContext::CrashTypeCrash = TEXT("Crash");
const TCHAR* const FGenericCrashContext::CrashTypeAssert = TEXT("Assert");
const TCHAR* const FGenericCrashContext::CrashTypeEnsure = TEXT("Ensure");
const TCHAR* const FGenericCrashContext::CrashTypeGPU = TEXT("GPUCrash");
const TCHAR* const FGenericCrashContext::CrashTypeHang = TEXT("Hang");
const TCHAR* const FGenericCrashContext::CrashTypeAbnormalShutdown = TEXT("AbnormalShutdown");

const TCHAR* const FGenericCrashContext::EngineModeExUnknown = TEXT("Unset");
const TCHAR* const FGenericCrashContext::EngineModeExDirty = TEXT("Dirty");
const TCHAR* const FGenericCrashContext::EngineModeExVanilla = TEXT("Vanilla");

bool FGenericCrashContext::bIsInitialized = false;
uint32 FGenericCrashContext::OutOfProcessCrashReporterPid = 0;
volatile int64 FGenericCrashContext::OutOfProcessCrashReporterExitCode = 0;
int32 FGenericCrashContext::StaticCrashContextIndex = 0;

const FGuid FGenericCrashContext::ExecutionGuid = FGuid::NewGuid();

namespace NCached
{
	static FSessionContext Session;
	static FUserSettingsContext UserSettings;
	static TArray<FString> EnabledPluginsList;
	static TMap<FString, FString> EngineData;
	static TMap<FString, FString> GameData;

	template <size_t CharCount, typename CharType>
	void Set(CharType(&Dest)[CharCount], const CharType* pSrc)
	{
		TCString<CharType>::Strncpy(Dest, pSrc, CharCount);
	}
}

void FGenericCrashContext::Initialize()
{
#if !NOINITCRASHREPORTER
	NCached::Session.bIsInternalBuild = FEngineBuildSettings::IsInternalBuild();
	NCached::Session.bIsPerforceBuild = FEngineBuildSettings::IsPerforceBuild();
	NCached::Session.bIsSourceDistribution = FEngineBuildSettings::IsSourceDistribution();
	NCached::Session.ProcessId = FPlatformProcess::GetCurrentProcessId();

	NCached::Set(NCached::Session.GameName, *FString::Printf(TEXT("UE4-%s"), FApp::GetProjectName()));
	NCached::Set(NCached::Session.GameSessionID, TEXT("")); // Updated by callback
	NCached::Set(NCached::Session.GameStateName, TEXT("")); // Updated by callback
	NCached::Set(NCached::Session.UserActivityHint, TEXT("")); // Updated by callback
	NCached::Set(NCached::Session.BuildConfigurationName, LexToString(FApp::GetBuildConfiguration()));
	NCached::Set(NCached::Session.ExecutableName, FPlatformProcess::ExecutableName());
	NCached::Set(NCached::Session.BaseDir, FPlatformProcess::BaseDir());
	NCached::Set(NCached::Session.RootDir, FPlatformMisc::RootDir());
	NCached::Set(NCached::Session.EpicAccountId, *FPlatformMisc::GetEpicAccountId());
	NCached::Set(NCached::Session.LoginIdStr, *FPlatformMisc::GetLoginId());

	FString OsVersion, OsSubVersion;
	FPlatformMisc::GetOSVersions(OsVersion, OsSubVersion);
	NCached::Set(NCached::Session.OsVersion, *OsVersion);
	NCached::Set(NCached::Session.OsSubVersion, *OsSubVersion);

	NCached::Session.NumberOfCores = FPlatformMisc::NumberOfCores();
	NCached::Session.NumberOfCoresIncludingHyperthreads = FPlatformMisc::NumberOfCoresIncludingHyperthreads();

	NCached::Set(NCached::Session.CPUVendor, *FPlatformMisc::GetCPUVendor());
	NCached::Set(NCached::Session.CPUBrand, *FPlatformMisc::GetCPUBrand());
	NCached::Set(NCached::Session.PrimaryGPUBrand, *FPlatformMisc::GetPrimaryGPUBrand());
	NCached::Set(NCached::Session.UserName, FPlatformProcess::UserName());
	NCached::Set(NCached::Session.DefaultLocale, *FPlatformMisc::GetDefaultLocale());

	NCached::Set(NCached::Session.PlatformName, FPlatformProperties::PlatformName());
	NCached::Set(NCached::Session.PlatformNameIni, FPlatformProperties::IniPlatformName());

	// Information that cannot be gathered if command line is not initialized (e.g. crash during static init)
	if (FCommandLine::IsInitialized())
	{
		NCached::Session.bIsUE4Release = FApp::IsEngineInstalled();
		NCached::Set(NCached::Session.CommandLine, (FCommandLine::IsInitialized() ? FCommandLine::GetOriginalForLogging() : TEXT("")));
		NCached::Set(NCached::Session.EngineMode, FGenericPlatformMisc::GetEngineMode());
		NCached::Set(NCached::Session.EngineModeEx, FGenericCrashContext::EngineModeExUnknown); // Updated from callback

		NCached::Set(NCached::UserSettings.LogFilePath, *FPlatformOutputDevices::GetAbsoluteLogFilename());

		// Use -epicapp value from the commandline to start. This will also be set by the game
		FParse::Value(FCommandLine::Get(), TEXT("EPICAPP="), NCached::Session.DeploymentName, CR_MAX_GENERIC_FIELD_CHARS, true);

		// Using the -fullcrashdump parameter will cause full memory minidumps to be created for crashes
		NCached::Session.CrashDumpMode = (int32)ECrashDumpMode::Default;
		if (FPlatformMisc::SupportsFullCrashDumps() && FCommandLine::IsInitialized())
		{
			const TCHAR* CmdLine = FCommandLine::Get();
			if (FParse::Param(CmdLine, TEXT("fullcrashdumpalways")))
			{
				NCached::Session.CrashDumpMode = (int32)ECrashDumpMode::FullDumpAlways;
			}
			else if (FParse::Param(CmdLine, TEXT("fullcrashdump")))
			{
				NCached::Session.CrashDumpMode = (int32)ECrashDumpMode::FullDump;
			}
		}

		NCached::UserSettings.bNoDialog = FApp::IsUnattended() || IsRunningDedicatedServer();
	}

	// Create a unique base guid for bug report ids
	const FGuid Guid = FGuid::NewGuid();
	const FString IniPlatformName(FPlatformProperties::IniPlatformName());
	NCached::Set(NCached::Session.CrashGUIDRoot, *FString::Printf(TEXT("%s%s-%s"), CrashGUIDRootPrefix, *IniPlatformName, *Guid.ToString(EGuidFormats::Digits)));

	if (GIsRunning)
	{
		if (FInternationalization::IsAvailable())
		{
			NCached::Session.LanguageLCID = FInternationalization::Get().GetCurrentCulture()->GetLCID();
		}
		else
		{
			FCulturePtr DefaultCulture = FInternationalization::Get().GetCulture(TEXT("en"));
			if (DefaultCulture.IsValid())
			{
				NCached::Session.LanguageLCID = DefaultCulture->GetLCID();
			}
			else
			{
				const int DefaultCultureLCID = 1033;
				NCached::Session.LanguageLCID = DefaultCultureLCID;
			}
		}
	}

	// Initialize delegate for updating SecondsSinceStart, because FPlatformTime::Seconds() is not POSIX safe.
	const float PollingInterval = 1.0f;
	FTicker::GetCoreTicker().AddTicker( FTickerDelegate::CreateLambda( []( float DeltaTime )
	{
    QUICK_SCOPE_CYCLE_COUNTER(STAT_NCachedCrashContextProperties_LambdaTicker);

		NCached::Session.SecondsSinceStart = int32(FPlatformTime::Seconds() - GStartTime);
		return true;
	} ), PollingInterval );

	FCoreDelegates::UserActivityStringChanged.AddLambda([](const FString& InUserActivity)
	{
		NCached::Set(NCached::Session.UserActivityHint, *InUserActivity);
	});

	FCoreDelegates::GameSessionIDChanged.AddLambda([](const FString& InGameSessionID)
	{
		NCached::Set(NCached::Session.GameSessionID, *InGameSessionID);
	});

	FCoreDelegates::GameStateClassChanged.AddLambda([](const FString& InGameStateName)
	{
		NCached::Set(NCached::Session.GameStateName, *InGameStateName);
	});

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FCoreDelegates::CrashOverrideParamsChanged.AddLambda([](const FCrashOverrideParameters& InParams)
	{
		if (InParams.bSetCrashReportClientMessageText)
		{
			NCached::Set(NCached::Session.CrashReportClientRichText, *InParams.CrashReportClientMessageText);
		}
		if (InParams.bSetGameNameSuffix)
		{
			NCached::Set(NCached::Session.GameName, *(FString(TEXT("UE4-")) + FApp::GetProjectName() + InParams.GameNameSuffix));
		}
		if (InParams.SendUnattendedBugReports.IsSet())
		{
			NCached::UserSettings.bSendUnattendedBugReports = InParams.SendUnattendedBugReports.GetValue();
		}
		if (InParams.SendUsageData.IsSet())
		{
			NCached::UserSettings.bSendUsageData = InParams.SendUsageData.GetValue();
		}
		SerializeTempCrashContextToFile();
	});
PRAGMA_ENABLE_DEPRECATION_WARNINGS

	FCoreDelegates::OnPostEngineInit.AddLambda([] 
	{
		// IsEditor global have been properly initialized here.
		NCached::Set(NCached::Session.EngineMode, FGenericPlatformMisc::GetEngineMode());
	});

	FCoreDelegates::IsVanillaProductChanged.AddLambda([](bool bIsVanilla)
	{
		NCached::Set(NCached::Session.EngineModeEx, bIsVanilla ? FGenericCrashContext::EngineModeExVanilla : FGenericCrashContext::EngineModeExDirty);
	});

	FCoreDelegates::ConfigReadyForUse.AddStatic(FGenericCrashContext::InitializeFromConfig);

	SerializeTempCrashContextToFile();

	CleanupPlatformSpecificFiles();

	bIsInitialized = true;
#endif	// !NOINITCRASHREPORTER
}

// When encoding the plugins list and engine and game data key/value pairs into the dynamic data segment
// we use 1 and 2 to denote delimiter and equals respectively. This is necessary since the values could 
// contain any characters normally used for delimiting.
const TCHAR* CR_PAIR_DELIM = TEXT("\x01");
const TCHAR* CR_PAIR_EQ = TEXT("\x02");

void FGenericCrashContext::InitializeFromContext(const FSessionContext& Session, const TCHAR* EnabledPluginsStr, const TCHAR* EngineDataStr, const TCHAR* GameDataStr)
{
	static const TCHAR* TokenDelim[] = { CR_PAIR_DELIM, CR_PAIR_EQ };

	// Copy the session struct which should be all pod types and fixed size buggers
	FMemory::Memcpy(NCached::Session, Session);
	
	// Parse the loaded plugins string, assume comma delimited values.
	if (EnabledPluginsStr)
	{
		TArray<FString> Tokens;
		FString(EnabledPluginsStr).ParseIntoArray(Tokens, TokenDelim, 2, true);
		NCached::EnabledPluginsList.Append(Tokens);
	}

	// Parse engine data, comma delimited key=value pairs.
	if (EngineDataStr)
	{
		TArray<FString> Tokens;
		FString(EngineDataStr).ParseIntoArray(Tokens, TokenDelim, 2, true);
		int32 i = 0;
		while ((i + 1) < Tokens.Num())
		{
			const FString& Key = Tokens[i++];
			const FString& Value = Tokens[i++];
			NCached::EngineData.Add(Key, Value);
		}
	}

	// Parse engine data, comma delimited key=value pairs.
	if (GameDataStr)
	{
		TArray<FString> Tokens;
		FString(GameDataStr).ParseIntoArray(Tokens, TokenDelim, 2, true);
		int32 i = 0;
		while ((i + 1) < Tokens.Num())
		{
			const FString& Key = Tokens[i++];
			const FString& Value = Tokens[i++];
			NCached::GameData.Add(Key, Value);
		}
	}

	SerializeTempCrashContextToFile();

	bIsInitialized = true;
}

void FGenericCrashContext::CopySharedCrashContext(FSharedCrashContext& Dst)
{
	//Copy the session
	FMemory::Memcpy(Dst.SessionContext, NCached::Session);
	FMemory::Memcpy(Dst.UserSettings, NCached::UserSettings);
	FMemory::Memset(Dst.DynamicData, 0);

	TCHAR* DynamicDataStart = &Dst.DynamicData[0];
	TCHAR* DynamicDataPtr = DynamicDataStart;

	#define CR_DYNAMIC_BUFFER_REMAIN uint32((CR_MAX_DYNAMIC_BUFFER_CHARS) - (DynamicDataPtr-DynamicDataStart))

	Dst.EnabledPluginsOffset = (uint32)(DynamicDataPtr - DynamicDataStart);
	Dst.EnabledPluginsNum = NCached::EnabledPluginsList.Num();
	for (const FString& Plugin : NCached::EnabledPluginsList)
	{
		FCString::Strncat(DynamicDataPtr, *Plugin, CR_DYNAMIC_BUFFER_REMAIN);
		FCString::Strncat(DynamicDataPtr, CR_PAIR_DELIM, CR_DYNAMIC_BUFFER_REMAIN);
	}
	DynamicDataPtr += FCString::Strlen(DynamicDataPtr) + 1;

	Dst.EngineDataOffset = (uint32)(DynamicDataPtr - DynamicDataStart);
	Dst.EngineDataNum = NCached::EngineData.Num();
	for (const TPair<FString, FString>& Pair : NCached::EngineData)
	{
		FCString::Strncat(DynamicDataPtr, *Pair.Key, CR_DYNAMIC_BUFFER_REMAIN);
		FCString::Strncat(DynamicDataPtr, CR_PAIR_EQ, CR_DYNAMIC_BUFFER_REMAIN);
		FCString::Strncat(DynamicDataPtr, *Pair.Value, CR_DYNAMIC_BUFFER_REMAIN);
		FCString::Strncat(DynamicDataPtr, CR_PAIR_DELIM, CR_DYNAMIC_BUFFER_REMAIN);
	}
	DynamicDataPtr += FCString::Strlen(DynamicDataPtr) + 1;

	Dst.GameDataOffset = (uint32)(DynamicDataPtr - DynamicDataStart);
	Dst.GameDataNum = NCached::GameData.Num();
	for (const TPair<FString, FString>& Pair : NCached::GameData)
	{
		FCString::Strncat(DynamicDataPtr, *Pair.Key, CR_DYNAMIC_BUFFER_REMAIN);
		FCString::Strncat(DynamicDataPtr, CR_PAIR_EQ, CR_DYNAMIC_BUFFER_REMAIN);
		FCString::Strncat(DynamicDataPtr, *Pair.Value, CR_DYNAMIC_BUFFER_REMAIN);
		FCString::Strncat(DynamicDataPtr, CR_PAIR_DELIM, CR_DYNAMIC_BUFFER_REMAIN);
	}
	DynamicDataPtr += FCString::Strlen(DynamicDataPtr) + 1;

	#undef CR_DYNAMIC_BUFFER_REMAIN
}

void FGenericCrashContext::SetMemoryStats(const FPlatformMemoryStats& InMemoryStats)
{
	NCached::Session.MemoryStats = InMemoryStats;

	// Update cached OOM stats
	NCached::Session.bIsOOM = FPlatformMemory::bIsOOM;
	NCached::Session.OOMAllocationSize = FPlatformMemory::OOMAllocationSize;
	NCached::Session.OOMAllocationAlignment = FPlatformMemory::OOMAllocationAlignment;

	SerializeTempCrashContextToFile();
}

void FGenericCrashContext::InitializeFromConfig()
{
#if !NOINITCRASHREPORTER
	PurgeOldCrashConfig();

	const bool bForceGetSection = false;
	const bool bConstSection = true;
	FConfigSection* CRCConfigSection = GConfig->GetSectionPrivate(ConfigSectionName, bForceGetSection, bConstSection, GEngineIni);

	if (CRCConfigSection != nullptr)
	{
		// Create a config file and save to a temp location. This file will be copied to
		// the crash folder for all crash reports create by this session.
		FConfigFile CrashConfigFile;

		FConfigSection CRCConfigSectionCopy(*CRCConfigSection);
		CrashConfigFile.Add(ConfigSectionName, CRCConfigSectionCopy);

		CrashConfigFile.Dirty = true;
		CrashConfigFile.Write(FString(GetCrashConfigFilePath()));
	}

	// Read the initial un-localized crash context text
	UpdateLocalizedStrings();

	// Set privacy settings -> WARNING: Ensure those setting have a default values in Engine/Config/BaseEditorSettings.ini file, otherwise, they will not be found.
	GConfig->GetBool(TEXT("/Script/UnrealEd.CrashReportsPrivacySettings"), TEXT("bSendUnattendedBugReports"), NCached::UserSettings.bSendUnattendedBugReports, GEditorSettingsIni);
	GConfig->GetBool(TEXT("/Script/UnrealEd.AnalyticsPrivacySettings"), TEXT("bSendUsageData"), NCached::UserSettings.bSendUsageData, GEditorSettingsIni);
	
	// Write a marker file to disk indicating the user has allowed unattended crash reports being
	// sent. This allows us to submit reports for crashes during static initialization when user
	// settings are not available. 
	FString MarkerFilePath = FString::Printf(TEXT("%s/NotAllowedUnattendedBugReports"), FPlatformProcess::ApplicationSettingsDir());
	if (!NCached::UserSettings.bSendUnattendedBugReports)
	{
		TUniquePtr<IFileHandle> File(FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*MarkerFilePath));
	}

	// Make sure we get updated text once the localized version is loaded
	FTextLocalizationManager::Get().OnTextRevisionChangedEvent.AddStatic(&UpdateLocalizedStrings);

	SerializeTempCrashContextToFile();

#endif	// !NOINITCRASHREPORTER
}

void FGenericCrashContext::UpdateLocalizedStrings()
{
#if !NOINITCRASHREPORTER
	// Allow overriding the crash text
	FText CrashReportClientRichText;
	if (GConfig->GetText(TEXT("CrashContextProperties"), TEXT("CrashReportClientRichText"), CrashReportClientRichText, GEngineIni))
	{
		NCached::Set(NCached::Session.CrashReportClientRichText, *CrashReportClientRichText.ToString());
	}
#endif
}

FGenericCrashContext::FGenericCrashContext(ECrashContextType InType, const TCHAR* InErrorMessage)
	: Type(InType)
	, CrashedThreadId(~uint32(0))
	, ErrorMessage(InErrorMessage)
	, NumMinidumpFramesToIgnore(0)
{
	CommonBuffer.Reserve( 32768 );
	CrashContextIndex = StaticCrashContextIndex++;
}

FString FGenericCrashContext::GetTempSessionContextFilePath(uint64 ProcessID)
{
	return FPlatformProcess::UserTempDir() / FString::Printf(TEXT("UECrashContext-%u.xml"), ProcessID);
}

TOptional<int32> FGenericCrashContext::GetOutOfProcessCrashReporterExitCode()
{
	TOptional<int32> ExitCode;
	int64 ExitCodeData = FPlatformAtomics::AtomicRead(&OutOfProcessCrashReporterExitCode);
	if (ExitCodeData & 0xFFFFFFFF00000000) // If one bit in the 32 MSB is set, the out of process exit code is set in the 32 LSB.
	{
		ExitCode.Emplace(static_cast<int32>(ExitCodeData)); // Truncate the 32 MSB.
	}
	return ExitCode;
}

void FGenericCrashContext::SetOutOfProcessCrashReporterExitCode(int32 ExitCode)
{
	int64 ExitCodeData = (1ll << 32) | ExitCode; // Set a bit in the 32 MSB to signal that the exit code is set
	FPlatformAtomics::AtomicStore(&OutOfProcessCrashReporterExitCode, ExitCodeData);
}

void FGenericCrashContext::SerializeTempCrashContextToFile()
{
	if (!IsOutOfProcessCrashReporter())
	{
		return;
	}

	FString SessionBuffer;
	SessionBuffer.Reserve(32 * 1024);

	AddHeader(SessionBuffer);

	SerializeSessionContext(SessionBuffer);
	SerializeUserSettings(SessionBuffer);

	AddFooter(SessionBuffer);

	const FString SessionFilePath = GetTempSessionContextFilePath(NCached::Session.ProcessId);
	FFileHelper::SaveStringToFile(SessionBuffer, *SessionFilePath);
}

void FGenericCrashContext::SerializeSessionContext(FString& Buffer)
{
	AddCrashPropertyInternal(Buffer, TEXT("ProcessId"), NCached::Session.ProcessId);
	AddCrashPropertyInternal(Buffer, TEXT("SecondsSinceStart"), NCached::Session.SecondsSinceStart);

	AddCrashPropertyInternal(Buffer, TEXT("IsInternalBuild"), NCached::Session.bIsInternalBuild);
	AddCrashPropertyInternal(Buffer, TEXT("IsPerforceBuild"), NCached::Session.bIsPerforceBuild);
	AddCrashPropertyInternal(Buffer, TEXT("IsSourceDistribution"), NCached::Session.bIsSourceDistribution);

	if (FCString::Strlen(NCached::Session.GameName) > 0)
	{
		AddCrashPropertyInternal(Buffer, TEXT("GameName"), NCached::Session.GameName);
	}
	else
	{
		const TCHAR* ProjectName = FApp::GetProjectName();
		if (ProjectName != nullptr && ProjectName[0] != 0)
		{
			AddCrashPropertyInternal(Buffer, TEXT("GameName"), *FString::Printf(TEXT("UE4-%s"), ProjectName));
		}
		else
		{
			AddCrashPropertyInternal(Buffer, TEXT("GameName"), TEXT(""));
		}
	}
	AddCrashPropertyInternal(Buffer, TEXT("ExecutableName"), NCached::Session.ExecutableName);
	AddCrashPropertyInternal(Buffer, TEXT("BuildConfiguration"), NCached::Session.BuildConfigurationName);
	AddCrashPropertyInternal(Buffer, TEXT("GameSessionID"), NCached::Session.GameSessionID);

	// Unique string specifying the symbols to be used by CrashReporter
#ifdef UE_SYMBOLS_VERSION
	FString Symbols = FString(UE_SYMBOLS_VERSION);
#else
	FString Symbols = FString::Printf(TEXT("%s"), FApp::GetBuildVersion());
#endif
#ifdef UE_APP_FLAVOR
	Symbols = FString::Printf(TEXT("%s-%s"), *Symbols, *FString(UE_APP_FLAVOR));
#endif
	Symbols = FString::Printf(TEXT("%s-%s-%s"), *Symbols, FPlatformMisc::GetUBTPlatform(), NCached::Session.BuildConfigurationName).Replace(TEXT("+"), TEXT("*"));
#ifdef UE_BUILD_FLAVOR
	Symbols = FString::Printf(TEXT("%s-%s"), *Symbols, *FString(UE_BUILD_FLAVOR));
#endif

	AddCrashPropertyInternal(Buffer, TEXT("Symbols"), Symbols);

	AddCrashPropertyInternal(Buffer, TEXT("PlatformName"), NCached::Session.PlatformName);
	AddCrashPropertyInternal(Buffer, TEXT("PlatformNameIni"), NCached::Session.PlatformNameIni);
	AddCrashPropertyInternal(Buffer, TEXT("EngineMode"), NCached::Session.EngineMode);
	AddCrashPropertyInternal(Buffer, TEXT("EngineModeEx"), NCached::Session.EngineModeEx);

	AddCrashPropertyInternal(Buffer, TEXT("DeploymentName"), NCached::Session.DeploymentName);

	AddCrashPropertyInternal(Buffer, TEXT("EngineVersion"), *FEngineVersion::Current().ToString());
	AddCrashPropertyInternal(Buffer, TEXT("CommandLine"), NCached::Session.CommandLine);
	AddCrashPropertyInternal(Buffer, TEXT("LanguageLCID"), NCached::Session.LanguageLCID);
	AddCrashPropertyInternal(Buffer, TEXT("AppDefaultLocale"), NCached::Session.DefaultLocale);
	AddCrashPropertyInternal(Buffer, TEXT("BuildVersion"), FApp::GetBuildVersion());
	AddCrashPropertyInternal(Buffer, TEXT("IsUE4Release"), NCached::Session.bIsUE4Release);

	// Need to set this at the time of the crash to check if requesting exit had been called
	AddCrashPropertyInternal(Buffer, TEXT("IsRequestingExit"), NCached::Session.bIsExitRequested);

	// Remove periods from user names to match AutoReporter user names
	// The name prefix is read by CrashRepository.AddNewCrash in the website code
	const bool bSendUserName = NCached::Session.bIsInternalBuild;
	FString SanitizedUserName = FString(NCached::Session.UserName).Replace(TEXT("."), TEXT(""));
	AddCrashPropertyInternal(Buffer, TEXT("UserName"), bSendUserName ? *SanitizedUserName : TEXT(""));

	AddCrashPropertyInternal(Buffer, TEXT("BaseDir"), NCached::Session.BaseDir);
	AddCrashPropertyInternal(Buffer, TEXT("RootDir"), NCached::Session.RootDir);
	AddCrashPropertyInternal(Buffer, TEXT("MachineId"), *FString(NCached::Session.LoginIdStr).ToUpper());
	AddCrashPropertyInternal(Buffer, TEXT("LoginId"), NCached::Session.LoginIdStr);
	AddCrashPropertyInternal(Buffer, TEXT("EpicAccountId"), NCached::Session.EpicAccountId);

	AddCrashPropertyInternal(Buffer, TEXT("SourceContext"), TEXT(""));
	AddCrashPropertyInternal(Buffer, TEXT("UserDescription"), TEXT(""));
	AddCrashPropertyInternal(Buffer, TEXT("UserActivityHint"), NCached::Session.UserActivityHint);
	AddCrashPropertyInternal(Buffer, TEXT("CrashDumpMode"), NCached::Session.CrashDumpMode);
	AddCrashPropertyInternal(Buffer, TEXT("GameStateName"), NCached::Session.GameStateName);

	// Add misc stats.
	AddCrashPropertyInternal(Buffer, TEXT("Misc.NumberOfCores"), NCached::Session.NumberOfCores);
	AddCrashPropertyInternal(Buffer, TEXT("Misc.NumberOfCoresIncludingHyperthreads"), NCached::Session.NumberOfCoresIncludingHyperthreads);
	AddCrashPropertyInternal(Buffer, TEXT("Misc.Is64bitOperatingSystem"), (int32) FPlatformMisc::Is64bitOperatingSystem());

	AddCrashPropertyInternal(Buffer, TEXT("Misc.CPUVendor"), NCached::Session.CPUVendor);
	AddCrashPropertyInternal(Buffer, TEXT("Misc.CPUBrand"), NCached::Session.CPUBrand);
	AddCrashPropertyInternal(Buffer, TEXT("Misc.PrimaryGPUBrand"), NCached::Session.PrimaryGPUBrand);
	AddCrashPropertyInternal(Buffer, TEXT("Misc.OSVersionMajor"), NCached::Session.OsVersion);
	AddCrashPropertyInternal(Buffer, TEXT("Misc.OSVersionMinor"), NCached::Session.OsSubVersion);

	// FPlatformMemory::GetConstants is called in the GCreateMalloc, so we can assume it is always valid.
	{
		// Add memory stats.
		const FPlatformMemoryConstants& MemConstants = FPlatformMemory::GetConstants();

		AddCrashPropertyInternal(Buffer, TEXT("MemoryStats.TotalPhysical"), (uint64) MemConstants.TotalPhysical);
		AddCrashPropertyInternal(Buffer, TEXT("MemoryStats.TotalVirtual"), (uint64) MemConstants.TotalVirtual);
		AddCrashPropertyInternal(Buffer, TEXT("MemoryStats.PageSize"), (uint64) MemConstants.PageSize);
		AddCrashPropertyInternal(Buffer, TEXT("MemoryStats.TotalPhysicalGB"), MemConstants.TotalPhysicalGB);
	}

	AddCrashPropertyInternal(Buffer, TEXT("MemoryStats.AvailablePhysical"), (uint64) NCached::Session.MemoryStats.AvailablePhysical);
	AddCrashPropertyInternal(Buffer, TEXT("MemoryStats.AvailableVirtual"), (uint64) NCached::Session.MemoryStats.AvailableVirtual);
	AddCrashPropertyInternal(Buffer, TEXT("MemoryStats.UsedPhysical"), (uint64) NCached::Session.MemoryStats.UsedPhysical);
	AddCrashPropertyInternal(Buffer, TEXT("MemoryStats.PeakUsedPhysical"), (uint64) NCached::Session.MemoryStats.PeakUsedPhysical);
	AddCrashPropertyInternal(Buffer, TEXT("MemoryStats.UsedVirtual"), (uint64) NCached::Session.MemoryStats.UsedVirtual);
	AddCrashPropertyInternal(Buffer, TEXT("MemoryStats.PeakUsedVirtual"), (uint64) NCached::Session.MemoryStats.PeakUsedVirtual);
	AddCrashPropertyInternal(Buffer, TEXT("MemoryStats.bIsOOM"), (int) NCached::Session.bIsOOM);
	AddCrashPropertyInternal(Buffer, TEXT("MemoryStats.OOMAllocationSize"), NCached::Session.OOMAllocationSize);
	AddCrashPropertyInternal(Buffer, TEXT("MemoryStats.OOMAllocationAlignment"), NCached::Session.OOMAllocationAlignment);
}

void FGenericCrashContext::SerializeUserSettings(FString& Buffer)
{
	AddCrashPropertyInternal(Buffer, TEXT("NoDialog"), NCached::UserSettings.bNoDialog);
	AddCrashPropertyInternal(Buffer, TEXT("SendUnattendedBugReports"), NCached::UserSettings.bSendUnattendedBugReports);
	AddCrashPropertyInternal(Buffer, TEXT("SendUsageData"), NCached::UserSettings.bSendUsageData);
	AddCrashPropertyInternal(Buffer, TEXT("LogFilePath"), FPlatformOutputDevices::GetAbsoluteLogFilename()); // Don't use the value cached, it may be out of date.
}

void FGenericCrashContext::SerializeContentToBuffer() const
{
	TCHAR CrashGUID[CrashGUIDLength];
	GetUniqueCrashName(CrashGUID, CrashGUIDLength);

	// Must conform against:
	// https://www.securecoding.cert.org/confluence/display/seccode/SIG30-C.+Call+only+asynchronous-safe+functions+within+signal+handlers
	AddHeader(CommonBuffer);

	BeginSection( CommonBuffer, RuntimePropertiesTag );
	AddCrashProperty( TEXT( "CrashVersion" ), (int32)ECrashDescVersions::VER_3_CrashContext );
	AddCrashProperty( TEXT( "ExecutionGuid" ), *ExecutionGuid.ToString() );
	AddCrashProperty( TEXT( "CrashGUID" ), (const TCHAR*)CrashGUID);

	AddCrashProperty( TEXT( "IsEnsure" ), (Type == ECrashContextType::Ensure) );
	AddCrashProperty( TEXT( "IsAssert" ), (Type == ECrashContextType::Assert) );
	AddCrashProperty( TEXT( "CrashType" ), GetCrashTypeString(Type) );
	AddCrashProperty( TEXT( "ErrorMessage" ), ErrorMessage );
	AddCrashProperty( TEXT( "CrashReporterMessage" ), NCached::Session.CrashReportClientRichText );

	SerializeSessionContext(CommonBuffer);

	// Legacy callstack element for current crash reporter
	AddCrashProperty( TEXT( "NumMinidumpFramesToIgnore"), NumMinidumpFramesToIgnore );
	// Allow platforms to override callstack property, on some platforms the callstack is not captured by native code, those callstacks can be substituted by platform code here.
	{
		CommonBuffer += TEXT("<CallStack>");
		CommonBuffer += GetCallstackProperty();
		CommonBuffer += TEXT("</CallStack>");
		CommonBuffer += LINE_TERMINATOR;
	}

	// Add new portable callstack element with crash stack
	AddPortableCallStack();
	AddPortableCallStackHash();

	{
		FString AllThreadStacks;
		if (GetPlatformAllThreadContextsString(AllThreadStacks))
		{
			CommonBuffer += TEXT("<Threads>");
			CommonBuffer += AllThreadStacks;
			CommonBuffer += TEXT("</Threads>");
			CommonBuffer += LINE_TERMINATOR;
		}
	}

	EndSection( CommonBuffer, RuntimePropertiesTag );

	// Add platform specific properties.
	BeginSection( CommonBuffer, PlatformPropertiesTag );
	AddPlatformSpecificProperties();
	// The name here is a bit cryptic, but we keep it to avoid breaking backend stuff.
	AddCrashProperty(TEXT("PlatformCallbackResult"), NCached::Session.CrashType);
	EndSection( CommonBuffer, PlatformPropertiesTag );

	// Add the engine data
	BeginSection( CommonBuffer, EngineDataTag );
	for (const TPair<FString, FString>& Pair : NCached::EngineData)
	{
		AddCrashProperty( *Pair.Key, *Pair.Value);
	}
	EndSection( CommonBuffer, EngineDataTag );

	// Add the game data
	BeginSection( CommonBuffer, GameDataTag );
	for (const TPair<FString, FString>& Pair : NCached::GameData)
	{
		AddCrashProperty( *Pair.Key, *Pair.Value);
	}
	EndSection( CommonBuffer, GameDataTag );

	// Writing out the list of plugin JSON descriptors causes us to run out of memory
	// in GMallocCrash on console, so enable this only for desktop platforms.
#if PLATFORM_DESKTOP
	if (NCached::EnabledPluginsList.Num() > 0)
	{
		BeginSection(CommonBuffer, EnabledPluginsTag);

		for (const FString& Str : NCached::EnabledPluginsList)
		{
			AddCrashProperty(TEXT("Plugin"), *Str);
		}

		EndSection(CommonBuffer, EnabledPluginsTag);
	}
#endif // PLATFORM_DESKTOP

	AddFooter(CommonBuffer);
}

const TCHAR* FGenericCrashContext::GetCallstackProperty() const
{
	return TEXT("");
}

void FGenericCrashContext::SetEngineExit(bool bIsExiting)
{
	NCached::Session.bIsExitRequested = IsEngineExitRequested();
}

void FGenericCrashContext::SetNumMinidumpFramesToIgnore(int InNumMinidumpFramesToIgnore)
{
	NumMinidumpFramesToIgnore = InNumMinidumpFramesToIgnore;
}

void FGenericCrashContext::SetDeploymentName(const FString& EpicApp)
{
	NCached::Set(NCached::Session.DeploymentName, *EpicApp);
}

void FGenericCrashContext::SetCrashTrigger(ECrashTrigger Type)
{
	NCached::Session.CrashType = (int32)Type;
}

void FGenericCrashContext::GetUniqueCrashName(TCHAR* GUIDBuffer, int32 BufferSize) const
{
	FCString::Snprintf(GUIDBuffer, BufferSize, TEXT("%s_%04i"), NCached::Session.CrashGUIDRoot, CrashContextIndex);
}

const bool FGenericCrashContext::IsFullCrashDump() const
{
	if(Type == ECrashContextType::Ensure)
	{
		return (NCached::Session.CrashDumpMode == (int32)ECrashDumpMode::FullDumpAlways);
	}
	else
	{
		return (NCached::Session.CrashDumpMode == (int32)ECrashDumpMode::FullDump) ||
			(NCached::Session.CrashDumpMode == (int32)ECrashDumpMode::FullDumpAlways);
	}
}

void FGenericCrashContext::SerializeAsXML( const TCHAR* Filename ) const
{
	SerializeContentToBuffer();
	// Use OS build-in functionality instead.
	FFileHelper::SaveStringToFile( CommonBuffer, Filename, FFileHelper::EEncodingOptions::AutoDetect );
}

void FGenericCrashContext::AddCrashPropertyInternal(FString& Buffer, const TCHAR* PropertyName, const TCHAR* PropertyValue)
{
	Buffer += TEXT( "<" );
	Buffer += PropertyName;
	Buffer += TEXT( ">" );

	AppendEscapedXMLString(Buffer, PropertyValue);

	Buffer += TEXT( "</" );
	Buffer += PropertyName;
	Buffer += TEXT( ">" );
	Buffer += LINE_TERMINATOR;
}

void FGenericCrashContext::AddPlatformSpecificProperties() const
{
	// Nothing really to do here. Can be overridden by the platform code.
	// @see FWindowsPlatformCrashContext::AddPlatformSpecificProperties
}

void FGenericCrashContext::AddPortableCallStackHash() const
{
	if (CallStack.Num() == 0)
	{
		AddCrashProperty(TEXT("PCallStackHash"), TEXT(""));
		return;
	}

	// This may allocate if its the first time calling into this function
	const TCHAR* ExeName = FPlatformProcess::ExecutableName();

	// We dont want this to be thrown into an FString as it will alloc memory
	const TCHAR* UE4EditorName = TEXT("UE4Editor");

	FSHA1 Sha;
	FSHAHash Hash;

	for (TArray<FCrashStackFrame>::TConstIterator It(CallStack); It; ++It)
	{
		// If we are our own module or our module contains UE4Editor we assume we own these. We cannot depend on offsets of system libs
		// as they may have different versions
		if (It->ModuleName == ExeName || It->ModuleName.Contains(UE4EditorName))
		{
			Sha.Update(reinterpret_cast<const uint8*>(&It->Offset), sizeof(It->Offset));
		}
	}

	Sha.Final();
	Sha.GetHash(Hash.Hash);

	FString EscapedPortableHash;

	// Allocations here on both the ToString and AppendEscapedXMLString it self adds to the out FString
	AppendEscapedXMLString(EscapedPortableHash, *Hash.ToString());

	AddCrashProperty(TEXT("PCallStackHash"), *EscapedPortableHash);
}

void FGenericCrashContext::AddPortableCallStack() const
{	
	if (CallStack.Num() == 0)
	{
		AddCrashProperty(TEXT("PCallStack"), TEXT(""));
		return;
	}

	FString CrashStackBuffer = LINE_TERMINATOR;

	// Get the max module name length for padding
	int32 MaxModuleLength = 0;
	for (TArray<FCrashStackFrame>::TConstIterator It(CallStack); It; ++It)
	{
		MaxModuleLength = FMath::Max(MaxModuleLength, It->ModuleName.Len());
	}

	for (TArray<FCrashStackFrame>::TConstIterator It(CallStack); It; ++It)
	{
		CrashStackBuffer += FString::Printf(TEXT("%-*s 0x%016llx + %-16llx"),MaxModuleLength + 1, *It->ModuleName, It->BaseAddress, It->Offset);
		CrashStackBuffer += LINE_TERMINATOR;
	}

	FString EscapedStackBuffer;

	AppendEscapedXMLString(EscapedStackBuffer, *CrashStackBuffer);

	AddCrashProperty(TEXT("PCallStack"), *EscapedStackBuffer);
}

void FGenericCrashContext::AddHeader(FString& Buffer)
{
	Buffer += TEXT("<?xml version=\"1.0\" encoding=\"UTF-8\"?>") LINE_TERMINATOR;
	BeginSection(Buffer, TEXT("FGenericCrashContext") );
}

void FGenericCrashContext::AddFooter(FString& Buffer)
{
	EndSection(Buffer, TEXT( "FGenericCrashContext" ));
}

void FGenericCrashContext::BeginSection(FString& Buffer, const TCHAR* SectionName)
{
	Buffer += TEXT( "<" );
	Buffer += SectionName;
	Buffer += TEXT(">");
	Buffer += LINE_TERMINATOR;
}

void FGenericCrashContext::EndSection(FString& Buffer, const TCHAR* SectionName)
{
	Buffer += TEXT( "</" );
	Buffer += SectionName;
	Buffer += TEXT( ">" );
	Buffer += LINE_TERMINATOR;
}

void FGenericCrashContext::AppendEscapedXMLString(FString& OutBuffer, const TCHAR* Text)
{
	if (!Text)
	{
		return;
	}

	while (*Text)
	{
		switch (*Text)
		{
		case TCHAR('&'):
			OutBuffer += TEXT("&amp;");
			break;
		case TCHAR('"'):
			OutBuffer += TEXT("&quot;");
			break;
		case TCHAR('\''):
			OutBuffer += TEXT("&apos;");
			break;
		case TCHAR('<'):
			OutBuffer += TEXT("&lt;");
			break;
		case TCHAR('>'):
			OutBuffer += TEXT("&gt;");
			break;
		case TCHAR('\r'):
			break;
		default:
			OutBuffer += *Text;
		};

		Text++;
	}
}

FString FGenericCrashContext::UnescapeXMLString( const FString& Text )
{
	return Text
		.Replace(TEXT("&amp;"), TEXT("&"))
		.Replace(TEXT("&quot;"), TEXT("\""))
		.Replace(TEXT("&apos;"), TEXT("'"))
		.Replace(TEXT("&lt;"), TEXT("<"))
		.Replace(TEXT("&gt;"), TEXT(">"));
}

FString FGenericCrashContext::GetCrashGameName()
{
	return FString(NCached::Session.GameName);
}

const TCHAR* FGenericCrashContext::GetCrashTypeString(ECrashContextType Type)
{
	switch (Type)
	{
	case ECrashContextType::Hang:
		return CrashTypeHang;
	case ECrashContextType::GPUCrash:
		return CrashTypeGPU;
	case ECrashContextType::Ensure:
		return CrashTypeEnsure;
	case ECrashContextType::Assert:
		return CrashTypeAssert;
	case ECrashContextType::AbnormalShutdown:
		return CrashTypeAbnormalShutdown;
	default:
		return CrashTypeCrash;
	}
}

const TCHAR* FGenericCrashContext::GetCrashConfigFilePath()
{
	if (FCString::Strlen(NCached::Session.CrashConfigFilePath) == 0)
	{
		FString CrashConfigFilePath = FPaths::Combine(GetCrashConfigFolder(), NCached::Session.CrashGUIDRoot, FGenericCrashContext::CrashConfigFileNameW);
		CrashConfigFilePath = IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*CrashConfigFilePath);
		NCached::Set(NCached::Session.CrashConfigFilePath, *CrashConfigFilePath);
	}
	return NCached::Session.CrashConfigFilePath;
}

const TCHAR* FGenericCrashContext::GetCrashConfigFolder()
{
	static FString CrashConfigFolder;
	if (CrashConfigFolder.IsEmpty())
	{
		CrashConfigFolder = FPaths::Combine(*FPaths::GeneratedConfigDir(), TEXT("CrashReportClient"));
	}
	return *CrashConfigFolder;
}

void FGenericCrashContext::PurgeOldCrashConfig()
{
	int32 PurgeDays = 2;
	GConfig->GetInt(ConfigSectionName, CrashConfigPurgeDays, PurgeDays, GEngineIni);

	if (PurgeDays > 0)
	{
		IFileManager& FileManager = IFileManager::Get();

		// Delete items older than PurgeDays
		TArray<FString> Directories;
		FileManager.FindFiles(Directories, *(FPaths::Combine(GetCrashConfigFolder(), CrashGUIDRootPrefix) + TEXT("*")), false, true);

		for (const FString& Dir : Directories)
		{
			const FString CrashConfigDirectory = FPaths::Combine(GetCrashConfigFolder(), *Dir);
			const FDateTime DirectoryAccessTime = FileManager.GetTimeStamp(*CrashConfigDirectory);
			if (FDateTime::Now() - DirectoryAccessTime > FTimespan::FromDays(PurgeDays))
			{
				FileManager.DeleteDirectory(*CrashConfigDirectory, false, true);
			}
		}
	}
}

void FGenericCrashContext::ResetEngineData()
{
	NCached::EngineData.Reset();
}

void FGenericCrashContext::SetEngineData(const FString& Key, const FString& Value)
{
	if (Value.Len() == 0)
	{
		// for testing purposes, only log values when they change, but don't pay the lookup price normally.
		UE_SUPPRESS(LogCrashContext, VeryVerbose, 
		{
			if (NCached::EngineData.Find(Key))
			{
				UE_LOG(LogCrashContext, VeryVerbose, TEXT("FGenericCrashContext::SetEngineData(%s, <RemoveKey>)"), *Key);
			}
		});
		NCached::EngineData.Remove(Key);
	}
	else
	{
		FString& OldVal = NCached::EngineData.FindOrAdd(Key);
		UE_SUPPRESS(LogCrashContext, VeryVerbose, 
		{
			if (OldVal != Value)
			{
				UE_LOG(LogCrashContext, VeryVerbose, TEXT("FGenericCrashContext::SetEngineData(%s, %s)"), *Key, *Value);
			}
		});
		OldVal = Value;
	}
}

void FGenericCrashContext::ResetGameData()
{
	NCached::GameData.Reset();
}

void FGenericCrashContext::SetGameData(const FString& Key, const FString& Value)
{
	if (Value.Len() == 0)
	{
		// for testing purposes, only log values when they change, but don't pay the lookup price normally.
		UE_SUPPRESS(LogCrashContext, VeryVerbose, 
		{
			if (NCached::GameData.Find(Key))
			{
				UE_LOG(LogCrashContext, VeryVerbose, TEXT("FGenericCrashContext::SetGameData(%s, <RemoveKey>)"), *Key);
			}
		});
		NCached::GameData.Remove(Key);
	}
	else
	{
		FString& OldVal = NCached::GameData.FindOrAdd(Key);
		UE_SUPPRESS(LogCrashContext, VeryVerbose, 
		{
			if (OldVal != Value)
			{
				UE_LOG(LogCrashContext, VeryVerbose, TEXT("FGenericCrashContext::SetGameData(%s, %s)"), *Key, *Value);
			}
		});
		OldVal = Value;
	}
}

void FGenericCrashContext::AddPlugin(const FString& PluginDesc)
{
	NCached::EnabledPluginsList.Add(PluginDesc);
}

void FGenericCrashContext::DumpLog(const FString& CrashFolderAbsolute)
{
	// Copy log
	const FString LogSrcAbsolute = FPlatformOutputDevices::GetAbsoluteLogFilename();
	FString LogFilename = FPaths::GetCleanFilename(LogSrcAbsolute);
	const FString LogDstAbsolute = FPaths::Combine(*CrashFolderAbsolute, *LogFilename);

	// If we have a memory only log, make sure it's dumped to file before we attach it to the report
#if !NO_LOGGING
	bool bMemoryOnly = FPlatformOutputDevices::GetLog()->IsMemoryOnly();
	bool bBacklogEnabled = FOutputDeviceRedirector::Get()->IsBacklogEnabled();

	if (bMemoryOnly || bBacklogEnabled)
	{
		TUniquePtr<FArchive> LogFile(IFileManager::Get().CreateFileWriter(*LogDstAbsolute, FILEWRITE_AllowRead));
		if (LogFile)
		{
			if (bMemoryOnly)
			{
				FPlatformOutputDevices::GetLog()->Dump(*LogFile);
			}
			else
			{
				FOutputDeviceArchiveWrapper Wrapper(LogFile.Get());
				GLog->SerializeBacklog(&Wrapper);
			}

			LogFile->Flush();
		}
	}
	else
	{
		const bool bReplace = true;
		const bool bEvenIfReadOnly = false;
		const bool bAttributes = false;
		FCopyProgress* const CopyProgress = nullptr;
		static_cast<void>(IFileManager::Get().Copy(*LogDstAbsolute, *LogSrcAbsolute, bReplace, bEvenIfReadOnly, bAttributes, CopyProgress, FILEREAD_AllowWrite, FILEWRITE_AllowRead));	// best effort, so don't care about result: couldn't copy -> tough, no log
	}
#endif // !NO_LOGGING

	
}

FORCENOINLINE void FGenericCrashContext::CapturePortableCallStack(int32 NumStackFramesToIgnore, void* Context)
{
	// If the callstack is for the executing thread, ignore this function
	if(Context == nullptr)
	{
		NumStackFramesToIgnore++;
	}

	// Capture the stack trace
	static const int StackTraceMaxDepth = 100;
	uint64 StackTrace[StackTraceMaxDepth];
	FMemory::Memzero(StackTrace);
	int32 StackTraceDepth = FPlatformStackWalk::CaptureStackBackTrace(StackTrace, StackTraceMaxDepth, Context);

	// Make sure we don't exceed the current stack depth
	NumStackFramesToIgnore = FMath::Min(NumStackFramesToIgnore, StackTraceDepth);

	// Generate the portable callstack from it
	SetPortableCallStack(StackTrace + NumStackFramesToIgnore, StackTraceDepth - NumStackFramesToIgnore);
}

void FGenericCrashContext::SetPortableCallStack(const uint64* StackFrames, int32 NumStackFrames)
{
	GetPortableCallStack(StackFrames, NumStackFrames, CallStack);
}

void FGenericCrashContext::GetPortableCallStack(const uint64* StackFrames, int32 NumStackFrames, TArray<FCrashStackFrame>& OutCallStack) const
{
	// Get all the modules in the current process
	uint32 NumModules = (uint32)FPlatformStackWalk::GetProcessModuleCount();

	TArray<FStackWalkModuleInfo> Modules;
	Modules.AddUninitialized(NumModules);

	NumModules = FPlatformStackWalk::GetProcessModuleSignatures(Modules.GetData(), NumModules);
	Modules.SetNum(NumModules);

	// Update the callstack with offsets from each module
	OutCallStack.Reset(NumStackFrames);
	for(int32 Idx = 0; Idx < NumStackFrames; Idx++)
	{
		const uint64 StackFrame = StackFrames[Idx];

		// Try to find the module containing this stack frame
		const FStackWalkModuleInfo* FoundModule = nullptr;
		for(const FStackWalkModuleInfo& Module : Modules)
		{
			if(StackFrame >= Module.BaseOfImage && StackFrame < Module.BaseOfImage + Module.ImageSize)
			{
				FoundModule = &Module;
				break;
			}
		}

		// Add the callstack item
		if(FoundModule == nullptr)
		{
			OutCallStack.Add(FCrashStackFrame(TEXT("Unknown"), 0, StackFrame));
		}
		else
		{
			OutCallStack.Add(FCrashStackFrame(FPaths::GetBaseFilename(FoundModule->ImageName), FoundModule->BaseOfImage, StackFrame - FoundModule->BaseOfImage));
		}
	}
}

void FGenericCrashContext::AddPortableThreadCallStack(uint32 ThreadId, const TCHAR* ThreadName, const uint64* StackFrames, int32 NumStackFrames)
{
	// Not implemented for generic class
}



void FGenericCrashContext::CopyPlatformSpecificFiles(const TCHAR* OutputDirectory, void* Context)
{
	// If present, include the crash report config file to pass config values to the CRC
	const TCHAR* CrashConfigSrcPath = GetCrashConfigFilePath();
	if (IFileManager::Get().FileExists(CrashConfigSrcPath))
	{
		FString CrashConfigFilename = FPaths::GetCleanFilename(CrashConfigSrcPath);
		const FString CrashConfigDstAbsolute = FPaths::Combine(OutputDirectory, *CrashConfigFilename);
		IFileManager::Get().Copy(*CrashConfigDstAbsolute, CrashConfigSrcPath);	// best effort, so don't care about result: couldn't copy -> tough, no config
	}

	
}

#if WITH_ADDITIONAL_CRASH_CONTEXTS

thread_local FAdditionalCrashContextStack FAdditionalCrashContextStack::ThreadContextProvider;
static FAdditionalCrashContextStack* GProviderHead;

FCriticalSection* GetAdditionalProviderLock()
{
	static FCriticalSection GAdditionalProviderLock;
	return &GAdditionalProviderLock;
}

FAdditionalCrashContextStack::FAdditionalCrashContextStack()
	: Next(nullptr)
{
	// Register by appending self to the the linked list. 
	FScopeLock _(GetAdditionalProviderLock());
	FAdditionalCrashContextStack** Current = &GProviderHead;
	while(*Current != nullptr)
	{
		Current = &((*Current)->Next);
	}
	*Current = this;
}

FAdditionalCrashContextStack::~FAdditionalCrashContextStack()
{
	// Unregister by iterating the list, replacing self with next
	// on the list.
	FScopeLock _(GetAdditionalProviderLock());
	FAdditionalCrashContextStack** Current = &GProviderHead;
	while (*Current != this)
	{
		Current = &((*Current)->Next);
	}
	*Current = this->Next;
}

void FAdditionalCrashContextStack::PushProvider(struct FScopedAdditionalCrashContextProvider* Provider)
{
	ThreadContextProvider.PushProviderInternal(Provider);
}

void FAdditionalCrashContextStack::PopProvider()
{
	ThreadContextProvider.PopProviderInternal();
}

void FAdditionalCrashContextStack::ExecuteProviders(FCrashContextExtendedWriter& Writer)
{
	// Attempt to lock. If a thread crashed while holding the lock
	// we could potentially deadlock here otherwise.
	FCriticalSection* AdditionalProviderLock = GetAdditionalProviderLock();
	if (AdditionalProviderLock->TryLock())
	{
		FAdditionalCrashContextStack* Provider = GProviderHead;
		while (Provider)
		{
			for (uint32 i = 0; i < Provider->StackIndex; i++)
			{
				const FScopedAdditionalCrashContextProvider* Callback = Provider->Stack[i];
				Callback->Execute(Writer);
			}
			Provider = Provider->Next;
		}
		AdditionalProviderLock->Unlock();
	}
}

struct FCrashContextExtendedWriterImpl : public FCrashContextExtendedWriter
{
	FCrashContextExtendedWriterImpl(const TCHAR* InOutputDirectory)
		: OutputDirectory(InOutputDirectory)
	{
	}
	
	void OutputBuffer(const TCHAR* Identifier, const uint8* Data, uint32 DataSize, const TCHAR* Extension)
	{
		TCHAR Filename[1024] = { 0 };
		FCString::Snprintf(Filename, 1024, TEXT("%s/%s.%s"), OutputDirectory, Identifier, Extension);
		TUniquePtr<IFileHandle> File(IPlatformFile::GetPlatformPhysical().OpenWrite(Filename));
		if (File)
		{
			File->Write(Data, DataSize);
			File->Flush();
		}
	}

	virtual void AddBuffer(const TCHAR* Identifier, const uint8* Data, uint32 DataSize) override
	{
		if (Identifier == nullptr || Data == nullptr || DataSize == 0)
		{
			return;
		}

		OutputBuffer(Identifier, Data, DataSize, TEXT("bin"));
	}

	virtual void AddString(const TCHAR* Identifier, const TCHAR* DataStr) override
	{
		if (Identifier == nullptr || DataStr == nullptr)
		{
			return;
		}

		FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Additional Crash Context (Key=\"%s\", Value=\"%s\")"), Identifier, DataStr);

		FTCHARToUTF8 Converter(DataStr);
		OutputBuffer(Identifier, (uint8*)Converter.Get(), Converter.Length(), TEXT("txt"));
	}

private:
	const TCHAR* OutputDirectory;
};

#endif //WITH_ADDITIONAL_CRASH_CONTEXTS 


void FGenericCrashContext::DumpAdditionalContext(const TCHAR* CrashFolderAbsolute)
{
#if WITH_ADDITIONAL_CRASH_CONTEXTS 
	FCrashContextExtendedWriterImpl Writer(CrashFolderAbsolute);
	FAdditionalCrashContextStack::ExecuteProviders(Writer);
#endif
}


/**
 * Attempts to create the output report directory.
 */
bool FGenericCrashContext::CreateCrashReportDirectory(const TCHAR* CrashGUIDRoot, int32 CrashIndex, FString& OutCrashDirectoryAbsolute)
{
	// Generate Crash GUID
	TCHAR CrashGUID[FGenericCrashContext::CrashGUIDLength];
	FCString::Snprintf(CrashGUID, FGenericCrashContext::CrashGUIDLength, TEXT("%s_%04i"), CrashGUIDRoot, CrashIndex);

	// The FPaths commands usually checks for command line override, if FCommandLine not yet
	// initialized we cannot create a directory. Also there is no way of knowing if the file manager
	// has been created.
	if (!FCommandLine::IsInitialized())
	{
		return false;
	}

	FString CrashFolder = FPaths::Combine(*FPaths::ProjectSavedDir(), TEXT("Crashes"), CrashGUID);
	OutCrashDirectoryAbsolute = IFileManager::Get().ConvertToAbsolutePathForExternalAppForWrite(*CrashFolder);
	return IFileManager::Get().MakeDirectory(*OutCrashDirectoryAbsolute, true);
}

FString RecoveryService::GetRecoveryServerName()
{
	// Requirements: To avoid collision, the name must be unique on the local machine (multiple instances) and across the local network (multiple users).
	static FGuid RecoverySessionGuid = FGuid::NewGuid();
	return RecoverySessionGuid.ToString();
}

FString RecoveryService::MakeSessionName()
{
	// Convention: The session name starts with the server name (uniqueness), followed by a zero-based unique sequence number (idendify the latest session reliably), the the session creation time and the project name.
	static TAtomic<int32> SessionNum(0);
	return FString::Printf(TEXT("%s_%d_%s_%s"), *RecoveryService::GetRecoveryServerName(), SessionNum++, *FDateTime::UtcNow().ToString(), FApp::GetProjectName());
}

bool RecoveryService::TokenizeSessionName(const FString& SessionName, FString* OutServerName, int32* SeqNum, FString* ProjName, FDateTime* DateTime)
{
	// Parse a sessionName created with 'MakeSessionName()' that have the following format: C6EACAD6419AF672D75E2EA91E05BF55_1_2019.12.05-08.59.03_FP_FirstPerson
	//     ServerName = C6EACAD6419AF672D75E2EA91E05BF55
	//     SeqNum = 1
	//     DateTime = 2019.12.05-08.59.03
	//     ProjName = FP_FirstPerson
	FRegexPattern Pattern(TEXT(R"((^[A-Z0-9]+)_([0-9])+_([0-9\.-]+)_(.+))")); // Need help with regex? Try https://regex101.com/
	FRegexMatcher Matcher(Pattern, SessionName);

	if (!Matcher.FindNext())
	{
		return false; // Failed to parse.
	}
	if (OutServerName)
	{
		*OutServerName = Matcher.GetCaptureGroup(1);
	}
	if (SeqNum)
	{
		LexFromString(*SeqNum, *Matcher.GetCaptureGroup(2));
	}
	if (ProjName)
	{
		*ProjName = Matcher.GetCaptureGroup(4);
	}
	if (DateTime)
	{
		return FDateTime::Parse(Matcher.GetCaptureGroup(3), *DateTime);
	}

	return true; // Successfully parsed.
}

