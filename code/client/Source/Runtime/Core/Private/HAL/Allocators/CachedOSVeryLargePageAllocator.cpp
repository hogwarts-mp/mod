// Copyright Epic Games, Inc. All Rights Reserved.

#include "HAL/Allocators/CachedOSVeryLargePageAllocator.h"
#include "HAL/UnrealMemory.h"
#include "Logging/LogMacros.h"
#include "CoreGlobals.h"
#include "HAL/LowLevelMemTracker.h"

#if UE_USE_VERYLARGEPAGEALLOCATOR

CORE_API bool GEnableVeryLargePageAllocator = true;
void FCachedOSVeryLargePageAllocator::Init()
{
	Block = FPlatformMemory::FPlatformVirtualMemoryBlock::AllocateVirtual(AddressSpaceToReserve);
	AddressSpaceReserved = (uintptr_t)Block.GetVirtualPointer();
	AddressSpaceReservedEnd = AddressSpaceReserved + AddressSpaceToReserve;
#if UE_VERYLARGEPAGEALLOCATOR_TAKEONALL64KBALLOCATIONS
	AddressSpaceReservedEndSmallPool = AddressSpaceReserved + (AddressSpaceToReserve / 2);
#else
	AddressSpaceReservedEndSmallPool = AddressSpaceReservedEnd;
#endif


	for (int i = 0; i < FMemory::AllocationHints::Max; i++)
	{
		FreeLargePagesHead[i] = nullptr;
		UsedLargePagesWithSpaceHead[i] = nullptr;
		UsedLargePagesHead[i] = nullptr;
	}
#if UE_VERYLARGEPAGEALLOCATOR_TAKEONALL64KBALLOCATIONS
	for (int i = 0; i < NumberOfLargePages/2; i++)
#else
	for (int i = 0; i < NumberOfLargePages; i++)
#endif
	{
		LargePagesArray[i].Init((void*)((uintptr_t)AddressSpaceReserved + (i*SizeOfLargePage)));
		LargePagesArray[i].LinkHead(FreeLargePagesHead[FMemory::AllocationHints::SmallPool]);
	}

#if UE_VERYLARGEPAGEALLOCATOR_TAKEONALL64KBALLOCATIONS
	for (int i = NumberOfLargePages / 2; i < NumberOfLargePages; i++)
	{
		LargePagesArray[i].Init((void*)((uintptr_t)AddressSpaceReserved + (i * SizeOfLargePage)));
		LargePagesArray[i].LinkHead(FreeLargePagesHead[FMemory::AllocationHints::Default]);
	}
#endif

	if (!GEnableVeryLargePageAllocator)
	{
		bEnabled = false;
	}
}

void* FCachedOSVeryLargePageAllocator::Allocate(SIZE_T Size, uint32 AllocationHint, FCriticalSection* Mutex)
{
	Size = Align(Size, 4096);

	void* ret = nullptr;

	if (bEnabled && Size == SizeOfSubPage)
	{
#if !UE_VERYLARGEPAGEALLOCATOR_TAKEONALL64KBALLOCATIONS
		if (AllocationHint == FMemory::AllocationHints::SmallPool)
#endif
		{
			FLargePage* LargePage = UsedLargePagesWithSpaceHead[AllocationHint];
			if (LargePage == nullptr)
			{
				LargePage = FreeLargePagesHead[AllocationHint];
				if (LargePage)
				{
					LargePage->AllocationHint = AllocationHint;
					LargePage->Unlink();
					{
#if UE_ALLOW_OSMEMORYLOCKFREE
						FScopeUnlock FScopeUnlock(Mutex);
#endif
						Block.Commit(LargePage->BaseAddress - AddressSpaceReserved, SizeOfLargePage);
					}
					LargePage->LinkHead(UsedLargePagesWithSpaceHead[AllocationHint]);
					CachedFree += SizeOfLargePage;
				}
			}
			if (LargePage != nullptr)
			{
				ret = LargePage->Allocate();
				if (ret)
				{
					if (LargePage->NumberOfFreeSubPages == 0)
					{
						LargePage->Unlink();
						LargePage->LinkHead(UsedLargePagesHead[AllocationHint]);
					}
					CachedFree -= SizeOfSubPage;
				}
				else
				{
					if (AllocationHint == FMemory::AllocationHints::SmallPool)
					{
						UE_CLOG(!ret, LogMemory, Fatal, TEXT("The FCachedOSVeryLargePageAllocator has run out of address space for SmallPool allocations, increase UE_VERYLARGEPAGEALLOCATOR_RESERVEDSIZEINGB for your platform!"));
					}
				}
			}
		}
	}

	if (ret == nullptr)
	{
		ret = CachedOSPageAllocator.Allocate(Size, AllocationHint, Mutex);
	}
	return ret;
}

#define LARGEPAGEALLOCATOR_SORT_OnAddress 1

void FCachedOSVeryLargePageAllocator::Free(void* Ptr, SIZE_T Size, FCriticalSection* Mutex)
{
	Size = Align(Size, 4096);
	uint64 Index = ((uintptr_t)Ptr - (uintptr_t)AddressSpaceReserved) / SizeOfLargePage;
	if (Index < (NumberOfLargePages))
	{
		FLargePage* LargePage = &LargePagesArray[Index];

		LargePage->Free(Ptr);
		CachedFree += SizeOfSubPage;

		if (LargePage->NumberOfFreeSubPages == NumberOfSubPagesPerLargePage)
		{
			// totally free, need to move which list we are in and remove the backing store
			LargePage->Unlink();
			{
#if UE_ALLOW_OSMEMORYLOCKFREE
				FScopeUnlock FScopeUnlock(Mutex);
#endif
				Block.Decommit(LargePage->BaseAddress - AddressSpaceReserved, SizeOfLargePage);
			}
			LargePage->LinkHead(FreeLargePagesHead[LargePage->AllocationHint]);
			CachedFree -= SizeOfLargePage;
		}
		else if (LargePage->NumberOfFreeSubPages == 1)
		{
			LargePage->Unlink();
#if LARGEPAGEALLOCATOR_SORT_OnAddress
			FLargePage* InsertPoint = UsedLargePagesWithSpaceHead[LargePage->AllocationHint];
			while (InsertPoint != nullptr)
			{
				if (LargePage->BaseAddress < InsertPoint->BaseAddress)	// sort on address
				{
					break;
				}
				InsertPoint = InsertPoint->Next();
			}
			if (InsertPoint == nullptr || InsertPoint == UsedLargePagesWithSpaceHead[LargePage->AllocationHint])
			{
				LargePage->LinkHead(UsedLargePagesWithSpaceHead[LargePage->AllocationHint]);
			}
			else
			{
				LargePage->LinkBefore(InsertPoint);
			}
#else
			LargePage->LinkHead(UsedLargePagesWithSpaceHead[LargePage->AllocationHint]);
#endif
		}
		else
		{
#if !LARGEPAGEALLOCATOR_SORT_OnAddress
			FLargePage* InsertPoint = LargePage->Next();
			FLargePage* LastInsertPoint = nullptr;

			if ((InsertPoint != nullptr) && LargePage->NumberOfFreeSubPages > InsertPoint->NumberOfFreeSubPages)
			{
				LastInsertPoint = InsertPoint;
				LargePage->Unlink();
				while (InsertPoint != nullptr)
				{
					if (LargePage->NumberOfFreeSubPages <= InsertPoint->NumberOfFreeSubPages)	// sort on number of free sub pages
					{
						break;
					}
					InsertPoint = InsertPoint->Next();
				}
				if (InsertPoint != nullptr)
				{
					LargePage->LinkBefore(InsertPoint);
				}
				else
				{
					LargePage->LinkAfter(LastInsertPoint);
				}
			}
#endif
		}
	}
	else
	{
		CachedOSPageAllocator.Free(Ptr, Size, Mutex);
	}
}

void FCachedOSVeryLargePageAllocator::FreeAll(FCriticalSection* Mutex)
{
	CachedOSPageAllocator.FreeAll(Mutex);
}
#endif