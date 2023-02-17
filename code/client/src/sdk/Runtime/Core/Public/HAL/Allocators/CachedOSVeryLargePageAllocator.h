// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreTypes.h"
#include "HAL/CriticalSection.h"
#include "HAL/UnrealMemory.h"
#include "Containers/List.h"
#include "HAL/Allocators/CachedOSPageAllocator.h"
#include "HAL/PlatformMemory.h"


#if UE_USE_VERYLARGEPAGEALLOCATOR

#if PLATFORM_64BITS
#define CACHEDOSVERYLARGEPAGEALLOCATOR_BYTE_LIMIT (128*1024*1024)
#else
#define CACHEDOSVERYLARGEPAGEALLOCATOR_BYTE_LIMIT (16*1024*1024)
#endif
//#define CACHEDOSVERYLARGEPAGEALLOCATOR_MAX_CACHED_OS_FREES (CACHEDOSVERYLARGEPAGEALLOCATOR_BYTE_LIMIT/(1024*64))
#define CACHEDOSVERYLARGEPAGEALLOCATOR_MAX_CACHED_OS_FREES (256)


#ifndef UE_VERYLARGEPAGEALLOCATOR_TAKEONALL64KBALLOCATIONS
#define UE_VERYLARGEPAGEALLOCATOR_TAKEONALL64KBALLOCATIONS 0
#endif

#ifndef UE_VERYLARGEPAGEALLOCATOR_RESERVED_SIZE_IN_GB
#define UE_VERYLARGEPAGEALLOCATOR_RESERVED_SIZE_IN_GB 2	//default to 2GB
#endif


#ifndef UE_VERYLARGEPAGEALLOCATOR_PAGESIZE_KB
#define UE_VERYLARGEPAGEALLOCATOR_PAGESIZE_KB 4096	//default to 4MB
#endif
class FCachedOSVeryLargePageAllocator
{
	// we make the address space twice as big as we need and use the 1st have for small pool allocations, the 2nd half is used for other allocations that are still == SizeOfSubPage
#if UE_VERYLARGEPAGEALLOCATOR_TAKEONALL64KBALLOCATIONS
	static const uint64 AddressSpaceToReserve = ((1024LL * 1024LL * 1024LL) * UE_VERYLARGEPAGEALLOCATOR_RESERVED_SIZE_IN_GB * 2LL);
	static const uint64 AddressSpaceToReserveSmall = AddressSpaceToReserve / 2;
#else
	static const uint64 AddressSpaceToReserve = ((1024 * 1024 * 1024LL) * UE_VERYLARGEPAGEALLOCATOR_RESERVED_SIZE_IN_GB);
	static const uint64 AddressSpaceToReserveSmall = AddressSpaceToReserve;
#endif
	static const uint64 SizeOfLargePage = (UE_VERYLARGEPAGEALLOCATOR_PAGESIZE_KB * 1024);
	static const uint64 SizeOfSubPage = (1024 * 64);
	static const uint64 NumberOfLargePages = (AddressSpaceToReserve / SizeOfLargePage);
	static const uint64 NumberOfSubPagesPerLargePage = (SizeOfLargePage / SizeOfSubPage);
public:

	FCachedOSVeryLargePageAllocator()
		: bEnabled(true)
		, CachedFree(0)
	{
		Init();
	}

	~FCachedOSVeryLargePageAllocator()
	{
		// this leaks everything!
	}

	void* Allocate(SIZE_T Size, uint32 AllocationHint = 0, FCriticalSection* Mutex = nullptr);

	void Free(void* Ptr, SIZE_T Size, FCriticalSection* Mutex = nullptr);

	void FreeAll(FCriticalSection* Mutex = nullptr);

	uint64 GetCachedFreeTotal()
	{
		return CachedFree + CachedOSPageAllocator.GetCachedFreeTotal();
	}

	FORCEINLINE bool IsPartOf(const void* Ptr)
	{
		if (((uintptr_t)Ptr - AddressSpaceReserved) < AddressSpaceToReserveSmall)
		{
			return true;
		}
		return false;
	}

private:

	void Init();

	bool bEnabled;
	uintptr_t	AddressSpaceReserved;
	uintptr_t	AddressSpaceReservedEndSmallPool;
	uintptr_t	AddressSpaceReservedEnd;
	uint64		CachedFree;

	FPlatformMemory::FPlatformVirtualMemoryBlock Block;

	struct FLargePage : public TIntrusiveLinkedList<FLargePage>
	{
		uintptr_t	FreeSubPages[NumberOfSubPagesPerLargePage];
		int32		NumberOfFreeSubPages;
		uint32		AllocationHint;

		uintptr_t	BaseAddress;

		void Init(void* InBaseAddress)
		{
			BaseAddress = (uintptr_t)InBaseAddress;
			NumberOfFreeSubPages = NumberOfSubPagesPerLargePage;
			uintptr_t Ptr = BaseAddress;
			for (int i = 0; i < NumberOfFreeSubPages; i++)
			{
				FreeSubPages[i] = Ptr;
				Ptr += SizeOfSubPage;
			}
		}

		void Free(void* Ptr)
		{
			FreeSubPages[NumberOfFreeSubPages++] = (uintptr_t)Ptr;
		}

		void* Allocate()
		{
			void* ret = nullptr;
			if (NumberOfFreeSubPages)
			{
				ret = (void*)FreeSubPages[--NumberOfFreeSubPages];
			}
			return ret;
		}
	};

	FLargePage*	FreeLargePagesHead[FMemory::AllocationHints::Max];				// no backing store

	FLargePage*	UsedLargePagesHead[FMemory::AllocationHints::Max];				// has backing store and is full

	FLargePage*	UsedLargePagesWithSpaceHead[FMemory::AllocationHints::Max];	// has backing store and still has room

	FLargePage	LargePagesArray[NumberOfLargePages];

	TCachedOSPageAllocator<CACHEDOSVERYLARGEPAGEALLOCATOR_MAX_CACHED_OS_FREES, CACHEDOSVERYLARGEPAGEALLOCATOR_BYTE_LIMIT> CachedOSPageAllocator;

};
CORE_API extern bool GEnableVeryLargePageAllocator;

#endif // UE_USE_VERYLARGEPAGEALLOCATOR