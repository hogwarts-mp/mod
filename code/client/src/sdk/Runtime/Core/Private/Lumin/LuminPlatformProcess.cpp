// Copyright Epic Games, Inc. All Rights Reserved.
// Copyright 2016 Magic Leap, Inc. All Rights Reserved.

#include "Lumin/LuminPlatformProcess.h"
#include "Lumin/LuminPlatformMisc.h"
#include "Android/AndroidPlatformRunnableThread.h"
#include "Misc/EngineVersion.h"
#include "Misc/Paths.h"
#include <sys/syscall.h>
#include <pthread.h>
#include <libgen.h>
#include <dlfcn.h>
#include "Lumin/CAPIShims/LuminAPIDispatch.h"

constexpr static uint64 ArmCores = MAKEAFFINITYMASK2(3,4);
constexpr static uint64 DenverCores = MAKEAFFINITYMASK1(2);
int64 FLuminAffinity::GameThreadMask = ArmCores;
int64 FLuminAffinity::RenderingThreadMask = ArmCores;
int64 FLuminAffinity::RTHeartBeatMask = ArmCores;
int64 FLuminAffinity::RHIThreadMask = ArmCores;
int64 FLuminAffinity::PoolThreadMask = DenverCores;
int64 FLuminAffinity::TaskGraphThreadMask = ArmCores;
int64 FLuminAffinity::TaskGraphBGTaskMask = ArmCores;
int64 FLuminAffinity::StatsThreadMask = ArmCores;
int64 FLuminAffinity::AudioThreadMask = DenverCores;

const TCHAR* FLuminPlatformProcess::ComputerName()
{
	return TEXT("Lumin Device");
}

namespace LuminProcess
{
	static TCHAR * ExecutablePath()
	{
		// @todo Lumin - If later we're generating an so and running that, this probably won't work
		// will need to pull the exe name through some other mechanism
		static TCHAR * CachedResult = nullptr;
		if (CachedResult == nullptr)
		{
			// The common Linux way of using lstat to dynamically discover the length
			// of the symlink name doesn't work on Lumin. As it returns a zero size
			// for the link. [RR]
			char * SelfPath = (char*)FMemory::Malloc(ANDROID_MAX_PATH + 1);
			FMemory::Memzero(SelfPath, ANDROID_MAX_PATH + 1);
			if (readlink("/proc/self/exe", SelfPath, ANDROID_MAX_PATH) == -1)
			{
				int ErrNo = errno;
				UE_LOG(LogHAL, Fatal, TEXT("readlink() failed with errno = %d (%s)"), ErrNo,
					StringCast< TCHAR >(strerror(ErrNo)).Get());
				// unreachable
				return CachedResult;
			}
			CachedResult = (TCHAR*)FMemory::Malloc((FCStringAnsi::Strlen(SelfPath) + 1)*sizeof(TCHAR));
			FCString::Strcpy(CachedResult, ANDROID_MAX_PATH, ANSI_TO_TCHAR(SelfPath));
			FMemory::Free(SelfPath);
		}
		return CachedResult;
	}
}

const TCHAR* FLuminPlatformProcess::UserSettingsDir()
{
	// TODO: Use corekit to obtain the writable location for this.
	const static TCHAR * CachedResult = ApplicationSettingsDir();
	return CachedResult;
}

const TCHAR* FLuminPlatformProcess::ApplicationSettingsDir()
{
	static FString CachedResultString = FLuminPlatformMisc::GetApplicationWritableDirectoryPath();
	return *CachedResultString;
}

const TCHAR* FLuminPlatformProcess::UserTempDir()
{
	static FString CachedResultString = FLuminPlatformMisc::GetApplicationTempDirectoryPath();
	return *CachedResultString;
}

const TCHAR* FLuminPlatformProcess::ExecutableName(bool bRemoveExtension)
{
	static TCHAR * CachedResult = nullptr;
	if (CachedResult == nullptr)
	{
		TCHAR * SelfPath = LuminProcess::ExecutablePath();
		if (SelfPath)
		{
			CachedResult = (TCHAR*)FMemory::Malloc((FCString::Strlen(SelfPath) + 1)*sizeof(TCHAR));
			FCString::Strcpy(CachedResult, FCString::Strlen(SelfPath), *FPaths::GetBaseFilename(SelfPath, bRemoveExtension));
		}
	}
	return CachedResult;
}

void FLuminPlatformProcess::LaunchURL(const TCHAR* URL, const TCHAR* Parms, FString* Error)
{
	check(URL);
	const FString URLWithParams = FString::Printf(TEXT("%s %s"), URL, Parms ? Parms : TEXT("")).TrimEnd();

	MLDispatchPacket* Packet = nullptr;
	MLResult Result = MLDispatchAllocateEmptyPacket(&Packet);
	if (Packet != nullptr)
	{
		Result = MLDispatchSetUri(Packet, TCHAR_TO_UTF8(*URLWithParams));
		if (Result == MLResult_Ok)
		{
			Result = MLDispatchTryOpenApplication(Packet);
			if (Result != MLResult_Ok)
			{
				UE_LOG(LogCore, Error, TEXT("Failed to launch URL %s: MLDispatchTryOpenApplication failure: %d"), *URLWithParams, static_cast<int32>(Result));
			}
		}
		else
		{
			UE_LOG(LogCore, Error, TEXT("Failed to launch URL %s: MLDispatchSetUri failure: %s"), *URLWithParams, static_cast<int32>(Result));
		}
		Result = MLDispatchReleasePacket(&Packet, true, false);
		if (Result != MLResult_Ok)
		{
			UE_LOG(LogCore, Error, TEXT("MLDispatchReleasePacket failed: %s"), static_cast<int32>(Result));
		}
	}
	else
	{
		UE_LOG(LogCore, Error, TEXT("Failed to launch URL %s: MLDispatchAllocateEmptyPacket failure: %s"), *URLWithParams, static_cast<int32>(Result));
	}
}

void* FLuminPlatformProcess::GetDllHandle(const TCHAR* Filename)
{
	check(Filename);
	FString AbsolutePath = FPaths::ConvertRelativePathToFull(Filename);

	int DlOpenMode = RTLD_LAZY;
	DlOpenMode |= RTLD_LOCAL; // Local symbol resolution when loading shared objects - Needed for Hot-Reload

	void *Handle = dlopen(TCHAR_TO_UTF8(*AbsolutePath), DlOpenMode);
	if (!Handle)
	{
		UE_LOG(LogLumin, Warning, TEXT("dlopen failed: %s"), UTF8_TO_TCHAR(dlerror()));
	}
	return Handle;
}

void FLuminPlatformProcess::FreeDllHandle(void* DllHandle)
{
	check(DllHandle);
	dlclose(DllHandle);
}

void* FLuminPlatformProcess::GetDllExport(void* DllHandle, const TCHAR* ProcName)
{
	check(DllHandle);
	check(ProcName);
	return dlsym(DllHandle, TCHAR_TO_ANSI(ProcName));
}

int32 FLuminPlatformProcess::GetDllApiVersion(const TCHAR* Filename)
{
	check(Filename);
	return FEngineVersion::CompatibleWith().GetChangelist();
}

const TCHAR* FLuminPlatformProcess::GetModulePrefix()
{
	return TEXT("lib");
}

const TCHAR* FLuminPlatformProcess::GetModuleExtension()
{
	return TEXT("so");
}

const TCHAR* FLuminPlatformProcess::GetBinariesSubdirectory()
{
	return TEXT(""); //binaries are located in bin/ we no longer have a subdirectory to return.
}

// Can be specified per device profile
// lumin.DefaultThreadAffinity MainGame 3 4 Rendering 2...
TAutoConsoleVariable<FString> CVarLuminDefaultThreadAffinity(
	TEXT("lumin.DefaultThreadAffinity"), 
	TEXT(""), 
	TEXT("Sets the thread affinity for Lumin platform. Sets of args [MainGame|Rendering|RTHeartBeat|RHI|Pool|TaskGraph|TaskGraphBG|Audio] [int affinity] [optional int affinity2] [optional int affinity3], ex: lumin.DefaultThreadAffinity=MainGame 3 4 Rendering 2"));

static void LuminSetAffinityOnThread()
{
	if (IsInActualRenderingThread()) // If RenderingThread is not started yet, affinity will applied at RT creation time 
	{
		FPlatformProcess::SetThreadAffinityMask(FPlatformAffinity::GetRenderingThreadMask());
	}
	else if (IsInGameThread())
	{
		FPlatformProcess::SetThreadAffinityMask(FPlatformAffinity::GetMainGameMask());
	}
}

static void ApplyDefaultThreadAffinity(IConsoleVariable* Var)
{
	FString AffinityCmd = CVarLuminDefaultThreadAffinity.GetValueOnAnyThread();

	TArray<FString> Args;
	if (AffinityCmd.ParseIntoArrayWS(Args) > 0)
	{
		constexpr int32 FirstAvailableCPU = 2;
		constexpr int32 LastAvailableCPU = 4;
		for (int32 i = 0; i < Args.Num(); i++)
		{
			uint64 Affinity = 0;
			int64* Mask = nullptr;

			// Determine which Mask should be updated
			if (Args[i] == TEXT("MainGame"))
			{
				Mask = &FLuminAffinity::GameThreadMask;
			}
			else if (Args[i] == TEXT("Rendering"))
			{
				Mask = &FLuminAffinity::RenderingThreadMask;
			}
			else if (Args[i] == TEXT("RTHeartBeat"))
			{
				Mask = &FLuminAffinity::RTHeartBeatMask;
			}
			else if (Args[i] == TEXT("RHI"))
			{
				Mask = &FLuminAffinity::RHIThreadMask;
			}
			else if (Args[i] == TEXT("Pool"))
			{
				Mask = &FLuminAffinity::PoolThreadMask;
			}
			else if (Args[i] == TEXT("TaskGraph"))
			{
				Mask = &FLuminAffinity::TaskGraphThreadMask;
			}
			else if (Args[i] == TEXT("TaskGraphBG"))
			{
				Mask = &FLuminAffinity::TaskGraphBGTaskMask;
			}
			else if (Args[i] == TEXT("Audio"))
			{
				Mask = &FLuminAffinity::AudioThreadMask;
			}
			else
			{
				UE_LOG(LogLumin, Warning, TEXT("Skipping unknown argument [%s] to lumin.DefaultThreadAffinity"), *Args[i]); 
				continue;
			}
			check(Mask != nullptr);

			// Store the current mask for logging purposes
			FString CurrentMask(Args[i]);

			// Parse the affinities specified for the current mask
			for (int32 j = i + 1; j < Args.Num() && Args[j].IsNumeric(); j++)
			{
				int32 Arg = FCString::Atoi(*Args[j]);
				if (Arg >= FirstAvailableCPU && Arg <= LastAvailableCPU)
				{
					Affinity |= MAKEAFFINITYMASK1(Arg);
				}
				else
				{
					UE_LOG(LogLumin, Warning, TEXT("Skipping invalid CPU affinity [%d] for %s Thread.  Only CPUs %d through %d are available for application use."), Arg, *CurrentMask, FirstAvailableCPU, LastAvailableCPU); 
				}

				// increment i since an Argument has been consumed
				i++;
			}

			// Update the current mask with the new affinity, as long as a valid affinity was supplied
			if (Affinity != 0)
			{
				*Mask = Affinity;
			}
		}

		if (!FApp::ShouldUseThreadingForPerformance())
		{
			FLuminAffinity::TaskGraphThreadMask = FLuminAffinity::TaskGraphBGTaskMask = FLuminAffinity::GameThreadMask;
			UE_LOG(LogLumin, Log, TEXT("Using Game Thread affinity for Task Graph threads since ShouldUseThreadingForPerformance() is false")); 
		}

		if (FTaskGraphInterface::IsRunning())
		{
			FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(
				FSimpleDelegateGraphTask::FDelegate::CreateStatic(&LuminSetAffinityOnThread),
				TStatId(), NULL, ENamedThreads::GetRenderThread());

			FSimpleDelegateGraphTask::CreateAndDispatchWhenReady(
				FSimpleDelegateGraphTask::FDelegate::CreateStatic(&LuminSetAffinityOnThread),
				TStatId(), NULL, ENamedThreads::GameThread);
		}
		else
		{
			LuminSetAffinityOnThread();
		}
	}
}

void LuminSetupDefaultThreadAffinity()
{
	ApplyDefaultThreadAffinity(nullptr);

	// Watch for CVar update
	CVarLuminDefaultThreadAffinity->SetOnChangedCallback(FConsoleVariableDelegate::CreateStatic(&ApplyDefaultThreadAffinity));
}