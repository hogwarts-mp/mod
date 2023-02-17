// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "HAL/PlatformStackWalk.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Internationalization/Internationalization.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/App.h"
#include "Modules/ModuleManager.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Internationalization/Regex.h"
#include <inttypes.h>
#include "Misc/App.h"

DEFINE_LOG_CATEGORY_STATIC(LogAutomationTest, Warning, All);

/*
	Determine the level that a log item should be written to the automation log based on the properties of the current test. 
	only Display/Warning/Error are supported in the automation log so anything with NoLogging/Log will not be shown
	(Should be moved under a namespace for 4.27).
*/
CORE_API ELogVerbosity::Type GetAutomationLogLevel(ELogVerbosity::Type LogVerbosity, FAutomationTestBase* CurrentTest)
{
	ELogVerbosity::Type EffectiveVerbosity = LogVerbosity;

	// agrant-todo: these should be controlled by FAutomationTestBase for 4.27 with the same project-level override that
	// FunctionalTest has. Now that warnings are correctly associated with tests they need to be something all tests
	// can leverage, not just functional tests
	static bool bSuppressLogWarnings = false;
	static bool bSuppressLogErrors = false;
	static bool bTreatLogWarningsAsTestErrors = false;

	static FAutomationTestBase* LastTest = nullptr;

	if (CurrentTest != LastTest)
	{
		// These can be changed in the editor so can't just be cached for the whole session
		GConfig->GetBool(TEXT("/Script/AutomationController.AutomationControllerSettings"), TEXT("bSuppressLogErrors"), bSuppressLogErrors, GEngineIni);
		GConfig->GetBool(TEXT("/Script/AutomationController.AutomationControllerSettings"), TEXT("bSuppressLogWarnings"), bSuppressLogWarnings, GEngineIni);
		GConfig->GetBool(TEXT("/Script/AutomationController.AutomationControllerSettings"), TEXT("bTreatLogWarningsAsTestErrors"), bTreatLogWarningsAsTestErrors, GEngineIni);
		LastTest = CurrentTest;
	}

	if (CurrentTest)
	{
		if (CurrentTest->SuppressLogs())
		{
			EffectiveVerbosity = ELogVerbosity::NoLogging;
		}
		else
		{
			if (EffectiveVerbosity == ELogVerbosity::Warning)
			{
				if (CurrentTest->SuppressLogWarnings() || bSuppressLogWarnings)
				{
					EffectiveVerbosity = ELogVerbosity::NoLogging;
				}
				else if (CurrentTest->ElevateLogWarningsToErrors() || bTreatLogWarningsAsTestErrors)
				{
					EffectiveVerbosity = ELogVerbosity::Error;
				}
			}

			if (EffectiveVerbosity == ELogVerbosity::Error)
			{
				if (CurrentTest->SuppressLogErrors() ||  bSuppressLogErrors)
				{
					EffectiveVerbosity = ELogVerbosity::NoLogging;
				}
			}
		}
	}

	return EffectiveVerbosity;
}

void FAutomationTestFramework::FAutomationTestOutputDevice::Serialize( const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category )
{
	const int32 STACK_OFFSET = 5;//FMsg::Logf_InternalImpl
	// TODO would be nice to search for the first stack frame that isn't in outputdevice or other logging files, would be more robust.

	if (!IsRunningCommandlet() && (Verbosity == ELogVerbosity::SetColor))
	{
		return;
	}

	// Ensure there's a valid unit test associated with the context
	if (CurTest)
	{
		bool CaptureLog = !CurTest->SuppressLogs()
			&& (Verbosity == ELogVerbosity::Error || Verbosity == ELogVerbosity::Warning || Verbosity == ELogVerbosity::Display);

		if (CaptureLog)
		{
		
			ELogVerbosity::Type EffectiveVerbosity = GetAutomationLogLevel(Verbosity, CurTest);
			
			// Errors
			if (EffectiveVerbosity == ELogVerbosity::Error)
			{
				CurTest->AddError(FString(V), STACK_OFFSET);
			}
			// Warnings
			else if (EffectiveVerbosity == ELogVerbosity::Warning)
			{
				CurTest->AddWarning(FString(V), STACK_OFFSET);
			}
			// Display
			else
			{
				CurTest->AddInfo(FString(V), STACK_OFFSET);
			}
		}
		// Log...etc
		else
		{
			// IMPORTANT NOTE: This code will never be called in a build with NO_LOGGING defined, which means pretty much
			// any Test or Shipping config build.  If you're trying to use the automation test framework for performance
			// data capture in a Test config, you'll want to call the AddAnalyticsItemToCurrentTest() function instead of
			// using this log interception stuff.

			FString LogString = FString(V);
			FString AnalyticsString = TEXT("AUTOMATIONANALYTICS");
			if (LogString.StartsWith(*AnalyticsString))
			{
				//Remove "analytics" from the string
				LogString.RightInline(LogString.Len() - (AnalyticsString.Len() + 1), false);

				CurTest->AddAnalyticsItem(LogString);
			}
			//else
			//{
			//	CurTest->AddInfo(LogString, STACK_OFFSET);
			//}
		}
	}
}

void FAutomationTestFramework::FAutomationTestMessageFilter::Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category)
{
	if (DestinationContext)
	{
		if ((Verbosity == ELogVerbosity::Warning) || (Verbosity == ELogVerbosity::Error))
		{
			if (CurTest->IsExpectedError(FString(V)))
			{
				Verbosity = ELogVerbosity::Verbose;
			}
		}

		DestinationContext->Serialize(V, Verbosity, Category);
	}
}

FAutomationTestFramework& FAutomationTestFramework::Get()
{
	static FAutomationTestFramework Framework;
	return Framework;
}

FString FAutomationTestFramework::GetUserAutomationDirectory() const
{
	const FString DefaultAutomationSubFolder = TEXT("Unreal Automation");
	return FString(FPlatformProcess::UserDir()) + DefaultAutomationSubFolder;
}

bool FAutomationTestFramework::RegisterAutomationTest( const FString& InTestNameToRegister, class FAutomationTestBase* InTestToRegister )
{
	const bool bAlreadyRegistered = AutomationTestClassNameToInstanceMap.Contains( InTestNameToRegister );
	if ( !bAlreadyRegistered )
	{
		AutomationTestClassNameToInstanceMap.Add( InTestNameToRegister, InTestToRegister );
	}
	return !bAlreadyRegistered;
}

bool FAutomationTestFramework::UnregisterAutomationTest( const FString& InTestNameToUnregister )
{
	const bool bRegistered = AutomationTestClassNameToInstanceMap.Contains( InTestNameToUnregister );
	if ( bRegistered )
	{
		AutomationTestClassNameToInstanceMap.Remove( InTestNameToUnregister );
	}
	return bRegistered;
}

void FAutomationTestFramework::EnqueueLatentCommand(TSharedPtr<IAutomationLatentCommand> NewCommand)
{
	//ensure latent commands are never used within smoke tests - will only catch when smokes are exclusively requested
	check((RequestedTestFilter & EAutomationTestFlags::FilterMask) != EAutomationTestFlags::SmokeFilter);

	//ensure we are currently "running a test"
	check(GIsAutomationTesting);

	LatentCommands.Enqueue(NewCommand);
}

void FAutomationTestFramework::EnqueueNetworkCommand(TSharedPtr<IAutomationNetworkCommand> NewCommand)
{
	//ensure latent commands are never used within smoke tests
	check((RequestedTestFilter & EAutomationTestFlags::FilterMask) != EAutomationTestFlags::SmokeFilter);

	//ensure we are currently "running a test"
	check(GIsAutomationTesting);

	NetworkCommands.Enqueue(NewCommand);
}

bool FAutomationTestFramework::ContainsTest( const FString& InTestName ) const
{
	return AutomationTestClassNameToInstanceMap.Contains( InTestName );
}

bool FAutomationTestFramework::RunSmokeTests()
{
	bool bAllSuccessful = true;

	uint32 PreviousRequestedTestFilter = RequestedTestFilter;
	//so extra log spam isn't generated
	RequestedTestFilter = EAutomationTestFlags::SmokeFilter;
	
	// Skip running on cooked platforms like mobile
	//@todo - better determination of whether to run than requires cooked data
	// Ensure there isn't another slow task in progress when trying to run unit tests
	const bool bRequiresCookedData = FPlatformProperties::RequiresCookedData();
	if ((!bRequiresCookedData && !GIsSlowTask && !GIsPlayInEditorWorld && !FPlatformProperties::IsProgram()) || bForceSmokeTests)
	{
		TArray<FAutomationTestInfo> TestInfo;

		GetValidTestNames( TestInfo );

		if ( TestInfo.Num() > 0 )
		{
			const double SmokeTestStartTime = FPlatformTime::Seconds();

			// Output the results of running the automation tests
			TMap<FString, FAutomationTestExecutionInfo> OutExecutionInfoMap;

			// Run each valid test
			FScopedSlowTask SlowTask((float)TestInfo.Num());

			// We disable capturing the stack when running smoke tests, it adds too much overhead to do it at startup.
			FAutomationTestFramework::Get().SetCaptureStack(false);

			double SlowestTestDuration = 0.0f;
			FString SlowestTestName;
			for ( int TestIndex = 0; TestIndex < TestInfo.Num(); ++TestIndex )
			{
				SlowTask.EnterProgressFrame(1);
				if (TestInfo[TestIndex].GetTestFlags() & EAutomationTestFlags::SmokeFilter )
				{
					FString TestCommand = TestInfo[TestIndex].GetTestName();
					FAutomationTestExecutionInfo& CurExecutionInfo = OutExecutionInfoMap.Add( TestCommand, FAutomationTestExecutionInfo() );
					
					int32 RoleIndex = 0;  //always default to "local" role index.  Only used for multi-participant tests
					StartTestByName( TestCommand, RoleIndex );
					const bool CurTestSuccessful = StopTest(CurExecutionInfo);

					bAllSuccessful = bAllSuccessful && CurTestSuccessful;

					if (CurTestSuccessful && CurExecutionInfo.Duration > SlowestTestDuration)
					{
						SlowestTestDuration = CurExecutionInfo.Duration;
						SlowestTestName = MoveTemp(TestCommand);
					}
				}
			}

			FAutomationTestFramework::Get().SetCaptureStack(true);

			const double TimeForTest = FPlatformTime::Seconds() - SmokeTestStartTime;
			if (TimeForTest > 2.0f)
			{
				//force a failure if a smoke test takes too long
				UE_LOG(LogAutomationTest, Warning, TEXT("Smoke tests took >2s to run (%.2fs). '%s' took %dms. "
					"SmokeFilter tier tests should take less than 1ms. Please optimize or move '%s' to a slower tier than SmokeFilter."), 
					TimeForTest, *SlowestTestName, static_cast<int32>(1000*SlowestTestDuration), *SlowestTestName);
			}

			FAutomationTestFramework::DumpAutomationTestExecutionInfo( OutExecutionInfoMap );
		}
	}
	else if( bRequiresCookedData )
	{
		UE_LOG( LogAutomationTest, Log, TEXT( "Skipping unit tests for the cooked build." ) );
	}
	else if (!FPlatformProperties::IsProgram())
	{
		UE_LOG(LogAutomationTest, Error, TEXT("Skipping unit tests.") );
		bAllSuccessful = false;
	}

	//revert to allowing all logs
	RequestedTestFilter = PreviousRequestedTestFilter;

	return bAllSuccessful;
}

void FAutomationTestFramework::ResetTests()
{
	bool bEnsureExists = false;
	bool bDeleteEntireTree = true;
	//make sure all transient files are deleted successfully
	IFileManager::Get().DeleteDirectory(*FPaths::AutomationTransientDir(), bEnsureExists, bDeleteEntireTree);
}

void FAutomationTestFramework::StartTestByName( const FString& InTestToRun, const int32 InRoleIndex )
{
	if (GIsAutomationTesting)
	{
		while(!LatentCommands.IsEmpty())
		{
			TSharedPtr<IAutomationLatentCommand> TempCommand;
			LatentCommands.Dequeue(TempCommand);
		}
		while(!NetworkCommands.IsEmpty())
		{
			TSharedPtr<IAutomationNetworkCommand> TempCommand;
			NetworkCommands.Dequeue(TempCommand);
		}
		FAutomationTestExecutionInfo TempExecutionInfo;
		StopTest(TempExecutionInfo);
	}

	FString TestName;
	FString Params;
	if (!InTestToRun.Split(TEXT(" "), &TestName, &Params, ESearchCase::CaseSensitive))
	{
		TestName = InTestToRun;
	}

	NetworkRoleIndex = InRoleIndex;

	// Ensure there isn't another slow task in progress when trying to run unit tests
	if ( !GIsSlowTask && !GIsPlayInEditorWorld )
	{
		// Ensure the test exists in the framework and is valid to run
		if ( ContainsTest( TestName ) )
		{
			// Make any setting changes that have to occur to support unit testing
			PrepForAutomationTests();

			InternalStartTest( InTestToRun );
		}
		else
		{
			UE_LOG(LogAutomationTest, Error, TEXT("Test %s does not exist and could not be run."), *InTestToRun);
		}
	}
	else
	{
		UE_LOG(LogAutomationTest, Error, TEXT("Test %s is too slow and could not be run."), *InTestToRun);
	}
}

bool FAutomationTestFramework::StopTest( FAutomationTestExecutionInfo& OutExecutionInfo )
{
	check(GIsAutomationTesting);
	
	bool bSuccessful = InternalStopTest(OutExecutionInfo);

	// Restore any changed settings now that unit testing has completed
	ConcludeAutomationTests();

	return bSuccessful;
}


bool FAutomationTestFramework::ExecuteLatentCommands()
{
	check(GIsAutomationTesting);

	bool bHadAnyLatentCommands = !LatentCommands.IsEmpty();
	while (!LatentCommands.IsEmpty())
	{
		//get the next command to execute
		TSharedPtr<IAutomationLatentCommand> NextCommand;
		LatentCommands.Peek(NextCommand);

		bool bComplete = NextCommand->InternalUpdate();
		if (bComplete)
		{
			//all done.  remove from the queue
			LatentCommands.Dequeue(NextCommand);
		}
		else
		{
			break;
		}
	}
	//need more processing on the next frame
	if (bHadAnyLatentCommands)
	{
		return false;
	}

	return true;
}

bool FAutomationTestFramework::ExecuteNetworkCommands()
{
	check(GIsAutomationTesting);
	bool bHadAnyNetworkCommands = !NetworkCommands.IsEmpty();

	if( bHadAnyNetworkCommands )
	{
		// Get the next command to execute
		TSharedPtr<IAutomationNetworkCommand> NextCommand;
		NetworkCommands.Dequeue(NextCommand);
		if (NextCommand->GetRoleIndex() == NetworkRoleIndex)
		{
			NextCommand->Run();
		}
	}

	return !bHadAnyNetworkCommands;
}

void FAutomationTestFramework::DequeueAllCommands()
{
	while (!LatentCommands.IsEmpty())
	{
		TSharedPtr<IAutomationLatentCommand> TempCommand;
		LatentCommands.Dequeue(TempCommand);
	}
	while (!NetworkCommands.IsEmpty())
	{
		TSharedPtr<IAutomationNetworkCommand> TempCommand;
		NetworkCommands.Dequeue(TempCommand);
	}
}

void FAutomationTestFramework::LoadTestModules( )
{
	const bool bRunningEditor = GIsEditor && !IsRunningCommandlet();

	bool bRunningSmokeTests = ((RequestedTestFilter & EAutomationTestFlags::FilterMask) == EAutomationTestFlags::SmokeFilter);
	if( !bRunningSmokeTests )
	{
		TArray<FString> EngineTestModules;
		GConfig->GetArray( TEXT("/Script/Engine.AutomationTestSettings"), TEXT("EngineTestModules"), EngineTestModules, GEngineIni);
		//Load any engine level modules.
		for( int32 EngineModuleId = 0; EngineModuleId < EngineTestModules.Num(); ++EngineModuleId)
		{
			const FName ModuleName = FName(*EngineTestModules[EngineModuleId]);
			//Make sure that there is a name available.  This can happen if a name is left blank in the Engine.ini
			if (ModuleName == NAME_None || ModuleName == TEXT("None"))
			{
				UE_LOG(LogAutomationTest, Warning, TEXT("The automation test module ('%s') doesn't have a valid name."), *ModuleName.ToString());
				continue;
			}
			if (!FModuleManager::Get().IsModuleLoaded(ModuleName))
			{
				UE_LOG(LogAutomationTest, Log, TEXT("Loading automation test module: '%s'."), *ModuleName.ToString());
				FModuleManager::Get().LoadModule(ModuleName);
			}
		}
		//Load any editor modules.
		if( bRunningEditor )
		{
			TArray<FString> EditorTestModules;
			GConfig->GetArray( TEXT("/Script/Engine.AutomationTestSettings"), TEXT("EditorTestModules"), EditorTestModules, GEngineIni);
			for( int32 EditorModuleId = 0; EditorModuleId < EditorTestModules.Num(); ++EditorModuleId )
			{
				const FName ModuleName = FName(*EditorTestModules[EditorModuleId]);
				//Make sure that there is a name available.  This can happen if a name is left blank in the Engine.ini
				if (ModuleName == NAME_None || ModuleName == TEXT("None"))
				{
					UE_LOG(LogAutomationTest, Warning, TEXT("The automation test module ('%s') doesn't have a valid name."), *ModuleName.ToString());
					continue;
				}
				if (!FModuleManager::Get().IsModuleLoaded(ModuleName))
				{
					UE_LOG(LogAutomationTest, Log, TEXT("Loading automation test module: '%s'."), *ModuleName.ToString());
					FModuleManager::Get().LoadModule(ModuleName);
				}
			}
		}
	}
}

void FAutomationTestFramework::BuildTestBlacklistFromConfig()
{
	TestBlacklist.Empty();
	if (GConfig)
	{

		const FString CommandLine = FCommandLine::Get();

		for (const TPair<FString, FConfigFile>& Config : *GConfig)
		{
			FConfigSection* BlacklistSection = GConfig->GetSectionPrivate(TEXT("AutomationTestBlacklist"), false, true, Config.Key);
			if (BlacklistSection)
			{
				// Parse all blacklist definitions of the format "BlacklistTest=(Map=/Game/Tests/MapName, Test=TestName, Reason="Foo")"
				for (FConfigSection::TIterator Section(*BlacklistSection); Section; ++Section)
				{
					if (Section.Key() == TEXT("BlacklistTest"))
					{
						FString BlacklistValue = Section.Value().GetValue();
						FString Map, Test, Reason, RHIs, Warn, ListName;
						bool bSuccess = false;

						if (FParse::Value(*BlacklistValue, TEXT("Test="), Test, true))
						{
							ListName = FString(Test);
							FParse::Value(*BlacklistValue, TEXT("Map="), Map, true);
							FParse::Value(*BlacklistValue, TEXT("Reason="), Reason);
							FParse::Value(*BlacklistValue, TEXT("RHIs="), RHIs);
							FParse::Value(*BlacklistValue, TEXT("Warn="), Warn);

							if (Map.IsEmpty())
							{
								// Test with no Map property
								bSuccess = true;
							}
							else if (Map.StartsWith(TEXT("/")))
							{
								// Account for Functional Tests based on Map - historically blacklisting was made only for functional tests
								ListName = TEXT("Project.Functional Tests.") + Map + TEXT(".") + ListName;
								bSuccess = true;
							}

						}

						if (bSuccess)
						{
							if ((!Map.IsEmpty() && CommandLine.Contains(Map)) || CommandLine.Contains(Test))
							{
								UE_LOG(LogAutomationTest, Warning, TEXT("Test '%s' is blacklisted but allowing due to command line."), *BlacklistValue);
							}
							else
							{
								ListName.RemoveSpacesInline();
								FBlacklistEntry& Entry = TestBlacklist.Add(ListName);
								Entry.Map = Map;
								Entry.Test = Test;
								Entry.Reason = Reason;
								if (!RHIs.IsEmpty())
								{
									RHIs.ToLower().ParseIntoArray(Entry.RHIs, TEXT(","), true);
									for (FString& RHI : Entry.RHIs)
									{
										RHI.TrimStartAndEndInline();
									}
								}
								Entry.bWarn = Warn.ToBool();
							}
						}
						else
						{
							UE_LOG(LogAutomationTest, Error, TEXT("Invalid blacklisted test definition: '%s'"), *BlacklistValue);
						}
					}
				}
			}
		}
	}

	if (TestBlacklist.Num() > 0)
	{
		UE_LOG(LogAutomationTest, Log, TEXT("Automated Test Blacklist:"));
		for (auto& KV : TestBlacklist)
		{
			UE_LOG(LogAutomationTest, Log, TEXT("\tTest: %s"), *KV.Key);
		}
	}
}

bool FAutomationTestFramework::IsBlacklisted(const FString& TestName, FString* OutReason, bool *OutWarn) const
{
	const FString ListName = TestName.Replace(TEXT(" "), TEXT(""));
	const FBlacklistEntry* Entry = TestBlacklist.Find(ListName);

	if (Entry)
	{
		if (Entry->RHIs.Num() != 0 && !Entry->RHIs.Contains(FApp::GetGraphicsRHI().ToLower()))
		{
			return false;
		}

		if (OutReason != nullptr)
		{
			*OutReason = Entry->Reason;
		}

		if (OutWarn != nullptr)
		{
			*OutWarn = Entry->bWarn;
		}
	}

	return Entry != nullptr;
}


void FAutomationTestFramework::GetValidTestNames( TArray<FAutomationTestInfo>& TestInfo ) const
{
	TestInfo.Empty();

	// Determine required application type (Editor, Game, or Commandlet)
	const bool bRunningEditor = GIsEditor && !IsRunningCommandlet();
	const bool bRunningGame = !GIsEditor || IsRunningGame();
	const bool bRunningCommandlet = IsRunningCommandlet();

	//application flags
	uint32 ApplicationSupportFlags = 0;
	if ( bRunningEditor )
	{
		ApplicationSupportFlags |= EAutomationTestFlags::EditorContext;
	}
	if ( bRunningGame )
	{
		ApplicationSupportFlags |= EAutomationTestFlags::ClientContext;
	}
	if ( bRunningCommandlet )
	{
		ApplicationSupportFlags |= EAutomationTestFlags::CommandletContext;
	}

	//Feature support - assume valid RHI until told otherwise
	uint32 FeatureSupportFlags = EAutomationTestFlags::FeatureMask;
	// @todo: Handle this correctly. GIsUsingNullRHI is defined at Engine-level, so it can't be used directly here in Core.
	// For now, assume Null RHI is only used for commandlets, servers, and when the command line specifies to use it.
	if (FPlatformProperties::SupportsWindowedMode())
	{
		bool bUsingNullRHI = FParse::Param( FCommandLine::Get(), TEXT("nullrhi") ) || IsRunningCommandlet() || IsRunningDedicatedServer();
		if (bUsingNullRHI)
		{
			FeatureSupportFlags &= (~EAutomationTestFlags::NonNullRHI);
		}
	}
	if (FApp::IsUnattended())
	{
		FeatureSupportFlags &= (~EAutomationTestFlags::RequiresUser);
	}

	for ( TMap<FString, FAutomationTestBase*>::TConstIterator TestIter( AutomationTestClassNameToInstanceMap ); TestIter; ++TestIter )
	{
		const FAutomationTestBase* CurTest = TestIter.Value();
		check( CurTest );

		uint32 CurTestFlags = CurTest->GetTestFlags();

		//filter out full tests when running smoke tests
		const bool bPassesFilterRequirement = ((CurTestFlags & RequestedTestFilter) != 0);

		//Application Tests
		uint32 CurTestApplicationFlags = (CurTestFlags & EAutomationTestFlags::ApplicationContextMask);
		const bool bPassesApplicationRequirements = (CurTestApplicationFlags == 0) || (CurTestApplicationFlags & ApplicationSupportFlags);
		
		//Feature Tests
		uint32 CurTestFeatureFlags = (CurTestFlags & EAutomationTestFlags::FeatureMask);
		const bool bPassesFeatureRequirements = (CurTestFeatureFlags == 0) || (CurTestFeatureFlags & FeatureSupportFlags);

		const bool bEnabled = (CurTestFlags & EAutomationTestFlags::Disabled) == 0;

		const double GenerateTestNamesStartTime = FPlatformTime::Seconds();
		
		if (bEnabled && bPassesApplicationRequirements && bPassesFeatureRequirements && bPassesFilterRequirement)
		{
			TArray<FAutomationTestInfo> TestsToAdd;
			CurTest->GenerateTestNames(TestsToAdd);
			for (FAutomationTestInfo& Test : TestsToAdd)
			{
				FString BlacklistReason;
				bool bWarn(false);
				FString TestName = Test.GetDisplayName();
				if (!IsBlacklisted(TestName.Replace(TEXT(" "), TEXT("")), &BlacklistReason, &bWarn))
				{
					TestInfo.Add(MoveTemp(Test));
				}
				else
				{
					if (bWarn)
					{
						UE_LOG(LogAutomationTest, Warning, TEXT("Test '%s' is blacklisted. %s"), *TestName, *BlacklistReason);
					}
					else
					{
						UE_LOG(LogAutomationTest, Display, TEXT("Test '%s' is blacklisted. %s"), *TestName, *BlacklistReason);
					}
				}
			}
			
		}

		// Make sure people are not writing complex tests that take forever to return the names of the tests
		// otherwise the session frontend locks up when looking at your local tests.
		const double GenerateTestNamesEndTime = FPlatformTime::Seconds();
		const double TimeForGetTests = static_cast<float>(GenerateTestNamesEndTime - GenerateTestNamesStartTime);
		if (TimeForGetTests > 10.0f)
		{
			//force a failure if a smoke test takes too long
			UE_LOG(LogAutomationTest, Warning, TEXT("Automation Test '%s' took > 10 seconds to return from GetTests(...): %.2fs"), *CurTest->GetTestName(), (float)TimeForGetTests);
		}
	}
}

bool FAutomationTestFramework::ShouldTestContent(const FString& Path) const
{
	static TArray<FString> TestLevelFolders;
	if ( TestLevelFolders.Num() == 0 )
	{
		GConfig->GetArray( TEXT("/Script/Engine.AutomationTestSettings"), TEXT("TestLevelFolders"), TestLevelFolders, GEngineIni);
	}

	bool bMatchingDirectory = false;
	for ( const FString& Folder : TestLevelFolders )
	{
		const FString PatternToCheck = FString::Printf(TEXT("/%s/"), *Folder);
		if ( Path.Contains(*PatternToCheck) )
		{
			bMatchingDirectory = true;
		}
	}
	if (bMatchingDirectory)
	{
		return true;
	}

	const FString RelativePath = FPaths::ConvertRelativePathToFull(Path);
	const FString DevelopersPath = FPaths::ConvertRelativePathToFull(FPaths::GameDevelopersDir());
	return bDeveloperDirectoryIncluded || !RelativePath.StartsWith(DevelopersPath);
}

void FAutomationTestFramework::SetDeveloperDirectoryIncluded(const bool bInDeveloperDirectoryIncluded)
{
	bDeveloperDirectoryIncluded = bInDeveloperDirectoryIncluded;
}

void FAutomationTestFramework::SetRequestedTestFilter(const uint32 InRequestedTestFlags)
{
	RequestedTestFilter = InRequestedTestFlags;
}

FOnTestScreenshotCaptured& FAutomationTestFramework::OnScreenshotCaptured()
{
	return TestScreenshotCapturedDelegate;
}

FOnTestScreenshotAndTraceCaptured& FAutomationTestFramework::OnScreenshotAndTraceCaptured()
{
	return TestScreenshotAndTraceCapturedDelegate;
}

void FAutomationTestFramework::PrepForAutomationTests()
{
	check(!GIsAutomationTesting);

	// Fire off callback signifying that unit testing is about to begin. This allows
	// other systems to prepare themselves as necessary without the unit testing framework having to know
	// about them.
	PreTestingEvent.Broadcast();

	OriginalGWarn = GWarn;
	AutomationTestMessageFilter.SetDestinationContext(GWarn);
	GWarn = &AutomationTestMessageFilter;
	GLog->AddOutputDevice(&AutomationTestOutputDevice);

	// Mark that unit testing has begun
	GIsAutomationTesting = true;
}

void FAutomationTestFramework::ConcludeAutomationTests()
{
	check(GIsAutomationTesting);
	
	// Mark that unit testing is over
	GIsAutomationTesting = false;

	GLog->RemoveOutputDevice(&AutomationTestOutputDevice);
	GWarn = OriginalGWarn;
	AutomationTestMessageFilter.SetDestinationContext(nullptr);
	OriginalGWarn = nullptr;

	// Fire off callback signifying that unit testing has concluded.
	PostTestingEvent.Broadcast();
}

/**
 * Helper method to dump the contents of the provided test name to execution info map to the provided feedback context
 *
 * @param	InContext		Context to dump the execution info to
 * @param	InInfoToDump	Execution info that should be dumped to the provided feedback context
 */
void FAutomationTestFramework::DumpAutomationTestExecutionInfo( const TMap<FString, FAutomationTestExecutionInfo>& InInfoToDump )
{
	const FString SuccessMessage = NSLOCTEXT("UnrealEd", "AutomationTest_Success", "Success").ToString();
	const FString FailMessage = NSLOCTEXT("UnrealEd", "AutomationTest_Fail", "Fail").ToString();
	for ( TMap<FString, FAutomationTestExecutionInfo>::TConstIterator MapIter(InInfoToDump); MapIter; ++MapIter )
	{
		const FString& CurTestName = MapIter.Key();
		const FAutomationTestExecutionInfo& CurExecutionInfo = MapIter.Value();

		UE_LOG(LogAutomationTest, Log, TEXT("%s: %s"), *CurTestName, CurExecutionInfo.bSuccessful ? *SuccessMessage : *FailMessage);

		for ( const FAutomationExecutionEntry& Entry : CurExecutionInfo.GetEntries() )
		{
			switch (Entry.Event.Type )
			{
				case EAutomationEventType::Info:
					UE_LOG(LogAutomationTest, Display, TEXT("%s"), *Entry.Event.Message);
					break;
				case EAutomationEventType::Warning:
					UE_LOG(LogAutomationTest, Warning, TEXT("%s"), *Entry.Event.Message);
					break;
				case EAutomationEventType::Error:
					UE_LOG(LogAutomationTest, Error, TEXT("%s"), *Entry.Event.Message);
					break;
			}
		}
	}
}

void FAutomationTestFramework::InternalStartTest( const FString& InTestToRun )
{
	Parameters.Empty();

	FString TestName;
	if (!InTestToRun.Split(TEXT(" "), &TestName, &Parameters, ESearchCase::CaseSensitive))
	{
		TestName = InTestToRun;
	}

	if ( ContainsTest( TestName ) )
	{
		CurrentTest = *( AutomationTestClassNameToInstanceMap.Find( TestName ) );
		check( CurrentTest );

		// Clear any execution info from the test in case it has been run before
		CurrentTest->ClearExecutionInfo();

		// Associate the test that is about to be run with the special unit test output device and feedback context
		AutomationTestOutputDevice.SetCurrentAutomationTest(CurrentTest);
		AutomationTestMessageFilter.SetCurrentAutomationTest(CurrentTest);

		StartTime = FPlatformTime::Seconds();

		//if non-
		uint32 NonSmokeTestFlags = (EAutomationTestFlags::FilterMask & (~EAutomationTestFlags::SmokeFilter));
		if (RequestedTestFilter & NonSmokeTestFlags)
		{
			UE_LOG(LogAutomationTest, Log, TEXT("%s %s is starting at %f"), *CurrentTest->GetBeautifiedTestName(), *Parameters, StartTime);
		}

		CurrentTest->SetTestContext(Parameters);

		// Run the test!
		bTestSuccessful = CurrentTest->RunTest(Parameters);
	}
}

bool FAutomationTestFramework::InternalStopTest(FAutomationTestExecutionInfo& OutExecutionInfo)
{
	check(GIsAutomationTesting);
	check(LatentCommands.IsEmpty());

	double EndTime = FPlatformTime::Seconds();
	double TimeForTest = static_cast<float>(EndTime - StartTime);
	uint32 NonSmokeTestFlags = (EAutomationTestFlags::FilterMask & (~EAutomationTestFlags::SmokeFilter));
	if (RequestedTestFilter & NonSmokeTestFlags)
	{
		UE_LOG(LogAutomationTest, Log, TEXT("%s %s ran in %f"), *CurrentTest->GetBeautifiedTestName(), *Parameters, TimeForTest);
	}

	// Determine if the test was successful based on three criteria:
	// 1) Did the test itself report success?
	// 2) Did any errors occur and were logged by the feedback context during execution?++----
	// 3) Did we meet any errors that were expected with this test
	bTestSuccessful = bTestSuccessful && !CurrentTest->HasAnyErrors() && CurrentTest->HasMetExpectedErrors();

	CurrentTest->ExpectedErrors.Empty();

	// Set the success state of the test based on the above criteria
	CurrentTest->SetSuccessState( bTestSuccessful );

	// Fill out the provided execution info with the info from the test
	CurrentTest->GetExecutionInfo( OutExecutionInfo );

	// Save off timing for the test
	OutExecutionInfo.Duration = TimeForTest;

	// Disassociate the test from the output device and feedback context
	AutomationTestOutputDevice.SetCurrentAutomationTest(nullptr);
	AutomationTestMessageFilter.SetCurrentAutomationTest(nullptr);

	// Release pointers to now-invalid data
	CurrentTest = NULL;

	return bTestSuccessful;
}

void FAutomationTestFramework::AddAnalyticsItemToCurrentTest( const FString& AnalyticsItem )
{
	if( CurrentTest != nullptr )
	{
		CurrentTest->AddAnalyticsItem( AnalyticsItem );
	}
	else
	{
		UE_LOG( LogAutomationTest, Warning, TEXT( "AddAnalyticsItemToCurrentTest() called when no automation test was actively running!" ) );
	}
}

void FAutomationTestFramework::NotifyScreenshotComparisonComplete(const FAutomationScreenshotCompareResults& CompareResults)
{
	OnScreenshotCompared.Broadcast(CompareResults);
}

void FAutomationTestFramework::NotifyTestDataRetrieved(bool bWasNew, const FString& JsonData)
{
	OnTestDataRetrieved.Broadcast(bWasNew, JsonData);
}

void FAutomationTestFramework::NotifyPerformanceDataRetrieved(bool bSuccess, const FString& ErrorMessage)
{
	OnPerformanceDataRetrieved.Broadcast(bSuccess, ErrorMessage);
}

void FAutomationTestFramework::NotifyScreenshotTakenAndCompared()
{
	OnScreenshotTakenAndCompared.Broadcast();
}

FAutomationTestFramework::FAutomationTestFramework()
	: RequestedTestFilter(EAutomationTestFlags::SmokeFilter)
	, StartTime(0.0f)
	, bTestSuccessful(false)
	, CurrentTest(nullptr)
	, bDeveloperDirectoryIncluded(false)
	, NetworkRoleIndex(0)
	, bForceSmokeTests(false)
	, bCaptureStack(true)
{
}

FAutomationTestFramework::~FAutomationTestFramework()
{
	AutomationTestClassNameToInstanceMap.Empty();
}

FString FAutomationExecutionEntry::ToString() const
{
	FString ComplexString;

	ComplexString = Event.Message;
	
	if ( !Filename.IsEmpty() && LineNumber > 0 )
	{
		ComplexString += TEXT(" [");
		ComplexString += Filename;
		ComplexString += TEXT("(");
		ComplexString += FString::FromInt(LineNumber);
		ComplexString += TEXT(")]");
	}

	if ( !Event.Context.IsEmpty() )
	{
		ComplexString += TEXT(" [");
		ComplexString += Event.Context;
		ComplexString += TEXT("] ");
	}

	return ComplexString;
}

//------------------------------------------------------------------------------

void FAutomationTestExecutionInfo::Clear()
{
	ContextStack.Reset();

	Entries.Empty();
	AnalyticsItems.Empty();

	Errors = 0;
	Warnings = 0;
}

int32 FAutomationTestExecutionInfo::RemoveAllEvents(EAutomationEventType EventType)
{
	return RemoveAllEvents([EventType] (const FAutomationEvent& Event) {
		return Event.Type == EventType;
	});
}

int32 FAutomationTestExecutionInfo::RemoveAllEvents(TFunctionRef<bool(FAutomationEvent&)> FilterPredicate)
{
	int32 TotalRemoved = Entries.RemoveAll([this, &FilterPredicate](FAutomationExecutionEntry& Entry) {
		if (FilterPredicate(Entry.Event))
		{
			switch (Entry.Event.Type)
			{
			case EAutomationEventType::Warning:
				Warnings--;
				break;
			case EAutomationEventType::Error:
				Errors--;
				break;
			}

			return true;
		}
		return false;
	});

	return TotalRemoved;
}

void FAutomationTestExecutionInfo::AddEvent(const FAutomationEvent& Event, int StackOffset)
{
	switch (Event.Type)
	{
	case EAutomationEventType::Warning:
		Warnings++;
		break;
	case EAutomationEventType::Error:
		Errors++;
		break;
	}

	int32 EntryIndex = 0;
	if (FAutomationTestFramework::Get().GetCaptureStack())
	{
		SAFE_GETSTACK(Stack, StackOffset + 1, 1);
		EntryIndex = Entries.Add(FAutomationExecutionEntry(Event, Stack[0].Filename, Stack[0].LineNumber));
	}
	else
	{
		EntryIndex = Entries.Add(FAutomationExecutionEntry(Event));
	}

	FAutomationExecutionEntry& NewEntry = Entries[EntryIndex];

	if (NewEntry.Event.Context.IsEmpty())
	{
		NewEntry.Event.Context = GetContext();
	}
}

void FAutomationTestExecutionInfo::AddWarning(const FString& WarningMessage)
{
	AddEvent(FAutomationEvent(EAutomationEventType::Warning, WarningMessage));
}

void FAutomationTestExecutionInfo::AddError(const FString& ErrorMessage)
{
	AddEvent(FAutomationEvent(EAutomationEventType::Error, ErrorMessage));
}

//------------------------------------------------------------------------------

FAutomationEvent FAutomationScreenshotCompareResults::ToAutomationEvent(const FString& ScreenhotName) const
{
	FAutomationEvent Event(EAutomationEventType::Info, TEXT(""));

	if (bWasNew)
	{
		Event.Type = EAutomationEventType::Warning;
		Event.Message = FString::Printf(TEXT("New Screenshot '%s' was discovered!  Please add a ground truth version of it."), *ScreenhotName);
	}
	else
	{
		if (bWasSimilar)
		{
			Event.Type = EAutomationEventType::Info;
			Event.Message = FString::Printf(TEXT("Screenshot '%s' was similar!  Global Difference = %f, Max Local Difference = %f"),
				*ScreenhotName, GlobalDifference, MaxLocalDifference);
		}
		else
		{
			Event.Type = EAutomationEventType::Error;

			if (ErrorMessage.IsEmpty())
			{
				Event.Message = FString::Printf(TEXT("Screenshot '%s' test failed, Screenshots were different!  Global Difference = %f, Max Local Difference = %f"),
					*ScreenhotName, GlobalDifference, MaxLocalDifference);
			}
			else
			{
				Event.Message = FString::Printf(TEXT("Screenshot '%s' test failed; Error = %s"), *ScreenhotName, *ErrorMessage);
			}
		}
	}

	Event.Artifact = UniqueId;
	return Event;
}

//------------------------------------------------------------------------------

void FAutomationTestBase::ClearExecutionInfo()
{
	ExecutionInfo.Clear();
}

void FAutomationTestBase::AddError(const FString& InError, int32 StackOffset)
{
	if( !IsExpectedError(InError))
	{
		ExecutionInfo.AddEvent(FAutomationEvent(EAutomationEventType::Error, InError), StackOffset + 1);
	}
}

void FAutomationTestBase::AddErrorIfFalse(bool bCondition, const FString& InError, int32 StackOffset)
{
	if (!bCondition)
	{
		AddError(InError, StackOffset);
	}
}

void FAutomationTestBase::AddErrorS(const FString& InError, const FString& InFilename, int32 InLineNumber)
{
	if ( !IsExpectedError(InError))
	{
		//ExecutionInfo.AddEvent(FAutomationEvent(EAutomationEventType::Error, InError, ExecutionInfo.GetContext(), InFilename, InLineNumber));
	}
}

void FAutomationTestBase::AddWarningS(const FString& InWarning, const FString& InFilename, int32 InLineNumber)
{
	if ( !IsExpectedError(InWarning))
	{
		//ExecutionInfo.AddEvent(FAutomationEvent(EAutomationEventType::Warning, InWarning, ExecutionInfo.GetContext(), InFilename, InLineNumber));
	}
}

void FAutomationTestBase::AddWarning( const FString& InWarning, int32 StackOffset )
{
	if ( !IsExpectedError(InWarning))
	{
		ExecutionInfo.AddEvent(FAutomationEvent(EAutomationEventType::Warning, InWarning), StackOffset + 1);
	}
}

void FAutomationTestBase::AddInfo( const FString& InLogItem, int32 StackOffset )
{
	ExecutionInfo.AddEvent(FAutomationEvent(EAutomationEventType::Info, InLogItem), StackOffset + 1);
}

void FAutomationTestBase::AddAnalyticsItem(const FString& InAnalyticsItem)
{
	ExecutionInfo.AnalyticsItems.Add(InAnalyticsItem);
}

void FAutomationTestBase::AddEvent(const FAutomationEvent& InEvent, int32 StackOffset)
{
	ExecutionInfo.AddEvent(InEvent, StackOffset + 1);
}

bool FAutomationTestBase::HasAnyErrors() const
{
	return ExecutionInfo.GetErrorTotal() > 0;
}

bool FAutomationTestBase::HasMetExpectedErrors()
{
	bool HasMetAllExpectedErrors = true;

	for (auto& EError : ExpectedErrors)
	{
		if ((EError.ExpectedNumberOfOccurrences > 0) && (EError.ExpectedNumberOfOccurrences != EError.ActualNumberOfOccurrences))
		{
			HasMetAllExpectedErrors = false;

			ExecutionInfo.AddEvent(FAutomationEvent(EAutomationEventType::Error,
				FString::Printf(TEXT("Expected Error or Warning matching '%s' to occur %d times with %s match type, but it was found %d time(s).")
					, *EError.ErrorPatternString
					, EError.ExpectedNumberOfOccurrences
					, EAutomationExpectedErrorFlags::ToString(EError.CompareType)
					, EError.ActualNumberOfOccurrences)
				, ExecutionInfo.GetContext()));			
		}
		else if (EError.ExpectedNumberOfOccurrences == 0)
		{
			if (EError.ActualNumberOfOccurrences == 0)
			{
				HasMetAllExpectedErrors = false;

				ExecutionInfo.AddEvent(FAutomationEvent(EAutomationEventType::Error,
					FString::Printf(TEXT("Expected suppressed Error or Warning matching '%s' did not occur."), *EError.ErrorPatternString),
					ExecutionInfo.GetContext()));
			}
			else
			{
				ExecutionInfo.AddEvent(FAutomationEvent(EAutomationEventType::Info,
					FString::Printf(TEXT("Suppressed expected Error or Warning matching '%s' %d times.")
						, *EError.ErrorPatternString
						, EError.ActualNumberOfOccurrences)
					, ExecutionInfo.GetContext()));
			}
		}
	}

	return HasMetAllExpectedErrors;
}

void FAutomationTestBase::SetSuccessState( bool bSuccessful )
{
	ExecutionInfo.bSuccessful = bSuccessful;
}

void FAutomationTestBase::GetExecutionInfo( FAutomationTestExecutionInfo& OutInfo ) const
{
	OutInfo = ExecutionInfo;
}

void FAutomationTestBase::AddExpectedError(FString ExpectedErrorPattern, EAutomationExpectedErrorFlags::MatchType InCompareType, int32 Occurrences)
{
	if (Occurrences >= 0)
	{
		// If we already have an error matching string in our list, let's not add it again.
		FAutomationExpectedError* FoundEntry = ExpectedErrors.FindByPredicate(
			[ExpectedErrorPattern](const FAutomationExpectedError& InItem) 
				{
					return InItem.ErrorPatternString == ExpectedErrorPattern; 
				}
		);

		if (FoundEntry)
		{
			UE_LOG(LogAutomationTest, Warning, TEXT("Adding expected error matching '%s' failed: cannot add duplicate entries"), *ExpectedErrorPattern)
		}
		else
		{
			// ToDo: After UE-44340 is resolved, create FAutomationExpectedError and check that its ErrorPattern is valid before adding
			ExpectedErrors.Add(FAutomationExpectedError(ExpectedErrorPattern, InCompareType, Occurrences));
		}
	}
	else
	{
		UE_LOG(LogAutomationTest, Error, TEXT("Adding expected error matching '%s' failed: number of expected occurrences must be >= 0"), *ExpectedErrorPattern);
	}
}

void FAutomationTestBase::GetExpectedErrors(TArray<FAutomationExpectedError>& OutInfo) const
{
	OutInfo = ExpectedErrors;
}

void FAutomationTestBase::GenerateTestNames(TArray<FAutomationTestInfo>& TestInfo) const
{
	TArray<FString> BeautifiedNames;
	TArray<FString> ParameterNames;
	GetTests(BeautifiedNames, ParameterNames);

	FString BeautifiedTestName = GetBeautifiedTestName();

	for (int32 ParameterIndex = 0; ParameterIndex < ParameterNames.Num(); ++ParameterIndex)
	{
		FString CompleteBeautifiedNames = BeautifiedTestName;
		FString CompleteTestName = TestName;

		if (ParameterNames[ParameterIndex].Len())
		{
			CompleteBeautifiedNames = FString::Printf(TEXT("%s.%s"), *BeautifiedTestName, *BeautifiedNames[ParameterIndex]);;
			CompleteTestName = FString::Printf(TEXT("%s %s"), *TestName, *ParameterNames[ParameterIndex]);
		}

		// Add the test info to our collection
		FAutomationTestInfo NewTestInfo(
			CompleteBeautifiedNames,
			CompleteBeautifiedNames,
			CompleteTestName,
			GetTestFlags(),
			GetRequiredDeviceNum(),
			ParameterNames[ParameterIndex],
			GetTestSourceFileName(CompleteTestName),
			GetTestSourceFileLine(CompleteTestName),
			GetTestAssetPath(ParameterNames[ParameterIndex]),
			GetTestOpenCommand(ParameterNames[ParameterIndex])
		);
		
		TestInfo.Add( NewTestInfo );
	}
}

// --------------------------------------------------------------------------------------

bool FAutomationTestBase::TestEqual(const TCHAR* What, const int32 Actual, const int32 Expected)
{
	if (Actual != Expected)
	{
		AddError(FString::Printf(TEXT("Expected '%s' to be %d, but it was %d."), What, Expected, Actual), 1);
		return false;
	}
	return true;
}

bool FAutomationTestBase::TestEqual(const TCHAR* What, const int64 Actual, const int64 Expected)
{
	if (Actual != Expected)
	{
		AddError(FString::Printf(TEXT("Expected '%s' to be %" PRId64 ", but it was %" PRId64 "."), What, Expected, Actual), 1);
		return false;
	}
	return true;
}

#if PLATFORM_64BITS
bool FAutomationTestBase::TestEqual(const TCHAR* What, const SIZE_T Actual, const SIZE_T Expected)
{
	if (Actual != Expected)
	{
		AddError(FString::Printf(TEXT("Expected '%s' to be %" PRIuPTR ", but it was %" PRIuPTR "."), What, Expected, Actual), 1);
		return false;
	}
	return true;
}
#endif

bool FAutomationTestBase::TestEqual(const TCHAR* What, const float Actual, const float Expected, float Tolerance)
{
	if (!FMath::IsNearlyEqual(Actual, Expected, Tolerance))
	{
		AddError(FString::Printf(TEXT("Expected '%s' to be %f, but it was %f within tolerance %f."), What, Expected, Actual, Tolerance), 1);
		return false;
	}
	return true;
}

bool FAutomationTestBase::TestEqual(const TCHAR* What, const double Actual, const double Expected, double Tolerance)
{
	if (!FMath::IsNearlyEqual(Actual, Expected, Tolerance))
	{
		AddError(FString::Printf(TEXT("Expected '%s' to be %f, but it was %f within tolerance %f."), What, Expected, Actual, Tolerance), 1);
		return false;
	}
	return true;
}

bool FAutomationTestBase::TestEqual(const TCHAR* What, const FVector Actual, const FVector Expected, float Tolerance)
{
	if (!Expected.Equals(Actual, Tolerance))
	{
		AddError(FString::Printf(TEXT("Expected '%s' to be %s, but it was %s within tolerance %f."), What, *Expected.ToString(), *Actual.ToString(), Tolerance), 1);
		return false;
	}
	return true;
}

bool FAutomationTestBase::TestEqual(const TCHAR* What, const FRotator Actual, const FRotator Expected, float Tolerance)
{
	if (!Expected.Equals(Actual, Tolerance))
	{
		AddError(FString::Printf(TEXT("Expected '%s' to be %s, but it was %s within tolerance %f."), What, *Expected.ToString(), *Actual.ToString(), Tolerance), 1);
		return false;
	}
	return true;
}

bool FAutomationTestBase::TestEqual(const TCHAR* What, const FColor Actual, const FColor Expected)
{
	if (Expected != Actual)
	{
		AddError(FString::Printf(TEXT("Expected '%s' to be %s, but it was %s."), What, *Expected.ToString(), *Actual.ToString()), 1);
		return false;
	}
	return true;
}

bool FAutomationTestBase::TestEqual(const TCHAR* What, const TCHAR* Actual, const TCHAR* Expected)
{
	if (FCString::Strcmp(Actual, Expected) != 0)
	{
		AddError(FString::Printf(TEXT("Expected '%s' to be %s, but it was %s."), What, Expected, Actual), 1);
		return false;
	}
	return true;
}

bool FAutomationTestBase::TestEqualInsensitive(const TCHAR* What, const TCHAR* Actual, const TCHAR* Expected)
{
	if (FCString::Stricmp(Actual, Expected) != 0)
	{
		AddError(FString::Printf(TEXT("Expected '%s' to be %s, but it was %s."), What, Expected, Actual), 1);
		return false;
	}
	return true;
}

bool FAutomationTestBase::TestFalse(const TCHAR* What, bool Value)
{
	if (Value)
	{
		AddError(FString::Printf(TEXT("Expected '%s' to be false."), What), 1);
		return false;
	}
	return true;
}

bool FAutomationTestBase::TestTrue(const TCHAR* What, bool Value)
{
	if (!Value)
	{
		AddError(FString::Printf(TEXT("Expected '%s' to be true."), What), 1);
		return false;
	}
	return true;
}

bool FAutomationTestBase::TestNull(const TCHAR* What, const void* Pointer)
{
	if ( Pointer != nullptr )
	{
		AddError(FString::Printf(TEXT("Expected '%s' to be null."), What), 1);
		return false;
	}
	return true;
}

bool FAutomationTestBase::IsExpectedError(const FString& Error)
{
	for (auto& EError : ExpectedErrors)
	{
		FRegexMatcher ErrorMatcher(EError.ErrorPattern, Error);

		if (ErrorMatcher.FindNext())
		{
			EError.ActualNumberOfOccurrences++;
			return true;
		}
	}

	return false;
}
