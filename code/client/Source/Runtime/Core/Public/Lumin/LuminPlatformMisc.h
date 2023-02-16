// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Android/AndroidPlatformMisc.h"
#include "Logging/LogVerbosity.h"

struct CORE_API FLuminPlatformMisc : public FAndroidMisc
{
	static void InitLifecycle();

	/** 
	 * Platform overrides 
	 */
	static void RequestExit(bool Force);
	static void PlatformPreInit();
	static void PlatformInit();
	static bool AllowRenderThread();
	static bool SupportsLocalCaching();
	static bool SupportsMessaging();
	static void GetValidTargetPlatforms(TArray<FString>& TargetPlatformNames);
	static bool GetOverrideResolution(int32 &ResX, int32& ResY);
	static const TCHAR* GetPlatformFeaturesModuleName();

	static bool GetUseVirtualJoysticks()
	{
		return false;
	}

#if !UE_BUILD_SHIPPING
	static bool IsDebuggerPresent();
#endif

	FORCEINLINE static void MemoryBarrier()
	{
		__sync_synchronize();
	}

	FORCEINLINE static const TCHAR* GetNullRHIShaderFormat()
	{
		return TEXT("GLSL_ES3_1_ANDROID");
	}

	static void LowLevelOutputDebugString(const TCHAR *Message);
	static void LocalPrint(const TCHAR* Str);

	static void LowLevelOutputDebugStringWithVerbosity(const TCHAR *Message, ELogVerbosity::Type Verbosity);
	static void VARARGS LowLevelOutputDebugStringfWithVerbosity(ELogVerbosity::Type Verbosity, const TCHAR *Format, ...);
	static void LocalPrintWithVerbosity(const TCHAR *Message, ELogVerbosity::Type Verbosity);

	/** Android overrides. */
	static bool ShouldUseVulkan();
	static bool ShouldUseDesktopVulkan();
	static const TCHAR* GetDefaultDeviceProfileName();

	/** Lumin specific. */
	static bool ShouldUseDesktopOpenGL();
	static const FString& GetApplicationWritableDirectoryPath();
	static const FString& GetApplicationPackageDirectoryPath();
	static const FString& GetApplicationTempDirectoryPath();
	static const FString& GetApplicationApplicationPackageName();
	static const FString& GetApplicationComponentName();

	static FString GetDefaultLocale();
	static FString GetDefaultLanguage();

private:
	static void InitApplicationPaths();

private:
	static FString WritableDirPath;
	static FString PackageDirPath;
	static FString TempDirPath;
	static FString PackageName;
	static FString ComponentName;

	static bool ApplicationPathsInitialized;
};

typedef FLuminPlatformMisc FPlatformMisc;
