// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Lumin/CAPIShims/LuminAPILifecycle.h"
#include "Logging/LogMacros.h"
#include "Lumin/LuminPlatformDelegates.h"

class CORE_API FLuminLifecycle
{
public:
	static void Initialize();
	static bool IsLifecycleInitialized();
private:
	static void Stop_Handler(void* ApplicationContext);
	static void Pause_Handler(void* ApplicationContext);
	static void Resume_Handler(void* ApplicationContext);
	static void UnloadResources_Handler(void* ApplicationContext);
	// To use lifecycle init args, launch the app using -
	// mldb launch -i "-arg1=value1 -arg2=value2" <package_name>
	static void OnNewInitArgs_Handler(void* ApplicationContext);

	static void OnDeviceActive_Handler(void* ApplicationContext);
	static void OnDeviceReality_Handler(void* ApplicationContext);
	static void OnDeviceStandby_Handler(void* ApplicationContext);

	static void OnFocusLost_Handler(void* ApplicationContext, MLLifecycleFocusLostReason reason);
	static void OnFocusGained_Handler(void* ApplicationContext);

	static void OnFEngineLoopInitComplete_Handler();

private:
	static void FirePendingInitArgs();

	static bool bIsEngineLoopInitComplete;
	static bool bIsAppPaused;
	static MLResult LifecycleState;
	static MLLifecycleCallbacksEx LifecycleCallbacks;
	static MLLifecycleInitArgList* InitArgList;
	static TArray<FString> InitStringArgs;
	static TArray<FLuminFileInfo> InitFileArgs;
};

DECLARE_LOG_CATEGORY_EXTERN(LogLifecycle, Log, All);
