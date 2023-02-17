// Copyright Epic Games, Inc. All Rights Reserved.

#include "Internationalization/TextLocalizationManager.h"
#include "Internationalization/TextLocalizationResource.h"
#include "Internationalization/ILocalizedTextSource.h"
#include "Internationalization/LocalizationResourceTextSource.h"
#include "Internationalization/PolyglotTextSource.h"
#include "Internationalization/StringTableRegistry.h"
#include "Internationalization/Cultures/LeetCulture.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/FileManager.h"
#include "HAL/LowLevelMemTracker.h"
#include "Misc/Parse.h"
#include "Misc/ScopeLock.h"
#include "Misc/CommandLine.h"
#include "Misc/LazySingleton.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Internationalization/StringTableCore.h"
#include "Internationalization/TextNamespaceUtil.h"
#include "Stats/Stats.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Templates/UniquePtr.h"
#include "Async/TaskGraphInterfaces.h"

DEFINE_LOG_CATEGORY_STATIC(LogTextLocalizationManager, Log, All);

enum class ERequestedCultureOverrideLevel : uint8
{
	CommandLine,
	EditorSettings,
	GameUserSettings,
	GameSettings,
	EngineSettings,
	Defaults,
};

bool IsLocalizationLockedByConfig()
{
	bool bIsLocalizationLocked = false;
	if (!GConfig->GetBool(TEXT("Internationalization"), TEXT("LockLocalization"), bIsLocalizationLocked, GGameIni))
	{
		GConfig->GetBool(TEXT("Internationalization"), TEXT("LockLocalization"), bIsLocalizationLocked, GEngineIni);
	}
	return bIsLocalizationLocked;
}

FString GetRequestedCulture(const TCHAR* InCommandLineKey, const TCHAR* InConfigKey, const TCHAR* InDefaultCulture, ERequestedCultureOverrideLevel& OutOverrideLevel)
{
	FString RequestedCulture;

	auto ReadSettingsFromCommandLine = [&RequestedCulture, &InCommandLineKey, &InConfigKey, &OutOverrideLevel]()
	{
#if ENABLE_LOC_TESTING
		if (RequestedCulture.IsEmpty() && FParse::Param(FCommandLine::Get(), *FLeetCulture::StaticGetName()))
		{
			RequestedCulture = FLeetCulture::StaticGetName();
			OutOverrideLevel = ERequestedCultureOverrideLevel::CommandLine;
		}
#endif

		if (RequestedCulture.IsEmpty() && FParse::Value(FCommandLine::Get(), TEXT("CULTUREFORCOOKING="), RequestedCulture))
		{
			OutOverrideLevel = ERequestedCultureOverrideLevel::CommandLine;

			// Write the culture passed in if first install...
			if (FParse::Param(FCommandLine::Get(), TEXT("firstinstall")) && InConfigKey)
			{
				GConfig->SetString(TEXT("Internationalization"), InConfigKey, *RequestedCulture, GEngineIni);
			}
		}

		if (RequestedCulture.IsEmpty() && InCommandLineKey && FParse::Value(FCommandLine::Get(), InCommandLineKey, RequestedCulture))
		{
			OutOverrideLevel = ERequestedCultureOverrideLevel::CommandLine;
		}

		if (RequestedCulture.IsEmpty() && FParse::Value(FCommandLine::Get(), TEXT("CULTURE="), RequestedCulture))
		{
			OutOverrideLevel = ERequestedCultureOverrideLevel::CommandLine;
		}
	};

	auto ReadSettingsFromConfig = [&RequestedCulture, &InConfigKey, &OutOverrideLevel](const FString& InConfigFilename, const ERequestedCultureOverrideLevel InConfigOverrideLevel)
	{
		if (RequestedCulture.IsEmpty() && InConfigKey && GConfig->GetString(TEXT("Internationalization"), InConfigKey, RequestedCulture, InConfigFilename))
		{
			OutOverrideLevel = InConfigOverrideLevel;
		}

		if (RequestedCulture.IsEmpty() && GConfig->GetString(TEXT("Internationalization"), TEXT("Culture"), RequestedCulture, InConfigFilename))
		{
			OutOverrideLevel = InConfigOverrideLevel;
		}
	};

	auto ReadSettingsFromDefaults = [&RequestedCulture, &InDefaultCulture, &OutOverrideLevel]()
	{
		if (RequestedCulture.IsEmpty() && InDefaultCulture)
		{
			RequestedCulture = InDefaultCulture;
			OutOverrideLevel = ERequestedCultureOverrideLevel::Defaults;
		}
	};

	// Read setting override specified on commandline.
	ReadSettingsFromCommandLine();
#if WITH_EDITOR
	// Read setting specified in editor configuration.
	if (GIsEditor)
	{
		ReadSettingsFromConfig(GEditorSettingsIni, ERequestedCultureOverrideLevel::EditorSettings);
	}
#endif // WITH_EDITOR
	// Read setting specified in game configurations.
	if (!GIsEditor)
	{
		ReadSettingsFromConfig(GGameUserSettingsIni, ERequestedCultureOverrideLevel::GameUserSettings);
		ReadSettingsFromConfig(GGameIni, ERequestedCultureOverrideLevel::GameSettings);
	}
	// Read setting specified in engine configuration.
	ReadSettingsFromConfig(GEngineIni, ERequestedCultureOverrideLevel::EngineSettings);
	// Read defaults
	ReadSettingsFromDefaults();

	return RequestedCulture;
}

FString GetRequestedLanguage(ERequestedCultureOverrideLevel& OutOverrideLevel)
{
	return GetRequestedCulture(TEXT("LANGUAGE="), TEXT("Language"), *FInternationalization::Get().GetDefaultLanguage()->GetName(), OutOverrideLevel);
}

FString GetRequestedLocale(ERequestedCultureOverrideLevel& OutOverrideLevel)
{
	return GetRequestedCulture(TEXT("LOCALE="), TEXT("Locale"), *FInternationalization::Get().GetDefaultLocale()->GetName(), OutOverrideLevel);
}

TArray<TTuple<FName, FString>> GetRequestedAssetGroups(const ERequestedCultureOverrideLevel InLanguageOverrideLevel)
{
	TArray<TTuple<FName, FString>> RequestedAssetGroups;

	auto ReadSettingsFromConfig = [&RequestedAssetGroups, &InLanguageOverrideLevel](const FString& InConfigFilename, const ERequestedCultureOverrideLevel InConfigOverrideLevel)
	{
		// Once the language has been overridden we stop parsing out new asset groups
		if (InLanguageOverrideLevel <= InConfigOverrideLevel)
		{
			if (const FConfigSection* AssetGroupCulturesSection = GConfig->GetSectionPrivate(TEXT("Internationalization.AssetGroupCultures"), false, true, InConfigFilename))
			{
				for (const auto& SectionEntryPair : *AssetGroupCulturesSection)
				{
					const bool bAlreadyExists = RequestedAssetGroups.ContainsByPredicate([&](const TTuple<FName, FString>& InRequestedAssetGroup)
					{
						return InRequestedAssetGroup.Key == SectionEntryPair.Key;
					});

					if (!bAlreadyExists)
					{
						RequestedAssetGroups.Add(MakeTuple(SectionEntryPair.Key, SectionEntryPair.Value.GetValue()));
					}
				}
			}
		}
	};

#if WITH_EDITOR
	// Read setting specified in editor configuration.
	if (GIsEditor)
	{
		ReadSettingsFromConfig(GEditorSettingsIni, ERequestedCultureOverrideLevel::EditorSettings);
	}
#endif // WITH_EDITOR
	// Read setting specified in game configurations.
	if (!GIsEditor)
	{
		ReadSettingsFromConfig(GGameUserSettingsIni, ERequestedCultureOverrideLevel::GameUserSettings);
		ReadSettingsFromConfig(GGameIni, ERequestedCultureOverrideLevel::GameSettings);
	}
	// Read setting specified in engine configuration.
	ReadSettingsFromConfig(GEngineIni, ERequestedCultureOverrideLevel::EngineSettings);

	return RequestedAssetGroups;
}

void ApplyDefaultCultureSettings(const ELocalizationLoadFlags LocLoadFlags)
{
	FInternationalization& I18N = FInternationalization::Get();

	auto LogCultureOverride = [](const TCHAR* InResult, const TCHAR* InOptionDisplayName, const ERequestedCultureOverrideLevel InOverrideLevel)
	{
		switch (InOverrideLevel)
		{
		case ERequestedCultureOverrideLevel::CommandLine:
			UE_LOG(LogInit, Log, TEXT("Overriding %s with command-line option (%s)."), InOptionDisplayName, InResult);
			break;
		case ERequestedCultureOverrideLevel::EditorSettings:
			UE_LOG(LogInit, Log, TEXT("Overriding language with editor %s configuration option (%s)."), InOptionDisplayName, InResult);
			break;
		case ERequestedCultureOverrideLevel::GameUserSettings:
			UE_LOG(LogInit, Log, TEXT("Overriding language with game user settings %s configuration option (%s)."), InOptionDisplayName, InResult);
			break;
		case ERequestedCultureOverrideLevel::GameSettings:
			UE_LOG(LogInit, Log, TEXT("Overriding language with game %s configuration option (%s)."), InOptionDisplayName, InResult);
			break;
		case ERequestedCultureOverrideLevel::EngineSettings:
			UE_LOG(LogInit, Log, TEXT("Overriding language with engine %s configuration option (%s)."), InOptionDisplayName, InResult);
			break;
		case ERequestedCultureOverrideLevel::Defaults:
			UE_LOG(LogInit, Log, TEXT("Using OS detected %s (%s)."), InOptionDisplayName, InResult);
			break;
		}
	};

	auto ValidateRequestedCulture = [LocLoadFlags, &I18N](const FString& InRequestedCulture, const FString& InFallbackCulture, const TCHAR* InLogDesc, const bool bRequireExactMatch) -> FString
	{
		FString TargetCultureName = InRequestedCulture;

#if ENABLE_LOC_TESTING
		if (TargetCultureName != FLeetCulture::StaticGetName())
#endif
		{
			// Validate the locale has data or fallback to one that does.
			const TArray<FString> AvailableCultureNames = FTextLocalizationManager::Get().GetLocalizedCultureNames(LocLoadFlags);
			auto ValidateCultureName = [&AvailableCultureNames, &I18N](const FString& InCultureToValidate) -> FString
			{
				const TArray<FString> PrioritizedCultureNames = I18N.GetPrioritizedCultureNames(InCultureToValidate);
				for (const FString& CultureName : PrioritizedCultureNames)
				{
					if (AvailableCultureNames.Contains(CultureName))
					{
						return CultureName;
					}
				}
				return FString();
			};

			const FString ValidCultureName = ValidateCultureName(InRequestedCulture);
			const FString ValidFallbackCultureName = ValidateCultureName(InFallbackCulture);

			if (!ValidCultureName.IsEmpty())
			{
				if (bRequireExactMatch && InRequestedCulture != ValidCultureName)
				{
					TargetCultureName = ValidCultureName;
					UE_LOG(LogTextLocalizationManager, Log, TEXT("No specific localization for '%s' exists, so the '%s' localization will be used."), *InRequestedCulture, *ValidCultureName, InLogDesc);
				}
			}
			else if (!ValidFallbackCultureName.IsEmpty())
			{
				TargetCultureName = ValidFallbackCultureName;
				UE_LOG(LogTextLocalizationManager, Log, TEXT("No localization for '%s' exists, so '%s' will be used for the %s."), *InRequestedCulture, *TargetCultureName, InLogDesc);
			}
			else
			{
				TargetCultureName = AvailableCultureNames.Num() > 0 ? AvailableCultureNames[0] : InFallbackCulture;
				UE_LOG(LogTextLocalizationManager, Log, TEXT("No localization for '%s' exists, so '%s' will be used for the %s."), *InRequestedCulture, *TargetCultureName, InLogDesc);
			}
		}

		return TargetCultureName;
	};

	FString FallbackLanguage = TEXT("en");
	if (EnumHasAnyFlags(LocLoadFlags, ELocalizationLoadFlags::Game))
	{
		// If this is a game, use the native culture of the game as the fallback
		FString NativeGameCulture = FTextLocalizationManager::Get().GetNativeCultureName(ELocalizedTextSourceCategory::Game);
		if (!NativeGameCulture.IsEmpty())
		{
			FallbackLanguage = MoveTemp(NativeGameCulture);
		}
	}

	FString RequestedLanguage;
	FString RequestedLocale;
	TArray<TTuple<FName, FString>> RequestedAssetGroups;
	{
		ERequestedCultureOverrideLevel LanguageOverrideLevel = ERequestedCultureOverrideLevel::Defaults;
		RequestedLanguage = GetRequestedLanguage(LanguageOverrideLevel);
		LogCultureOverride(*RequestedLanguage, TEXT("language"), LanguageOverrideLevel);

		ERequestedCultureOverrideLevel LocaleOverrideLevel = ERequestedCultureOverrideLevel::Defaults;
		RequestedLocale = GetRequestedLocale(LocaleOverrideLevel);
		LogCultureOverride(*RequestedLocale, TEXT("locale"), LocaleOverrideLevel);

		RequestedAssetGroups = GetRequestedAssetGroups(LanguageOverrideLevel);
	}

	// Validate that we have translations for this language and locale
	// Note: We skip the locale check for the editor as we a limited number of translations, but want to allow locale correct display of numbers, dates, etc
	const FString TargetLanguage = ValidateRequestedCulture(RequestedLanguage, FallbackLanguage, TEXT("language"), true);
	const FString TargetLocale = GIsEditor ? RequestedLocale : ValidateRequestedCulture(RequestedLocale, TargetLanguage, TEXT("locale"), false);
	if (TargetLanguage == TargetLocale)
	{
		I18N.SetCurrentLanguageAndLocale(TargetLanguage);
	}
	else
	{
		I18N.SetCurrentLanguage(TargetLanguage);
		I18N.SetCurrentLocale(TargetLocale);
	}

	for (const auto& RequestedAssetGroupPair : RequestedAssetGroups)
	{
		const FString TargetAssetGroupCulture = ValidateRequestedCulture(RequestedAssetGroupPair.Value, TargetLanguage, *FString::Printf(TEXT("'%s' asset group"), *RequestedAssetGroupPair.Key.ToString()), false);
		if (TargetAssetGroupCulture != TargetLanguage)
		{
			I18N.SetCurrentAssetGroupCulture(RequestedAssetGroupPair.Key, TargetAssetGroupCulture);
		}
	}
}

void BeginPreInitTextLocalization()
{
	LLM_SCOPE(ELLMTag::Localization);

	SCOPED_BOOT_TIMING("BeginPreInitTextLocalization");
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("BeginPreInitTextLocalization"), STAT_BeginPreInitTextLocalization, STATGROUP_LoadTime);

	// Bind this delegate before the PAK file loader is created
	FCoreDelegates::OnPakFileMounted2.AddRaw(&FTextLocalizationManager::Get(), &FTextLocalizationManager::OnPakFileMounted);
}

void BeginInitTextLocalization()
{
	LLM_SCOPE(ELLMTag::Localization);

	SCOPED_BOOT_TIMING("BeginInitTextLocalization");
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("BeginInitTextLocalization"), STAT_BeginInitTextLocalization, STATGROUP_LoadTime);

	// Initialize FInternationalization before we bind to OnCultureChanged, otherwise we can accidentally initialize
	// twice since FInternationalization::Initialize sets the culture.
	FInternationalization::Get();
	FInternationalization::Get().OnCultureChanged().AddRaw(&FTextLocalizationManager::Get(), &FTextLocalizationManager::OnCultureChanged);
}

void InitEngineTextLocalization()
{
	LLM_SCOPE(ELLMTag::Localization);
	
	SCOPED_BOOT_TIMING("InitEngineTextLocalization");
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("InitEngineTextLocalization"), STAT_InitEngineTextLocalization, STATGROUP_LoadTime);

	// Make sure the String Table Registry is initialized as it may trigger module loads
	FStringTableRegistry::Get();
	FStringTableRedirects::InitStringTableRedirects();

	// Run this now that the config system is definitely initialized
	// to refresh anything that was cached before it was ready
	FInternationalization::Get().RefreshCultureDisplayNames(FInternationalization::Get().GetCurrentLanguage()->GetPrioritizedParentCultureNames());

	ELocalizationLoadFlags LocLoadFlags = ELocalizationLoadFlags::None;
	LocLoadFlags |= (WITH_EDITOR ? ELocalizationLoadFlags::Editor : ELocalizationLoadFlags::None);
	LocLoadFlags |= ELocalizationLoadFlags::Engine;
	LocLoadFlags |= ELocalizationLoadFlags::Additional;
	
	ELocalizationLoadFlags ApplyLocLoadFlags = LocLoadFlags;
	ApplyLocLoadFlags |= FApp::IsGame() ? ELocalizationLoadFlags::Game : ELocalizationLoadFlags::None;

	// Setting InitializedFlags to None ensures we don't pick up the culture change
	// notification if ApplyDefaultCultureSettings changes the default culture
	{
		TGuardValue<ETextLocalizationManagerInitializedFlags> InitializedFlagsGuard(FTextLocalizationManager::Get().InitializedFlags, ETextLocalizationManagerInitializedFlags::None);
		ApplyDefaultCultureSettings(ApplyLocLoadFlags);
	}

#if WITH_EDITOR
	FTextLocalizationManager::Get().GameLocalizationPreviewAutoEnableCount = 0;
	FTextLocalizationManager::Get().bIsGameLocalizationPreviewEnabled = false;
	FTextLocalizationManager::Get().bIsLocalizationLocked = IsLocalizationLockedByConfig();
#endif

	// Clear the native cultures for the engine and editor (they will re-cache later if used)
	TextLocalizationResourceUtil::ClearNativeEngineCultureName();
#if WITH_EDITOR
	TextLocalizationResourceUtil::ClearNativeEditorCultureName();
#endif

	FTextLocalizationManager::Get().LoadLocalizationResourcesForCulture(FInternationalization::Get().GetCurrentLanguage()->GetName(), LocLoadFlags);
	FTextLocalizationManager::Get().InitializedFlags |= ETextLocalizationManagerInitializedFlags::Engine;
}

static FGraphEventRef InitGameTextLocalizationTask;

void BeginInitGameTextLocalization()
{
	if (!FApp::IsGame())
	{
		// early out because we are not a game ;)
		return;
	}

	LLM_SCOPE(ELLMTag::Localization);

	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("InitGameTextLocalization"), STAT_InitGameTextLocalization, STATGROUP_LoadTime);

	// Refresh the cached config data before applying the default culture, as the game may have patched in new config data since the cache was built
	FInternationalization::Get().RefreshCachedConfigData();

	// Setting InitializedFlags to None ensures we don't pick up the culture change
	// notification if ApplyDefaultCultureSettings changes the default culture
	const FString PreviousLanguage = FInternationalization::Get().GetCurrentLanguage()->GetName();
	{
		TGuardValue<ETextLocalizationManagerInitializedFlags> InitializedFlagsGuard(FTextLocalizationManager::Get().InitializedFlags, ETextLocalizationManagerInitializedFlags::None);
		ApplyDefaultCultureSettings(ELocalizationLoadFlags::Game);
	}
	const FString CurrentLanguage = FInternationalization::Get().GetCurrentLanguage()->GetName();

	// Clear the native cultures for the game (it will re-cache later if used)
	TextLocalizationResourceUtil::ClearNativeProjectCultureName();

	ELocalizationLoadFlags LocLoadFlags = ELocalizationLoadFlags::Game;
	if (PreviousLanguage != CurrentLanguage)
	{
		// If the active language changed, then we also need to reload the Engine and Additional localization data 
		// too, as this wouldn't have happened when the culture changed above due to the InitializedFlags guard
		LocLoadFlags |= ELocalizationLoadFlags::Engine;
		LocLoadFlags |= ELocalizationLoadFlags::Additional;
	}

	FTextLocalizationManager::Get().InitializedFlags |= ETextLocalizationManagerInitializedFlags::Initializing;
	auto TaskLambda = [LocLoadFlags, InitializedFlags = FTextLocalizationManager::Get().InitializedFlags]()
	{
		SCOPED_BOOT_TIMING("InitGameTextLocalization");

		FTextLocalizationManager::Get().LoadLocalizationResourcesForCulture(FInternationalization::Get().GetCurrentLanguage()->GetName(), LocLoadFlags);
		FTextLocalizationManager::Get().InitializedFlags = (InitializedFlags & ~ETextLocalizationManagerInitializedFlags::Initializing) | ETextLocalizationManagerInitializedFlags::Game;
		//FTextLocalizationManager::Get().DumpMemoryInfo();
		FTextLocalizationManager::Get().CompactDataStructures();
		//FTextLocalizationManager::Get().DumpMemoryInfo();
	};
	if (FTaskGraphInterface::IsRunning())
	{
		InitGameTextLocalizationTask = FFunctionGraphTask::CreateAndDispatchWhenReady(MoveTemp(TaskLambda), TStatId());
	}
	else
	{
		TaskLambda();
	}
}

void EndInitGameTextLocalization()
{
	SCOPED_BOOT_TIMING("WaitForInitGameTextLocalization");
	if (InitGameTextLocalizationTask)
	{
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(InitGameTextLocalizationTask);
	}
}

void InitGameTextLocalization()
{
	BeginInitGameTextLocalization();
	EndInitGameTextLocalization();
}

FTextLocalizationManager& FTextLocalizationManager::Get()
{
	return TLazySingleton<FTextLocalizationManager>::Get();
}

void FTextLocalizationManager::TearDown()
{
	TLazySingleton<FTextLocalizationManager>::TearDown();
	FTextKey::TearDown();
}

FTextLocalizationManager::FTextLocalizationManager()
	: TextRevisionCounter(0)
	, LocResTextSource(MakeShared<FLocalizationResourceTextSource>())
	, PolyglotTextSource(MakeShared<FPolyglotTextSource>())
{
	const bool bRefreshResources = false;
	RegisterTextSource(LocResTextSource.ToSharedRef(), bRefreshResources);
	RegisterTextSource(PolyglotTextSource.ToSharedRef(), bRefreshResources);
}

void FTextLocalizationManager::DumpMemoryInfo()
{
	FScopeLock ScopeLock(&SynchronizationObject);

	UE_LOG(LogTextLocalizationManager, Log, TEXT("DisplayStringLookupTable.GetAllocatedSize()=%d elems=%d"), DisplayStringLookupTable.GetAllocatedSize(), DisplayStringLookupTable.Num());

	UE_LOG(LogTextLocalizationManager, Log, TEXT("NamespaceKeyLookupTable.GetAllocatedSize()=%d elems=%d"), NamespaceKeyLookupTable.GetAllocatedSize(), NamespaceKeyLookupTable.Num());

	UE_LOG(LogTextLocalizationManager, Log, TEXT("LocalTextRevisions.GetAllocatedSize()=%d elems=%d"), LocalTextRevisions.GetAllocatedSize(), LocalTextRevisions.Num());
}

void FTextLocalizationManager::CompactDataStructures()
{
	FScopeLock ScopeLock(&SynchronizationObject);
	double StartTime = FPlatformTime::Seconds();
	DisplayStringLookupTable.Shrink();
	LocalTextRevisions.Shrink();
	NamespaceKeyLookupTable.Shrink();
	FTextKey::CompactDataStructures();
	UE_LOG(LogTextLocalizationManager, Log, TEXT("Compacting localization data took %6.2fms"), 1000.0 * (FPlatformTime::Seconds() - StartTime));
}

FString FTextLocalizationManager::GetRequestedLanguageName() const
{
	ERequestedCultureOverrideLevel LanguageOverrideLevel = ERequestedCultureOverrideLevel::Defaults;
	return GetRequestedLanguage(LanguageOverrideLevel);
}

FString FTextLocalizationManager::GetRequestedLocaleName() const
{
	ERequestedCultureOverrideLevel LocaleOverrideLevel = ERequestedCultureOverrideLevel::Defaults;
	return GetRequestedLocale(LocaleOverrideLevel);
}

FString FTextLocalizationManager::GetNativeCultureName(const ELocalizedTextSourceCategory InCategory) const
{
	FString NativeCultureName;
	for (const TSharedPtr<ILocalizedTextSource>& LocalizedTextSource : LocalizedTextSources)
	{
		if (LocalizedTextSource->GetNativeCultureName(InCategory, NativeCultureName))
		{
			break;
		}
	}
	return NativeCultureName;
}

TArray<FString> FTextLocalizationManager::GetLocalizedCultureNames(const ELocalizationLoadFlags InLoadFlags) const
{
	TSet<FString> LocalizedCultureNameSet;
	for (const TSharedPtr<ILocalizedTextSource>& LocalizedTextSource : LocalizedTextSources)
	{
		LocalizedTextSource->GetLocalizedCultureNames(InLoadFlags, LocalizedCultureNameSet);
	}

	TArray<FString> LocalizedCultureNames = LocalizedCultureNameSet.Array();
	LocalizedCultureNames.Sort();
	return LocalizedCultureNames;
}

void FTextLocalizationManager::RegisterTextSource(const TSharedRef<ILocalizedTextSource>& InLocalizedTextSource, const bool InRefreshResources)
{
	ensureMsgf(!IsInitializing(), TEXT("Localized text source registered during game text initialization"));

	LocalizedTextSources.Add(InLocalizedTextSource);
	LocalizedTextSources.StableSort([](const TSharedPtr<ILocalizedTextSource>& InLocalizedTextSourceOne, const TSharedPtr<ILocalizedTextSource>& InLocalizedTextSourceTwo)
	{
		return InLocalizedTextSourceOne->GetPriority() > InLocalizedTextSourceTwo->GetPriority();
	});

	if (InRefreshResources)
	{
		RefreshResources();
	}
}

void FTextLocalizationManager::RegisterPolyglotTextData(const FPolyglotTextData& InPolyglotTextData, const bool InAddDisplayString)
{
	check(PolyglotTextSource.IsValid());
	RegisterPolyglotTextData(TArrayView<const FPolyglotTextData>(&InPolyglotTextData, 1), InAddDisplayString);
}

void FTextLocalizationManager::RegisterPolyglotTextData(TArrayView<const FPolyglotTextData> InPolyglotTextDataArray, const bool InAddDisplayStrings)
{
	for (const FPolyglotTextData& PolyglotTextData : InPolyglotTextDataArray)
	{
		if (PolyglotTextData.IsValid())
		{
			PolyglotTextSource->RegisterPolyglotTextData(PolyglotTextData);
		}
	}

	if (InAddDisplayStrings)
	{
		auto GetLocalizedStringForPolyglotData = [this](const FPolyglotTextData& PolyglotTextData, FString& OutLocalizedString) -> bool
		{
			// Work out which culture to use - this is typically the current language unless we're in the 
			// editor where the game localization preview affects the language we use for game text
			FString CultureName;
			if (PolyglotTextData.GetCategory() != ELocalizedTextSourceCategory::Game || !GIsEditor)
			{
				CultureName = FInternationalization::Get().GetCurrentLanguage()->GetName();
			}
#if WITH_EDITOR
			else if (bIsGameLocalizationPreviewEnabled)
			{
				CultureName = GetConfiguredGameLocalizationPreviewLanguage();
			}
#endif

			if (!CultureName.IsEmpty())
			{
				const TArray<FString> PrioritizedCultureNames = FInternationalization::Get().GetPrioritizedCultureNames(CultureName);
				for (const FString& PrioritizedCultureName : PrioritizedCultureNames)
				{
					if (PolyglotTextData.GetLocalizedString(PrioritizedCultureName, OutLocalizedString))
					{
						return true;
					}
				}
			}

			if (PolyglotTextData.IsMinimalPatch())
			{
				return false;
			}

			OutLocalizedString = PolyglotTextData.GetNativeString();
			return true;
		};

		FTextLocalizationResource TextLocalizationResource;
		for (const FPolyglotTextData& PolyglotTextData : InPolyglotTextDataArray)
		{
			if (!PolyglotTextData.IsValid())
			{
				continue;
			}

			FString LocalizedString;
			if (GetLocalizedStringForPolyglotData(PolyglotTextData, LocalizedString))
			{
				TextLocalizationResource.AddEntry(
					PolyglotTextData.GetNamespace(),
					PolyglotTextData.GetKey(),
					PolyglotTextData.GetNativeString(),
					LocalizedString,
					0
					);
			}
		}

		if (!TextLocalizationResource.IsEmpty())
		{
			UpdateFromLocalizations(MoveTemp(TextLocalizationResource));
		}
	}
}

FTextDisplayStringPtr FTextLocalizationManager::FindDisplayString(const FTextKey& Namespace, const FTextKey& Key, const FString* const SourceString)
{
	FScopeLock ScopeLock( &SynchronizationObject );

	const FTextId TextId(Namespace, Key);

	FDisplayStringEntry* LiveEntry = DisplayStringLookupTable.Find(TextId);

	if ( LiveEntry != nullptr && ( !SourceString || LiveEntry->SourceStringHash == FTextLocalizationResource::HashString(*SourceString) ) )
	{
		return LiveEntry->DisplayString;
	}

	return nullptr;
}

FTextDisplayStringRef FTextLocalizationManager::GetDisplayString(const FTextKey& Namespace, const FTextKey& Key, const FString* const SourceString)
{
	FScopeLock ScopeLock( &SynchronizationObject );

	// Hack fix for old assets that don't have namespace/key info.
	if (Namespace.IsEmpty() && Key.IsEmpty())
	{
		return MakeShared<FString, ESPMode::ThreadSafe>(SourceString ? *SourceString : FString());
	}

#if ENABLE_LOC_TESTING
	const bool bShouldLEETIFYAll = IsInitialized() && FInternationalization::Get().GetCurrentLanguage()->GetName() == FLeetCulture::StaticGetName();

	// Attempt to set bShouldLEETIFYUnlocalizedString appropriately, only once, after the commandline is initialized and parsed.
	static bool bShouldLEETIFYUnlocalizedString = false;
	{
		static bool bHasParsedCommandLine = false;
		if (!bHasParsedCommandLine && FCommandLine::IsInitialized())
		{
			bShouldLEETIFYUnlocalizedString = FParse::Param(FCommandLine::Get(), TEXT("LEETIFYUnlocalized"));
			bHasParsedCommandLine = true;
		}
	}
#endif

	const FTextId TextId(Namespace, Key);

	const uint32 SourceStringHash = SourceString ? FTextLocalizationResource::HashString(*SourceString) : 0;

	FDisplayStringEntry* LiveEntry = DisplayStringLookupTable.Find(TextId);

	// In builds with stable keys enabled, we want to use the display string from the "clean" version of the text (if the sources match) as this is the only version that is translated
	const FString* DisplayString = SourceString;
	FDisplayStringEntry* DisplayLiveEntry = nullptr;
#if USE_STABLE_LOCALIZATION_KEYS
	if (GIsEditor)
	{
		const FTextKey DisplayNamespace = TextNamespaceUtil::StripPackageNamespace(TextId.GetNamespace().GetChars());

		DisplayLiveEntry = DisplayStringLookupTable.Find(FTextId(DisplayNamespace, TextId.GetKey()));

		if (DisplayLiveEntry)
		{
			if (!SourceString || DisplayLiveEntry->SourceStringHash == SourceStringHash)
			{
				DisplayString = &DisplayLiveEntry->DisplayString.Get();
			}
			else
			{
				DisplayLiveEntry = nullptr;
			}
		}
	}
#endif // USE_STABLE_LOCALIZATION_KEYS

	// Entry is present.
	if (LiveEntry)
	{
		// If the source string (hash) is different, the local source has changed and should override - can't be localized.
		if (SourceStringHash != LiveEntry->SourceStringHash && DisplayString)
		{
			LiveEntry->SourceStringHash = SourceStringHash;
			*LiveEntry->DisplayString = *DisplayString;
			DirtyLocalRevisionForDisplayString(LiveEntry->DisplayString);

#if ENABLE_LOC_TESTING
			if (bShouldLEETIFYAll || bShouldLEETIFYUnlocalizedString)
			{
				FInternationalization::Leetify(*LiveEntry->DisplayString);
				if (LiveEntry->DisplayString->Equals(*DisplayString, ESearchCase::CaseSensitive))
				{
					UE_LOG(LogTextLocalizationManager, Warning, TEXT("Leetify failed to alter a string (%s)."), **DisplayString);
				}
			}
#endif

			UE_LOG(LogTextLocalizationManager, Verbose, TEXT("An attempt was made to get a localized string (Namespace:%s, Key:%s), but the source string hash does not match - the source string (%s) will be used."), TextId.GetNamespace().GetChars(), TextId.GetKey().GetChars(), **LiveEntry->DisplayString);

#if ENABLE_LOC_TESTING
			LiveEntry->bIsLocalized = bShouldLEETIFYAll;
#else
			LiveEntry->bIsLocalized = false;
#endif
		}

		return LiveEntry->DisplayString;
	}
	// Entry is absent, but has a related entry to clone.
	else if (DisplayLiveEntry)
	{
		check(!SourceString || DisplayLiveEntry->SourceStringHash == SourceStringHash);
		check(DisplayString && &DisplayLiveEntry->DisplayString.Get() == DisplayString);

		// Clone the entry for the active ID, and assign it a new display string instance (as all entries must have a unique display string instance).
		FDisplayStringEntry NewEntry(*DisplayLiveEntry);
		NewEntry.DisplayString = MakeShared<FString, ESPMode::ThreadSafe>(*DisplayString);

		DisplayStringLookupTable.Emplace(TextId, NewEntry);
		NamespaceKeyLookupTable.Emplace(NewEntry.DisplayString, TextId);

		return NewEntry.DisplayString;
	}
	// Entry is absent.
	else
	{
		// Don't log warnings about unlocalized strings if the system hasn't been initialized - we simply don't have localization data yet.
		if (IsInitialized())
		{
			UE_LOG(LogTextLocalizationManager, Verbose, TEXT("An attempt was made to get a localized string (Namespace:%s, Key:%s, Source:%s), but it did not exist."), TextId.GetNamespace().GetChars(), TextId.GetKey().GetChars(), SourceString ? **SourceString : TEXT(""));
		}

		const FTextDisplayStringRef UnlocalizedString = MakeShared<FString, ESPMode::ThreadSafe>(DisplayString ? *DisplayString : FString());

#if ENABLE_LOC_TESTING
		if ((bShouldLEETIFYAll || bShouldLEETIFYUnlocalizedString) && DisplayString)
		{
			FInternationalization::Leetify(*UnlocalizedString);
			if (UnlocalizedString->Equals(*DisplayString, ESearchCase::CaseSensitive))
			{
				UE_LOG(LogTextLocalizationManager, Warning, TEXT("Leetify failed to alter a string (%s)."), **DisplayString);
			}
		}
#endif

		// Make entries so that they can be updated when system is initialized or a culture swap occurs.
		FDisplayStringEntry NewEntry(
#if ENABLE_LOC_TESTING
			bShouldLEETIFYAll					/*bIsLocalized*/
#else
			false								/*bIsLocalized*/
#endif
			, FTextKey()						/*LocResID*/
			, SourceStringHash					/*SourceStringHash*/
			, UnlocalizedString					/*String*/
		);

		DisplayStringLookupTable.Emplace(TextId, NewEntry);
		NamespaceKeyLookupTable.Emplace(NewEntry.DisplayString, TextId);

		return UnlocalizedString;
	}
}

#if WITH_EDITORONLY_DATA
bool FTextLocalizationManager::GetLocResID(const FTextKey& Namespace, const FTextKey& Key, FString& OutLocResId)
{
	FScopeLock ScopeLock(&SynchronizationObject);

	const FTextId TextId(Namespace, Key);

	FDisplayStringEntry* LiveEntry = DisplayStringLookupTable.Find(TextId);

	if (LiveEntry != nullptr && !LiveEntry->LocResID.IsEmpty())
	{
		OutLocResId = LiveEntry->LocResID.GetChars();
		return true;
	}

	return false;
}
#endif

bool FTextLocalizationManager::FindNamespaceAndKeyFromDisplayString(const FTextDisplayStringRef& InDisplayString, FString& OutNamespace, FString& OutKey)
{
	FScopeLock ScopeLock( &SynchronizationObject );

	const FTextId* NamespaceKeyEntry = NamespaceKeyLookupTable.Find(InDisplayString);

	if (NamespaceKeyEntry)
	{
		OutNamespace = NamespaceKeyEntry->GetNamespace().GetChars();
		OutKey = NamespaceKeyEntry->GetKey().GetChars();
		return true;
	}

	return false;
}

bool FTextLocalizationManager::FindNamespaceAndKeyFromDisplayString(const FTextDisplayStringRef& InDisplayString, FTextKey& OutNamespace, FTextKey& OutKey)
{
	FScopeLock ScopeLock(&SynchronizationObject);

	const FTextId* NamespaceKeyEntry = NamespaceKeyLookupTable.Find(InDisplayString);

	if (NamespaceKeyEntry)
	{
		OutNamespace = NamespaceKeyEntry->GetNamespace();
		OutKey = NamespaceKeyEntry->GetKey();
		return true;
	}

	return false;
}

uint16 FTextLocalizationManager::GetLocalRevisionForDisplayString(const FTextDisplayStringRef& InDisplayString)
{
	FScopeLock ScopeLock( &SynchronizationObject );

	uint16* FoundLocalRevision = LocalTextRevisions.Find(InDisplayString);
	return (FoundLocalRevision) ? *FoundLocalRevision : 0;
}

bool FTextLocalizationManager::AddDisplayString(const FTextDisplayStringRef& DisplayString, const FTextKey& Namespace, const FTextKey& Key)
{
	FScopeLock ScopeLock( &SynchronizationObject );

	const FTextId TextId(Namespace, Key);

	// Try to find existing entries.
	const FTextId* ReverseLiveTableEntry = NamespaceKeyLookupTable.Find(DisplayString);
	FDisplayStringEntry* ExistingDisplayStringEntry = DisplayStringLookupTable.Find(TextId);

	// If there are any existing entries, they may cause a conflict, unless they're exactly the same as what we would be adding.
	if ( (ExistingDisplayStringEntry && ExistingDisplayStringEntry->DisplayString != DisplayString) || // Namespace and key mustn't be associated with a different display string.
		(ReverseLiveTableEntry && *ReverseLiveTableEntry != TextId) ) // Display string mustn't be associated with a different namespace and key.
	{
		return false;
	}

	// Add the necessary associations in both directions.
	DisplayStringLookupTable.Emplace(TextId, FDisplayStringEntry(false, FTextKey(), FTextLocalizationResource::HashString(*DisplayString), DisplayString));
	NamespaceKeyLookupTable.Emplace(DisplayString, TextId);

	return true;
}

bool FTextLocalizationManager::UpdateDisplayString(const FTextDisplayStringRef& DisplayString, const FString& Value, const FTextKey& Namespace, const FTextKey& Key)
{
	FScopeLock ScopeLock( &SynchronizationObject );

	// Get entry from reverse live table. Contains current namespace and key values.
	FTextId& ReverseLiveTableEntry = NamespaceKeyLookupTable[DisplayString];

	const FTextId TextId(Namespace, Key);

	// Copy old live table entry over as new live table entry and destroy old live table entry if the namespace or key has changed.
	if (ReverseLiveTableEntry != TextId)
	{
		FDisplayStringEntry* NewDisplayStringEntry = DisplayStringLookupTable.Find(TextId);
		if (NewDisplayStringEntry)
		{
			// Can not update, that namespace and key combination is already in use by another string.
			return false;
		}
		else
		{
			// Get old namespace keys table and old live table entry under old key.
			FDisplayStringEntry* OldDisplayStringEntry = DisplayStringLookupTable.Find(ReverseLiveTableEntry);

			// Copy old live table entry to new key in the new namespace key table.
			check(OldDisplayStringEntry);
			DisplayStringLookupTable.Emplace(TextId, *OldDisplayStringEntry);

			// Remove old live table entry and old key in the old namespace key table.
			DisplayStringLookupTable.Remove(ReverseLiveTableEntry);
		}
	}

	// Update display string value.
	*DisplayString = Value;
	DirtyLocalRevisionForDisplayString(DisplayString);

	// Update entry from reverse live table.
	ReverseLiveTableEntry = TextId;

	return true;
}

void FTextLocalizationManager::UpdateFromLocalizationResource(const FString& LocalizationResourceFilePath)
{
	FTextLocalizationResource TextLocalizationResource;
	TextLocalizationResource.LoadFromFile(LocalizationResourceFilePath, 0);
	UpdateFromLocalizationResource(TextLocalizationResource);
}

void FTextLocalizationManager::UpdateFromLocalizationResource(const FTextLocalizationResource& TextLocalizationResource)
{
	UpdateFromLocalizations(CopyTemp(TextLocalizationResource));
}

void FTextLocalizationManager::RefreshResources()
{
	ensureMsgf(!IsInitializing(), TEXT("Reloading text localization resources during game text initialization"));

	ELocalizationLoadFlags LocLoadFlags = ELocalizationLoadFlags::None;
	LocLoadFlags |= (WITH_EDITOR ? ELocalizationLoadFlags::Editor : ELocalizationLoadFlags::None);
	LocLoadFlags |= (FApp::IsGame() ? ELocalizationLoadFlags::Game : ELocalizationLoadFlags::None);
	LocLoadFlags |= ELocalizationLoadFlags::Engine;
	LocLoadFlags |= ELocalizationLoadFlags::Native;
	LocLoadFlags |= ELocalizationLoadFlags::Additional;

	LoadLocalizationResourcesForCulture(FInternationalization::Get().GetCurrentLanguage()->GetName(), LocLoadFlags);
}

void FTextLocalizationManager::OnPakFileMounted(const IPakFile& PakFile)
{
	SCOPED_BOOT_TIMING("FTextLocalizationManager::OnPakFileMounted");
	LLM_SCOPE(ELLMTag::Localization);

	int32 ChunkId = PakFile.PakGetPakchunkIndex();
	if (ChunkId == INDEX_NONE || ChunkId == 0 || PakFile.GetNumFiles() == 0)
	{
		// Skip empty (IoStore), non-chunked PAK files, and chunk 0 as that contains the standard localization data
		return;
	}

	UE_LOG(LogTextLocalizationManager, Verbose, TEXT("Request to load localization data for chunk %d (from PAK '%s')"), ChunkId, *PakFile.PakGetPakFilename());

	// Skip this request if we've already loaded the data for this chunk via the request for a previous PAK sub-file load notification
	if (LocResTextSource->HasRegisteredChunkId(ChunkId))
	{
		UE_LOG(LogTextLocalizationManager, Verbose, TEXT("Skipped loading localization data for chunk %d (from PAK '%s') as this chunk has already been processed"), ChunkId, *PakFile.PakGetPakFilename());
		return;
	}
	
	// If we're being notified so early that even InitEngineTextLocalization hasn't run, then we can't safely make the queries below as things like GConfig may not be available yet!
	if (!IsInitialized())
	{
		// Track this so that full resource refreshes (eg, changing culture) work as expected
		LocResTextSource->RegisterChunkId(ChunkId);

		UE_LOG(LogTextLocalizationManager, Verbose, TEXT("Skipped loading localization data for chunk %d (from PAK '%s') as the localization manager isn't ready"), ChunkId, *PakFile.PakGetPakFilename());
		return;
	}

	ensureMsgf(!IsInitializing(), TEXT("Pak file mounted during game text initialization"));

	// Note: We only allow game localization targets to be chunked, and the layout is assumed to follow our standard pattern (as used by the localization dashboard and FLocTextHelper)
	const TArray<FString> ChunkedLocalizationTargets = FLocalizationResourceTextSource::GetChunkedLocalizationTargets();

	// Check to see whether all the required localization data is now available
	// This may not be the case if this PAK was split into multiple sub-files, and the localization data was split between them
	TArray<FString> PrioritizedLocalizationPaths;
	for (const FString& LocalizationTarget : ChunkedLocalizationTargets)
	{
		const FString ChunkedLocalizationTargetName = TextLocalizationResourceUtil::GetLocalizationTargetNameForChunkId(LocalizationTarget, ChunkId);

		FString ChunkedLocalizationTargetPath = FPaths::ProjectContentDir() / TEXT("Localization") / ChunkedLocalizationTargetName;
		if (!IFileManager::Get().DirectoryExists(*ChunkedLocalizationTargetPath))
		{
			UE_LOG(LogTextLocalizationManager, Verbose, TEXT("Skipped loading localization data for chunk %d (from PAK '%s') as the localization directory for '%s' was not yet available"), ChunkId, *PakFile.PakGetPakFilename(), *ChunkedLocalizationTargetName);
			return;
		}

		FTextLocalizationMetaDataResource LocMetaResource;
		{
			const FString LocMetaFilename = ChunkedLocalizationTargetPath / FString::Printf(TEXT("%s.locmeta"), *ChunkedLocalizationTargetName);
			if (!IFileManager::Get().FileExists(*LocMetaFilename))
			{
				UE_LOG(LogTextLocalizationManager, Verbose, TEXT("Skipped loading localization data for chunk %d (from PAK '%s') as the LocMeta file for '%s' was not yet available"), ChunkId, *PakFile.PakGetPakFilename(), *ChunkedLocalizationTargetName);
				return;
			}
			if (!LocMetaResource.LoadFromFile(LocMetaFilename))
			{
				UE_LOG(LogTextLocalizationManager, Verbose, TEXT("Skipped loading localization data for chunk %d (from PAK '%s') as the LocMeta file for '%s' failed to load"), ChunkId, *PakFile.PakGetPakFilename(), *ChunkedLocalizationTargetName);
				return;
			}
		}

		for (const FString& CompiledCulture : LocMetaResource.CompiledCultures)
		{
			const FString LocResFilename = ChunkedLocalizationTargetPath / CompiledCulture / FString::Printf(TEXT("%s.locres"), *ChunkedLocalizationTargetName);
			if (!IFileManager::Get().FileExists(*LocResFilename))
			{
				UE_LOG(LogTextLocalizationManager, Verbose, TEXT("Skipped loading localization data for chunk %d (from PAK '%s') as the '%s' LocRes file for '%s' was not yet available"), ChunkId, *PakFile.PakGetPakFilename(), *CompiledCulture, *ChunkedLocalizationTargetName);
				return;
			}
		}

		PrioritizedLocalizationPaths.Add(MoveTemp(ChunkedLocalizationTargetPath));
	}

	// Track this so that full resource refreshes (eg, changing culture) work as expected
	LocResTextSource->RegisterChunkId(ChunkId);

	if (!EnumHasAnyFlags(InitializedFlags, ETextLocalizationManagerInitializedFlags::Game))
	{
		// If we've not yet initialized game localization then don't bother patching, as the full initialization path will load the data for this chunk
		return;
	}

	// Load the resources from each target in this chunk
	const TArray<FString> PrioritizedCultureNames = FInternationalization::Get().GetPrioritizedCultureNames(FInternationalization::Get().GetCurrentLanguage()->GetName());
	const ELocalizationLoadFlags LocLoadFlags = ELocalizationLoadFlags::Game;
	FTextLocalizationResource UnusedNativeResource;
	FTextLocalizationResource LocalizedResource;
	for (const FString& PrioritizedLocalizationPath : PrioritizedLocalizationPaths)
	{
		UE_LOG(LogTextLocalizationManager, Verbose, TEXT("Loading chunked localization data from '%s'"), *PrioritizedLocalizationPath);
	}
	LocResTextSource->LoadLocalizedResourcesFromPaths(TArrayView<FString>(), PrioritizedLocalizationPaths, TArrayView<FString>(), LocLoadFlags, PrioritizedCultureNames, UnusedNativeResource, LocalizedResource);

	// Allow any higher priority text sources to override the text loaded for the chunk (eg, to allow polyglot hot-fixes to take priority)
	// Note: If any text sources don't support dynamic queries, then we must do a much slower full refresh instead :(
	bool bNeedsFullRefresh = false;
	{
		// Copy the IDs array as QueryLocalizedResource can update the map
		TArray<FTextId> ChunkTextIds;
		LocalizedResource.Entries.GenerateKeyArray(ChunkTextIds);

		for (const TSharedPtr<ILocalizedTextSource>& LocalizedTextSource : LocalizedTextSources)
		{
			if (LocalizedTextSource->GetPriority() <= LocResTextSource->GetPriority())
			{
				continue;
			}

			for (const FTextId& ChunkTextId : ChunkTextIds)
			{
				if (LocalizedTextSource->QueryLocalizedResource(LocLoadFlags, PrioritizedCultureNames, ChunkTextId, UnusedNativeResource, LocalizedResource) == EQueryLocalizedResourceResult::NotImplemented)
				{
					bNeedsFullRefresh = true;
					break;
				}
			}

			if (bNeedsFullRefresh)
			{
				break;
			}
		}
	}

	// Apply the new data
	if (bNeedsFullRefresh)
	{
		UE_LOG(LogTextLocalizationManager, Verbose, TEXT("Patching chunked localization data failed, performing full refresh"));
		RefreshResources();
	}
	else
	{
		UE_LOG(LogTextLocalizationManager, Verbose, TEXT("Patching chunked localization data for %d entries"), LocalizedResource.Entries.Num());
		UpdateFromLocalizations(MoveTemp(LocalizedResource), /*bDirtyTextRevision*/true);
	}
}

void FTextLocalizationManager::OnCultureChanged()
{
    if (!IsInitialized())
	{
		// Ignore culture changes while the text localization manager is still being initialized
		// The correct data will be loaded by EndInitTextLocalization
		return;
	}

	ensureMsgf(!IsInitializing(), TEXT("Culture changed during game text initialization"));

	ELocalizationLoadFlags LocLoadFlags = ELocalizationLoadFlags::None;
	LocLoadFlags |= (WITH_EDITOR ? ELocalizationLoadFlags::Editor : ELocalizationLoadFlags::None);
	LocLoadFlags |= (FApp::IsGame() ? ELocalizationLoadFlags::Game : ELocalizationLoadFlags::None);
	LocLoadFlags |= ELocalizationLoadFlags::Engine;
	LocLoadFlags |= ELocalizationLoadFlags::Native;
	LocLoadFlags |= ELocalizationLoadFlags::Additional;

	LoadLocalizationResourcesForCulture(FInternationalization::Get().GetCurrentLanguage()->GetName(), LocLoadFlags);
}

void FTextLocalizationManager::LoadLocalizationResourcesForCulture(const FString& CultureName, const ELocalizationLoadFlags LocLoadFlags)
{
    LLM_SCOPE(ELLMTag::Localization);

	// Don't attempt to process an empty culture name, early-out.
	if (CultureName.IsEmpty())
	{
		return;
	}

	// Can't load localization resources for a culture that doesn't exist, early-out.
	const FCulturePtr Culture = FInternationalization::Get().GetCulture(CultureName);
	if (!Culture.IsValid())
	{
		return;
	}

	LoadLocalizationResourcesForPrioritizedCultures(FInternationalization::Get().GetPrioritizedCultureNames(CultureName), LocLoadFlags);
}

void FTextLocalizationManager::LoadLocalizationResourcesForPrioritizedCultures(TArrayView<const FString> PrioritizedCultureNames, const ELocalizationLoadFlags LocLoadFlags)
{
	LLM_SCOPE(ELLMTag::Localization);

	// Nothing to do?
	if (PrioritizedCultureNames.Num() == 0)
	{
		return;
	}

	// Leet-ify always needs the native text to operate on, so force native data if we're loading for LEET
	ELocalizationLoadFlags FinalLocLoadFlags = LocLoadFlags;
#if ENABLE_LOC_TESTING
	if (PrioritizedCultureNames[0] == FLeetCulture::StaticGetName())
	{
		FinalLocLoadFlags |= ELocalizationLoadFlags::Native;
	}
#endif

	// Load the resources from each text source
	FTextLocalizationResource NativeResource;
	FTextLocalizationResource LocalizedResource;
	for (const TSharedPtr<ILocalizedTextSource>& LocalizedTextSource : LocalizedTextSources)
	{
		LocalizedTextSource->LoadLocalizedResources(FinalLocLoadFlags, PrioritizedCultureNames, NativeResource, LocalizedResource);
	}

	// When loc testing is enabled, UpdateFromNative also takes care of restoring non-localized text which is why the condition below is gated
#if !ENABLE_LOC_TESTING
	if (!NativeResource.IsEmpty())
#endif
	{
		UpdateFromNative(MoveTemp(NativeResource), /*bDirtyTextRevision*/false);
	}

#if ENABLE_LOC_TESTING
	// The leet culture is fake. Just leet-ify existing strings.
	if (PrioritizedCultureNames[0] == FLeetCulture::StaticGetName())
	{
		// Lock while updating the tables
		{
			FScopeLock ScopeLock(&SynchronizationObject);

			for (auto& DisplayStringPair : DisplayStringLookupTable)
			{
				FDisplayStringEntry& LiveEntry = DisplayStringPair.Value;
				LiveEntry.bIsLocalized = true;
				LiveEntry.NativeStringBackup = *LiveEntry.DisplayString;
				FInternationalization::Leetify(*LiveEntry.DisplayString);
			}
		}
	}
	else
#endif
	{
		// Replace localizations with those of the loaded localization resources.
		if (!LocalizedResource.IsEmpty())
		{
			UpdateFromLocalizations(MoveTemp(LocalizedResource), /*bDirtyTextRevision*/false);
		}
	}

	DirtyTextRevision();
}

void FTextLocalizationManager::UpdateFromNative(FTextLocalizationResource&& TextLocalizationResource, const bool bDirtyTextRevision)
{
	// Lock while updating the tables
	{
		FScopeLock ScopeLock(&SynchronizationObject);

		DisplayStringLookupTable.Reserve(TextLocalizationResource.Entries.Num());
		NamespaceKeyLookupTable.Reserve(TextLocalizationResource.Entries.Num());

		// Add/update entries
		// Note: This code doesn't handle "leet-ification" itself as it is resetting everything to a known "good" state ("leet-ification" happens later on the "good" native text)
		for (auto& EntryPair : TextLocalizationResource.Entries)
		{
			const FTextId TextId = EntryPair.Key;
			FTextLocalizationResource::FEntry& NewEntry = EntryPair.Value;

			FDisplayStringEntry* LiveEntry = DisplayStringLookupTable.Find(TextId);
			if (LiveEntry)
			{
				// Update existing entry
				if (LiveEntry->SourceStringHash == NewEntry.SourceStringHash)
				{
					LiveEntry->bIsLocalized = true;
					*LiveEntry->DisplayString = MoveTemp(NewEntry.LocalizedString);
#if WITH_EDITORONLY_DATA
					LiveEntry->LocResID = NewEntry.LocResID;
#endif	// WITH_EDITORONLY_DATA
#if ENABLE_LOC_TESTING
					LiveEntry->NativeStringBackup.Reset();
#endif	// ENABLE_LOC_TESTING
				}
			}
			else
			{
				// Add new entry
				FDisplayStringEntry NewLiveEntry(
					true,																			/*bIsLocalized*/
					NewEntry.LocResID,																/*LocResID*/
					NewEntry.SourceStringHash,														/*SourceStringHash*/
					MakeShared<FString, ESPMode::ThreadSafe>(MoveTemp(NewEntry.LocalizedString))	/*String*/
				);

				DisplayStringLookupTable.Emplace(TextId, NewLiveEntry);
				NamespaceKeyLookupTable.Emplace(NewLiveEntry.DisplayString, TextId);
			}
		}

		// Note: Do not use TextLocalizationResource after this point as we may have stolen some of its strings
		TextLocalizationResource.Entries.Reset();

		// Perform any additional processing over existing entries
#if ENABLE_LOC_TESTING || USE_STABLE_LOCALIZATION_KEYS
		for (auto& DisplayStringPair : DisplayStringLookupTable)
		{
			FDisplayStringEntry& LiveEntry = DisplayStringPair.Value;

#if USE_STABLE_LOCALIZATION_KEYS
			// In builds with stable keys enabled, we have to update the display strings from the "clean" version of the text (if the sources match) as this is the only version that is translated
			if (GIsEditor)
			{
				const FString LiveNamespace = DisplayStringPair.Key.GetNamespace().GetChars();
				const FString DisplayNamespace = TextNamespaceUtil::StripPackageNamespace(LiveNamespace);
				if (!LiveNamespace.Equals(DisplayNamespace, ESearchCase::CaseSensitive))
				{
					const FDisplayStringEntry* DisplayStringEntry = DisplayStringLookupTable.Find(FTextId(DisplayNamespace, DisplayStringPair.Key.GetKey()));
					if (DisplayStringEntry && LiveEntry.SourceStringHash == DisplayStringEntry->SourceStringHash)
					{
						LiveEntry.bIsLocalized = true;
						*LiveEntry.DisplayString = *DisplayStringEntry->DisplayString;
#if WITH_EDITORONLY_DATA
						LiveEntry.LocResID = DisplayStringEntry->LocResID;
#endif	// WITH_EDITORONLY_DATA
#if ENABLE_LOC_TESTING
						LiveEntry.NativeStringBackup.Reset();
#endif	// ENABLE_LOC_TESTING
					}
				}
			}
#endif	// USE_STABLE_LOCALIZATION_KEYS

#if ENABLE_LOC_TESTING
			// Restore the pre-leet state (if any)
			if (!LiveEntry.NativeStringBackup.IsEmpty())
			{
				LiveEntry.bIsLocalized = false;
				*LiveEntry.DisplayString = MoveTemp(LiveEntry.NativeStringBackup);
				LiveEntry.NativeStringBackup.Reset();
#if WITH_EDITORONLY_DATA
				LiveEntry.LocResID = FTextKey();
#endif	// WITH_EDITORONLY_DATA
			}
#endif	// ENABLE_LOC_TESTING
		}
#endif	// ENABLE_LOC_TESTING || USE_STABLE_LOCALIZATION_KEYS
	}

	if (bDirtyTextRevision)
	{
		DirtyTextRevision();
	}
}

void FTextLocalizationManager::UpdateFromLocalizations(FTextLocalizationResource&& TextLocalizationResource, const bool bDirtyTextRevision)
{
	static const bool bShouldLEETIFYUnlocalizedString = FParse::Param(FCommandLine::Get(), TEXT("LEETIFYUnlocalized"));

	// Lock while updating the tables
	{
		FScopeLock ScopeLock(&SynchronizationObject);

		DisplayStringLookupTable.Reserve(TextLocalizationResource.Entries.Num());
		NamespaceKeyLookupTable.Reserve(TextLocalizationResource.Entries.Num());

		// Add/update entries
		for (auto& EntryPair : TextLocalizationResource.Entries)
		{
			const FTextId TextId = EntryPair.Key;
			FTextLocalizationResource::FEntry& NewEntry = EntryPair.Value;

			FDisplayStringEntry* LiveEntry = DisplayStringLookupTable.Find(TextId);
			if (LiveEntry)
			{
				// Update existing entry
				// If the source string hashes are are the same, we can replace the display string.
				// Otherwise, it would suggest the source string has changed and the new localization may be based off of an old source string.
				if (LiveEntry->SourceStringHash == NewEntry.SourceStringHash)
				{
					LiveEntry->bIsLocalized = true;
					*LiveEntry->DisplayString = MoveTemp(NewEntry.LocalizedString);
#if WITH_EDITORONLY_DATA
					LiveEntry->LocResID = NewEntry.LocResID;
#endif	// WITH_EDITORONLY_DATA
				}
#if ENABLE_LOC_TESTING
				else if (bShouldLEETIFYUnlocalizedString)
				{
					LiveEntry->bIsLocalized = false;
					FInternationalization::Leetify(*LiveEntry->DisplayString);
#if WITH_EDITORONLY_DATA
					LiveEntry->LocResID = FTextKey();
#endif	// WITH_EDITORONLY_DATA
				}
#endif	// ENABLE_LOC_TESTING
			}
			else
			{
				// Add new entry
				FDisplayStringEntry NewLiveEntry(
					true,																			/*bIsLocalized*/
					NewEntry.LocResID,																/*LocResID*/
					NewEntry.SourceStringHash,														/*SourceStringHash*/
					MakeShared<FString, ESPMode::ThreadSafe>(MoveTemp(NewEntry.LocalizedString))	/*String*/
				);

				DisplayStringLookupTable.Emplace(TextId, NewLiveEntry);
				NamespaceKeyLookupTable.Emplace(NewLiveEntry.DisplayString, TextId);
			}
		}

		// Note: Do not use TextLocalizationResource after this point as we may have stolen some of its strings
		TextLocalizationResource.Entries.Reset();

		// Perform any additional processing over existing entries
#if USE_STABLE_LOCALIZATION_KEYS
		if (GIsEditor)
		{
			for (auto& DisplayStringPair : DisplayStringLookupTable)
			{
				FDisplayStringEntry& LiveEntry = DisplayStringPair.Value;

				// In builds with stable keys enabled, we have to update the display strings from the "clean" version of the text (if the sources match) as this is the only version that is translated
				const FString LiveNamespace = DisplayStringPair.Key.GetNamespace().GetChars();
				const FString DisplayNamespace = TextNamespaceUtil::StripPackageNamespace(LiveNamespace);
				if (!LiveNamespace.Equals(DisplayNamespace, ESearchCase::CaseSensitive))
				{
					const FDisplayStringEntry* DisplayStringEntry = DisplayStringLookupTable.Find(FTextId(DisplayNamespace, DisplayStringPair.Key.GetKey()));

					// If the source string hashes are are the same, we can replace the display string.
					// Otherwise, it would suggest the source string has changed and the new localization may be based off of an old source string.
					if (DisplayStringEntry && LiveEntry.SourceStringHash == DisplayStringEntry->SourceStringHash)
					{
						LiveEntry.bIsLocalized = true;
						*LiveEntry.DisplayString = *DisplayStringEntry->DisplayString;
#if WITH_EDITORONLY_DATA
						LiveEntry.LocResID = DisplayStringEntry->LocResID;
#endif	// WITH_EDITORONLY_DATA
					}
#if ENABLE_LOC_TESTING
					else if (bShouldLEETIFYUnlocalizedString)
					{
						LiveEntry.bIsLocalized = false;
						FInternationalization::Leetify(*LiveEntry.DisplayString);
#if WITH_EDITORONLY_DATA
						LiveEntry.LocResID = FTextKey();
#endif	// WITH_EDITORONLY_DATA
					}
#endif	// ENABLE_LOC_TESTING
				}
			}
		}
#endif	// USE_STABLE_LOCALIZATION_KEYS
	}

	if (bDirtyTextRevision)
	{
		DirtyTextRevision();
	}
}

void FTextLocalizationManager::DirtyLocalRevisionForDisplayString(const FTextDisplayStringRef& InDisplayString)
{
	FScopeLock ScopeLock(&SynchronizationObject);

	uint16* FoundLocalRevision = LocalTextRevisions.Find(InDisplayString);
	if (FoundLocalRevision)
	{
		while (++(*FoundLocalRevision) == 0) {} // Zero is special, don't allow an overflow to stay at zero
	}
	else
	{
		LocalTextRevisions.Add(InDisplayString, 1);
	}
}

void FTextLocalizationManager::DirtyTextRevision()
{
	// Lock while updating the data
	{
		FScopeLock ScopeLock(&SynchronizationObject);

		while (++TextRevisionCounter == 0) {} // Zero is special, don't allow an overflow to stay at zero
		LocalTextRevisions.Empty();
	}

	OnTextRevisionChangedEvent.Broadcast();
}

#if WITH_EDITOR
void FTextLocalizationManager::EnableGameLocalizationPreview()
{
	EnableGameLocalizationPreview(GetConfiguredGameLocalizationPreviewLanguage());
}

void FTextLocalizationManager::EnableGameLocalizationPreview(const FString& CultureName)
{
	// This only works in the editor
	if (!GIsEditor)
	{
		return;
	}

	// We need the native game culture to be available for this preview to work correctly
	const FString NativeGameCulture = GetNativeCultureName(ELocalizedTextSourceCategory::Game);
	if (NativeGameCulture.IsEmpty())
	{
		return;
	}

	const FString PreviewCulture = CultureName.IsEmpty() ? NativeGameCulture : CultureName;
	bIsGameLocalizationPreviewEnabled = PreviewCulture != NativeGameCulture;
	bIsLocalizationLocked = IsLocalizationLockedByConfig() || bIsGameLocalizationPreviewEnabled;

	TArray<FString> PrioritizedCultureNames;
	if (bIsGameLocalizationPreviewEnabled)
	{
		PrioritizedCultureNames = FInternationalization::Get().GetPrioritizedCultureNames(PreviewCulture);
	}
	else
	{
		PrioritizedCultureNames.Add(PreviewCulture);
	}

	ELocalizationLoadFlags LocLoadFlags = ELocalizationLoadFlags::Game | ELocalizationLoadFlags::ForceLocalizedGame;
	LocLoadFlags |= (bIsGameLocalizationPreviewEnabled ? ELocalizationLoadFlags::Native : ELocalizationLoadFlags::None);

	LoadLocalizationResourcesForPrioritizedCultures(PrioritizedCultureNames, LocLoadFlags);
}

void FTextLocalizationManager::DisableGameLocalizationPreview()
{
	EnableGameLocalizationPreview(GetNativeCultureName(ELocalizedTextSourceCategory::Game));
}

bool FTextLocalizationManager::IsGameLocalizationPreviewEnabled() const
{
	return bIsGameLocalizationPreviewEnabled;
}

void FTextLocalizationManager::PushAutoEnableGameLocalizationPreview()
{
	++GameLocalizationPreviewAutoEnableCount;
}

void FTextLocalizationManager::PopAutoEnableGameLocalizationPreview()
{
	checkf(GameLocalizationPreviewAutoEnableCount > 0, TEXT("Call to PopAutoEnableGameLocalizationPreview missing corresponding call to PushAutoEnableGameLocalizationPreview!"));
	--GameLocalizationPreviewAutoEnableCount;
}

bool FTextLocalizationManager::ShouldGameLocalizationPreviewAutoEnable() const
{
	return GameLocalizationPreviewAutoEnableCount > 0;
}

void FTextLocalizationManager::ConfigureGameLocalizationPreviewLanguage(const FString& CultureName)
{
	GConfig->SetString(TEXT("Internationalization"), TEXT("PreviewGameLanguage"), *CultureName, GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

FString FTextLocalizationManager::GetConfiguredGameLocalizationPreviewLanguage() const
{
	return GConfig->GetStr(TEXT("Internationalization"), TEXT("PreviewGameLanguage"), GEditorPerProjectIni);
}

bool FTextLocalizationManager::IsLocalizationLocked() const
{
	return bIsLocalizationLocked;
}
#endif
