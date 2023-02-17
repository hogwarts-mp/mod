// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AndroidPlatformStackWalk.cpp: Android implementations of stack walk functions
=============================================================================*/

#include "Android/AndroidPlatformStackWalk.h"
#include "HAL/PlatformMemory.h"
#include "Misc/CString.h"
#include <unwind.h>
#include <dlfcn.h>
#include <cxxabi.h>
#include <stdio.h>
#include <android/log.h>
#include "Android/AndroidSignals.h"

#include <android/log.h>

#include "Misc/OutputDevice.h"
#include "Logging/LogMacros.h"

#define HAS_LIBUNWIND PLATFORM_ANDROID_ARM64 && !PLATFORM_LUMIN

#if HAS_LIBUNWIND
#define UNW_LOCAL_ONLY
#include "libunwind.h"
#endif
#if ANDROID_HAS_RTSIGNALS
#include <syscall.h>
#endif
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ScopeExit.h"

void FAndroidPlatformStackWalk::NotifyPlatformVersionInit()
{
#if HAS_LIBUNWIND
	// Without this stack walk might touch executable memory and ASan will terminate the app on that.
	// Xom protection presents the same issue and is enabled on android 10 devices when the targetsdk is 29 or higher.
	// see https://source.android.com/devices/tech/debug/execute-only-memory
	if (RUNNING_WITH_ASAN || (FAndroidMisc::GetTargetSDKVersion() >= 29 && FAndroidMisc::GetAndroidMajorVersion() == 10))
	{
		// prevent libunwind attempting to deref IP during signal frame test. (this will make backtrace called from a signal less useful.)
		unw_disable_signal_frame_test(1);
	}
#endif
}

void FAndroidPlatformStackWalk::ProgramCounterToSymbolInfo(uint64 ProgramCounter, FProgramCounterSymbolInfo& out_SymbolInfo)
{
	Dl_info DylibInfo;
	int32 Result = dladdr((const void*)ProgramCounter, &DylibInfo);
	if (Result == 0)
	{
		return;
	}

	out_SymbolInfo.ProgramCounter = ProgramCounter;

	int32 Status = 0;
	ANSICHAR* DemangledName = NULL;

	// Increased the size of the demangle destination to reduce the chances that abi::__cxa_demangle will allocate
	// this causes the app to hang as malloc isn't signal handler safe. Ideally we wouldn't call this function in a handler.
	size_t DemangledNameLen = 8192;
	ANSICHAR DemangledNameBuffer[8192] = { 0 };
	DemangledName = abi::__cxa_demangle(DylibInfo.dli_sname, DemangledNameBuffer, &DemangledNameLen, &Status);

	if (DemangledName)
	{
		// C++ function
		FCStringAnsi::Sprintf(out_SymbolInfo.FunctionName, "%s ", DemangledName);
	}
	else if (DylibInfo.dli_sname)
	{
		// C function
		FCStringAnsi::Sprintf(out_SymbolInfo.FunctionName, "%s() ", DylibInfo.dli_sname);
	}
	else
	{
		// Unknown!
		FCStringAnsi::Sprintf(out_SymbolInfo.FunctionName, "[Unknown]() ");
	}

	// No line number available.
	// TODO open libUE4.so from the apk and get the DWARF-2 data.
	FCStringAnsi::Strcat(out_SymbolInfo.Filename, "Unknown");
	out_SymbolInfo.LineNumber = 0;

	// Offset of the symbol in the module, eg offset into libUE4.so needed for offline addr2line use.
	out_SymbolInfo.OffsetInModule = ProgramCounter - (uint64)DylibInfo.dli_fbase;

	// Write out Module information.
	ANSICHAR* DylibPath = (ANSICHAR*)DylibInfo.dli_fname;
	ANSICHAR* DylibName = FCStringAnsi::Strrchr(DylibPath, '/');
	if (DylibName)
	{
		DylibName += 1;
	}
	else
	{
		DylibName = DylibPath;
	}
	FCStringAnsi::Strcpy(out_SymbolInfo.ModuleName, DylibName);
}

namespace AndroidStackWalkHelpers
{
	uint64* BackTrace;
	uint32 MaxDepth;

	static _Unwind_Reason_Code BacktraceCallback(struct _Unwind_Context* Context, void* InDepthPtr)
	{
		uint32* DepthPtr = (uint32*)InDepthPtr;

		// stop if filled the buffer
		if (*DepthPtr >= MaxDepth)
		{
			return _Unwind_Reason_Code::_URC_END_OF_STACK;
		}

		uint64 ip = (uint64)_Unwind_GetIP(Context);
		if (ip)
		{
			BackTrace[*DepthPtr] = ip;
			(*DepthPtr)++;
		}
		return _Unwind_Reason_Code::_URC_NO_REASON;
	}
}

#if HAS_LIBUNWIND
// code based on unw_backtrace using signal context for the walk, note that this code was originally intended to walk the current stack.
// Since it walks a signal context it includes the first frame.
static int backtrace_signal(void* sigcontext, void **buffer, int size)
{
	unw_cursor_t cursor;
	unw_word_t ip;
	int n = 0;

	if (unw_init_local2(&cursor, (unw_context_t *)sigcontext, 1) < 0)
	{
		return 0;
	}

	do
	{
		if (n >= size)
		{
			return n;
		}

		if (unw_get_reg(&cursor, UNW_REG_IP, &ip) < 0)
		{
			return n;
		}
		buffer[n++] = (void *)(uintptr_t)ip;
	} while (unw_step(&cursor) > 0);

	return n;
}
#endif

extern int32 unwind_backtrace_signal(void* sigcontext, uint64* Backtrace, int32 MaxDepth);

uint32 FAndroidPlatformStackWalk::CaptureStackBackTrace(uint64* BackTrace, uint32 MaxDepth, void* Context)
{
#if PLATFORM_ANDROID_ARM64
	if (FAndroidMisc::GetTargetSDKVersion() >= 29 && FAndroidMisc::GetAndroidMajorVersion() == 10)
	{
		// UE-103382
		// due to execute-only memory (xom) we cannot currently walk the stack on Android 10 devices when targeting Android 29 or greater.
		static int32 OnceOnly = 0;
		if (Context == nullptr && OnceOnly == 0)
		{
			__android_log_print(ANDROID_LOG_DEBUG, "UE4", "FAndroidPlatformStackWalk::CaptureStackBackTrace disabled on Android 10 with TargetSDK >= 29 due to XOM.");
			OnceOnly = 1;
		}
		return 0;
	}
#endif

	// Make sure we have place to store the information
	if (BackTrace == NULL || MaxDepth == 0)
	{
		return 0;
	}

	// zero results
	FPlatformMemory::Memzero(BackTrace, MaxDepth*sizeof(uint64));
	
#if PLATFORM_ANDROID_ARM
	if (Context != nullptr)
	{
		// Android signal handlers always catch signals before user handlers and passes it down to user later
		// _Unwind_Backtrace does not use signal context and will produce wrong callstack in this case
		// We use code from libcorkscrew to unwind backtrace using actual signal context
		// Code taken from https://android.googlesource.com/platform/system/core/+/jb-dev/libcorkscrew/arch-arm/backtrace-arm.c
		return unwind_backtrace_signal(Context, BackTrace, MaxDepth);
	}
#elif HAS_LIBUNWIND 
	if (Context)
	{
		// Android signal handlers always catch signals before user handlers and passes it down to user later
		// unw_backtrace does not use signal context and will produce wrong callstack in this case
		// We use code from libunwind to unwind backtrace using actual signal context
		return backtrace_signal(Context, (void**)BackTrace, MaxDepth);
	}
	else
	{
		return unw_backtrace((void**)BackTrace, MaxDepth);
	}
#endif 

	AndroidStackWalkHelpers::BackTrace = BackTrace;
	AndroidStackWalkHelpers::MaxDepth = MaxDepth;
	uint32 Depth = 0;
	_Unwind_Backtrace(AndroidStackWalkHelpers::BacktraceCallback, &Depth);
	return Depth;
}

bool FAndroidPlatformStackWalk::SymbolInfoToHumanReadableString(const FProgramCounterSymbolInfo& SymbolInfo, ANSICHAR* HumanReadableString, SIZE_T HumanReadableStringSize)
{
	const int32 MAX_TEMP_SPRINTF = 256;

	//
	// Callstack lines should be written in this standard format. These are parsed by tools so it is important that
	// extra elements are not inserted!
	//
	//	0xaddress module!func [file]
	// 
	// E.g. 0x045C8D01 OrionClient.self(0x00009034)!UEngine::PerformError() [D:\Epic\Orion\Engine\Source\Runtime\Engine\Private\UnrealEngine.cpp:6481]
	//
	// Module may be omitted, everything else should be present, or substituted with a string that conforms to the expected type
	//
	// E.g 0x00000000 (0x00000000) UnknownFunction []
	//
	// 
	if (HumanReadableString && HumanReadableStringSize > 0)
	{
		ANSICHAR StackLine[MAX_SPRINTF] = { 0 };

		// Strip module path.
		const ANSICHAR* Pos0 = FCStringAnsi::Strrchr(SymbolInfo.ModuleName, '\\');
		const ANSICHAR* Pos1 = FCStringAnsi::Strrchr(SymbolInfo.ModuleName, '/');
		const UPTRINT RealPos = FMath::Max((UPTRINT)Pos0, (UPTRINT)Pos1);
		const ANSICHAR* StrippedModuleName = RealPos > 0 ? (const ANSICHAR*)(RealPos + 1) : SymbolInfo.ModuleName;

		// Start with address
		ANSICHAR PCAddress[MAX_TEMP_SPRINTF] = { 0 };
		FCStringAnsi::Snprintf(PCAddress, MAX_TEMP_SPRINTF, "0x%016llX ", SymbolInfo.ProgramCounter);
		FCStringAnsi::Strncat(StackLine, PCAddress, MAX_SPRINTF);

		// Module if it's present
		const bool bHasValidModuleName = FCStringAnsi::Strlen(StrippedModuleName) > 0;
		if (bHasValidModuleName)
		{
			FCStringAnsi::Strncat(StackLine, StrippedModuleName, MAX_SPRINTF);
		}

		FCStringAnsi::Snprintf(PCAddress, MAX_TEMP_SPRINTF, "(0x%016llX)", SymbolInfo.OffsetInModule);
		FCStringAnsi::Strncat(StackLine, PCAddress, MAX_SPRINTF);
		FCStringAnsi::Strncat(StackLine, "!", MAX_SPRINTF);

		// Function if it's available, unknown if it's not
		const bool bHasValidFunctionName = FCStringAnsi::Strlen(SymbolInfo.FunctionName) > 0;
		if (bHasValidFunctionName)
		{
			FCStringAnsi::Strncat(StackLine, SymbolInfo.FunctionName, MAX_SPRINTF);
		}
		else
		{
			FCStringAnsi::Strncat(StackLine, "UnknownFunction", MAX_SPRINTF);
		}

		// file info
		const bool bHasValidFilename = FCStringAnsi::Strlen(SymbolInfo.Filename) > 0 && SymbolInfo.LineNumber > 0;
		if (bHasValidFilename)
		{
			ANSICHAR FilenameAndLineNumber[MAX_TEMP_SPRINTF] = { 0 };
			FCStringAnsi::Snprintf(FilenameAndLineNumber, MAX_TEMP_SPRINTF, " [%s:%i]", SymbolInfo.Filename, SymbolInfo.LineNumber);
			FCStringAnsi::Strncat(StackLine, FilenameAndLineNumber, MAX_SPRINTF);
		}
		else
		{
			FCStringAnsi::Strncat(StackLine, " []", MAX_SPRINTF);
		}

		// Append the stack line.
		FCStringAnsi::Strncat(HumanReadableString, StackLine, HumanReadableStringSize);

		// Return true, if we have a valid function name.
		return bHasValidFunctionName;
	}
	return false;
}


static float GThreadCallStackRequestMaxWait = 0.5f;
static FAutoConsoleVariableRef CVarAndroidPlatformThreadCallStackRequestMaxWait(
	TEXT("AndroidPlatformThreadStackWalk.RequestMaxWait"),
	GThreadCallStackRequestMaxWait,
	TEXT("The number of seconds to spin before an individual back trace has timed out."));

static float GThreadCallStackMaxWait = 5.0f;
static TAutoConsoleVariable<float> CVarAndroidPlatformThreadCallStackMaxWait(
	TEXT("AndroidPlatformThreadStackWalk.MaxWait"),
	GThreadCallStackMaxWait,
	TEXT("The number of seconds allowed to spin before killing the process, with the assumption the back trace handler has hung."));

#if ANDROID_HAS_RTSIGNALS
/** Passed in through sigqueue for gathering of a callstack from a signal */
struct ThreadStackUserData
{
	uint64* BackTrace;
	int32 BackTraceCount;
	SIZE_T CallStackSize;
};

int32 ThreadStackBackTraceStatus = 0;
static const int32 ThreadStackBackTraceCurrentStatus_RUNNING = -2;
static const int32 ThreadStackBackTraceCurrentStatus_DONE = -3;

static ThreadStackUserData SignalThreadStackUserData;

// the callback when THREAD_CALLSTACK_GENERATOR is being processed.
void FAndroidPlatformStackWalk::HandleBackTraceSignal(siginfo* Info, void* Context)
{
	if (FPlatformAtomics::InterlockedCompareExchange(&ThreadStackBackTraceStatus, ThreadStackBackTraceCurrentStatus_RUNNING, Info->si_value.sival_int) == Info->si_value.sival_int)
	{
		SignalThreadStackUserData.BackTraceCount = FPlatformStackWalk::CaptureStackBackTrace(SignalThreadStackUserData.BackTrace, SignalThreadStackUserData.CallStackSize, Context);
		FPlatformAtomics::AtomicStore(&ThreadStackBackTraceStatus, ThreadStackBackTraceCurrentStatus_DONE);
	}
}

// Sends a signal to ThreadId, wait AndroidPlatformThreadStackWalk.RequestMaxWait seconds for result or time out and return 0.
// if callstack capture begins, but takes > AndroidPlatformThreadStackWalk.MaxWait the process will be killed.
// Is not thread safe, returns 0 if a CaptureThreadStackBackTrace is occurring on another thread.
uint32 FAndroidPlatformStackWalk::CaptureThreadStackBackTrace(uint64 ThreadId, uint64* BackTrace, uint32 MaxDepth)
{
	static TAtomic<bool> bHasReentered(false);
	bool bExpected = false;
	if (!bHasReentered.CompareExchange(bExpected, true))
	{
		return 0;
	}
	ON_SCOPE_EXIT
	{
		bHasReentered = false;
	};

	auto GatherCallstackFromThread = [](uint64 TargetThreadId)
	{
		static int32 ThreadStackBackTraceNextRequest = 0;
		int32 CurrentThreadStackBackTrace = ThreadStackBackTraceNextRequest++;

		auto WaitForSignalHandlerToFinishOrCrash = [CurrentThreadStackBackTrace]()
		{
			const float PollTime = 0.001f;

			for (float CurrentTime = 0; CurrentTime <= GThreadCallStackMaxWait; CurrentTime += PollTime)
			{
				if (FPlatformAtomics::InterlockedCompareExchange(&ThreadStackBackTraceStatus, ThreadStackBackTraceNextRequest, ThreadStackBackTraceCurrentStatus_DONE) == ThreadStackBackTraceCurrentStatus_DONE)
				{
					// success 
					return SignalThreadStackUserData.BackTraceCount;
				}

				// signal timed out
				if (CurrentTime > GThreadCallStackRequestMaxWait && FPlatformAtomics::InterlockedCompareExchange(&ThreadStackBackTraceStatus, ThreadStackBackTraceNextRequest, CurrentThreadStackBackTrace) == CurrentThreadStackBackTrace)
				{
					// request not yet started, skip it.
					return 0;
				}
				FPlatformProcess::SleepNoStats(PollTime);
			}

			// We have waited for as long as we should for the signal handler to finish. Assume it has hang and we need to kill our selfs
			*(int*)0x10 = 0x0;
			return 0;
		};

		sigval UserData;
		UserData.sival_int = CurrentThreadStackBackTrace;

		siginfo_t info;
		memset(&info, 0, sizeof(siginfo_t));
		info.si_signo = THREAD_CALLSTACK_GENERATOR;
		info.si_code = SI_QUEUE;
		info.si_pid = syscall(SYS_getpid);
		info.si_uid = syscall(SYS_getuid);
		info.si_value = UserData;

		// Avoid using sigqueue here as if the ThreadId is already blocked and in a signal handler
		// sigqueue will try a different thread signal handler and report the wrong callstack
		if (syscall(SYS_rt_tgsigqueueinfo, info.si_pid, TargetThreadId, THREAD_CALLSTACK_GENERATOR, &info) == 0)
		{
			return WaitForSignalHandlerToFinishOrCrash();
		}
		else
		{
			// we failed to send the signal, update the current status.
			FPlatformAtomics::AtomicStore(&ThreadStackBackTraceStatus, ThreadStackBackTraceNextRequest);
		}

		return 0;
	};

	SignalThreadStackUserData.CallStackSize = MaxDepth;
	SignalThreadStackUserData.BackTrace = BackTrace;
	SignalThreadStackUserData.BackTraceCount = 0;

	return GatherCallstackFromThread(ThreadId);
}
#else
uint32 FAndroidPlatformStackWalk::CaptureThreadStackBackTrace(uint64 ThreadId, uint64* BackTrace, uint32 MaxDepth)
{
	return 0;
}
#endif //ANDROID_HAS_RTSIGNALS