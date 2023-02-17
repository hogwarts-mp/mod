// Copyright Epic Games, Inc. All Rights Reserved.

#include "HAL/MallocBinned2.h"
#include "Logging/LogMacros.h"
#include "Misc/ScopeLock.h"
#include "Templates/Function.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "Stats/Stats.h"
#include "HAL/IConsoleManager.h"
#include "HAL/MemoryMisc.h"
#include "HAL/PlatformMisc.h"
#include "Misc/App.h"
#include "HAL/MallocTimer.h"
#include "ProfilingDebugging/CsvProfiler.h"
#if CSV_PROFILER
CSV_DEFINE_CATEGORY_MODULE(CORE_API, FMemory, true);
#endif

PRAGMA_DISABLE_UNSAFE_TYPECAST_WARNINGS

#if BINNED2_ALLOW_RUNTIME_TWEAKING

int32 GMallocBinned2PerThreadCaches = DEFAULT_GMallocBinned2PerThreadCaches;
static FAutoConsoleVariableRef GMallocBinned2PerThreadCachesCVar(
	TEXT("MallocBinned2.PerThreadCaches"),
	GMallocBinned2PerThreadCaches,
	TEXT("Enables per-thread caches of small (<= 32768 byte) allocations from FMallocBinned2")
	);

int32 GMallocBinned2BundleSize = DEFAULT_GMallocBinned2BundleSize;
static FAutoConsoleVariableRef GMallocBinned2BundleSizeCVar(
	TEXT("MallocBinned2.BundleSize"),
	GMallocBinned2BundleSize,
	TEXT("Max size in bytes of per-block bundles used in the recycling process")
	);

int32 GMallocBinned2BundleCount = DEFAULT_GMallocBinned2BundleCount;
static FAutoConsoleVariableRef GMallocBinned2BundleCountCVar(
	TEXT("MallocBinned2.BundleCount"),
	GMallocBinned2BundleCount,
	TEXT("Max count in blocks per-block bundles used in the recycling process")
	);

int32 GMallocBinned2MaxBundlesBeforeRecycle = BINNED2_MAX_GMallocBinned2MaxBundlesBeforeRecycle;
static FAutoConsoleVariableRef GMallocBinned2MaxBundlesBeforeRecycleCVar(
	TEXT("MallocBinned2.BundleRecycleCount"),
	GMallocBinned2MaxBundlesBeforeRecycle,
	TEXT("Number of freed bundles in the global recycler before it returns them to the system, per-block size. Limited by BINNED2_MAX_GMallocBinned2MaxBundlesBeforeRecycle (currently 4)")
	);

int32 GMallocBinned2AllocExtra = DEFAULT_GMallocBinned2AllocExtra;
static FAutoConsoleVariableRef GMallocBinned2AllocExtraCVar(
	TEXT("MallocBinned2.AllocExtra"),
	GMallocBinned2AllocExtra,
	TEXT("When we do acquire the lock, how many blocks cached in TLS caches. In no case will we grab more than a page.")
	);

#endif

float GMallocBinned2FlushThreadCacheMaxWaitTime = 0.02f;
static FAutoConsoleVariableRef GMallocBinned2FlushThreadCacheMaxWaitTimeCVar(
	TEXT("MallocBinned2.FlushThreadCacheMaxWaitTime"),
	GMallocBinned2FlushThreadCacheMaxWaitTime,
	TEXT("The threshold of time before warning about FlushCurrentThreadCache taking too long (seconds)."),
	ECVF_ReadOnly
);

#if BINNED2_ALLOCATOR_STATS
TAtomic<int64> AllocatedSmallPoolMemory(0); // memory that's requested to be allocated by the game
TAtomic<int64> AllocatedOSSmallPoolMemory(0);

TAtomic<int64> AllocatedLargePoolMemory(0); // memory requests to the OS which don't fit in the small pool
TAtomic<int64> AllocatedLargePoolMemoryWAlignment(0); // when we allocate at OS level we need to align to a size

int64 Binned2PoolInfoMemory = 0;
int64 Binned2HashMemory = 0;
int64 Binned2TLSMemory = 0;
#endif

#if BINNED2_ALLOCATOR_STATS_VALIDATION
int64 AllocatedSmallPoolMemoryValidation = 0;
FCriticalSection ValidationCriticalSection;
int32 RecursionCounter = 0;
#endif

// Block sizes are based around getting the maximum amount of allocations per pool, with as little alignment waste as possible.
// Block sizes should be close to even divisors of the system page size, and well distributed.
// They must be 16-byte aligned as well.
static uint16 SmallBlockSizes[] =
{
	16, 32, 48, 64, 80, 96, 112, 128,
	160, 192, 224, 256, 288, 320, 384, 448,
	512, 576, 640, 704, 768, 896, 1024 - 16, 1168,
	1360, 1632, 2048 - 16, 2336, 2720, 3264, 4096 - 16, 4368,
	4672, 5040, 5456, 5952, 6544 - 16, 7280, 8192 - 16, 9360,
	10912, 13104, 16384 - 16, 21840, 32768 - 16
};

MS_ALIGN(PLATFORM_CACHE_LINE_SIZE) static uint8 UnusedAlignPadding[PLATFORM_CACHE_LINE_SIZE] GCC_ALIGN(PLATFORM_CACHE_LINE_SIZE) = { 0 };
uint16 FMallocBinned2::SmallBlockSizesReversed[BINNED2_SMALL_POOL_COUNT] = { 0 };
uint32 FMallocBinned2::Binned2TlsSlot = 0;
uint32 FMallocBinned2::OsAllocationGranularity = 0;
uint32 FMallocBinned2::PageSize = 0;
FMallocBinned2* FMallocBinned2::MallocBinned2 = nullptr;
// Mapping of sizes to small table indices
uint8 FMallocBinned2::MemSizeToIndex[1 + (BINNED2_MAX_SMALL_POOL_SIZE >> BINNED2_MINIMUM_ALIGNMENT_SHIFT)] = { 0 };

FMallocBinned2::FPoolList::FPoolList()
	: Front(nullptr)
{
}

FMallocBinned2::FPoolTable::FPoolTable()
	: BlockSize(0)
{
}

struct FMallocBinned2::FPoolInfo
{
	static FORCEINLINE bool IsSupportedSize(SIZE_T Size)
	{
		bool bResult = IsAligned(Size, BINNED2_MINIMUM_ALIGNMENT);
		bResult = bResult && (Size >> BINNED2_MINIMUM_ALIGNMENT_SHIFT) <= SIZE_T(MAX_uint32);
		return bResult;
	}

	enum class ECanary : uint16
	{
		Unassigned = 0x3941,
		FirstFreeBlockIsOSAllocSize = 0x17ea,
		FirstFreeBlockIsPtr = 0xf317
	};

 public:	uint16      Taken;          // Number of allocated elements in this pool, when counts down to zero can free the entire pool	
public:	ECanary		Canary;	// See ECanary
private:	uint32      AllocSize;      // Number of bytes allocated
 public:	FFreeBlock* FirstFreeBlock; // Pointer to first free memory in this pool or the OS Allocation Size in bytes if this allocation is not binned
 public:	FPoolInfo*  Next;           // Pointer to next pool
 public:	FPoolInfo** PtrToPrevNext;  // Pointer to whichever pointer points to this pool

#if PLATFORM_32BITS
/** Explicit padding for 32 bit builds */
private: uint8 Padding[12]; // 32
#endif

public:
	FPoolInfo()
		: Taken(0)
		, Canary(ECanary::Unassigned)
		, AllocSize(0)
		, FirstFreeBlock(nullptr)
		, Next(nullptr)
		, PtrToPrevNext(nullptr)
	{
	}
	void CheckCanary(ECanary ShouldBe) const
	{
		if (Canary != ShouldBe)
		{
			UE_LOG(LogMemory, Fatal, TEXT("MallocBinned2 Corruption Canary was 0x%x, should be 0x%x"), int32(Canary), int32(ShouldBe));
		}
	}
	void SetCanary(ECanary ShouldBe, bool bPreexisting, bool bGuaranteedToBeNew)
	{
		if (bPreexisting)
		{
			if (bGuaranteedToBeNew)
			{
				UE_LOG(LogMemory, Fatal, TEXT("MallocBinned2 Corruption Canary was 0x%x, should be 0x%x. This block is both preexisting and guaranteed to be new; which makes no sense."), int32(Canary), int32(ShouldBe));
			}
			if (ShouldBe == ECanary::Unassigned)
			{
				if (Canary != ECanary::FirstFreeBlockIsOSAllocSize && Canary != ECanary::FirstFreeBlockIsPtr)
				{
					UE_LOG(LogMemory, Fatal, TEXT("MallocBinned2 Corruption Canary was 0x%x, will be 0x%x because this block should be preexisting and in use."), int32(Canary), int32(ShouldBe));
				}
			}
			else if (Canary != ShouldBe)
			{
				UE_LOG(LogMemory, Fatal, TEXT("MallocBinned2 Corruption Canary was 0x%x, should be 0x%x because this block should be preexisting."), int32(Canary), int32(ShouldBe));
			}
		}
		else
		{
			if (bGuaranteedToBeNew)
			{
				if (Canary != ECanary::Unassigned)
				{
					UE_LOG(LogMemory, Fatal, TEXT("MallocBinned2 Corruption Canary was 0x%x, will be 0x%x. This block is guaranteed to be new yet is it already assigned."), int32(Canary), int32(ShouldBe));
				}
			}
			else if (Canary != ShouldBe && Canary != ECanary::Unassigned)
			{
				UE_LOG(LogMemory, Fatal, TEXT("MallocBinned2 Corruption Canary was 0x%x, will be 0x%x does not have an expected value."), int32(Canary), int32(ShouldBe));
			}
		}
		Canary = ShouldBe;
	}
	bool HasFreeRegularBlock() const
	{
		CheckCanary(ECanary::FirstFreeBlockIsPtr);
		return FirstFreeBlock && FirstFreeBlock->GetNumFreeRegularBlocks() != 0;
	}

	void* AllocateRegularBlock()
	{
		check(HasFreeRegularBlock());
		++Taken;
		void* Result = FirstFreeBlock->AllocateRegularBlock();
		ExhaustPoolIfNecessary();
		return Result;
	}

	SIZE_T GetOSRequestedBytes() const
	{
		return SIZE_T(AllocSize) << BINNED2_MINIMUM_ALIGNMENT_SHIFT;
	}

	UPTRINT GetOsAllocatedBytes() const
	{
		CheckCanary(ECanary::FirstFreeBlockIsOSAllocSize);
		return (UPTRINT)FirstFreeBlock;
	}

	void SetOSAllocationSizes(SIZE_T InRequestedBytes, UPTRINT InAllocatedBytes)
	{
		CheckCanary(ECanary::FirstFreeBlockIsOSAllocSize);
		checkSlow(InRequestedBytes != 0);                // Shouldn't be pooling zero byte allocations
		checkSlow(InAllocatedBytes >= InRequestedBytes); // We must be allocating at least as much as we requested
		checkSlow(IsSupportedSize(InRequestedBytes));    // We must be allocating a size we can store

		AllocSize      = uint32(InRequestedBytes >> BINNED2_MINIMUM_ALIGNMENT_SHIFT);
		FirstFreeBlock = (FFreeBlock*)InAllocatedBytes;
	}

	void Link(FPoolInfo*& PrevNext)
	{
		if (PrevNext)
		{
			PrevNext->PtrToPrevNext = &Next;
		}
		Next          = PrevNext;
		PtrToPrevNext = &PrevNext;
		PrevNext      = this;
	}

	void Unlink()
	{
		if (Next)
		{
			Next->PtrToPrevNext = PtrToPrevNext;
		}
		*PtrToPrevNext = Next;
	}

private:
	void ExhaustPoolIfNecessary()
	{
		if (FirstFreeBlock->GetNumFreeRegularBlocks() == 0)// && (FirstFreeBlock->GetShortBlockSize() == 0 || !FirstFreeBlock->HasFreeShortBlock()))
		{
			FirstFreeBlock = (FFreeBlock*)FirstFreeBlock->NextFreeBlock;
		}
		check(!FirstFreeBlock || FirstFreeBlock->GetNumFreeRegularBlocks() != 0);
	}
};



/** Hash table struct for retrieving allocation book keeping information */
struct FMallocBinned2::PoolHashBucket
{
	UPTRINT         BucketIndex;
	FPoolInfo*      FirstPool;
	PoolHashBucket* Prev;
	PoolHashBucket* Next;

	PoolHashBucket()
	{
		BucketIndex = 0;
		FirstPool   = nullptr;
		Prev        = this;
		Next        = this;
	}

	void Link(PoolHashBucket* After)
	{
		After->Prev = Prev;
		After->Next = this;
		Prev ->Next = After;
		this ->Prev = After;
	}

	void Unlink()
	{
		Next->Prev = Prev;
		Prev->Next = Next;
		Prev       = this;
		Next       = this;
	}
};



struct FMallocBinned2::Private
{
	// Implementation. 
	static CA_NO_RETURN void OutOfMemory(uint64 Size, uint32 Alignment=0)
	{
		// this is expected not to return
		FPlatformMemory::OnOutOfMemory(Size, Alignment);
	}

	/**
	 * Gets the FPoolInfo for a memory address. If no valid info exists one is created.
	 */
	static FPoolInfo* GetOrCreatePoolInfo(FMallocBinned2& Allocator, void* InPtr, FMallocBinned2::FPoolInfo::ECanary Kind, bool bPreexisting)
	{
		/** 
		 * Creates an array of FPoolInfo structures for tracking allocations.
		 */
		auto CreatePoolArray = [](uint64 NumPools)
		{
			uint64 PoolArraySize = NumPools * sizeof(FPoolInfo);

			void* Result;
			{
				LLM_PLATFORM_SCOPE(ELLMTag::FMalloc);
				Result = FPlatformMemory::BinnedAllocFromOS(PoolArraySize);
#if BINNED2_ALLOCATOR_STATS
				Binned2PoolInfoMemory += PoolArraySize;
#endif
			}

			if (!Result)
			{
				OutOfMemory(PoolArraySize);
			}

			DefaultConstructItems<FPoolInfo>(Result, NumPools);
			return (FPoolInfo*)Result;
		};

		uint32 BucketIndex;
		UPTRINT BucketIndexCollision;
		uint32  PoolIndex;
		Allocator.PtrToPoolMapping.GetHashBucketAndPoolIndices(InPtr, BucketIndex, BucketIndexCollision, PoolIndex);

		PoolHashBucket* FirstBucket = &Allocator.HashBuckets[BucketIndex];
		PoolHashBucket* Collision   = FirstBucket;
		do
		{
			if (!Collision->FirstPool)
			{
				Collision->BucketIndex = BucketIndexCollision;
				Collision->FirstPool   = CreatePoolArray(Allocator.NumPoolsPerPage);
				Collision->FirstPool[PoolIndex].SetCanary(Kind, bPreexisting, true);
				return &Collision->FirstPool[PoolIndex];
			}

			if (Collision->BucketIndex == BucketIndexCollision)
			{
				Collision->FirstPool[PoolIndex].SetCanary(Kind, bPreexisting, false);
				return &Collision->FirstPool[PoolIndex];
			}

			Collision = Collision->Next;
		}
		while (Collision != FirstBucket);

		// Create a new hash bucket entry
		if (!Allocator.HashBucketFreeList)
		{
			{
				LLM_PLATFORM_SCOPE(ELLMTag::FMalloc);
				Allocator.HashBucketFreeList = (PoolHashBucket*)FPlatformMemory::BinnedAllocFromOS(FMallocBinned2::PageSize);
#if BINNED2_ALLOCATOR_STATS
				Binned2HashMemory += FMallocBinned2::PageSize;
#endif
			}

			for (UPTRINT i = 0, n = PageSize / sizeof(PoolHashBucket); i < n; ++i)
			{
				Allocator.HashBucketFreeList->Link(new (Allocator.HashBucketFreeList + i) PoolHashBucket());
			}
		}

		PoolHashBucket* NextFree  = Allocator.HashBucketFreeList->Next;
		PoolHashBucket* NewBucket = Allocator.HashBucketFreeList;

		NewBucket->Unlink();

		if (NextFree == NewBucket)
		{
			NextFree = nullptr;
		}
		Allocator.HashBucketFreeList = NextFree;

		if (!NewBucket->FirstPool)
		{
			NewBucket->FirstPool = CreatePoolArray(Allocator.NumPoolsPerPage);
			NewBucket->FirstPool[PoolIndex].SetCanary(Kind, bPreexisting, true);
		}
		else
		{
			NewBucket->FirstPool[PoolIndex].SetCanary(Kind, bPreexisting, false);
		}

		NewBucket->BucketIndex = BucketIndexCollision;

		FirstBucket->Link(NewBucket);

		return &NewBucket->FirstPool[PoolIndex];
	}

	static FPoolInfo* FindPoolInfo(FMallocBinned2& Allocator, void* InPtr)
	{
		uint32 BucketIndex;
		UPTRINT BucketIndexCollision;
		uint32  PoolIndex;
		Allocator.PtrToPoolMapping.GetHashBucketAndPoolIndices(InPtr, BucketIndex, BucketIndexCollision, PoolIndex);


		PoolHashBucket* FirstBucket = &Allocator.HashBuckets[BucketIndex];
		PoolHashBucket* Collision   = FirstBucket;
		do
		{
			if (Collision->BucketIndex == BucketIndexCollision)
			{
				return &Collision->FirstPool[PoolIndex];
			}

			Collision = Collision->Next;
		}
		while (Collision != FirstBucket);

		return nullptr;
	}

	struct FGlobalRecycler
	{
		bool PushBundle(uint32 InPoolIndex, FBundleNode* InBundle)
		{
			uint32 NumCachedBundles = FMath::Min<uint32>(GMallocBinned2MaxBundlesBeforeRecycle, BINNED2_MAX_GMallocBinned2MaxBundlesBeforeRecycle);
			for (uint32 Slot = 0; Slot < NumCachedBundles; Slot++)
			{
				if (!Bundles[InPoolIndex].FreeBundles[Slot])
				{
					if (!FPlatformAtomics::InterlockedCompareExchangePointer((void**)&Bundles[InPoolIndex].FreeBundles[Slot], InBundle, nullptr))
					{
						return true;
					}
				}
			}
			return false;
		}

		FBundleNode* PopBundle(uint32 InPoolIndex)
		{
			uint32 NumCachedBundles = FMath::Min<uint32>(GMallocBinned2MaxBundlesBeforeRecycle, BINNED2_MAX_GMallocBinned2MaxBundlesBeforeRecycle);
			for (uint32 Slot = 0; Slot < NumCachedBundles; Slot++)
			{
				FBundleNode* Result = Bundles[InPoolIndex].FreeBundles[Slot];
				if (Result)
				{
					if (FPlatformAtomics::InterlockedCompareExchangePointer((void**)&Bundles[InPoolIndex].FreeBundles[Slot], nullptr, Result) == Result)
					{
						return Result;
					}
				}
			}
			return nullptr;
		}

	private:
		struct FPaddedBundlePointer
		{
			FBundleNode* FreeBundles[BINNED2_MAX_GMallocBinned2MaxBundlesBeforeRecycle];
#define BUNDLE_PADDING (PLATFORM_CACHE_LINE_SIZE - sizeof(FBundleNode*) * BINNED2_MAX_GMallocBinned2MaxBundlesBeforeRecycle)
#if (4 + (4 * PLATFORM_64BITS)) * BINNED2_MAX_GMallocBinned2MaxBundlesBeforeRecycle < PLATFORM_CACHE_LINE_SIZE
			uint8 Padding[BUNDLE_PADDING];
#endif
			FPaddedBundlePointer()
			{
				DefaultConstructItems<FBundleNode*>(FreeBundles, BINNED2_MAX_GMallocBinned2MaxBundlesBeforeRecycle);
			}
		};
		static_assert(sizeof(FPaddedBundlePointer) == PLATFORM_CACHE_LINE_SIZE, "FPaddedBundlePointer should be the same size as a cache line");
		MS_ALIGN(PLATFORM_CACHE_LINE_SIZE) FPaddedBundlePointer Bundles[BINNED2_SMALL_POOL_COUNT] GCC_ALIGN(PLATFORM_CACHE_LINE_SIZE);
	};

	static FGlobalRecycler GGlobalRecycler;

	static void FreeBundles(FMallocBinned2& Allocator, FBundleNode* BundlesToRecycle, uint32 InBlockSize, uint32 InPoolIndex)
	{
		FPoolTable& Table = Allocator.SmallPoolTables[InPoolIndex];

		FBundleNode* Bundle = BundlesToRecycle;
		while (Bundle)
		{
			FBundleNode* NextBundle = Bundle->NextBundle;

			FBundleNode* Node = Bundle;
			do
			{
				FBundleNode* NextNode = Node->NextNodeInCurrentBundle;
				FPoolInfo*   NodePool = FindPoolInfo(Allocator, Node);
				if (!NodePool)
				{
					UE_LOG(LogMemory, Fatal, TEXT("FMallocBinned2 Attempt to free an unrecognized small block %p"), Node);
				}
				NodePool->CheckCanary(FPoolInfo::ECanary::FirstFreeBlockIsPtr);

				// If this pool was exhausted, move to available list.
				if (!NodePool->FirstFreeBlock)
				{
					Table.ActivePools.LinkToFront(NodePool);
				}
				else
				{
					check(NodePool->FirstFreeBlock->Canary == 0 || NodePool->FirstFreeBlock->IsCanaryOk());
				}

				// Free a pooled allocation.
				FFreeBlock* Free = (FFreeBlock*)Node;
				Free->NumFreeBlocks = 1;
				Free->NextFreeBlock = NodePool->FirstFreeBlock;
				Free->BlockSize     = InBlockSize;
				Free->Canary = FFreeBlock::CANARY_VALUE;
				Free->PoolIndex = InPoolIndex;
				NodePool->FirstFreeBlock   = Free;

				// Free this pool.
				check(NodePool->Taken >= 1);
				if (--NodePool->Taken == 0)
				{
					NodePool->SetCanary(FPoolInfo::ECanary::Unassigned, true, false);
					FFreeBlock* BasePtrOfNode = GetPoolHeaderFromPointer(Node);

					// Free the OS memory.
					NodePool->Unlink();
					Allocator.CachedOSPageAllocator.Free(BasePtrOfNode, Allocator.PageSize);
#if BINNED2_ALLOCATOR_STATS
					AllocatedOSSmallPoolMemory -= ((int64)Allocator.PageSize);
#endif
				}

				Node = NextNode;
			} while (Node);

			Bundle = NextBundle;
		}
	}


	static FCriticalSection& GetFreeBlockListsRegistrationMutex()
	{
		static FCriticalSection FreeBlockListsRegistrationMutex;
		return FreeBlockListsRegistrationMutex;
	}
	static TArray<FPerThreadFreeBlockLists*>& GetRegisteredFreeBlockLists()
	{
		static TArray<FPerThreadFreeBlockLists*> RegisteredFreeBlockLists;
		return RegisteredFreeBlockLists;
	}
	static void RegisterThreadFreeBlockLists( FPerThreadFreeBlockLists* FreeBlockLists )
	{
		FScopeLock Lock(&GetFreeBlockListsRegistrationMutex());
#if BINNED2_ALLOCATOR_STATS_VALIDATION
		++RecursionCounter;
#endif
		GetRegisteredFreeBlockLists().Add(FreeBlockLists);
#if BINNED2_ALLOCATOR_STATS_VALIDATION
		--RecursionCounter;
#endif
	}
	static void UnregisterThreadFreeBlockLists( FPerThreadFreeBlockLists* FreeBlockLists )
	{
		FScopeLock Lock(&GetFreeBlockListsRegistrationMutex());
#if BINNED2_ALLOCATOR_STATS_VALIDATION
		++RecursionCounter;
#endif
		GetRegisteredFreeBlockLists().Remove(FreeBlockLists);
#if BINNED2_ALLOCATOR_STATS_VALIDATION
		--RecursionCounter;
#endif
#if BINNED2_ALLOCATOR_STATS
		FMallocBinned2::FPerThreadFreeBlockLists::ConsolidatedMemory += FreeBlockLists->AllocatedMemory;
#endif
	}
};

FMallocBinned2::Private::FGlobalRecycler FMallocBinned2::Private::GGlobalRecycler;

#if BINNED2_ALLOCATOR_STATS
int64 FMallocBinned2::FPerThreadFreeBlockLists::ConsolidatedMemory = 0;
#endif

FORCEINLINE bool FMallocBinned2::FPoolList::IsEmpty() const
{
	return Front == nullptr;
}

FORCEINLINE FMallocBinned2::FPoolInfo& FMallocBinned2::FPoolList::GetFrontPool()
{
	check(!IsEmpty());
	return *Front;
}

FORCEINLINE const FMallocBinned2::FPoolInfo& FMallocBinned2::FPoolList::GetFrontPool() const
{
	check(!IsEmpty());
	return *Front;
}

void FMallocBinned2::FPoolList::LinkToFront(FPoolInfo* Pool)
{
	Pool->Unlink();
	Pool->Link(Front);
}

FMallocBinned2::FPoolInfo& FMallocBinned2::FPoolList::PushNewPoolToFront(FMallocBinned2& Allocator, uint32 InBlockSize, uint32 InPoolIndex)
{
	const uint32 LocalPageSize = Allocator.PageSize;

	// Allocate memory.
	void* FreePtr = Allocator.CachedOSPageAllocator.Allocate(LocalPageSize, FMemory::AllocationHints::SmallPool);
	if (!FreePtr)
	{
		Private::OutOfMemory(LocalPageSize);
	}
	FFreeBlock* Free = new (FreePtr) FFreeBlock(LocalPageSize, InBlockSize, InPoolIndex);
#if BINNED2_ALLOCATOR_STATS
	AllocatedOSSmallPoolMemory += (int64)LocalPageSize;
#endif
	check(IsAligned(Free, LocalPageSize));
	// Create pool
	FPoolInfo* Result = Private::GetOrCreatePoolInfo(Allocator, Free, FPoolInfo::ECanary::FirstFreeBlockIsPtr, false);
	Result->Link(Front);
	Result->Taken          = 0;
	Result->FirstFreeBlock = Free;

	return *Result;
}

FMallocBinned2::FMallocBinned2()
	: HashBucketFreeList(nullptr)
{
	static bool bOnce = false;
	check(!bOnce); // this is now a singleton-like thing and you cannot make multiple copies
	bOnce = true;

	for (uint32 Index = 0; Index != BINNED2_SMALL_POOL_COUNT; ++Index)
	{
		uint32 Partner = BINNED2_SMALL_POOL_COUNT - Index - 1;
		SmallBlockSizesReversed[Index] = SmallBlockSizes[Partner];
	}
	FGenericPlatformMemoryConstants Constants = FPlatformMemory::GetConstants();
	PageSize = Constants.BinnedPageSize;
	OsAllocationGranularity = Constants.BinnedAllocationGranularity ? Constants.BinnedAllocationGranularity : PageSize;
	NumPoolsPerPage = PageSize / sizeof(FPoolInfo);
	PtrToPoolMapping.Init(PageSize, NumPoolsPerPage, Constants.AddressLimit);

	checkf(FMath::IsPowerOfTwo(PageSize), TEXT("OS page size must be a power of two"));
	checkf(FMath::IsPowerOfTwo(Constants.AddressLimit), TEXT("OS address limit must be a power of two"));
	checkf(Constants.AddressLimit > PageSize, TEXT("OS address limit must be greater than the page size")); // Check to catch 32 bit overflow in AddressLimit
	checkf(SmallBlockSizes[BINNED2_SMALL_POOL_COUNT - 1] == BINNED2_MAX_SMALL_POOL_SIZE, TEXT("BINNED2_MAX_SMALL_POOL_SIZE must equal the smallest block size"));
	checkf(PageSize % BINNED2_LARGE_ALLOC == 0, TEXT("OS page size must be a multiple of BINNED2_LARGE_ALLOC"));
	checkf(sizeof(FMallocBinned2::FFreeBlock) <= SmallBlockSizes[0], TEXT("Pool header must be able to fit into the smallest block"));
	static_assert(UE_ARRAY_COUNT(SmallBlockSizes) == BINNED2_SMALL_POOL_COUNT, "Small block size array size must match BINNED2_SMALL_POOL_COUNT");
	static_assert(UE_ARRAY_COUNT(SmallBlockSizes) <= 256, "Small block size array size must fit in a byte");
	static_assert(sizeof(FFreeBlock) <= BINNED2_MINIMUM_ALIGNMENT, "Free block struct must be small enough to fit into a block.");

	// Init pool tables.
	for (uint32 Index = 0; Index != BINNED2_SMALL_POOL_COUNT; ++Index)
	{
		checkf(Index == 0 || SmallBlockSizes[Index - 1] < SmallBlockSizes[Index], TEXT("Small block sizes must be strictly increasing"));
		checkf(SmallBlockSizes[Index] <= PageSize, TEXT("Small block size must be small enough to fit into a page"));
		checkf(SmallBlockSizes[Index] % BINNED2_MINIMUM_ALIGNMENT == 0, TEXT("Small block size must be a multiple of BINNED2_MINIMUM_ALIGNMENT"));

		SmallPoolTables[Index].BlockSize = SmallBlockSizes[Index];
	}

	// Set up pool mappings
	uint8* IndexEntry = MemSizeToIndex;
	uint32  PoolIndex  = 0;
	for (uint32 Index = 0; Index != 1 + (BINNED2_MAX_SMALL_POOL_SIZE >> BINNED2_MINIMUM_ALIGNMENT_SHIFT); ++Index)
	{
		
		uint32 BlockSize = Index << BINNED2_MINIMUM_ALIGNMENT_SHIFT; // inverse of int32 Index = int32((Size >> BINNED2_MINIMUM_ALIGNMENT_SHIFT));
		while (SmallBlockSizes[PoolIndex] < BlockSize)
		{
			++PoolIndex;
			check(PoolIndex != BINNED2_SMALL_POOL_COUNT);
		}
		check(PoolIndex < 256);
		*IndexEntry++ = uint8(PoolIndex);
	}
	// now reverse the pool sizes for cache coherency

	for (uint32 Index = 0; Index != BINNED2_SMALL_POOL_COUNT; ++Index)
	{
		uint32 Partner = BINNED2_SMALL_POOL_COUNT - Index - 1;
		SmallBlockSizesReversed[Index] = SmallBlockSizes[Partner];
	}

	uint64 MaxHashBuckets = PtrToPoolMapping.GetMaxHashBuckets();

	{
		LLM_PLATFORM_SCOPE(ELLMTag::FMalloc);
		HashBuckets = (PoolHashBucket*)FPlatformMemory::BinnedAllocFromOS(Align(MaxHashBuckets * sizeof(PoolHashBucket), OsAllocationGranularity));
#if BINNED2_ALLOCATOR_STATS
		Binned2HashMemory += Align(MaxHashBuckets * sizeof(PoolHashBucket), OsAllocationGranularity);
#endif
	}

	DefaultConstructItems<PoolHashBucket>(HashBuckets, MaxHashBuckets);
	MallocBinned2 = this;
	GFixedMallocLocationPtr = (FMalloc**)(&MallocBinned2);
}

FMallocBinned2::~FMallocBinned2()
{
}

bool FMallocBinned2::IsInternallyThreadSafe() const
{ 
	return true;
}

void* FMallocBinned2::MallocExternalSmall(SIZE_T Size, uint32 Alignment)
{
	uint32 PoolIndex = BoundSizeToPoolIndex(Size);
	FPerThreadFreeBlockLists* Lists = GMallocBinned2PerThreadCaches ? FPerThreadFreeBlockLists::Get() : nullptr;
	if (Lists)
	{
		if (Lists->ObtainRecycledPartial(PoolIndex))
		{
			if (void* Result = Lists->Malloc(PoolIndex))
			{
#if BINNED2_ALLOCATOR_STATS
				uint32 BlockSize = PoolIndexToBlockSize(PoolIndex);
				Lists->AllocatedMemory += BlockSize;
#endif
				return Result;
			}
		}
	}

	FScopeLock Lock(&Mutex);

	// Allocate from small object pool.
	FPoolTable& Table = SmallPoolTables[PoolIndex];

	FPoolInfo* Pool;
	if (!Table.ActivePools.IsEmpty())
	{
		Pool = &Table.ActivePools.GetFrontPool();
	}
	else
	{
		Pool = &Table.ActivePools.PushNewPoolToFront(*this, Table.BlockSize, PoolIndex);
	}

	void* Result = Pool->AllocateRegularBlock();
#if BINNED2_ALLOCATOR_STATS
	AllocatedSmallPoolMemory += PoolIndexToBlockSize(PoolIndex);
#endif // BINNED2_ALLOCATOR_STATS
	if (GMallocBinned2AllocExtra)
	{
		if (Lists)
		{
			// prefill the free list with some allocations so we are less likely to hit this slow path with the mutex 
			for (int32 Index = 0; Index < GMallocBinned2AllocExtra && Pool->HasFreeRegularBlock(); Index++)
			{
				if (!Lists->Free(Result, PoolIndex, Table.BlockSize))
				{
					break;
				}
				Result = Pool->AllocateRegularBlock();
			}
		}
	}
	if (!Pool->HasFreeRegularBlock())
	{
		Table.ExhaustedPools.LinkToFront(Pool);
	}

	return Result;
}

void* FMallocBinned2::MallocExternalLarge(SIZE_T Size, uint32 Alignment)
{
	static_assert(DEFAULT_ALIGNMENT <= BINNED2_MINIMUM_ALIGNMENT, "DEFAULT_ALIGNMENT is assumed to be zero"); // used below

	Alignment = FMath::Max<uint32>(Alignment, BINNED2_MINIMUM_ALIGNMENT);
	Size = Align(FMath::Max((SIZE_T)1, Size), Alignment);

	checkf(FMath::IsPowerOfTwo(Alignment), TEXT("Invalid Malloc alignment: '%u' is not a power of two"), Alignment);
	checkf(Alignment <= PageSize, TEXT("Invalid Malloc alignment: '%u' is greater than the page size '%u'"), Alignment, PageSize);
	checkf(FMallocBinned2::FPoolInfo::IsSupportedSize(Size), TEXT("Invalid Malloc size: '%" SIZE_T_FMT "'"), Size);

	UPTRINT AlignedSize = Align(Size, OsAllocationGranularity);

	FPoolInfo* Pool;
	void*      Result;
	{
		FScopeLock Lock(&Mutex);

		// Use OS for non-pooled allocations.
		Result = CachedOSPageAllocator.Allocate(AlignedSize, 0, &Mutex);
		if (!Result)
		{
			Private::OutOfMemory(AlignedSize);
		}

		Pool = Private::GetOrCreatePoolInfo(*this, Result, FPoolInfo::ECanary::FirstFreeBlockIsOSAllocSize, false);
	}

	UE_CLOG(!IsAligned(Result, Alignment) ,LogMemory, Fatal, TEXT("FMallocBinned2 alignment was too large for OS. Alignment=%d   Ptr=%p"), Alignment, Result);
	check(IsAligned(Result, PageSize) && IsOSAllocation(Result));

#if BINNED2_ALLOCATOR_STATS
	AllocatedLargePoolMemory += (int64)Size;
	AllocatedLargePoolMemoryWAlignment += AlignedSize;
#endif

	// Create pool.
	check(Size > 0 && Size <= AlignedSize && AlignedSize >= OsAllocationGranularity);
	Pool->SetOSAllocationSizes(Size, AlignedSize);

	return Result;
}

void* FMallocBinned2::ReallocExternal(void* Ptr, SIZE_T NewSize, uint32 Alignment)
{
	if (NewSize == 0)
	{
		FMallocBinned2::FreeExternal(Ptr);
		return nullptr;
	}

	static_assert(DEFAULT_ALIGNMENT <= BINNED2_MINIMUM_ALIGNMENT, "DEFAULT_ALIGNMENT is assumed to be zero"); // used below

	if (!IsOSAllocation(Ptr))
	{
		check(Ptr); // null is 64k aligned so we should not be here
		// Reallocate to a smaller/bigger pool if necessary
		FFreeBlock* Free = GetPoolHeaderFromPointer(Ptr);
		Free->CanaryTest();
		uint32 BlockSize = Free->BlockSize;
		uint32 PoolIndex = Free->PoolIndex;
		if (
			((NewSize <= BlockSize) & (Alignment <= BINNED2_MINIMUM_ALIGNMENT)) && // one branch, not two
			(PoolIndex == 0 || NewSize > PoolIndexToBlockSize(PoolIndex - 1)))
		{
			return Ptr;
		}

		// Reallocate and copy the data across
		void* Result = FMallocBinned2::MallocSelect(NewSize, Alignment);
		FMemory::Memcpy(Result, Ptr, FMath::Min<SIZE_T>(NewSize, BlockSize));
		FMallocBinned2::FreeExternal(Ptr);
		return Result;
	}
	if (!Ptr)
	{
		void* Result = FMallocBinned2::MallocSelect(NewSize, Alignment);
		return Result;
	}

	// Allocated from OS.
	Mutex.Lock();
	FPoolInfo* Pool = Private::FindPoolInfo(*this, Ptr);
	UE_CLOG(!Pool, LogMemory, Fatal, TEXT("FMallocBinned2 Attempt to realloc an unrecognized block %p"), Ptr);

	UPTRINT PoolOsBytes = Pool->GetOsAllocatedBytes();
	SIZE_T PoolOSRequestedBytes = Pool->GetOSRequestedBytes();
	checkf(PoolOSRequestedBytes <= PoolOsBytes, TEXT("FMallocBinned2::ReallocExternal %d %d"), int32(PoolOSRequestedBytes), int32(PoolOsBytes));
	bool bUseSmallMalloc = UseSmallAlloc(NewSize, Alignment);
	if (NewSize > PoolOsBytes || // can't fit in the old block
		bUseSmallMalloc || // can switch to the small block allocator
		Align(NewSize, OsAllocationGranularity) < PoolOsBytes) // we can get some pages back
	{
		// Grow or shrink.
		void* Result;
		if (bUseSmallMalloc)
		{
			// Unlock before a small alloc, which rarely takes a lock
			Mutex.Unlock();
			Result = MallocExternalSmall(NewSize, Alignment);
		}
		else
		{
			// Unlock after a large alloc, which does take a lock, to save unlocking and re-locking unnecessarily
			Result = MallocExternalLarge(NewSize, Alignment);
			Mutex.Unlock();
		}

		FMemory::Memcpy(Result, Ptr, FMath::Min(NewSize, PoolOSRequestedBytes));
		FMallocBinned2::FreeExternal(Ptr);
		return Result;
	}

	Mutex.Unlock();

	Alignment = FMath::Max<uint32>(Alignment, BINNED2_MINIMUM_ALIGNMENT);
	NewSize = Align(FMath::Max((SIZE_T)1, NewSize), Alignment);

	checkf(FMath::IsPowerOfTwo(Alignment), TEXT("Invalid Realloc alignment: '%u' is not a power of two"), Alignment);
	checkf(Alignment <= PageSize, TEXT("Invalid Realloc alignment: '%u' is greater than the page size '%u'"), Alignment, PageSize);
	checkf(FMallocBinned2::FPoolInfo::IsSupportedSize(NewSize), TEXT("Invalid Realloc size: '%" SIZE_T_FMT "'"), NewSize);

#if BINNED2_ALLOCATOR_STATS
	AllocatedLargePoolMemory += ((int64)NewSize) - ((int64)PoolOSRequestedBytes);
	// don't need to change the AllocatedLargePoolMemoryWAlignment because we didn't reallocate so it's the same size
#endif

	Pool->SetOSAllocationSizes(NewSize, PoolOsBytes);

	return Ptr;
}

void FMallocBinned2::FreeExternal(void* Ptr)
{
	if (!IsOSAllocation(Ptr))
	{
		check(Ptr); // null is 64k aligned so we should not be here
		FFreeBlock* BasePtr = GetPoolHeaderFromPointer(Ptr);
		BasePtr->CanaryTest();
		uint32 BlockSize = BasePtr->BlockSize;
		uint32 PoolIndex = BasePtr->PoolIndex;

		FBundleNode* BundlesToRecycle = nullptr;
		FPerThreadFreeBlockLists* Lists = GMallocBinned2PerThreadCaches ? FPerThreadFreeBlockLists::Get() : nullptr;
		if (Lists)
		{
			BundlesToRecycle = Lists->RecycleFullBundle(BasePtr->PoolIndex);
			bool bPushed = Lists->Free(Ptr, PoolIndex, BlockSize);
			check(bPushed);
#if BINNED2_ALLOCATOR_STATS
			Lists->AllocatedMemory -= BlockSize;
#endif
		}
		else
		{
			BundlesToRecycle = (FBundleNode*)Ptr;
			BundlesToRecycle->NextNodeInCurrentBundle = nullptr;
		}
		if (BundlesToRecycle)
		{
			BundlesToRecycle->NextBundle = nullptr;
			FScopeLock Lock(&Mutex);
			Private::FreeBundles(*this, BundlesToRecycle, BlockSize, PoolIndex);
#if BINNED2_ALLOCATOR_STATS
			if (!Lists)
			{
				// lists track their own stat track them instead in the global stat if we don't have lists
				AllocatedSmallPoolMemory -= ((int64)(BlockSize));
			}
#endif
		}
	}
	else if (Ptr)
	{
		FScopeLock Lock(&Mutex);
		FPoolInfo* Pool = Private::FindPoolInfo(*this, Ptr);
		if (!Pool)
		{
			UE_LOG(LogMemory, Fatal, TEXT("FMallocBinned2 Attempt to free an unrecognized block %p"), Ptr);
		}
		UPTRINT PoolOsBytes = Pool->GetOsAllocatedBytes();
		SIZE_T PoolOSRequestedBytes = Pool->GetOSRequestedBytes();

#if BINNED2_ALLOCATOR_STATS
		AllocatedLargePoolMemory -= ((int64)PoolOSRequestedBytes);
		AllocatedLargePoolMemoryWAlignment -= ((int64)PoolOsBytes);
#endif

		checkf(PoolOSRequestedBytes <= PoolOsBytes, TEXT("FMallocBinned2::FreeExternal %d %d"), int32(PoolOSRequestedBytes), int32(PoolOsBytes));
		Pool->SetCanary(FPoolInfo::ECanary::Unassigned, true, false);
		// Free an OS allocation.
		CachedOSPageAllocator.Free(Ptr, PoolOsBytes, &Mutex);
	}
}

bool FMallocBinned2::GetAllocationSizeExternal(void* Ptr, SIZE_T& SizeOut)
{
	if (!IsOSAllocation(Ptr))
	{
		check(Ptr); // null is 64k aligned so we should not be here
		const FFreeBlock* Free = GetPoolHeaderFromPointer(Ptr);
		Free->CanaryTest();
		uint32 BlockSize = Free->BlockSize;
		SizeOut = BlockSize;
		return true;
	}
	if (!Ptr)
	{
		return false;
	}

	FPoolInfo* Pool;
	{
		FScopeLock Lock(&Mutex);
		Pool = Private::FindPoolInfo(*this, Ptr);
	}

	if (!Pool)
	{
		UE_LOG(LogMemory, Fatal, TEXT("FMallocBinned2 Attempt to GetAllocationSizeExternal an unrecognized block %p"), Ptr);
	}
	UPTRINT PoolOsBytes = Pool->GetOsAllocatedBytes();
	SIZE_T PoolOSRequestedBytes = Pool->GetOSRequestedBytes();
	checkf(PoolOSRequestedBytes <= PoolOsBytes, TEXT("FMallocBinned2::GetAllocationSizeExternal %d %d"), int32(PoolOSRequestedBytes), int32(PoolOsBytes));
	SizeOut = PoolOsBytes;
	return true;
}

void FMallocBinned2::FPoolList::ValidateActivePools()
{
	for (FPoolInfo** PoolPtr = &Front; *PoolPtr; PoolPtr = &(*PoolPtr)->Next)
	{
		FPoolInfo* Pool = *PoolPtr;
		check(Pool->PtrToPrevNext == PoolPtr);
		check(Pool->FirstFreeBlock);
		for (FFreeBlock* Free = Pool->FirstFreeBlock; Free; Free = (FFreeBlock*)Free->NextFreeBlock)
		{
			check(Free->GetNumFreeRegularBlocks() > 0);
		}
	}
}

void FMallocBinned2::FPoolList::ValidateExhaustedPools()
{
	for (FPoolInfo** PoolPtr = &Front; *PoolPtr; PoolPtr = &(*PoolPtr)->Next)
	{
		FPoolInfo* Pool = *PoolPtr;
		check(Pool->PtrToPrevNext == PoolPtr);
		check(!Pool->FirstFreeBlock);
	}
}

bool FMallocBinned2::ValidateHeap()
{
	FScopeLock Lock(&Mutex);

	for (FPoolTable& Table : SmallPoolTables)
	{
		Table.ActivePools.ValidateActivePools();
		Table.ExhaustedPools.ValidateExhaustedPools();
	}

	return true;
}

const TCHAR* FMallocBinned2::GetDescriptiveName()
{
	return TEXT("binned2");
}

void FMallocBinned2::FlushCurrentThreadCache()
{
	double StartTimeInner = FPlatformTime::Seconds();
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FMallocBinned2_FlushCurrentThreadCache);
	FPerThreadFreeBlockLists* Lists = FPerThreadFreeBlockLists::Get();

	double WaitForMutexTime = 0.0;
	double WaitForMutexAndTrimTime = 0.0;

	if (Lists)
	{
		FScopeLock Lock(&Mutex);
		WaitForMutexTime = FPlatformTime::Seconds() - StartTimeInner;
		for (int32 PoolIndex = 0; PoolIndex != BINNED2_SMALL_POOL_COUNT; ++PoolIndex)
		{
			FBundleNode* Bundles = Lists->PopBundles(PoolIndex);
			if (Bundles)
			{
				Private::FreeBundles(*this, Bundles, PoolIndexToBlockSize(PoolIndex), PoolIndex);
			}
		}
		WaitForMutexAndTrimTime = FPlatformTime::Seconds() - StartTimeInner;
	}

	// These logs must happen outside the above mutex to avoid deadlocks
	if (WaitForMutexTime > GMallocBinned2FlushThreadCacheMaxWaitTime)
	{
		UE_LOG(LogMemory, Warning, TEXT("FMallocBinned2 took %6.2fms to wait for mutex for trim."), WaitForMutexTime * 1000.0f);
	}
	if (WaitForMutexAndTrimTime > GMallocBinned2FlushThreadCacheMaxWaitTime)
	{
		UE_LOG(LogMemory, Warning, TEXT("FMallocBinned2 took %6.2fms to wait for mutex AND trim."), WaitForMutexAndTrimTime * 1000.0f);
	}
}

#include "Async/TaskGraphInterfaces.h"

void FMallocBinned2::Trim(bool bTrimThreadCaches)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FMallocBinned2_Trim);

	if (GMallocBinned2PerThreadCaches  &&  bTrimThreadCaches)
	{
		//double StartTime = FPlatformTime::Seconds();
		TFunction<void(ENamedThreads::Type CurrentThread)> Broadcast =
			[this](ENamedThreads::Type MyThread)
		{
			FlushCurrentThreadCache();
		};
		// Skip task threads on desktop platforms as it is too slow and they don't have much memory
		if (PLATFORM_DESKTOP)
		{
			FTaskGraphInterface::BroadcastSlow_OnlyUseForSpecialPurposes(false, false, Broadcast);
		}
		else
		{
			FTaskGraphInterface::BroadcastSlow_OnlyUseForSpecialPurposes(FPlatformProcess::SupportsMultithreading() && FApp::ShouldUseThreadingForPerformance(), false, Broadcast);
		}
		//UE_LOG(LogTemp, Display, TEXT("Trim Broadcast = %6.2fms"), 1000.0f * float(FPlatformTime::Seconds() - StartTime));
	}
	{
		//double StartTime = FPlatformTime::Seconds();
		FScopeLock Lock(&Mutex);
		CachedOSPageAllocator.FreeAll(&Mutex);
		//UE_LOG(LogTemp, Display, TEXT("Trim CachedOSPageAllocator = %6.2fms"), 1000.0f * float(FPlatformTime::Seconds() - StartTime));
	}
}

void FMallocBinned2::SetupTLSCachesOnCurrentThread()
{
	if (!BINNED2_ALLOW_RUNTIME_TWEAKING && !GMallocBinned2PerThreadCaches)
	{
		return;
	}
	if (!FMallocBinned2::Binned2TlsSlot)
	{
		FMallocBinned2::Binned2TlsSlot = FPlatformTLS::AllocTlsSlot();
	}
	check(FMallocBinned2::Binned2TlsSlot);
	FPerThreadFreeBlockLists::SetTLS();
}

void FMallocBinned2::ClearAndDisableTLSCachesOnCurrentThread()
{
	FlushCurrentThreadCache();
	FPerThreadFreeBlockLists::ClearTLS();
}


bool FMallocBinned2::FFreeBlockList::ObtainPartial(uint32 InPoolIndex)
{
	if (!PartialBundle.Head)
	{
		PartialBundle.Count = 0;
		PartialBundle.Head = FMallocBinned2::Private::GGlobalRecycler.PopBundle(InPoolIndex);
		if (PartialBundle.Head)
		{
			PartialBundle.Count = PartialBundle.Head->Count;
			PartialBundle.Head->NextBundle = nullptr;
			return true;
		}
		return false;
	}
	return true;
}

FMallocBinned2::FBundleNode* FMallocBinned2::FFreeBlockList::RecyleFull(uint32 InPoolIndex)
{
	FMallocBinned2::FBundleNode* Result = nullptr;
	if (FullBundle.Head)
	{
		FullBundle.Head->Count = FullBundle.Count;
		if (!FMallocBinned2::Private::GGlobalRecycler.PushBundle(InPoolIndex, FullBundle.Head))
		{
			Result = FullBundle.Head;
			Result->NextBundle = nullptr;
		}
		FullBundle.Reset();
	}
	return Result;
}

FMallocBinned2::FBundleNode* FMallocBinned2::FFreeBlockList::PopBundles(uint32 InPoolIndex)
{
	FBundleNode* Partial = PartialBundle.Head;
	if (Partial)
	{
		PartialBundle.Reset();
		Partial->NextBundle = nullptr;
	}

	FBundleNode* Full = FullBundle.Head;
	if (Full)
	{
		FullBundle.Reset();
		Full->NextBundle = nullptr;
	}

	FBundleNode* Result = Partial;
	if (Result)
	{
		Result->NextBundle = Full;
	}
	else
	{
		Result = Full;
	}

	return Result;
}

void FMallocBinned2::FPerThreadFreeBlockLists::SetTLS()
{
	check(FMallocBinned2::Binned2TlsSlot);
	FPerThreadFreeBlockLists* ThreadSingleton = (FPerThreadFreeBlockLists*)FPlatformTLS::GetTlsValue(FMallocBinned2::Binned2TlsSlot);
	if (!ThreadSingleton)
	{
		LLM_PLATFORM_SCOPE(ELLMTag::FMalloc);
		ThreadSingleton = new (FPlatformMemory::BinnedAllocFromOS(Align(sizeof(FPerThreadFreeBlockLists), FMallocBinned2::OsAllocationGranularity))) FPerThreadFreeBlockLists();
#if BINNED2_ALLOCATOR_STATS
		Binned2TLSMemory += Align(sizeof(FPerThreadFreeBlockLists), FMallocBinned2::OsAllocationGranularity);
#endif
		FPlatformTLS::SetTlsValue(FMallocBinned2::Binned2TlsSlot, ThreadSingleton);
		FMallocBinned2::Private::RegisterThreadFreeBlockLists(ThreadSingleton);
	}
}

void FMallocBinned2::FPerThreadFreeBlockLists::ClearTLS()
{
	check(FMallocBinned2::Binned2TlsSlot);
	FPerThreadFreeBlockLists* ThreadSingleton = (FPerThreadFreeBlockLists*)FPlatformTLS::GetTlsValue(FMallocBinned2::Binned2TlsSlot);
	if ( ThreadSingleton )
	{
		FMallocBinned2::Private::UnregisterThreadFreeBlockLists(ThreadSingleton);
	}
	FPlatformTLS::SetTlsValue(FMallocBinned2::Binned2TlsSlot, nullptr);
}

void FMallocBinned2::FFreeBlock::CanaryFail() const
{
	UE_LOG(LogMemory, Fatal, TEXT("FMallocBinned2 Attempt to realloc an unrecognized block %p   canary == 0x%x != 0x%x"), (void*)this, (int32)Canary, (int32)FMallocBinned2::FFreeBlock::CANARY_VALUE);
}

#if BINNED2_ALLOCATOR_STATS
int64 FMallocBinned2::GetTotalAllocatedSmallPoolMemory() const
{
	int64 FreeBlockAllocatedMemory = 0;
	{
		FScopeLock Lock(&Private::GetFreeBlockListsRegistrationMutex());
		for (const FPerThreadFreeBlockLists* FreeBlockLists : Private::GetRegisteredFreeBlockLists())
		{
			FreeBlockAllocatedMemory += FreeBlockLists->AllocatedMemory;
		}
		FreeBlockAllocatedMemory += FPerThreadFreeBlockLists::ConsolidatedMemory;
	}

	return AllocatedSmallPoolMemory.Load(EMemoryOrder::Relaxed) + FreeBlockAllocatedMemory;
}
#endif

void FMallocBinned2::GetAllocatorStats( FGenericMemoryStats& OutStats )
{
#if BINNED2_ALLOCATOR_STATS

	int64  TotalAllocatedSmallPoolMemory           = GetTotalAllocatedSmallPoolMemory();
	int64  LocalAllocatedOSSmallPoolMemory         = AllocatedOSSmallPoolMemory.Load(EMemoryOrder::Relaxed);
	int64  LocalAllocatedLargePoolMemory           = AllocatedLargePoolMemory.Load(EMemoryOrder::Relaxed);
	int64  LocalAllocatedLargePoolMemoryWAlignment = AllocatedLargePoolMemoryWAlignment.Load(EMemoryOrder::Relaxed);
	uint64 OSPageAllocatorCachedFreeSize           = CachedOSPageAllocator.GetCachedFreeTotal();

	OutStats.Add(TEXT("AllocatedSmallPoolMemory"), TotalAllocatedSmallPoolMemory);
	OutStats.Add(TEXT("AllocatedOSSmallPoolMemory"), LocalAllocatedOSSmallPoolMemory);
	OutStats.Add(TEXT("AllocatedLargePoolMemory"), LocalAllocatedLargePoolMemory);
	OutStats.Add(TEXT("AllocatedLargePoolMemoryWAlignment"), LocalAllocatedLargePoolMemoryWAlignment);
	OutStats.Add(TEXT("PageAllocatorFreeCacheSize"), OSPageAllocatorCachedFreeSize);

	uint64 TotalAllocated = TotalAllocatedSmallPoolMemory + LocalAllocatedLargePoolMemory;
	uint64 TotalOSAllocated = LocalAllocatedOSSmallPoolMemory + LocalAllocatedLargePoolMemoryWAlignment + OSPageAllocatorCachedFreeSize;

	OutStats.Add(TEXT("TotalAllocated"), TotalAllocated);
	OutStats.Add(TEXT("TotalOSAllocated"), TotalOSAllocated);
#endif
	FMalloc::GetAllocatorStats(OutStats);
}

void FMallocBinned2::DumpAllocatorStats(class FOutputDevice& Ar)
{
#if BINNED2_ALLOCATOR_STATS

	int64  TotalAllocatedSmallPoolMemory           = GetTotalAllocatedSmallPoolMemory();
	int64  LocalAllocatedLargePoolMemory           = AllocatedLargePoolMemory.Load(EMemoryOrder::Relaxed);
	int64  LocalAllocatedLargePoolMemoryWAlignment = AllocatedLargePoolMemoryWAlignment.Load(EMemoryOrder::Relaxed);
	uint64 OSPageAllocatorCachedFreeSize           = CachedOSPageAllocator.GetCachedFreeTotal();

	Ar.Logf(TEXT("FMallocBinned2 Mem report"));
	Ar.Logf(TEXT("Constants.BinnedPageSize = %d"), int32(PageSize));
	Ar.Logf(TEXT("Constants.BinnedAllocationGranularity = %d"), int32(OsAllocationGranularity));
	Ar.Logf(TEXT("Small Pool Allocations: %fmb  (including block size padding)"), ((double)TotalAllocatedSmallPoolMemory) / (1024.0f * 1024.0f));
	Ar.Logf(TEXT("Small Pool OS Allocated: %fmb"), ((double)AllocatedOSSmallPoolMemory) / (1024.0f * 1024.0f));
	Ar.Logf(TEXT("Large Pool Requested Allocations: %fmb"), ((double)AllocatedLargePoolMemory) / (1024.0f * 1024.0f));
	Ar.Logf(TEXT("Large Pool OS Allocated: %fmb"), ((double)AllocatedLargePoolMemoryWAlignment) / (1024.0f * 1024.0f));
	Ar.Logf(TEXT("Requested Allocations: %fmb"), ((double)LocalAllocatedLargePoolMemory) / (1024.0f * 1024.0f));
	Ar.Logf(TEXT("OS Allocated: %fmb"), ((double)LocalAllocatedLargePoolMemoryWAlignment) / (1024.0f * 1024.0f));
	Ar.Logf(TEXT("PoolInfo: %fmb"), ((double)Binned2PoolInfoMemory) / (1024.0f * 1024.0f));
	Ar.Logf(TEXT("Hash: %fmb"), ((double)Binned2HashMemory) / (1024.0f * 1024.0f));
	Ar.Logf(TEXT("TLS: %fmb"), ((double)Binned2TLSMemory) / (1024.0f * 1024.0f));
	Ar.Logf(TEXT("Total allocated from OS: %fmb"),
		((double)
			AllocatedOSSmallPoolMemory + AllocatedLargePoolMemoryWAlignment + Binned2PoolInfoMemory + Binned2HashMemory + Binned2TLSMemory
			) / (1024.0f * 1024.0f));
	Ar.Logf(TEXT("Cached free OS pages: %fmb"), ((double)OSPageAllocatorCachedFreeSize) / (1024.0f * 1024.0f));
#else
	Ar.Logf(TEXT("Allocator Stats for binned2 are not in this build set BINNED2_ALLOCATOR_STATS 1 in MallocBinned2.cpp"));
#endif
}
#if !BINNED2_INLINE
	#if PLATFORM_USES_FIXED_GMalloc_CLASS && !FORCE_ANSI_ALLOCATOR && USE_MALLOC_BINNED2
		//#define FMEMORY_INLINE_FUNCTION_DECORATOR  FORCEINLINE
		#define FMEMORY_INLINE_GMalloc (FMallocBinned2::MallocBinned2)
		#include "FMemory.inl"
	#endif
#endif

void FMallocBinned2::UpdateStats()
{
#if CSV_PROFILER
	CSV_CUSTOM_STAT(FMemory, AllocatorCachedSlackMB, (int32)(CachedOSPageAllocator.GetCachedFreeTotal()/(1024*1024)), ECsvCustomStatOp::Set);
#endif

	FScopedVirtualMallocTimer::UpdateStats();
}


PRAGMA_ENABLE_UNSAFE_TYPECAST_WARNINGS
