// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GenericPlatformMisc.cpp: Generic implementations of misc platform functions
=============================================================================*/

#include "Unix/UnixPlatformMisc.h"
#include "Misc/AssertionMacros.h"
#include "GenericPlatform/GenericPlatformMemory.h"
#include "HAL/UnrealMemory.h"
#include "Templates/UnrealTemplate.h"
#include "Math/UnrealMathUtility.h"
#include "Containers/Array.h"
#include "Containers/UnrealString.h"
#include "Misc/DateTime.h"
#include "HAL/PlatformTime.h"
#include "Containers/StringConv.h"
#include "Logging/LogMacros.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"
#include "Misc/App.h"
#include "Unix/UnixPlatformCrashContext.h"
#include "Misc/ConfigCacheIni.h"
#include "GenericPlatform/GenericPlatformChunkInstall.h"

#if PLATFORM_HAS_CPUID
	#include <cpuid.h>
#endif // PLATFORM_HAS_CPUID
#include <sys/sysinfo.h>
#include <sched.h>
#include <sys/vfs.h>
#include <sys/ioctl.h>

#include <ifaddrs.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <linux/limits.h>

#include "Modules/ModuleManager.h"
#include "HAL/ThreadHeartBeat.h"

#include "FramePro/FrameProProfiler.h"
#include "BuildSettings.h"

extern bool GInitializedSDL;

static int SysGetRandomSupported = -1;

namespace PlatformMiscLimits
{
	enum
	{
		MaxOsGuidLength = 32
	};
};

namespace
{
	/**
	 * Empty handler so some signals are just not ignored
	 */
	void EmptyChildHandler(int32 Signal, siginfo_t* Info, void* Context)
	{
	}

	/**
	 * Installs SIGCHLD signal handler so we can wait for our children (otherwise they are reaped automatically)
	 */
	void InstallChildExitedSignalHanlder()
	{
		struct sigaction Action;
		FMemory::Memzero(Action);
		Action.sa_sigaction = EmptyChildHandler;
		sigfillset(&Action.sa_mask);
		Action.sa_flags = SA_SIGINFO | SA_RESTART | SA_ONSTACK;
		sigaction(SIGCHLD, &Action, nullptr);
	}
}

void FUnixPlatformMisc::NormalizePath(FString& InPath)
{
	// only expand if path starts with ~, e.g. ~/ should be expanded, /~ should not
	if (InPath.StartsWith(TEXT("~"), ESearchCase::CaseSensitive))	// case sensitive is quicker, and our substring doesn't care
	{
		InPath = InPath.Replace(TEXT("~"), FPlatformProcess::UserHomeDir(), ESearchCase::CaseSensitive);
	}
}

size_t CORE_API GCacheLineSize = PLATFORM_CACHE_LINE_SIZE;

void UnixPlatform_UpdateCacheLineSize()
{
	// sysfs "API", as usual ;/
	FILE * SysFsFile = fopen("/sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size", "r");
	if (SysFsFile)
	{
		int SystemLineSize = 0;
		if (1 == fscanf(SysFsFile, "%d", &SystemLineSize))
		{
			if (SystemLineSize > 0)
			{
				GCacheLineSize = SystemLineSize;
			}
		}
		fclose(SysFsFile);
	}
}

// Defined in UnixPlatformMemory
extern bool GUseKSM;
extern bool GKSMMergeAllPages;

static void UnixPlatForm_CheckIfKSMUsable()
{
	// https://www.kernel.org/doc/Documentation/vm/ksm.txt
	if (GUseKSM)
	{
		int KSMRunEnabled = 0;
		if (FILE* KSMRunFile = fopen("/sys/kernel/mm/ksm/run", "r"))
		{
			if (fscanf(KSMRunFile, "%d", &KSMRunEnabled) != 1)
			{
				KSMRunEnabled = 0;
			}

			fclose(KSMRunFile);
		}

		// The range for PagesToScan is 0 <--> max uint32_t
		uint32_t PagesToScan = 0;
		if (FILE* KSMPagesToScanFile = fopen("/sys/kernel/mm/ksm/pages_to_scan", "r"))
		{
			if (fscanf(KSMPagesToScanFile, "%u", &PagesToScan) != 1)
			{
				PagesToScan = 0;
			}

			fclose(KSMPagesToScanFile);
		}

		if (!KSMRunEnabled)
		{
			GUseKSM = 0;
			UE_LOG(LogInit, Error, TEXT("Cannot run ksm when its disabled in the kernel. Please check /sys/kernel/mm/ksm/run"));
		}
		else
		{
			if (PagesToScan <= 0)
			{
				GUseKSM = 0;
				UE_LOG(LogInit, Error, TEXT("KSM enabled but number of pages to be scanned is 0 which will implicitly disable KSM. Please check /sys/kernel/mm/ksm/pages_to_scan"));
			}
			else
			{
				UE_LOG(LogInit, Log, TEXT("KSM enabled. Number of pages to be scanned before ksmd goes to sleep: %u"), PagesToScan);
			}
		}
	}

	// Disable if GUseKSM is disabled from kernel settings
	GKSMMergeAllPages = GUseKSM && GKSMMergeAllPages;
}

// Init'ed in UnixPlatformMemory for now. Once the old crash symbolicator is gone remove this
extern bool CORE_API GUseNewCrashSymbolicator;

// Function used to read and store the entire *.sym file associated with the main module in memory.
// Helps greatly by reducing I/O during ensures/crashes. Only suggested when running a monolithic build
extern void CORE_API UnixPlatformStackWalk_PreloadModuleSymbolFile();

void FUnixPlatformMisc::PlatformInit()
{
	// install a platform-specific signal handler
	InstallChildExitedSignalHanlder();

	// do not remove the below check for IsFirstInstance() - it is not just for logging, it actually lays the claim to be first
	bool bFirstInstance = FPlatformProcess::IsFirstInstance();
	bool bIsNullRHI = !FApp::CanEverRender();

	bool bPreloadedModuleSymbolFile = FParse::Param(FCommandLine::Get(), TEXT("preloadmodulesymbols"));

	UnixPlatForm_CheckIfKSMUsable();

	UE_LOG(LogInit, Log, TEXT("Unix hardware info:"));
	UE_LOG(LogInit, Log, TEXT(" - we are %sthe first instance of this executable"), bFirstInstance ? TEXT("") : TEXT("not "));
	UE_LOG(LogInit, Log, TEXT(" - this process' id (pid) is %d, parent process' id (ppid) is %d"), static_cast< int32 >(getpid()), static_cast< int32 >(getppid()));
	UE_LOG(LogInit, Log, TEXT(" - we are %srunning under debugger"), IsDebuggerPresent() ? TEXT("") : TEXT("not "));
	UE_LOG(LogInit, Log, TEXT(" - machine network name is '%s'"), FPlatformProcess::ComputerName());
	UE_LOG(LogInit, Log, TEXT(" - user name is '%s' (%s)"), FPlatformProcess::UserName(), FPlatformProcess::UserName(false));
	UE_LOG(LogInit, Log, TEXT(" - we're logged in %s"), FPlatformMisc::HasBeenStartedRemotely() ? TEXT("remotely") : TEXT("locally"));
	UE_LOG(LogInit, Log, TEXT(" - we're running %s rendering"), bIsNullRHI ? TEXT("without") : TEXT("with"));
	UE_LOG(LogInit, Log, TEXT(" - CPU: %s '%s' (signature: 0x%X)"), *FPlatformMisc::GetCPUVendor(), *FPlatformMisc::GetCPUBrand(), FPlatformMisc::GetCPUInfo());
	UE_LOG(LogInit, Log, TEXT(" - Number of physical cores available for the process: %d"), FPlatformMisc::NumberOfCores());
	UE_LOG(LogInit, Log, TEXT(" - Number of logical cores available for the process: %d"), FPlatformMisc::NumberOfCoresIncludingHyperthreads());
	UnixPlatform_UpdateCacheLineSize();
	UE_LOG(LogInit, Log, TEXT(" - Cache line size: %zu"), GCacheLineSize);
	UE_LOG(LogInit, Log, TEXT(" - Memory allocator used: %s"), GMalloc->GetDescriptiveName());
	UE_LOG(LogInit, Log, TEXT(" - This binary is optimized with LTO: %s, PGO: %s, instrumented for PGO data collection: %s"),
		PLATFORM_COMPILER_OPTIMIZATION_LTCG ? TEXT("yes") : TEXT("no"),
		FPlatformMisc::IsPGOEnabled() ? TEXT("yes") : TEXT("no"),
		PLATFORM_COMPILER_OPTIMIZATION_PG_PROFILING ? TEXT("yes") : TEXT("no")
		);
	UE_LOG(LogInit, Log, TEXT(" - This is %s build."), BuildSettings::IsLicenseeVersion() ? TEXT("a licensee") : TEXT("an internal"));

	FPlatformTime::PrintCalibrationLog();

	UE_LOG(LogInit, Log, TEXT("Unix-specific commandline switches:"));
	UE_LOG(LogInit, Log, TEXT(" -ansimalloc - use malloc()/free() from libc (useful for tools like valgrind and electric fence)"));
	UE_LOG(LogInit, Log, TEXT(" -jemalloc - use jemalloc for all memory allocation"));
	UE_LOG(LogInit, Log, TEXT(" -binnedmalloc - use binned malloc  for all memory allocation"));
	UE_LOG(LogInit, Log, TEXT(" -filemapcachesize=NUMBER - set the size for case-sensitive file mapping cache"));
	UE_LOG(LogInit, Log, TEXT(" -useksm - uses kernel same-page mapping (KSM) for mapped memory (%s)"), GUseKSM ? TEXT("ON") : TEXT("OFF"));
	UE_LOG(LogInit, Log, TEXT(" -ksmmergeall - marks all mmap'd memory pages suitable for KSM (%s)"), GKSMMergeAllPages ? TEXT("ON") : TEXT("OFF"));
	UE_LOG(LogInit, Log, TEXT(" -preloadmodulesymbols - Loads the main module symbols file into memory (%s)"), bPreloadedModuleSymbolFile ? TEXT("ON") : TEXT("OFF"));
	UE_LOG(LogInit, Log, TEXT(" -sigdfl=SIGNAL - Allows a specific signal to be set to its default handler rather then ignoring the signal"));

	// [RCL] FIXME: this should be printed in specific modules, if at all
	UE_LOG(LogInit, Log, TEXT(" -httpproxy=ADDRESS:PORT - redirects HTTP requests to a proxy (only supported if compiled with libcurl)"));
	UE_LOG(LogInit, Log, TEXT(" -reuseconn - allow libcurl to reuse HTTP connections (only matters if compiled with libcurl)"));
	UE_LOG(LogInit, Log, TEXT(" -virtmemkb=NUMBER - sets process virtual memory (address space) limit (overrides VirtualMemoryLimitInKB value from .ini)"));

	if (bPreloadedModuleSymbolFile)
	{
		UnixPlatformStackWalk_PreloadModuleSymbolFile();
	}

	if (FPlatformMisc::HasBeenStartedRemotely() || FPlatformMisc::IsDebuggerPresent())
	{
		// print output immediately
		setvbuf(stdout, NULL, _IONBF, 0);
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("norandomguids")))
	{
		// If "-norandomguids" specified, don't use SYS_getrandom syscall
		SysGetRandomSupported = 0;
	}

	// This symbol is used for debugging but with LTO enabled it gets stripped as nothing is using it
	// Lets use it here just to log if its valid under VeryVerbose settings
	extern uint8** GNameBlocksDebug;
	if (GNameBlocksDebug)
	{
		UE_LOG(LogInit, VeryVerbose, TEXT("GNameBlocksDebug Valid - %i"), !!GNameBlocksDebug);
	}
}

extern void CORE_API UnixPlatformStackWalk_UnloadPreloadedModuleSymbol();
volatile sig_atomic_t GDeferedExitLogging = 0;

void FUnixPlatformMisc::PlatformTearDown()
{
	// We requested to close from a signal so we couldnt print.
	if (GDeferedExitLogging)
	{
		uint8 OverriddenErrorLevel = 0;
		if (FPlatformMisc::HasOverriddenReturnCode(&OverriddenErrorLevel))
		{
			UE_LOG(LogCore, Log, TEXT("FUnixPlatformMisc::RequestExit(bForce=false, ReturnCode=%d)"), OverriddenErrorLevel);
		}
		else
		{
			UE_LOG(LogCore, Log, TEXT("FUnixPlatformMisc::RequestExit(false)"));
		}
	}

	UnixPlatformStackWalk_UnloadPreloadedModuleSymbol();
	FPlatformProcess::CeaseBeingFirstInstance();
}

int32 FUnixPlatformMisc::GetMaxPathLength()
{
	return PATH_MAX;
}

void FUnixPlatformMisc::GetEnvironmentVariable(const TCHAR* InVariableName, TCHAR* Result, int32 ResultLength)
{
	FString VariableName = InVariableName;
	VariableName.ReplaceInline(TEXT("-"), TEXT("_"));
	ANSICHAR *AnsiResult = secure_getenv(TCHAR_TO_ANSI(*VariableName));
	if (AnsiResult)
	{
		FCString::Strncpy(Result, UTF8_TO_TCHAR(AnsiResult), ResultLength);
	}
	else
	{
		*Result = 0;
	}
}

FString FUnixPlatformMisc::GetEnvironmentVariable(const TCHAR* InVariableName)
{
	FString VariableName = InVariableName;
	VariableName.ReplaceInline(TEXT("-"), TEXT("_"));
	ANSICHAR *AnsiResult = secure_getenv(TCHAR_TO_ANSI(*VariableName));
	if (AnsiResult)
	{
		return UTF8_TO_TCHAR(AnsiResult);
	}
	else
	{
		return FString();
	}
}

void FUnixPlatformMisc::SetEnvironmentVar(const TCHAR* InVariableName, const TCHAR* Value)
{
	FString VariableName = InVariableName;
	VariableName.ReplaceInline(TEXT("-"), TEXT("_"));
	if (Value == NULL || Value[0] == TEXT('\0'))
	{
		unsetenv(TCHAR_TO_ANSI(*VariableName));
	}
	else
	{
		setenv(TCHAR_TO_ANSI(*VariableName), TCHAR_TO_ANSI(Value), 1);
	}
}

void FUnixPlatformMisc::LowLevelOutputDebugString(const TCHAR *Message)
{
	static_assert(PLATFORM_USE_LS_SPEC_FOR_WIDECHAR, "Check printf format");
	fprintf(stderr, "%s", TCHAR_TO_UTF8(Message));	// there's no good way to implement that really
}

extern volatile sig_atomic_t GEnteredSignalHandler;
uint8 GOverriddenReturnCode = 0;
bool GHasOverriddenReturnCode = false;

void FUnixPlatformMisc::RequestExit(bool Force)
{
	if (GEnteredSignalHandler)
	{
		// Still log something but use a signal-safe function
		const ANSICHAR ExitMsg[] = "FUnixPlatformMisc::RequestExit\n";
		write(STDOUT_FILENO, ExitMsg, sizeof(ExitMsg));

		GDeferedExitLogging = 1;
	}
	else
	{
		UE_LOG(LogCore, Log,  TEXT("FUnixPlatformMisc::RequestExit(%i)"), Force );
	}

	if(Force)
	{
		// Force immediate exit. Cannot call abort() here, because abort() raises SIGABRT which we treat as crash
		// (to prevent other, particularly third party libs, from quitting without us noticing)
		// Propagate override return code, but normally don't exit with 0, so the parent knows it wasn't a normal exit.
		if (GHasOverriddenReturnCode)
		{
			_exit(GOverriddenReturnCode);
		}
		else
		{
			_exit(1);
		}
	}

	if (GEnteredSignalHandler)
	{
		// Lets set our selfs to request exit as the generic platform request exit could log
		// This is deprecated but one of the few excpetions to leave around for now as we dont want to UE_LOG as that may allocate memory
		// ShouldRequestExit *should* only be used this way in cases where non-reentrant code is required
#if UE_SET_REQUEST_EXIT_ON_TICK_ONLY
		extern bool GShouldRequestExit;
		GShouldRequestExit = 1;
#else
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		GIsRequestingExit = 1;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#endif // UE_SET_REQUEST_EXIT_ON_TICK_ONLY
	}
	else
	{
		// Tell the platform specific code we want to exit cleanly from the main loop.
		FGenericPlatformMisc::RequestExit(Force);
	}
}

void FUnixPlatformMisc::RequestExitWithStatus(bool Force, uint8 ReturnCode)
{
	if (GEnteredSignalHandler)
	{
		// Still log something but use a signal-safe function
		const ANSICHAR ExitMsg[] = "FUnixPlatformMisc::RequestExitWithStatus\n";
		write(STDOUT_FILENO, ExitMsg, sizeof(ExitMsg));

		GDeferedExitLogging = 1;
	}
	else
	{
		UE_LOG(LogCore, Log, TEXT("FUnixPlatformMisc::RequestExit(bForce=%s, ReturnCode=%d)"), Force ? TEXT("true") : TEXT("false"), ReturnCode);
	}

	GOverriddenReturnCode = ReturnCode;
	GHasOverriddenReturnCode = true;

	return FPlatformMisc::RequestExit(Force);
}

bool FUnixPlatformMisc::HasOverriddenReturnCode(uint8 * OverriddenReturnCodeToUsePtr)
{
	if (GHasOverriddenReturnCode && OverriddenReturnCodeToUsePtr != nullptr)
	{
		*OverriddenReturnCodeToUsePtr = GOverriddenReturnCode;
	}

	return GHasOverriddenReturnCode;
}

FString FUnixPlatformMisc::GetOSVersion()
{
	// TODO [RCL] 2015-07-15: check if /etc/os-release or /etc/redhat-release exist and parse it
	// See void FUnixPlatformSurvey::GetOSName(FHardwareSurveyResults& OutResults)
	return FString();
}

const TCHAR* FUnixPlatformMisc::GetSystemErrorMessage(TCHAR* OutBuffer, int32 BufferCount, int32 Error)
{
	check(OutBuffer && BufferCount);
	if (Error == 0)
	{
		Error = errno;
	}

	FString Message = FString::Printf(TEXT("errno=%d (%s)"), Error, UTF8_TO_TCHAR(strerror(Error)));
	FCString::Strncpy(OutBuffer, *Message, BufferCount);

	return OutBuffer;
}

CORE_API TFunction<EAppReturnType::Type(EAppMsgType::Type MsgType, const TCHAR* Text, const TCHAR* Caption)> MessageBoxExtCallback;

EAppReturnType::Type FUnixPlatformMisc::MessageBoxExt(EAppMsgType::Type MsgType, const TCHAR* Text, const TCHAR* Caption)
{
	if(MessageBoxExtCallback)
	{
		return MessageBoxExtCallback(MsgType, Text, Caption);
	}
	else
	{
		return FGenericPlatformMisc::MessageBoxExt(MsgType, Text, Caption);
	}
}

int32 FUnixPlatformMisc::NumberOfCores()
{
	// WARNING: this function ignores edge cases like affinity mask changes (and even more fringe cases like CPUs going offline)
	// in the name of performance (higher level code calls NumberOfCores() way too often...)
	static int32 NumberOfCores = 0;
	if (NumberOfCores == 0)
	{
		if (FParse::Param(FCommandLine::Get(), TEXT("usehyperthreading")))
		{
			NumberOfCores = NumberOfCoresIncludingHyperthreads();
		}
		else
		{
			cpu_set_t AvailableCpusMask;
			CPU_ZERO(&AvailableCpusMask);

			if (0 != sched_getaffinity(0, sizeof(AvailableCpusMask), &AvailableCpusMask))
			{
				NumberOfCores = 1;	// we are running on something, right?
			}
			else
			{
				char FileNameBuffer[1024];
				struct CpuInfo
				{
					int Core;
					int Package;
				}
				CpuInfos[CPU_SETSIZE];

				FMemory::Memzero(CpuInfos);
				int MaxCoreId = 0;
				int MaxPackageId = 0;
				int NumCpusAvailable = 0;

				for(int32 CpuIdx = 0; CpuIdx < CPU_SETSIZE; ++CpuIdx)
				{
					if (CPU_ISSET(CpuIdx, &AvailableCpusMask))
					{
						++NumCpusAvailable;

						sprintf(FileNameBuffer, "/sys/devices/system/cpu/cpu%d/topology/core_id", CpuIdx);

						if (FILE* CoreIdFile = fopen(FileNameBuffer, "r"))
						{
							if (1 != fscanf(CoreIdFile, "%d", &CpuInfos[CpuIdx].Core))
							{
								CpuInfos[CpuIdx].Core = 0;
							}
							fclose(CoreIdFile);
						}

						sprintf(FileNameBuffer, "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", CpuIdx);

						if (FILE* PackageIdFile = fopen(FileNameBuffer, "r"))
						{
							// physical_package_id can be -1 on embedded devices - treat all CPUs as separate in that case.
							if (1 != fscanf(PackageIdFile, "%d", &CpuInfos[CpuIdx].Package) || CpuInfos[CpuIdx].Package < 0)
							{
								CpuInfos[CpuIdx].Package = CpuInfos[CpuIdx].Core;
							}
							fclose(PackageIdFile);
						}

						MaxCoreId = FMath::Max(MaxCoreId, CpuInfos[CpuIdx].Core);
						MaxPackageId = FMath::Max(MaxPackageId, CpuInfos[CpuIdx].Package);
					}
				}

				int NumCores = MaxCoreId + 1;
				int NumPackages = MaxPackageId + 1;
				int NumPairs = NumPackages * NumCores;

				// AArch64 topology seems to be incompatible with the above assumptions, particularly, core_id can be all 0 while the cores themselves are obviously independent. 
				// Check if num CPUs available to us is more than 2 per core (i.e. more than reasonable when hyperthreading is involved), and if so, don't trust the topology.
				if (2 * NumCores < NumCpusAvailable)
				{
					NumberOfCores = NumCpusAvailable;	// consider all CPUs to be separate
				}
				else
				{
					unsigned char * Pairs = reinterpret_cast<unsigned char *>(FMemory_Alloca(NumPairs * sizeof(unsigned char)));
					FMemory::Memzero(Pairs, NumPairs * sizeof(unsigned char));

					for (int32 CpuIdx = 0; CpuIdx < CPU_SETSIZE; ++CpuIdx)
					{
						if (CPU_ISSET(CpuIdx, &AvailableCpusMask))
						{
							Pairs[CpuInfos[CpuIdx].Package * NumCores + CpuInfos[CpuIdx].Core] = 1;
						}
					}

					for (int32 Idx = 0; Idx < NumPairs; ++Idx)
					{
						NumberOfCores += Pairs[Idx];
					}
				}
			}
		}

		// never allow it to be less than 1, we are running on something
		NumberOfCores = FMath::Max(1, NumberOfCores);
	}

	return NumberOfCores;
}

int32 FUnixPlatformMisc::NumberOfCoresIncludingHyperthreads()
{
	// WARNING: this function ignores edge cases like affinity mask changes (and even more fringe cases like CPUs going offline)
	// in the name of performance (higher level code calls NumberOfCores() way too often...)
	static int32 NumCoreIds = 0;
	if (NumCoreIds == 0)
	{
		cpu_set_t AvailableCpusMask;
		CPU_ZERO(&AvailableCpusMask);

		if (0 != sched_getaffinity(0, sizeof(AvailableCpusMask), &AvailableCpusMask))
		{
			NumCoreIds = 1;	// we are running on something, right?
		}
		else
		{
			return CPU_COUNT(&AvailableCpusMask);
		}
	}

	return NumCoreIds;
}

const TCHAR* FUnixPlatformMisc::GetNullRHIShaderFormat()
{
	return TEXT("SF_VULKAN_SM5");
}

bool FUnixPlatformMisc::HasCPUIDInstruction()
{
#if PLATFORM_HAS_CPUID
	return __get_cpuid_max(0, 0) != 0;
#else
	return false;	// Unix ARM or something more exotic
#endif // PLATFORM_HAS_CPUID
}

FString FUnixPlatformMisc::GetCPUVendor()
{
	static TCHAR Result[13] = TEXT("NonX86Vendor");
	static bool bHaveResult = false;

	if (!bHaveResult)
	{
#if PLATFORM_HAS_CPUID
		union
		{
			char Buffer[12 + 1];
			struct
			{
				int dw0;
				int dw1;
				int dw2;
			} Dw;
		} VendorResult;

		int Dummy;
		__cpuid(0, Dummy, VendorResult.Dw.dw0, VendorResult.Dw.dw2, VendorResult.Dw.dw1);

		VendorResult.Buffer[12] = 0;

		FCString::Strncpy(Result, UTF8_TO_TCHAR(VendorResult.Buffer), UE_ARRAY_COUNT(Result));
#else
		// use /proc?
#endif // PLATFORM_HAS_CPUID

		bHaveResult = true;
	}

	return FString(Result);
}

uint32 FUnixPlatformMisc::GetCPUInfo()
{
	static uint32 Info = 0;
	static bool bHaveResult = false;

	if (!bHaveResult)
	{
#if PLATFORM_HAS_CPUID
		int Dummy[3];
		__cpuid(1, Info, Dummy[0], Dummy[1], Dummy[2]);
#endif // PLATFORM_HAS_CPUID

		bHaveResult = true;
	}

	return Info;
}

FString FUnixPlatformMisc::GetCPUBrand()
{
	static TCHAR Result[64] = TEXT("NonX86CPUBrand");
	static bool bHaveResult = false;

	if (!bHaveResult)
	{
#if PLATFORM_HAS_CPUID
		// @see for more information http://msdn.microsoft.com/en-us/library/vstudio/hskdteyh(v=vs.100).aspx
		ANSICHAR BrandString[0x40] = { 0 };
		int32 CPUInfo[4] = { -1 };
		const SIZE_T CPUInfoSize = sizeof(CPUInfo);

		__cpuid(0x80000000, CPUInfo[0], CPUInfo[1], CPUInfo[2], CPUInfo[3]);
		const uint32 MaxExtIDs = CPUInfo[0];

		if (MaxExtIDs >= 0x80000004)
		{
			const uint32 FirstBrandString = 0x80000002;
			const uint32 NumBrandStrings = 3;
			for (uint32 Index = 0; Index < NumBrandStrings; ++Index)
			{
				__cpuid(FirstBrandString + Index, CPUInfo[0], CPUInfo[1], CPUInfo[2], CPUInfo[3]);
				FPlatformMemory::Memcpy(BrandString + CPUInfoSize * Index, CPUInfo, CPUInfoSize);
			}
		}

		FCString::Strncpy(Result, UTF8_TO_TCHAR(BrandString), UE_ARRAY_COUNT(Result));
#else
		// use /proc?
#endif // PLATFORM_HAS_CPUID

		bHaveResult = true;
	}

	return FString(Result);
}

// __builtin_popcountll() will not be compiled to use popcnt instruction unless -mpopcnt or a sufficiently recent target CPU arch is passed (which UBT doesn't by default)
#if defined(__POPCNT__)
	#define UE4_LINUX_NEED_TO_CHECK_FOR_POPCNT_PRESENCE				(PLATFORM_ENABLE_POPCNT_INTRINSIC)
#else
	#define UE4_LINUX_NEED_TO_CHECK_FOR_POPCNT_PRESENCE				0
#endif // __POPCNT__

bool FUnixPlatformMisc::HasNonoptionalCPUFeatures()
{
	static bool bHasNonOptionalFeature = false;
	static bool bHaveResult = false;

	if (!bHaveResult)
	{
#if PLATFORM_HAS_CPUID
		int Info[4];
		__cpuid(1, Info[0], Info[1], Info[2], Info[3]);
	
	#if UE4_LINUX_NEED_TO_CHECK_FOR_POPCNT_PRESENCE
		bHasNonOptionalFeature = (Info[2] & (1 << 23)) != 0;
	#endif // UE4_LINUX_NEED_TO_CHECK_FOR_POPCNT
#endif // PLATFORM_HAS_CPUID

		bHaveResult = true;
	}

	return bHasNonOptionalFeature;
}

bool FUnixPlatformMisc::NeedsNonoptionalCPUFeaturesCheck()
{
#if UE4_LINUX_NEED_TO_CHECK_FOR_POPCNT_PRESENCE
	return true;
#else
	return false;
#endif
}


#if !UE_BUILD_SHIPPING
bool FUnixPlatformMisc::IsDebuggerPresent()
{
	extern CORE_API bool GIgnoreDebugger;
	if (GIgnoreDebugger)
	{
		return false;
	}

	// If a process is tracing this one then TracerPid in /proc/self/status will
	// be the id of the tracing process. Use SignalHandler safe functions 
	
	int StatusFile = open("/proc/self/status", O_RDONLY);
	if (StatusFile == -1) 
	{
		// Failed - unknown debugger status.
		return false;
	}

	char Buffer[256];	
	ssize_t Length = read(StatusFile, Buffer, sizeof(Buffer));
	
	bool bDebugging = false;
	const char* TracerString = "TracerPid:\t";
	const ssize_t LenTracerString = strlen(TracerString);
	int i = 0;

	while((Length - i) > LenTracerString)
	{
		// TracerPid is found
		if (strncmp(&Buffer[i], TracerString, LenTracerString) == 0)
		{
			// 0 if no process is tracing.
			bDebugging = Buffer[i + LenTracerString] != '0';
			break;
		}

		++i;
	}

	close(StatusFile);
	return bDebugging;
}
#endif // !UE_BUILD_SHIPPING

bool FUnixPlatformMisc::HasBeenStartedRemotely()
{
	static bool bHaveAnswer = false;
	static bool bResult = false;

	if (!bHaveAnswer)
	{
		const char * VarValue = secure_getenv("SSH_CONNECTION");
		bResult = (VarValue && strlen(VarValue) != 0);
		bHaveAnswer = true;
	}

	return bResult;
}

FString FUnixPlatformMisc::GetOperatingSystemId()
{
	static bool bHasCachedResult = false;
	static FString CachedResult;

	if (!bHasCachedResult)
	{
		int OsGuidFile = open("/etc/machine-id", O_RDONLY);
		if (OsGuidFile != -1)
		{
			char Buffer[PlatformMiscLimits::MaxOsGuidLength + 1] = {0};
			ssize_t ReadBytes = read(OsGuidFile, Buffer, sizeof(Buffer) - 1);

			if (ReadBytes > 0)
			{
				CachedResult = UTF8_TO_TCHAR(Buffer);
			}

			close(OsGuidFile);
		}

		// old POSIX gethostid() is not useful. It is impossible to have globally unique 32-bit GUIDs and most
		// systems don't try hard implementing it these days (glibc will return a permuted IP address, often 127.0.0.1)
		// Due to that, we just ignore that call and consider lack of systemd's /etc/machine-id a failure to obtain the host id.

		bHasCachedResult = true;	// even if we failed to read the real one
	}

	return CachedResult;
}

bool FUnixPlatformMisc::GetDiskTotalAndFreeSpace(const FString& InPath, uint64& TotalNumberOfBytes, uint64& NumberOfFreeBytes)
{
	struct statfs FSStat = { 0 };
	FTCHARToUTF8 Converter(*InPath);
	int Err = statfs((ANSICHAR*)Converter.Get(), &FSStat);
	if (Err == 0)
	{
		TotalNumberOfBytes = FSStat.f_blocks * FSStat.f_bsize;
		NumberOfFreeBytes = FSStat.f_bavail * FSStat.f_bsize;
	}
	else
	{
		int ErrNo = errno;
		UE_LOG(LogCore, Warning, TEXT("Unable to statfs('%s'): errno=%d (%s)"), *InPath, ErrNo, UTF8_TO_TCHAR(strerror(ErrNo)));
	}
	return (Err == 0);
}


TArray<uint8> FUnixPlatformMisc::GetMacAddress()
{
	struct ifaddrs *ifap, *ifaptr;
	TArray<uint8> Result;

	if (getifaddrs(&ifap) == 0)
	{
		for (ifaptr = ifap; ifaptr != nullptr; ifaptr = (ifaptr)->ifa_next)
		{
			struct ifreq ifr;

			strncpy(ifr.ifr_name, ifaptr->ifa_name, IFNAMSIZ-1);

			int Socket = socket(AF_UNIX, SOCK_DGRAM, 0);
			if (Socket == -1)
			{
				continue;
			}

			if (ioctl(Socket, SIOCGIFHWADDR, &ifr) == -1)
			{
				close(Socket);
				continue;
			}

			close(Socket);

			if (ifr.ifr_hwaddr.sa_family != ARPHRD_ETHER)
			{
				continue;
			}

			const uint8 *MAC = (uint8 *) ifr.ifr_hwaddr.sa_data;

			for (int32 i=0; i < 6; i++)
			{
				Result.Add(MAC[i]);
			}

			break;
		}

		freeifaddrs(ifap);
	}

	return Result;
}


static int64 LastBatteryCheck = 0;
static bool bIsOnBattery = false;

bool FUnixPlatformMisc::IsRunningOnBattery()
{
	char Scratch[8];
	FDateTime Time = FDateTime::Now();
	int64 Seconds = Time.ToUnixTimestamp();

	// don't poll the OS for battery state on every tick. Just do it once every 10 seconds.
	if (LastBatteryCheck != 0 && (Seconds - LastBatteryCheck) < 10)
	{
		return bIsOnBattery;
	}

	LastBatteryCheck = Seconds;
	bIsOnBattery = false;

	// [RCL] 2015-09-30 FIXME: find a more robust way?
	const int kHardCodedNumBatteries = 10;
	for (int IdxBattery = 0; IdxBattery < kHardCodedNumBatteries; ++IdxBattery)
	{
		char Filename[128];
		sprintf(Filename, "/sys/class/power_supply/ADP%d/online", IdxBattery);

		int State = open(Filename, O_RDONLY);
		if (State != -1)
		{
			// found ACAD device. check its state.
			ssize_t ReadBytes = read(State, Scratch, 1);
			close(State);

			if (ReadBytes > 0)
			{
				bIsOnBattery = (Scratch[0] == '0');
			}

			break;	// quit checking after we found at least one
		}
	}

	// lack of ADP most likely means that we're not on laptop at all

	return bIsOnBattery;
}

#if PLATFORM_UNIX && defined(_GNU_SOURCE)

#include <sys/syscall.h>

// http://man7.org/linux/man-pages/man2/getrandom.2.html
// getrandom() was introduced in version 3.17 of the Linux kernel
//   and glibc version 2.25.

// Check known platforms if SYS_getrandom isn't defined
#if !defined(SYS_getrandom)
	#if PLATFORM_CPU_X86_FAMILY && PLATFORM_64BITS
		#define SYS_getrandom 318
	#elif PLATFORM_CPU_X86_FAMILY && !PLATFORM_64BITS
		#define SYS_getrandom 355
	#elif PLATFORM_CPU_ARM_FAMILY && PLATFORM_64BITS
		#define SYS_getrandom 278
	#elif PLATFORM_CPU_ARM_FAMILY && !PLATFORM_64BITS
		#define SYS_getrandom 384
	#endif
#endif // !defined(SYS_getrandom)

#endif // PLATFORM_UNIX && _GNU_SOURCE

namespace
{
#if defined(SYS_getrandom)

#if !defined(GRND_NONBLOCK)
	#define GRND_NONBLOCK 0x0001
#endif
	
	int SysGetRandom(void *buf, size_t buflen)
	{
		if (SysGetRandomSupported < 0)
		{
			int Ret = syscall(SYS_getrandom, buf, buflen, GRND_NONBLOCK);
	
			// If -1 is returned with ENOSYS, kernel doesn't support getrandom
			SysGetRandomSupported = ((Ret == -1) && (errno == ENOSYS)) ? 0 : 1;
		}
	
		return SysGetRandomSupported ?
			syscall(SYS_getrandom, buf, buflen, GRND_NONBLOCK) : -1;
	}
	
#else

	int SysGetRandom(void *buf, size_t buflen)
	{
		return -1;
	}
	
#endif // !SYS_getrandom
}

// If we fail to create a Guid with urandom fallback to the generic platform.
// This maybe need to be tweaked for Servers and hard fail here
void FUnixPlatformMisc::CreateGuid(FGuid& Result)
{
	int BytesRead = SysGetRandom(&Result, sizeof(Result));

	if (BytesRead == sizeof(Result))
	{
		// https://tools.ietf.org/html/rfc4122#section-4.4
		// https://en.wikipedia.org/wiki/Universally_unique_identifier
		//
		// The 4 bits of digit M indicate the UUID version, and the 1–3
		//   most significant bits of digit N indicate the UUID variant.
		// xxxxxxxx-xxxx-Mxxx-Nxxx-xxxxxxxxxxxx
		Result[1] = (Result[1] & 0xffff0fff) | 0x00004000; // version 4
		Result[2] = (Result[2] & 0x3fffffff) | 0x80000000; // variant 1
	}
	else
	{
		// Fall back to generic CreateGuid
		FGenericPlatformMisc::CreateGuid(Result);
	}
}

#if STATS || ENABLE_STATNAMEDEVENTS
void FUnixPlatformMisc::BeginNamedEventFrame()
{
#if FRAMEPRO_ENABLED
	FFrameProProfiler::FrameStart();
#endif // FRAMEPRO_ENABLED
}

void FUnixPlatformMisc::BeginNamedEvent(const struct FColor& Color, const TCHAR* Text)
{
#if FRAMEPRO_ENABLED
	FFrameProProfiler::PushEvent(Text);
#endif
}

void FUnixPlatformMisc::BeginNamedEvent(const struct FColor& Color, const ANSICHAR* Text)
{
#if FRAMEPRO_ENABLED
	FFrameProProfiler::PushEvent(Text);
#endif
}

void FUnixPlatformMisc::EndNamedEvent()
{
#if FRAMEPRO_ENABLED
	FFrameProProfiler::PopEvent();
#endif
}

void FUnixPlatformMisc::CustomNamedStat(const TCHAR* Text, float Value, const TCHAR* Graph, const TCHAR* Unit)
{
	FRAMEPRO_DYNAMIC_CUSTOM_STAT(TCHAR_TO_WCHAR(Text), Value, TCHAR_TO_WCHAR(Graph), TCHAR_TO_WCHAR(Unit), FRAMEPRO_COLOUR(255,255,255));
}

void FUnixPlatformMisc::CustomNamedStat(const ANSICHAR* Text, float Value, const ANSICHAR* Graph, const ANSICHAR* Unit)
{
	FRAMEPRO_DYNAMIC_CUSTOM_STAT(Text, Value, Graph, Unit, FRAMEPRO_COLOUR(255,255,255));
}
#endif

CORE_API TFunction<void()> UngrabAllInputCallback;

void FUnixPlatformMisc::UngrabAllInput()
{
	if(UngrabAllInputCallback)
	{
		UngrabAllInputCallback();
	}
}

FString FUnixPlatformMisc::GetLoginId()
{
	return FString::Printf(TEXT("%s-%08x"), *GetOperatingSystemId(), static_cast<uint32>(geteuid()));
}

IPlatformChunkInstall* FUnixPlatformMisc::GetPlatformChunkInstall()
{
	static IPlatformChunkInstall* ChunkInstall = nullptr;
	static bool bIniChecked = false;
	if (!ChunkInstall || !bIniChecked)
	{
		IPlatformChunkInstallModule* PlatformChunkInstallModule = nullptr;
		if (!GEngineIni.IsEmpty())
		{
			FString InstallModule;
			GConfig->GetString(TEXT("StreamingInstall"), TEXT("DefaultProviderName"), InstallModule, GEngineIni);
			FModuleStatus Status;
			if (FModuleManager::Get().QueryModule(*InstallModule, Status))
			{
				PlatformChunkInstallModule = FModuleManager::LoadModulePtr<IPlatformChunkInstallModule>(*InstallModule);
				if (PlatformChunkInstallModule != nullptr)
				{
					// Attempt to grab the platform installer
					ChunkInstall = PlatformChunkInstallModule->GetPlatformChunkInstall();
				}
			}
			bIniChecked = true;
		}

		if (PlatformChunkInstallModule == nullptr)
		{
			// Placeholder instance
			ChunkInstall = FGenericPlatformMisc::GetPlatformChunkInstall();
		}
	}

	return ChunkInstall;
}

bool FUnixPlatformMisc::SetStoredValues(const FString& InStoreId, const FString& InSectionName, const TMap<FString, FString>& InKeyValues)
{
	check(!InStoreId.IsEmpty());
	check(!InSectionName.IsEmpty());

	const FString ConfigPath = FString(FPlatformProcess::ApplicationSettingsDir()) / InStoreId / FString(TEXT("KeyValueStore.ini"));

	FConfigFile ConfigFile;
	ConfigFile.Read(ConfigPath);

	for (auto const& InKeyValue : InKeyValues)
	{
		check(!InKeyValue.Key.IsEmpty());

		FConfigSection& Section = ConfigFile.FindOrAdd(InSectionName);

		FConfigValue& KeyValue = Section.FindOrAdd(*InKeyValue.Key);
		KeyValue = FConfigValue(InKeyValue.Value);
	}

	ConfigFile.Dirty = true;
	return ConfigFile.Write(ConfigPath);
}
