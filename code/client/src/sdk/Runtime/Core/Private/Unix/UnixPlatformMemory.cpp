// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	UnixPlatformMemory.cpp: Unix platform memory functions
=============================================================================*/

#include "Unix/UnixPlatformMemory.h"
#include "Unix/UnixForkPageProtector.h"
#include "Misc/AssertionMacros.h"
#include "Misc/CoreDelegates.h"
#include "Misc/FeedbackContext.h"
#include "Math/NumericLimits.h"
#include "Math/UnrealMathUtility.h"
#include "Templates/UnrealTemplate.h"
#include "Containers/UnrealString.h"
#include "Logging/LogMacros.h"
#include "HAL/MallocAnsi.h"
#include "HAL/MallocJemalloc.h"
#include "HAL/MallocBinned.h"
#include "HAL/MallocBinned2.h"
#include "HAL/MallocReplayProxy.h"
#include "HAL/MallocStomp.h"
#include "HAL/PlatformMallocCrash.h"

#if PLATFORM_FREEBSD
	#include <kvm.h>
#else
	#include <sys/sysinfo.h>
#endif
#include <sys/file.h>
#include <sys/mman.h>

#include "GenericPlatform/OSAllocationPool.h"
#include "Misc/ScopeLock.h"

// on 64 bit Linux, it is easier to run out of vm.max_map_count than of other limits. Due to that, trade VIRT (address space) size for smaller amount of distinct mappings
// by not leaving holes between them (kernel will coalesce the adjoining mappings into a single one)
// Disable by default as this causes large wasted virtual memory areas as we've to cut out 64k aligned pointers from a larger memory area then requested but then leave them mmap'd
#define UE4_PLATFORM_REDUCE_NUMBER_OF_MAPS					(0 && PLATFORM_UNIX && PLATFORM_64BITS)

// do not do a root privilege check on non-x86-64 platforms (assume an embedded device)
#ifndef UE4_DO_ROOT_PRIVILEGE_CHECK
  #if defined(_M_X64) || defined(__x86_64__) || defined (__amd64__)
    #define UE4_DO_ROOT_PRIVILEGE_CHECK	 1
  #else
    #define UE4_DO_ROOT_PRIVILEGE_CHECK	 0
  #endif // defined(_M_X64) || defined(__x86_64__) || defined (__amd64__) 
#endif // ifndef UE4_DO_ROOT_PRIVILEGE_CHECK

// Set rather to use BinnedMalloc2 for binned malloc, can be overridden below
#define USE_MALLOC_BINNED2 (1)

// Used in UnixPlatformStackwalk to skip the crash handling callstack frames.
bool CORE_API GFullCrashCallstack = false;

// Used to enable kernel shared memory from mmap'd memory
bool CORE_API GUseKSM = false;
bool CORE_API GKSMMergeAllPages = false;

// Used to enable or disable timing of ensures. Enabled by default
bool CORE_API GTimeEnsures = true;

// Allows settings a specific signal to maintain its default handler rather then ignoring the signal
int32 CORE_API GSignalToDefault = 0;

#if UE_SERVER
// Scale factor for how much we would like to increase or decrease the memory pool size
float CORE_API GPoolTableScale = 1.0f;
#endif

// Used to set the maximum number of file mappings.
#if UE_EDITOR
int32 CORE_API GMaxNumberFileMappingCache = 10000;
#else
int32 CORE_API GMaxNumberFileMappingCache = 100;
#endif

namespace
{
	// The max allowed to be set for the caching
	const int32 MaximumAllowedMaxNumFileMappingCache = 1000000;
	bool GEnableProtectForkedPages = false;
}

/** Controls growth of pools - see PooledVirtualMemoryAllocator.cpp */
extern float GVMAPoolScale;

/** Make Decommit no-op (this significantly speeds up freeing memory at the expense of larger resident footprint) */
bool GMemoryRangeDecommitIsNoOp = (UE_SERVER == 0);

void FUnixPlatformMemory::Init()
{
	FGenericPlatformMemory::Init();

	const FPlatformMemoryConstants& MemoryConstants = FPlatformMemory::GetConstants();
	UE_LOG(LogInit, Log, TEXT(" - Physical RAM available (not considering process quota): %d GB (%lu MB, %lu KB, %lu bytes)"), 
		MemoryConstants.TotalPhysicalGB, 
		MemoryConstants.TotalPhysical / ( 1024ULL * 1024ULL ), 
		MemoryConstants.TotalPhysical / 1024ULL, 
		MemoryConstants.TotalPhysical);
	UE_LOG(LogInit, Log, TEXT(" - VirtualMemoryAllocator pools will grow at scale %g"), GVMAPoolScale);
	UE_LOG(LogInit, Log, TEXT(" - MemoryRangeDecommit() will %s"), 
		GMemoryRangeDecommitIsNoOp ? TEXT("be a no-op (re-run with -vmapoolevict to change)") : TEXT("will evict the memory from RAM (re-run with -novmapoolevict to change)"));
}

bool FUnixPlatformMemory::HasForkPageProtectorEnabled()
{
	return COMPILE_FORK_PAGE_PROTECTOR && GEnableProtectForkedPages;
}

class FMalloc* FUnixPlatformMemory::BaseAllocator()
{
#if UE4_DO_ROOT_PRIVILEGE_CHECK && !IS_PROGRAM
	// This function gets executed very early, way before main() (because global constructors will allocate memory).
	// This makes it ideal, if unobvious, place for a root privilege check.
	if (geteuid() == 0)
	{
		fprintf(stderr, "Refusing to run with the root privileges.\n");
		FPlatformMisc::RequestExit(true);
		// unreachable
		return nullptr;
	}
#endif // UE4_DO_ROOT_PRIVILEGE_CHECK && !IS_PROGRAM

#if UE_USE_MALLOC_REPLAY_PROXY
	bool bAddReplayProxy = false;
#endif // UE_USE_MALLOC_REPLAY_PROXY

	if (USE_MALLOC_BINNED2)
	{
		AllocatorToUse = EMemoryAllocatorToUse::Binned2;
	}
	else 
	{
		AllocatorToUse = EMemoryAllocatorToUse::Binned;
	}
	
	if (FORCE_ANSI_ALLOCATOR)
	{
		AllocatorToUse = EMemoryAllocatorToUse::Ansi;
	}
	else
	{
		// Allow overriding on the command line.
		// We get here before main due to global ctors, so need to do some hackery to get command line args
		if (FILE* CmdLineFile = fopen("/proc/self/cmdline", "r"))
		{
			char * Arg = nullptr;
			size_t Size = 0;
			while(getdelim(&Arg, &Size, 0, CmdLineFile) != -1)
			{
#if PLATFORM_SUPPORTS_JEMALLOC
				if (FCStringAnsi::Stricmp(Arg, "-jemalloc") == 0)
				{
					AllocatorToUse = EMemoryAllocatorToUse::Jemalloc;
					break;
				}
#endif // PLATFORM_SUPPORTS_JEMALLOC
				if (FCStringAnsi::Stricmp(Arg, "-ansimalloc") == 0)
				{
					AllocatorToUse = EMemoryAllocatorToUse::Ansi;
					break;
				}

				if (FCStringAnsi::Stricmp(Arg, "-binnedmalloc") == 0)
				{
					AllocatorToUse = EMemoryAllocatorToUse::Binned;
					break;
				}

				if (FCStringAnsi::Stricmp(Arg, "-binnedmalloc2") == 0)
				{
					AllocatorToUse = EMemoryAllocatorToUse::Binned2;
					break;
				}

				if (FCStringAnsi::Stricmp(Arg, "-fullcrashcallstack") == 0)
				{
					GFullCrashCallstack = true;
				}

				if (FCStringAnsi::Stricmp(Arg, "-useksm") == 0)
				{
					GUseKSM = true;
				}

				if (FCStringAnsi::Stricmp(Arg, "-ksmmergeall") == 0)
				{
					GKSMMergeAllPages = true;
				}

				if (FCStringAnsi::Stricmp(Arg, "-noensuretiming") == 0)
				{
					GTimeEnsures = false;
				}

				const char SignalToDefaultCmd[] = "-sigdfl=";
				if (const char* Cmd = FCStringAnsi::Stristr(Arg, SignalToDefaultCmd))
				{
					int32 SignalToDefault = FCStringAnsi::Atoi(Cmd + sizeof(SignalToDefaultCmd) - 1);

					// Valid signals are only from 1 -> SIGRTMAX
					if (SignalToDefault > SIGRTMAX)
					{
						SignalToDefault = 0;
					}

					GSignalToDefault = FMath::Max(SignalToDefault, 0);
				}

				const char FileMapCacheCmd[] = "-filemapcachesize=";
				if (const char* Cmd = FCStringAnsi::Stristr(Arg, FileMapCacheCmd))
				{
					int32 Max = FCStringAnsi::Atoi(Cmd + sizeof(FileMapCacheCmd) - 1);
					GMaxNumberFileMappingCache = FMath::Clamp(Max, 0, MaximumAllowedMaxNumFileMappingCache);
				}

#if UE_USE_MALLOC_REPLAY_PROXY
				if (FCStringAnsi::Stricmp(Arg, "-mallocsavereplay") == 0)
				{
					bAddReplayProxy = true;
				}
#endif // UE_USE_MALLOC_REPLAY_PROXY
#if WITH_MALLOC_STOMP
				if (FCStringAnsi::Stricmp(Arg, "-stompmalloc") == 0)
				{
					AllocatorToUse = EMemoryAllocatorToUse::Stomp;
					break;
				}
#endif // WITH_MALLOC_STOMP

				const char VMAPoolScaleSwitch[] = "-vmapoolscale=";
				if (const char* Cmd = FCStringAnsi::Stristr(Arg, VMAPoolScaleSwitch))
				{
					float PoolScale = FCStringAnsi::Atof(Cmd + sizeof(VMAPoolScaleSwitch) - 1);
					GVMAPoolScale = FMath::Max(PoolScale, 1.0f);
				}

				if (FCStringAnsi::Stricmp(Arg, "-vmapoolevict") == 0)
				{
					GMemoryRangeDecommitIsNoOp = false;
				}
				if (FCStringAnsi::Stricmp(Arg, "-novmapoolevict") == 0)
				{
					GMemoryRangeDecommitIsNoOp = true;
				}
				if (FCStringAnsi::Stricmp(Arg, "-protectforkedpages") == 0)
				{
					GEnableProtectForkedPages = true;
				}
			}
			free(Arg);
			fclose(CmdLineFile);
		}
	}

	FMalloc * Allocator = NULL;

	switch (AllocatorToUse)
	{
	case EMemoryAllocatorToUse::Ansi:
		Allocator = new FMallocAnsi();
		break;

#if WITH_MALLOC_STOMP
	case EMemoryAllocatorToUse::Stomp:
		Allocator = new FMallocStomp();
		break;
#endif

#if PLATFORM_SUPPORTS_JEMALLOC
	case EMemoryAllocatorToUse::Jemalloc:
		Allocator = new FMallocJemalloc();
		break;
#endif // PLATFORM_SUPPORTS_JEMALLOC

	case EMemoryAllocatorToUse::Binned2:
		Allocator = new FMallocBinned2();
		break;

	default:	// intentional fall-through
	case EMemoryAllocatorToUse::Binned:
		Allocator = new FMallocBinned(FPlatformMemory::GetConstants().BinnedPageSize & MAX_uint32, 0x100000000);
		break;
	}

#if UE_BUILD_DEBUG
	printf("Using %s.\n", Allocator ? TCHAR_TO_UTF8(Allocator->GetDescriptiveName()) : "NULL allocator! We will probably crash right away");
#endif // UE_BUILD_DEBUG

#if UE_USE_MALLOC_REPLAY_PROXY
	if (bAddReplayProxy)
	{
		Allocator = new FMallocReplayProxy(Allocator);
	}
#endif // UE_USE_MALLOC_REPLAY_PROXY

	return Allocator;
}

bool FUnixPlatformMemory::PageProtect(void* const Ptr, const SIZE_T Size, const bool bCanRead, const bool bCanWrite)
{
	int32 ProtectMode;
	if (bCanRead && bCanWrite)
	{
		ProtectMode = PROT_READ | PROT_WRITE;
	}
	else if (bCanRead)
	{
		ProtectMode = PROT_READ;
	}
	else if (bCanWrite)
	{
		ProtectMode = PROT_WRITE;
	}
	else
	{
		ProtectMode = PROT_NONE;
	}
	return mprotect(Ptr, Size, ProtectMode) == 0;
}


static void MarkMappedMemoryMergable(void* Pointer, SIZE_T Size)
{
	const SIZE_T BinnedPageSize = FPlatformMemory::GetConstants().BinnedPageSize;

	// If we dont want to merge all pages only merge chunks larger then BinnedPageSize
	if (GUseKSM && (GKSMMergeAllPages || Size > BinnedPageSize))
	{
		int Ret = madvise(Pointer, Size, MADV_MERGEABLE);
		if (Ret != 0)
		{
			int ErrNo = errno;
			UE_LOG(LogHAL, Fatal, TEXT("madvise(addr=%p, length=%d, advice=MADV_MERGEABLE) failed with errno = %d (%s)"),
				Pointer, Size, ErrNo, StringCast< TCHAR >(strerror(ErrNo)).Get());
			// unreachable
		}
	}
}

#ifndef MALLOC_LEAKDETECTION
	#define MALLOC_LEAKDETECTION 0
#endif
// check bookkeeping info against the passed in parameters in Debug and Development (the latter only in games and servers. also, only if leak detection is disabled, otherwise things are very slow)
#define UE4_PLATFORM_SANITY_CHECK_OS_ALLOCATIONS			(UE_BUILD_DEBUG || (UE_BUILD_DEVELOPMENT && (UE_GAME || UE_SERVER) && !MALLOC_LEAKDETECTION))

/** This structure is stored in the page after each OS allocation and checks that its properties are valid on Free. Should be less than the page size (4096 on all platforms we support) */
struct FOSAllocationDescriptor
{
	enum class MagicType : uint64
	{
		Marker = 0xd0c233ccf493dfb0
	};

	/** Magic that makes sure that we are not passed a pointer somewhere into the middle of the allocation (and/or the structure wasn't stomped). */
	MagicType	Magic;

	/** This should include the descriptor itself. */
	void*		PointerToUnmap;

	/** This should include the total size of allocation, so after unmapping it everything is gone, including the descriptor */
	SIZE_T		SizeToUnmap;

	/** Debug info that makes sure that the correct size is preserved. */
	SIZE_T		OriginalSizeAsPassed;
};

void* FUnixPlatformMemory::BinnedAllocFromOS(SIZE_T Size)
{
	static SIZE_T OSPageSize = FPlatformMemory::GetConstants().PageSize;
	// guard against someone not passing size in whole pages
	SIZE_T SizeInWholePages = (Size % OSPageSize) ? (Size + OSPageSize - (Size % OSPageSize)) : Size;
	void* Pointer = nullptr;

	// Binned expects OS allocations to be BinnedPageSize-aligned, and that page is at least 64KB. mmap() alone cannot do this, so carve out the needed chunks.
	const SIZE_T ExpectedAlignment = FPlatformMemory::GetConstants().BinnedPageSize;
	// Descriptor is only used if we're sanity checking. However, #ifdef'ing its use would make the code more fragile. Size needs to be at least one page.
	const SIZE_T DescriptorSize = (UE4_PLATFORM_REDUCE_NUMBER_OF_MAPS != 0 || UE4_PLATFORM_SANITY_CHECK_OS_ALLOCATIONS != 0) ? OSPageSize : 0;

	SIZE_T ActualSizeMapped = SizeInWholePages + ExpectedAlignment;

	// the remainder of the map will be used for the descriptor, if any.
	// we always allocate at least one page more than needed
	void* PointerWeGotFromMMap = mmap(nullptr, ActualSizeMapped, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	// store these values, to unmap later

	Pointer = PointerWeGotFromMMap;
	if (Pointer == MAP_FAILED)
	{
		FPlatformMemory::OnOutOfMemory(ActualSizeMapped, ExpectedAlignment);
		// unreachable
		return nullptr;
	}

	SIZE_T Offset = (reinterpret_cast<SIZE_T>(Pointer) % ExpectedAlignment);

	// See if we need to unmap anything in the front. If the pointer happened to be aligned, we don't need to unmap anything.
	if (LIKELY(Offset != 0))
	{
		// figure out how much to unmap before the alignment.
		SIZE_T SizeToNextAlignedPointer = ExpectedAlignment - Offset;
		void* AlignedPointer = reinterpret_cast<void*>(reinterpret_cast<SIZE_T>(Pointer) + SizeToNextAlignedPointer);

		// do not unmap if we're trying to reduce the number of distinct maps, since holes prevent the Linux kernel from coalescing two adjoining mmap()s into a single VMA
		if (!UE4_PLATFORM_REDUCE_NUMBER_OF_MAPS)
		{
			// unmap the part before
			if (munmap(Pointer, SizeToNextAlignedPointer) != 0)
			{
				FPlatformMemory::OnOutOfMemory(SizeToNextAlignedPointer, ExpectedAlignment);
				// unreachable
				return nullptr;
			}

			// account for reduced mmaped size
			ActualSizeMapped -= SizeToNextAlignedPointer;
		}

		// now, make it appear as if we initially got the allocation right
		Pointer = AlignedPointer;
	}

	// at this point, Pointer is aligned at the expected alignment - either we lucked out on the initial allocation
	// or we already got rid of the extra memory that was allocated in the front.
	checkf((reinterpret_cast<SIZE_T>(Pointer) % ExpectedAlignment) == 0, TEXT("BinnedAllocFromOS(): Internal error: did not align the pointer as expected."));

	// do not unmap if we're trying to reduce the number of distinct maps, since holes prevent the Linux kernel from coalescing two adjoining mmap()s into a single VMA
	if (!UE4_PLATFORM_REDUCE_NUMBER_OF_MAPS)
	{
		// Now unmap the tail only, if any, but leave just enough space for the descriptor
		void* TailPtr = reinterpret_cast<void*>(reinterpret_cast<SIZE_T>(Pointer) + SizeInWholePages + DescriptorSize);
		SIZE_T TailSize = ActualSizeMapped - SizeInWholePages - DescriptorSize;

		if (LIKELY(TailSize > 0))
		{
			if (munmap(TailPtr, TailSize) != 0)
			{
				FPlatformMemory::OnOutOfMemory(TailSize, ExpectedAlignment);
				// unreachable
				return nullptr;
			}
		}
	}

	// we're done with this allocation, fill in the descriptor with the info
	if (LIKELY(DescriptorSize > 0))
	{
		FOSAllocationDescriptor* AllocDescriptor = reinterpret_cast<FOSAllocationDescriptor*>(reinterpret_cast<SIZE_T>(Pointer) + Size);
		AllocDescriptor->Magic = FOSAllocationDescriptor::MagicType::Marker;
		if (!UE4_PLATFORM_REDUCE_NUMBER_OF_MAPS)
		{
			AllocDescriptor->PointerToUnmap = Pointer;
			AllocDescriptor->SizeToUnmap = SizeInWholePages + DescriptorSize;
		}
		else
		{
			AllocDescriptor->PointerToUnmap = PointerWeGotFromMMap;
			AllocDescriptor->SizeToUnmap = ActualSizeMapped;
		}
		AllocDescriptor->OriginalSizeAsPassed = Size;
	}

	LLM(FLowLevelMemTracker::Get().OnLowLevelAlloc(ELLMTracker::Platform, Pointer, Size));
	UE::FForkPageProtector::Get().AddMemoryRegion(Pointer, Size);

	return Pointer;
}

void FUnixPlatformMemory::BinnedFreeToOS(void* Ptr, SIZE_T Size)
{
	LLM(FLowLevelMemTracker::Get().OnLowLevelFree(ELLMTracker::Platform, Ptr));
	// guard against someone not passing size in whole pages
	static SIZE_T OSPageSize = FPlatformMemory::GetConstants().PageSize;
	SIZE_T SizeInWholePages = (Size % OSPageSize) ? (Size + OSPageSize - (Size % OSPageSize)) : Size;

	if (UE4_PLATFORM_REDUCE_NUMBER_OF_MAPS || UE4_PLATFORM_SANITY_CHECK_OS_ALLOCATIONS)
	{
		const SIZE_T DescriptorSize = OSPageSize;

		FOSAllocationDescriptor* AllocDescriptor = reinterpret_cast<FOSAllocationDescriptor*>(reinterpret_cast<SIZE_T>(Ptr) + Size);
		if (UNLIKELY(AllocDescriptor->Magic != FOSAllocationDescriptor::MagicType::Marker))
		{
			UE_LOG(LogHAL, Fatal, TEXT("BinnedFreeToOS() has been passed an address %p (size %llu) not allocated through it."), Ptr, (uint64)Size);
			// unreachable
			return;
		}

		void* PointerToUnmap = AllocDescriptor->PointerToUnmap;
		SIZE_T SizeToUnmap = AllocDescriptor->SizeToUnmap;

		UE::FForkPageProtector::Get().FreeMemoryRegion(PointerToUnmap);

		// do checks, from most to least serious
		if (UE4_PLATFORM_SANITY_CHECK_OS_ALLOCATIONS != 0)
		{
			// this check only makes sense when we're not reducing number of maps, since the pointer will have to be different.
			if (UE4_PLATFORM_REDUCE_NUMBER_OF_MAPS == 0)
			{
				if (UNLIKELY(PointerToUnmap != Ptr || SizeToUnmap != SizeInWholePages + DescriptorSize))
				{
					UE_LOG(LogHAL, Fatal, TEXT("BinnedFreeToOS(): info mismatch: descriptor ptr: %p, size %llu, but our pointer is %p and size %llu."), PointerToUnmap, SizeToUnmap, AllocDescriptor, (uint64)(SizeInWholePages + DescriptorSize));
					// unreachable
					return;
				}
			}

			if (UNLIKELY(AllocDescriptor->OriginalSizeAsPassed != Size))
			{
				UE_LOG(LogHAL, Fatal, TEXT("BinnedFreeToOS(): info mismatch: descriptor original size %llu, our size is %llu for pointer %p"), AllocDescriptor->OriginalSizeAsPassed, Size, Ptr);
				// unreachable
				return;
			}
		}

		AllocDescriptor = nullptr;	// just so no one touches it

		if (UNLIKELY(munmap(PointerToUnmap, SizeToUnmap) != 0))
		{
			FPlatformMemory::OnOutOfMemory(SizeToUnmap, 0);
			// unreachable
		}
	}
	else
	{
		UE::FForkPageProtector::Get().FreeMemoryRegion(Ptr);

		if (UNLIKELY(munmap(Ptr, SizeInWholePages) != 0))
		{
			FPlatformMemory::OnOutOfMemory(SizeInWholePages, 0);
			// unreachable
		}
	}
}

size_t FUnixPlatformMemory::FPlatformVirtualMemoryBlock::GetVirtualSizeAlignment()
{
	static SIZE_T OSPageSize = FPlatformMemory::GetConstants().PageSize;
	return OSPageSize;
}

size_t FUnixPlatformMemory::FPlatformVirtualMemoryBlock::GetCommitAlignment()
{
	static SIZE_T OSPageSize = FPlatformMemory::GetConstants().PageSize;
	return OSPageSize;
}

FUnixPlatformMemory::FPlatformVirtualMemoryBlock FUnixPlatformMemory::FPlatformVirtualMemoryBlock::AllocateVirtual(size_t InSize, size_t InAlignment)
{
	FPlatformVirtualMemoryBlock Result;
	InSize = Align(InSize, GetVirtualSizeAlignment());
	Result.VMSizeDivVirtualSizeAlignment = InSize / GetVirtualSizeAlignment();

	size_t Alignment = FMath::Max(InAlignment, GetVirtualSizeAlignment());
	check(Alignment <= GetVirtualSizeAlignment());

	Result.Ptr = mmap(nullptr, Result.GetActualSize(), PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	if (LIKELY(Result.Ptr != MAP_FAILED))
	{
		MarkMappedMemoryMergable(Result.Ptr, Result.GetActualSize());

		UE::FForkPageProtector::Get().AddMemoryRegion(Result.Ptr, Result.GetActualSize());
	}
	else
	{
		FPlatformMemory::OnOutOfMemory(Result.GetActualSize(), InAlignment);
	}
	check(Result.Ptr && IsAligned(Result.Ptr, Alignment));
	return Result;
}



void FUnixPlatformMemory::FPlatformVirtualMemoryBlock::FreeVirtual()
{
	if (Ptr)
	{
		check(GetActualSize() > 0);
		if (munmap(Ptr, GetActualSize()) != 0)
		{
			// we can ran out of VMAs here
			FPlatformMemory::OnOutOfMemory(GetActualSize(), 0);
			// unreachable
		}

		UE::FForkPageProtector::Get().FreeMemoryRegion(Ptr);

		Ptr = nullptr;
		VMSizeDivVirtualSizeAlignment = 0;
	}
}

void FUnixPlatformMemory::FPlatformVirtualMemoryBlock::Commit(size_t InOffset, size_t InSize)
{
	check(IsAligned(InOffset, GetCommitAlignment()) && IsAligned(InSize, GetCommitAlignment()));
	check(InOffset >= 0 && InSize >= 0 && InOffset + InSize <= GetActualSize() && Ptr);
}

void FUnixPlatformMemory::FPlatformVirtualMemoryBlock::Decommit(size_t InOffset, size_t InSize)
{
	check(IsAligned(InOffset, GetCommitAlignment()) && IsAligned(InSize, GetCommitAlignment()));
	check(InOffset >= 0 && InSize >= 0 && InOffset + InSize <= GetActualSize() && Ptr);
	if (!LIKELY(GMemoryRangeDecommitIsNoOp))
	{
		madvise(((uint8*)Ptr) + InOffset, InSize, MADV_DONTNEED);
	}
}


namespace UnixPlatformMemory
{
	/**
	 * @brief Returns value in bytes from a status line
	 * @param Line in format "Blah:  10000 kB" - needs to be writable as it will modify it
	 * @return value in bytes (10240000, i.e. 10000 * 1024 for the above example)
	 */
	uint64 GetBytesFromStatusLine(char * Line)
	{
		check(Line);
		int Len = strlen(Line);

		// Len should be long enough to hold at least " kB\n"
		const int kSuffixLength = 4;	// " kB\n"
		if (Len <= kSuffixLength)
		{
			return 0;
		}

		const int NewLen = Len - kSuffixLength;

		// let's check that this is indeed "kB"
		char * Suffix = &Line[NewLen];
		if (strcmp(Suffix, " kB\n") != 0)
		{
			// Unix the kernel changed the format, huh?
			return 0;
		}

		// kill the kB
		*Suffix = 0;

        // find the beginning of the number
		for (const char* NumberBegin = Line; NumberBegin < Suffix; ++NumberBegin)
		{
			if (isdigit(*NumberBegin))
			{
				return atoll(NumberBegin) * 1024ULL;
			}
		}

		// we were unable to find whitespace in front of the number
		return 0;
	}
}

FPlatformMemoryStats FUnixPlatformMemory::GetStats()
{
	FPlatformMemoryStats MemoryStats;	// will init from constants

#if PLATFORM_FREEBSD

	const FPlatformMemoryConstants& MemoryConstants = FPlatformMemory::GetConstants();

	size_t size = sizeof(SIZE_T);

	SIZE_T SysFreeCount = 0;
	sysctlbyname("vm.stats.vm.v_free_count", &SysFreeCount, &size, NULL, 0);

	SIZE_T SysActiveCount = 0;
	sysctlbyname("vm.stats.vm.v_active_count", &SysActiveCount, &size, NULL, 0);

	// Get swap info from kvm api
	kvm_t* Kvm = kvm_open(NULL, "/dev/null", NULL, O_RDONLY, NULL);
	struct kvm_swap KvmSwap;
	kvm_getswapinfo(Kvm, &KvmSwap, 1, 0);
	kvm_close(Kvm);

	MemoryStats.AvailablePhysical = SysFreeCount * MemoryConstants.PageSize;
	MemoryStats.AvailableVirtual = (KvmSwap.ksw_total - KvmSwap.ksw_used) * MemoryConstants.PageSize;
	MemoryStats.UsedPhysical = SysActiveCount * MemoryConstants.PageSize;
	MemoryStats.UsedVirtual = KvmSwap.ksw_used * MemoryConstants.PageSize;

#else

	// open to all kind of overflows, thanks to Unix approach of exposing system stats via /proc and lack of proper C API
	// And no, sysinfo() isn't useful for this (cannot get the same value for MemAvailable through it for example).

	if (FILE* FileGlobalMemStats = fopen("/proc/meminfo", "r"))
	{
		int FieldsSetSuccessfully = 0;
		uint64 MemFree = 0, Cached = 0;
		do
		{
			char LineBuffer[256] = {0};
			char *Line = fgets(LineBuffer, UE_ARRAY_COUNT(LineBuffer), FileGlobalMemStats);
			if (Line == nullptr)
			{
				break;	// eof or an error
			}

			// if we have MemAvailable, favor that (see http://git.kernel.org/cgit/linux/kernel/git/torvalds/linux.git/commit/?id=34e431b0ae398fc54ea69ff85ec700722c9da773)
			if (strstr(Line, "MemAvailable:") == Line)
			{
				MemoryStats.AvailablePhysical = UnixPlatformMemory::GetBytesFromStatusLine(Line);
				++FieldsSetSuccessfully;
			}
			else if (strstr(Line, "SwapFree:") == Line)
			{
				MemoryStats.AvailableVirtual = UnixPlatformMemory::GetBytesFromStatusLine(Line);
				++FieldsSetSuccessfully;
			}
			else if (strstr(Line, "MemFree:") == Line)
			{
				MemFree = UnixPlatformMemory::GetBytesFromStatusLine(Line);
				++FieldsSetSuccessfully;
			}
			else if (strstr(Line, "Cached:") == Line)
			{
				Cached = UnixPlatformMemory::GetBytesFromStatusLine(Line);
				++FieldsSetSuccessfully;
			}
		}
		while(FieldsSetSuccessfully < 4);

		// if we didn't have MemAvailable (kernels < 3.14 or CentOS 6.x), use free + cached as a (bad) approximation
		if (MemoryStats.AvailablePhysical == 0)
		{
			MemoryStats.AvailablePhysical = FMath::Min(MemFree + Cached, MemoryStats.TotalPhysical);
		}

		fclose(FileGlobalMemStats);
	}

	// again /proc "API" :/
	if (FILE* ProcMemStats = fopen("/proc/self/status", "r"))
	{
		int FieldsSetSuccessfully = 0;
		do
		{
			char LineBuffer[256] = {0};
			char *Line = fgets(LineBuffer, UE_ARRAY_COUNT(LineBuffer), ProcMemStats);
			if (Line == nullptr)
			{
				break;	// eof or an error
			}

			if (strstr(Line, "VmPeak:") == Line)
			{
				MemoryStats.PeakUsedVirtual = UnixPlatformMemory::GetBytesFromStatusLine(Line);
				++FieldsSetSuccessfully;
			}
			else if (strstr(Line, "VmSize:") == Line)
			{
				MemoryStats.UsedVirtual = UnixPlatformMemory::GetBytesFromStatusLine(Line);
				++FieldsSetSuccessfully;
			}
			else if (strstr(Line, "VmHWM:") == Line)
			{
				MemoryStats.PeakUsedPhysical = UnixPlatformMemory::GetBytesFromStatusLine(Line);
				++FieldsSetSuccessfully;
			}
			else if (strstr(Line, "VmRSS:") == Line)
			{
				MemoryStats.UsedPhysical = UnixPlatformMemory::GetBytesFromStatusLine(Line);
				++FieldsSetSuccessfully;
			}
		}
		while(FieldsSetSuccessfully < 4);

		fclose(ProcMemStats);
	}

#endif // PLATFORM_FREEBSD

	// sanitize stats as sometimes peak < used for some reason
	MemoryStats.PeakUsedVirtual = FMath::Max(MemoryStats.PeakUsedVirtual, MemoryStats.UsedVirtual);
	MemoryStats.PeakUsedPhysical = FMath::Max(MemoryStats.PeakUsedPhysical, MemoryStats.UsedPhysical);

	return MemoryStats;
}

FExtendedPlatformMemoryStats FUnixPlatformMemory::GetExtendedStats()
{
	FExtendedPlatformMemoryStats MemoryStats;

	// More /proc "API" :/
	MemoryStats.Shared_Clean = 0;
	MemoryStats.Shared_Dirty = 0;
	MemoryStats.Private_Clean = 0;
	MemoryStats.Private_Dirty = 0;
	if (FILE* ProcSMaps = fopen("/proc/self/smaps", "r"))
	{
		do
		{
			char LineBuffer[256] = { 0 };
			char *Line = fgets(LineBuffer, UE_ARRAY_COUNT(LineBuffer), ProcSMaps);
			if (Line == nullptr)
			{
				break;	// eof or an error
			}

			if (strstr(Line, "Shared_Clean:") == Line)
			{
				MemoryStats.Shared_Clean += UnixPlatformMemory::GetBytesFromStatusLine(Line);
			}
			else if (strstr(Line, "Shared_Dirty:") == Line)
			{
				MemoryStats.Shared_Dirty += UnixPlatformMemory::GetBytesFromStatusLine(Line);
			}
			if (strstr(Line, "Private_Clean:") == Line)
			{
				MemoryStats.Private_Clean += UnixPlatformMemory::GetBytesFromStatusLine(Line);
			}
			else if (strstr(Line, "Private_Dirty:") == Line)
			{
				MemoryStats.Private_Dirty += UnixPlatformMemory::GetBytesFromStatusLine(Line);
			}
		} while (!feof(ProcSMaps));

		fclose(ProcSMaps);
	}

	return MemoryStats;
}

const FPlatformMemoryConstants& FUnixPlatformMemory::GetConstants()
{
	static FPlatformMemoryConstants MemoryConstants;

	if( MemoryConstants.TotalPhysical == 0 )
	{
#if PLATFORM_FREEBSD

		size_t Size = sizeof(SIZE_T);

		SIZE_T SysPageCount = 0;
		sysctlbyname("vm.stats.vm.v_page_count", &SysPageCount, &Size, NULL, 0);

		SIZE_T SysPageSize = 0;
		sysctlbyname("vm.stats.vm.v_page_size", &SysPageSize, &Size, NULL, 0);

		// Get swap info from kvm api
		kvm_t* Kvm = kvm_open(NULL, "/dev/null", NULL, O_RDONLY, NULL);
		struct kvm_swap KvmSwap;
		kvm_getswapinfo(Kvm, &KvmSwap, 1, 0);
		kvm_close(Kvm);

		MemoryConstants.TotalPhysical = SysPageCount * SysPageSize;
		MemoryConstants.TotalVirtual = KvmSwap.ksw_total * SysPageSize;
		MemoryConstants.PageSize = SysPageSize;

#else
 
		// Gather platform memory stats.
		struct sysinfo SysInfo;
		unsigned long long MaxPhysicalRAMBytes = 0;
		unsigned long long MaxVirtualRAMBytes = 0;

		if (0 == sysinfo(&SysInfo))
		{
			MaxPhysicalRAMBytes = static_cast< unsigned long long >( SysInfo.mem_unit ) * static_cast< unsigned long long >( SysInfo.totalram );
			MaxVirtualRAMBytes = static_cast< unsigned long long >( SysInfo.mem_unit ) * static_cast< unsigned long long >( SysInfo.totalswap );
		}

		MemoryConstants.TotalPhysical = MaxPhysicalRAMBytes;
		MemoryConstants.TotalVirtual = MaxVirtualRAMBytes;

#endif // PLATFORM_FREEBSD

		MemoryConstants.TotalPhysicalGB = (MemoryConstants.TotalPhysical + 1024ULL * 1024ULL * 1024ULL - 1) / 1024ULL / 1024ULL / 1024ULL;

		MemoryConstants.PageSize = sysconf(_SC_PAGESIZE);
		MemoryConstants.BinnedPageSize = FMath::Max((SIZE_T)65536, MemoryConstants.PageSize);
		MemoryConstants.BinnedAllocationGranularity = MemoryConstants.PageSize;
		MemoryConstants.OsAllocationGranularity = MemoryConstants.PageSize;
	}

	return MemoryConstants;	
}

FPlatformMemory::FSharedMemoryRegion* FUnixPlatformMemory::MapNamedSharedMemoryRegion(const FString& InName, bool bCreate, uint32 AccessMode, SIZE_T Size)
{
	// expecting platform-independent name, so convert it to match platform requirements
	FString Name("/");
	Name += InName;
	FTCHARToUTF8 NameUTF8(*Name);

	// correct size to match platform constraints
	FPlatformMemoryConstants MemConstants = FPlatformMemory::GetConstants();
	check(MemConstants.PageSize > 0);	// also relying on it being power of two, which should be true in foreseeable future
	if (Size & (MemConstants.PageSize - 1))
	{
		Size = Size & ~(MemConstants.PageSize - 1);
		Size += MemConstants.PageSize;
	}

	int ShmOpenFlags = bCreate ? O_CREAT : 0;
	// note that you cannot combine O_RDONLY and O_WRONLY to get O_RDWR
	check(AccessMode != 0);
	if (AccessMode == FPlatformMemory::ESharedMemoryAccess::Read)
	{
		ShmOpenFlags |= O_RDONLY;
	}
	else if (AccessMode == FPlatformMemory::ESharedMemoryAccess::Write)
	{
		ShmOpenFlags |= O_WRONLY;
	}
	else if (AccessMode == (FPlatformMemory::ESharedMemoryAccess::Write | FPlatformMemory::ESharedMemoryAccess::Read))
	{
		ShmOpenFlags |= O_RDWR;
	}

	int ShmOpenMode = (S_IRUSR | S_IWUSR) | (S_IRGRP | S_IWGRP) | (S_IROTH | S_IWOTH );	// 0666

	// open the object
	int SharedMemoryFd = shm_open(NameUTF8.Get(), ShmOpenFlags, ShmOpenMode);
	if (SharedMemoryFd == -1)
	{
		int ErrNo = errno;
		UE_LOG(LogHAL, Warning, TEXT("shm_open(name='%s', flags=0x%x, mode=0x%x) failed with errno = %d (%s)"), *Name, ShmOpenFlags, ShmOpenMode, ErrNo, 
			StringCast< TCHAR >(strerror(ErrNo)).Get());
		return NULL;
	}

	// truncate if creating (note that we may still don't have rights to do so)
	if (bCreate)
	{
		int Res = ftruncate(SharedMemoryFd, Size);
		if (Res != 0)
		{
			int ErrNo = errno;
			UE_LOG(LogHAL, Warning, TEXT("ftruncate(fd=%d, size=%d) failed with errno = %d (%s)"), SharedMemoryFd, Size, ErrNo, 
				StringCast< TCHAR >(strerror(ErrNo)).Get());
			shm_unlink(NameUTF8.Get());
			return NULL;
		}
	}

	// map
	int MmapProtFlags = 0;
	if (AccessMode & FPlatformMemory::ESharedMemoryAccess::Read)
	{
		MmapProtFlags |= PROT_READ;
	}

	if (AccessMode & FPlatformMemory::ESharedMemoryAccess::Write)
	{
		MmapProtFlags |= PROT_WRITE;
	}

	void *Ptr = mmap(NULL, Size, MmapProtFlags, MAP_SHARED, SharedMemoryFd, 0);
	if (Ptr == MAP_FAILED)
	{
		int ErrNo = errno;
		UE_LOG(LogHAL, Warning, TEXT("mmap(addr=NULL, length=%d, prot=0x%x, flags=MAP_SHARED, fd=%d, 0) failed with errno = %d (%s)"), Size, MmapProtFlags, SharedMemoryFd, ErrNo, 
			StringCast< TCHAR >(strerror(ErrNo)).Get());

		if (bCreate)
		{
			shm_unlink(NameUTF8.Get());
		}
		return NULL;
	}

	return new FUnixSharedMemoryRegion(Name, AccessMode, Ptr, Size, SharedMemoryFd, bCreate);
}

bool FUnixPlatformMemory::UnmapNamedSharedMemoryRegion(FSharedMemoryRegion * MemoryRegion)
{
	bool bAllSucceeded = true;

	if (MemoryRegion)
	{
		FUnixSharedMemoryRegion * UnixRegion = static_cast< FUnixSharedMemoryRegion* >( MemoryRegion );

		if (munmap(UnixRegion->GetAddress(), UnixRegion->GetSize()) == -1) 
		{
			bAllSucceeded = false;

			int ErrNo = errno;
			UE_LOG(LogHAL, Warning, TEXT("munmap(addr=%p, len=%d) failed with errno = %d (%s)"), UnixRegion->GetAddress(), UnixRegion->GetSize(), ErrNo, 
				StringCast< TCHAR >(strerror(ErrNo)).Get());
		}

		if (close(UnixRegion->GetFileDescriptor()) == -1)
		{
			bAllSucceeded = false;

			int ErrNo = errno;
			UE_LOG(LogHAL, Warning, TEXT("close(fd=%d) failed with errno = %d (%s)"), UnixRegion->GetFileDescriptor(), ErrNo, 
				StringCast< TCHAR >(strerror(ErrNo)).Get());
		}

		if (UnixRegion->NeedsToUnlinkRegion())
		{
			FTCHARToUTF8 NameUTF8(UnixRegion->GetName());
			if (shm_unlink(NameUTF8.Get()) == -1)
			{
				bAllSucceeded = false;

				int ErrNo = errno;
				UE_LOG(LogHAL, Warning, TEXT("shm_unlink(name='%s') failed with errno = %d (%s)"), UnixRegion->GetName(), ErrNo, 
					StringCast< TCHAR >(strerror(ErrNo)).Get());
			}
		}

		// delete the region
		delete UnixRegion;
	}

	return bAllSucceeded;
}

void FUnixPlatformMemory::OnOutOfMemory(uint64 Size, uint32 Alignment)
{
	// Update memory stats before we enter the crash handler.
	OOMAllocationSize = Size;
	OOMAllocationAlignment = Alignment;

	// only call this code one time - if already OOM, abort
	if (bIsOOM)
	{
		return;
	}
	bIsOOM = true;

	FMalloc* Prev = GMalloc;
	FPlatformMallocCrash::Get().SetAsGMalloc();

	FPlatformMemoryStats PlatformMemoryStats = FPlatformMemory::GetStats();

	UE_LOG(LogMemory, Warning, TEXT("MemoryStats:")\
		TEXT("\n\tAvailablePhysical %llu")\
		TEXT("\n\t AvailableVirtual %llu")\
		TEXT("\n\t     UsedPhysical %llu")\
		TEXT("\n\t PeakUsedPhysical %llu")\
		TEXT("\n\t      UsedVirtual %llu")\
		TEXT("\n\t  PeakUsedVirtual %llu"),
		(uint64)PlatformMemoryStats.AvailablePhysical,
		(uint64)PlatformMemoryStats.AvailableVirtual,
		(uint64)PlatformMemoryStats.UsedPhysical,
		(uint64)PlatformMemoryStats.PeakUsedPhysical,
		(uint64)PlatformMemoryStats.UsedVirtual,
		(uint64)PlatformMemoryStats.PeakUsedVirtual);
	if (GWarn)
	{
		Prev->DumpAllocatorStats(*GWarn);
	}

	// let any registered handlers go
	FCoreDelegates::GetOutOfMemoryDelegate().Broadcast();

	UE_LOG(LogMemory, Fatal, TEXT("Ran out of memory allocating %llu bytes with alignment %u"), Size, Alignment);
	// unreachable
}

/**
* LLM uses these low level functions (LLMAlloc and LLMFree) to allocate memory. It grabs
* the function pointers by calling FPlatformMemory::GetLLMAllocFunctions. If these functions
* are not implemented GetLLMAllocFunctions should return false and LLM will be disabled.
*/

#if ENABLE_LOW_LEVEL_MEM_TRACKER

void* LLMAlloc(size_t Size)
{
	void* Ptr = mmap(nullptr, Size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);

	return Ptr;
}

void LLMFree(void* Addr, size_t Size)
{
	if (Addr != nullptr && munmap(Addr, Size) != 0)
	{
		const int ErrNo = errno;
		UE_LOG(LogHAL, Fatal, TEXT("munmap(addr=%p, len=%llu) failed with errno = %d (%s)"), Addr, Size,
			ErrNo, StringCast< TCHAR >(strerror(ErrNo)).Get());
	}
}

#endif

bool FUnixPlatformMemory::GetLLMAllocFunctions(void*(*&OutAllocFunction)(size_t), void(*&OutFreeFunction)(void*, size_t), int32& OutAlignment)
{
#if ENABLE_LOW_LEVEL_MEM_TRACKER
	OutAllocFunction = LLMAlloc;
	OutFreeFunction = LLMFree;
	OutAlignment = FPlatformMemory::GetConstants().PageSize;
	return true;
#else
	return false;
#endif
}
