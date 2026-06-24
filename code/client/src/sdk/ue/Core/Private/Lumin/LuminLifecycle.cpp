// Copyright Epic Games, Inc. All Rights Reserved.

#include "Lumin/LuminLifecycle.h"
#include "EngineDefines.h"
#include "Misc/CallbackDevice.h"
#include "Async/TaskGraphInterfaces.h"
#include "Misc/CommandLine.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Lumin/LuminPlatformFile.h"
#include "Lumin/CAPIShims/LuminAPIFileInfo.h"

DEFINE_LOG_CATEGORY(LogLifecycle);

bool FLuminLifecycle::bIsEngineLoopInitComplete = false;
bool FLuminLifecycle::bIsAppPaused = false;
MLResult FLuminLifecycle::LifecycleState = MLResult_UnspecifiedFailure;
MLLifecycleInitArgList* FLuminLifecycle::InitArgList = nullptr;
MLLifecycleCallbacksEx FLuminLifecycle::LifecycleCallbacks;
TArray<FString> FLuminLifecycle::InitStringArgs;
TArray<FLuminFileInfo> FLuminLifecycle::InitFileArgs;

void FLuminLifecycle::Initialize()
{
	if (IsLifecycleInitialized())
	{
		return;
	}

	MLLifecycleCallbacksExInit(&LifecycleCallbacks);
	LifecycleCallbacks.on_stop = Stop_Handler;
	LifecycleCallbacks.on_pause = Pause_Handler;
	LifecycleCallbacks.on_resume = Resume_Handler;
	LifecycleCallbacks.on_unload_resources = UnloadResources_Handler;
	LifecycleCallbacks.on_new_initarg = OnNewInitArgs_Handler;
	LifecycleCallbacks.on_device_active = OnDeviceActive_Handler;
	LifecycleCallbacks.on_device_reality = OnDeviceReality_Handler;
	LifecycleCallbacks.on_device_standby = OnDeviceStandby_Handler;
	LifecycleCallbacks.on_focus_lost = OnFocusLost_Handler;
	LifecycleCallbacks.on_focus_gained = OnFocusGained_Handler;

	LifecycleState = MLLifecycleInitEx(&LifecycleCallbacks, nullptr);

	FCoreDelegates::OnFEngineLoopInitComplete.AddStatic(FLuminLifecycle::OnFEngineLoopInitComplete_Handler);

	// TODO: confirm this comment for ml_lifecycle.
	// There's a known issue where ck_lifecycle_init will fail to initialize if the debugger is attached.
	// Ideally, this should assert since the app won't be able to react to events correctly.
	if (LifecycleState != MLResult_Ok)
	{
		FPlatformMisc::LowLevelOutputDebugString(TEXT("Lifecycle system failed to initialize! App may not suspend, resume, or teriminate correctly."));
	}
	else
	{
		// It is possible that FLuminLifecycle::Initialize() is called before LaunchLumin::InitCommandLine().
		// So initialize command line here to take in args passed via mldb launch.
		if (!FCommandLine::IsInitialized())
		{
			// initialize the command line to an empty string
			FCommandLine::Set(TEXT(""));
		}
		OnNewInitArgs_Handler(nullptr);
	}
}

bool FLuminLifecycle::IsLifecycleInitialized()
{
	return (LifecycleState == MLResult_Ok);
}

void FLuminLifecycle::Stop_Handler(void* ApplicationContext)
{
	UE_LOG(LogLifecycle, Log, TEXT("FLuminLifecycle : The application is being stopped by the system."));

	if (FTaskGraphInterface::IsRunning())
	{
		FGraphEventRef WillTerminateTask = FFunctionGraphTask::CreateAndDispatchWhenReady([&]()
		{
			FCoreDelegates::ApplicationWillTerminateDelegate.Broadcast();
		}, TStatId(), nullptr, ENamedThreads::GameThread);
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(WillTerminateTask);
	}

	if (InitArgList != nullptr)
	{
		MLResult Result = MLLifecycleFreeInitArgList(&InitArgList);
		UE_CLOG(MLResult_Ok != Result, LogLifecycle, Error, TEXT("Error %s freeing init args list."), UTF8_TO_TCHAR(MLGetResultString(Result)));
	}

	FPlatformMisc::RequestExit(false);
}

void FLuminLifecycle::Pause_Handler(void* ApplicationContext)
{
	UE_LOG(LogLifecycle, Log, TEXT("FLuminLifecycle : The application is being paused / suspended by the system."));

	// TODO: confirm this comment for ml_lifecycle.
	// Currently, the lifecycle service can invoke "pause" multiple times, so we need to guard against it.
	if (!bIsAppPaused)
	{
		if (FTaskGraphInterface::IsRunning())
		{
			FGraphEventRef DeactivateTask = FFunctionGraphTask::CreateAndDispatchWhenReady([&]()
			{
				FCoreDelegates::ApplicationWillDeactivateDelegate.Broadcast();
			}, TStatId(), nullptr, ENamedThreads::GameThread);
			FGraphEventRef EnterBackgroundTask = FFunctionGraphTask::CreateAndDispatchWhenReady([&]()
			{
				FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Broadcast();
			}, TStatId(), DeactivateTask, ENamedThreads::GameThread);
			FTaskGraphInterface::Get().WaitUntilTaskCompletes(EnterBackgroundTask);
		}

		bIsAppPaused = true;
	}
}

void FLuminLifecycle::Resume_Handler(void* ApplicationContext)
{
	UE_LOG(LogLifecycle, Log, TEXT("FLuminLifecycle : The application is being resumed after being suspended."));

	// TODO: confirm this comment for ml_lifecycle.
	// Currently, the lifecycle service can invoke "resume" multiple times, so we need to guard against it.
	if (bIsAppPaused)
	{
		if (FTaskGraphInterface::IsRunning())
		{
			FGraphEventRef EnterForegroundTask = FFunctionGraphTask::CreateAndDispatchWhenReady([&]()
			{
				FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Broadcast();
			}, TStatId(), nullptr, ENamedThreads::GameThread);

			FGraphEventRef ReactivateTask = FFunctionGraphTask::CreateAndDispatchWhenReady([&]()
			{
				FCoreDelegates::ApplicationHasReactivatedDelegate.Broadcast();
			}, TStatId(), EnterForegroundTask, ENamedThreads::GameThread);
			FTaskGraphInterface::Get().WaitUntilTaskCompletes(ReactivateTask);
		}

		bIsAppPaused = false;

		// if an app is resumed from a paused state, init_args callback is received before resume.
		// in that case, we simply cache the args and fire them after the engine actually resumes.
		// Otherwise, certain events like changing the map while the app is paused might cause a crash, or a deadlock.
		FirePendingInitArgs();
	}
}

void FLuminLifecycle::UnloadResources_Handler(void* ApplicationContext)
{
	UE_LOG(LogLifecycle, Log, TEXT("FLuminLifecycle : The application is being asked to free up cached resources by the system."));

	if (FTaskGraphInterface::IsRunning())
	{
		FGraphEventRef UnloadResourcesTask = FFunctionGraphTask::CreateAndDispatchWhenReady([&]()
		{
			FCoreDelegates::ApplicationShouldUnloadResourcesDelegate.Broadcast();
		}, TStatId(), nullptr, ENamedThreads::GameThread);
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(UnloadResourcesTask);
	}
}

void FLuminLifecycle::OnNewInitArgs_Handler(void* ApplicationContext)
{
	IPlatformFile& PlatformFile = IPlatformFile::GetPlatformPhysical();
	FLuminPlatformFile* LuminPlatformFile = static_cast<FLuminPlatformFile*>(&PlatformFile);

	InitArgList = nullptr;
	MLResult Result = MLLifecycleGetInitArgList(&InitArgList);
	if (Result == MLResult_Ok && InitArgList != nullptr)
	{
		int64_t InitArgCount = 0;
		Result = MLLifecycleGetInitArgListLength(InitArgList, &InitArgCount);
		if (Result == MLResult_Ok && InitArgCount > 0)
		{
			for (int64 i = 0; i < InitArgCount; ++i)
			{
				const MLLifecycleInitArg* InitArg = nullptr;
				Result = MLLifecycleGetInitArgByIndex(InitArgList, i, &InitArg);
				if (Result == MLResult_Ok && InitArg != nullptr)
				{
					const char* Arg = nullptr;
					Result = MLLifecycleGetInitArgUri(InitArg, &Arg);
					if (Result == MLResult_Ok && Arg != nullptr)
					{
						// Start with a space because the command line already in place may not have any trailing spaces.
						FString ArgStr = TEXT(" ");
						ArgStr.Append(UTF8_TO_TCHAR(Arg));
						ArgStr.TrimEndInline();
						FCommandLine::Append(*ArgStr);
	
						InitStringArgs.Add(UTF8_TO_TCHAR(Arg));
					}

					int64_t FileInfoListLength = 0;
					Result = MLLifecycleGetFileInfoListLength(InitArg, &FileInfoListLength);
					FPlatformMisc::LowLevelOutputDebugStringf(TEXT("FileInfoListLength = %lld"), FileInfoListLength);
					for (int64_t j = 0; j < FileInfoListLength; ++j)
					{
						const MLFileInfo* FileInfo = nullptr;
						Result = MLLifecycleGetFileInfoByIndex(InitArg, j, &FileInfo);
						if (Result == MLResult_Ok && FileInfo != nullptr)
						{
							FLuminFileInfo LuminFile;
							const char* Filename = nullptr;
							Result = MLFileInfoGetFileName(FileInfo, &Filename);
							if (Result == MLResult_Ok && Filename != nullptr)
							{
								LuminFile.FileName = UTF8_TO_TCHAR(Filename);
							}

							const char* MimeType = nullptr;
							Result = MLFileInfoGetMimeType(FileInfo, &MimeType);
							if (Result == MLResult_Ok && MimeType != nullptr)
							{
								LuminFile.MimeType = UTF8_TO_TCHAR(MimeType);
							}

							LuminFile.FileHandle = LuminPlatformFile->GetFileHandleForMLFileInfo(FileInfo);
							if (LuminFile.FileHandle != nullptr)
							{
								InitFileArgs.Add(LuminFile);
							}
						}
					}
				}
			}

			if (bIsEngineLoopInitComplete && !bIsAppPaused)
			{
				FirePendingInitArgs();
			}
		}
	}
}

void FLuminLifecycle::OnDeviceActive_Handler(void* ApplicationContext)
{
	UE_LOG(LogLifecycle, Log, TEXT("FLuminLifecycle : The device is active again."));
	if (FTaskGraphInterface::IsRunning())
	{
		FGraphEventRef WillTerminateTask = FFunctionGraphTask::CreateAndDispatchWhenReady([&]()
		{
#if PLATFORM_LUMIN
			FLuminDelegates::DeviceHasReactivatedDelegate.Broadcast();
#endif // PLATFORM_LUMIN
		}, TStatId(), nullptr, ENamedThreads::GameThread);
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(WillTerminateTask);
	}
}

void FLuminLifecycle::OnDeviceReality_Handler(void* ApplicationContext)
{
	UE_LOG(LogLifecycle, Log, TEXT("FLuminLifecycle : The device's reality button has been pressed."));
	if (FTaskGraphInterface::IsRunning())
	{
		FGraphEventRef WillTerminateTask = FFunctionGraphTask::CreateAndDispatchWhenReady([&]()
		{
#if PLATFORM_LUMIN
			FLuminDelegates::DeviceWillEnterRealityModeDelegate.Broadcast();
#endif // PLATFORM_LUMIN
		}, TStatId(), nullptr, ENamedThreads::GameThread);
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(WillTerminateTask);
	}
}

void FLuminLifecycle::OnDeviceStandby_Handler(void* ApplicationContext)
{
	UE_LOG(LogLifecycle, Log, TEXT("FLuminLifecycle : The device is going into standby."));
	if (FTaskGraphInterface::IsRunning())
	{
		FGraphEventRef WillTerminateTask = FFunctionGraphTask::CreateAndDispatchWhenReady([&]()
		{
#if PLATFORM_LUMIN
			FLuminDelegates::DeviceWillGoInStandbyDelegate.Broadcast();
#endif // PLATFORM_LUMIN
		}, TStatId(), nullptr, ENamedThreads::GameThread);
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(WillTerminateTask);
	}
}

void FLuminLifecycle::OnFocusLost_Handler(void* ApplicationContext, MLLifecycleFocusLostReason reason)
{
	UE_LOG(LogLifecycle, Log, TEXT("FLuminLifecycle : Input focus lost."));
	if (FTaskGraphInterface::IsRunning())
	{
		FGraphEventRef FocusLostTask = FFunctionGraphTask::CreateAndDispatchWhenReady([&]()
		{
#if PLATFORM_LUMIN
			FLuminDelegates::FocusLostDelegate.Broadcast(reason);
#endif // PLATFORM_LUMIN
		}, TStatId(), nullptr, ENamedThreads::GameThread);
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(FocusLostTask);
	}
}

void FLuminLifecycle::OnFocusGained_Handler(void* ApplicationContext)
{
	UE_LOG(LogLifecycle, Log, TEXT("FLuminLifecycle : Input focus gained."));
	if (FTaskGraphInterface::IsRunning())
	{
		FGraphEventRef FocusGainedTask = FFunctionGraphTask::CreateAndDispatchWhenReady([&]()
		{
#if PLATFORM_LUMIN
			FLuminDelegates::FocusGainedDelegate.Broadcast();
#endif // PLATFORM_LUMIN
		}, TStatId(), nullptr, ENamedThreads::GameThread);
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(FocusGainedTask);
	}
}

void FLuminLifecycle::OnFEngineLoopInitComplete_Handler()
{
	bIsEngineLoopInitComplete = true;
	FirePendingInitArgs();
}

void FLuminLifecycle::FirePendingInitArgs()
{
	if ((InitStringArgs.Num() > 0 || InitFileArgs.Num() > 0) && FTaskGraphInterface::IsRunning())
	{
		FPlatformMisc::LowLevelOutputDebugString(TEXT("FLuminLifecycle :: Firing startup args..."));
		FGraphEventRef StartupArgumentsTask = FFunctionGraphTask::CreateAndDispatchWhenReady([&]()
		{
			FCoreDelegates::ApplicationReceivedStartupArgumentsDelegate.Broadcast(InitStringArgs);
		}, TStatId(), nullptr, ENamedThreads::GameThread);
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(StartupArgumentsTask);

		StartupArgumentsTask = FFunctionGraphTask::CreateAndDispatchWhenReady([&]()
		{
			FLuminDelegates::LuminAppReceivedStartupArgumentsDelegate.Broadcast(InitStringArgs, InitFileArgs);
		}, TStatId(), nullptr, ENamedThreads::GameThread);
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(StartupArgumentsTask);

		InitStringArgs.Empty();
		InitFileArgs.Empty();
	}
}
