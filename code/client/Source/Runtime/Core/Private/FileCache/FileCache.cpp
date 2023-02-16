// Copyright Epic Games, Inc. All Rights Reserved.

#include "FileCache/FileCache.h"
#include "Containers/BinaryHeap.h"
#include "Containers/Queue.h"
#include "Containers/LockFreeList.h"
#include "Templates/TypeHash.h"
#include "Misc/ScopeLock.h"
#include "Async/AsyncFileHandle.h"
#include "Async/TaskGraphInterfaces.h"
#include "ProfilingDebugging/LoadTimeTracker.h"
#include "HAL/PlatformFile.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/IConsoleManager.h"

DECLARE_STATS_GROUP(TEXT("Streaming File Cache"), STATGROUP_SFC, STATCAT_Advanced);

DECLARE_CYCLE_STAT(TEXT("Create Handle"), STAT_SFC_CreateHandle, STATGROUP_SFC);
DECLARE_CYCLE_STAT(TEXT("Read Data"), STAT_SFC_ReadData, STATGROUP_SFC);
DECLARE_CYCLE_STAT(TEXT("EvictAll"), STAT_SFC_EvictAll, STATGROUP_SFC);

// These below are pretty high throughput and probably should be removed once the system gets more mature
DECLARE_CYCLE_STAT(TEXT("Find Eviction Candidate"), STAT_SFC_FindEvictionCandidate, STATGROUP_SFC);

DEFINE_LOG_CATEGORY_STATIC(LogStreamingFileCache, Log, All);

static const int CacheLineSize = 64 * 1024;

static int32 GNumFileCacheBlocks = 256;
static FAutoConsoleVariableRef CVarNumFileCacheBlocks(
	TEXT("fc.NumFileCacheBlocks"),
	GNumFileCacheBlocks,
	TEXT("Number of blocks in the global file cache object\n"),
	ECVF_RenderThreadSafe
);

// 
// Strongly typed ids to avoid confusion in the code
// 
template <int SetBlockSize, typename Parameter> class StrongBlockIdentifier
{
	static const int InvalidHandle = 0xFFFFFFFF;

public:
	static const int32 BlockSize = SetBlockSize;

	StrongBlockIdentifier() : Id(InvalidHandle) {}
	explicit StrongBlockIdentifier(int32 SetId) : Id(SetId) {}

	inline bool IsValid() const { return Id != InvalidHandle; }
	inline int32 Get() const { checkSlow(IsValid()); return Id; }

	inline StrongBlockIdentifier& operator++() { Id = Id + 1; return *this; }
	inline StrongBlockIdentifier& operator--() { Id = Id - 1; return *this; }
	inline StrongBlockIdentifier operator++(int) { StrongBlockIdentifier Temp(*this); operator++(); return Temp; }
	inline StrongBlockIdentifier operator--(int) { StrongBlockIdentifier Temp(*this); operator--(); return Temp; }

	// Get the offset in the file to read this block
	inline int64 GetOffset() const { checkSlow(IsValid()); return (int64)Id * (int64)BlockSize; }
	inline int64 GetSize() const { checkSlow(IsValid()); return BlockSize; }

	// Get the number of bytes that need to be read for this block
	// takes into account incomplete blocks at the end of the file
	inline int64 GetSize(int64 FileSize) const { checkSlow(IsValid()); return FMath::Min((int64)BlockSize, FileSize - GetOffset()); }

	friend inline uint32 GetTypeHash(const StrongBlockIdentifier<SetBlockSize, Parameter>& Info) { return GetTypeHash(Info.Id); }

	inline bool operator==(const StrongBlockIdentifier<SetBlockSize, Parameter>&Other) const { return Id == Other.Id; }
	inline bool operator!=(const StrongBlockIdentifier<SetBlockSize, Parameter>&Other) const { return Id != Other.Id; }

private:
	int32 Id;
};

using CacheLineID = StrongBlockIdentifier<CacheLineSize, struct CacheLineStrongType>; // Unique per file handle
using CacheSlotID = StrongBlockIdentifier<CacheLineSize, struct CacheSlotStrongType>; // Unique per cache

class FFileCacheHandle;

// Some terminology:
// A line: A fixed size block of a file on disc that can be brought into the cache
// Slot: A fixed size piece of memory that can contain the data for a certain line in memory

////////////////

class FFileCache
{
public:
	explicit FFileCache(int32 NumSlots);

	~FFileCache()
	{
		FMemory::Free(Memory);
	}

	uint8* GetSlotMemory(CacheSlotID SlotID)
	{
		check(SlotID.Get() < SlotInfo.Num() - 1);
		check(IsSlotLocked(SlotID)); // slot must be locked in order to access memory
		return Memory + SlotID.Get() * CacheSlotID::BlockSize;
	}

	CacheSlotID AcquireAndLockSlot(FFileCacheHandle* InHandle, CacheLineID InLineID);
	bool IsSlotLocked(CacheSlotID InSlotID) const;
	void LockSlot(CacheSlotID InSlotID);
	void UnlockSlot(CacheSlotID InSlotID);

	// if InFile is null, will evict all slots
	bool EvictAll(FFileCacheHandle* InFile = nullptr);

	void FlushCompletedRequests();

	struct FSlotInfo
	{
		FFileCacheHandle* Handle;
		CacheLineID LineID;
		int32 NextSlotIndex;
		int32 PrevSlotIndex;
		int32 LockCount;
	};

	void EvictFileCacheFromConsole()
	{
		EvictAll();
	}

	void PushCompletedRequest(IAsyncReadRequest* Request)
	{
		check(Request);
		CompletedRequests.Push(Request);
		if (((uint32)CompletedRequestsCounter.Increment() % 32u) == 0u)
		{
			FFunctionGraphTask::CreateAndDispatchWhenReady([this]()
			{
				while (IAsyncReadRequest* CompletedRequest = this->CompletedRequests.Pop())
				{
					// Requests are added to this list from the completed callback, but the final completion flag is not set until after callback is finished
					// This means that there's a narrow window where the request is not technically considered to be complete yet
					verify(CompletedRequest->WaitCompletion());
					delete CompletedRequest;
				}
			}, TStatId());
		}
	}

	inline void UnlinkSlot(int32 SlotIndex)
	{
		check(SlotIndex != 0);
		FSlotInfo& Info = SlotInfo[SlotIndex];
		SlotInfo[Info.PrevSlotIndex].NextSlotIndex = Info.NextSlotIndex;
		SlotInfo[Info.NextSlotIndex].PrevSlotIndex = Info.PrevSlotIndex;
		Info.NextSlotIndex = Info.PrevSlotIndex = SlotIndex;
	}

	inline void LinkSlotTail(int32 SlotIndex)
	{
		check(SlotIndex != 0);
		FSlotInfo& HeadInfo = SlotInfo[0];
		FSlotInfo& Info = SlotInfo[SlotIndex];
		check(Info.NextSlotIndex == SlotIndex);
		check(Info.PrevSlotIndex == SlotIndex);

		Info.NextSlotIndex = 0;
		Info.PrevSlotIndex = HeadInfo.PrevSlotIndex;
		SlotInfo[HeadInfo.PrevSlotIndex].NextSlotIndex = SlotIndex;
		HeadInfo.PrevSlotIndex = SlotIndex;
	}

	inline void LinkSlotHead(int32 SlotIndex)
	{
		check(SlotIndex != 0);
		FSlotInfo& HeadInfo = SlotInfo[0];
		FSlotInfo& Info = SlotInfo[SlotIndex];
		check(Info.NextSlotIndex == SlotIndex);
		check(Info.PrevSlotIndex == SlotIndex);

		Info.NextSlotIndex = HeadInfo.NextSlotIndex;
		Info.PrevSlotIndex = 0;
		SlotInfo[HeadInfo.NextSlotIndex].PrevSlotIndex = SlotIndex;
		HeadInfo.NextSlotIndex = SlotIndex;
	}

	FCriticalSection CriticalSection;

	FAutoConsoleCommand EvictFileCacheCommand;

	TLockFreePointerListUnordered<IAsyncReadRequest, PLATFORM_CACHE_LINE_SIZE> CompletedRequests;
	FThreadSafeCounter CompletedRequestsCounter;

	// allocated with an extra dummy entry at index0 for linked list head
	TArray<FSlotInfo> SlotInfo;
	uint8* Memory;
	int32 SizeInBytes;
	int32 NumFreeSlots;
};

static FFileCache &GetCache()
{
	static FFileCache TheCache(GNumFileCacheBlocks);
	return TheCache;
}

///////////////

class FFileCacheHandle : public IFileCacheHandle
{
public:

	FFileCacheHandle(IAsyncReadFileHandle* InHandle);
	virtual ~FFileCacheHandle() override;

	//
	// Block helper functions. These are just convenience around basic math.
	// 

 // templated uses of this may end up converting int64 to int32, but it's up to the user of the template to know
	PRAGMA_DISABLE_UNSAFE_TYPECAST_WARNINGS
		/*
		 * Get the block id that contains the specified offset
		 */
		template<typename BlockIDType> inline BlockIDType GetBlock(int64 Offset)
	{
		return BlockIDType(FMath::DivideAndRoundDown(Offset, (int64)BlockIDType::BlockSize));
	}
	PRAGMA_ENABLE_UNSAFE_TYPECAST_WARNINGS

		template<typename BlockIDType> inline int32 GetNumBlocks(int64 Offset, int64 Size)
	{
		BlockIDType FirstBlock = GetBlock<BlockIDType>(Offset);
		BlockIDType LastBlock = GetBlock<BlockIDType>(Offset + Size - 1);// Block containing the last byte
		return (LastBlock.Get() - FirstBlock.Get()) + 1;
	}

	// Returns the offset within the first block covering the byte range to read from
	template<typename BlockIDType> inline int64 GetBlockOffset(int64 Offset)
	{
		return Offset - FMath::DivideAndRoundDown(Offset, (int64)BlockIDType::BlockSize) *  BlockIDType::BlockSize;
	}

	// Returns the size within the first cache line covering the byte range to read
	template<typename BlockIDType> inline int64 GetBlockSize(int64 Offset, int64 Size)
	{
		int64 OffsetInBlock = GetBlockOffset<BlockIDType>(Offset);
		return FMath::Min((int64)(BlockIDType::BlockSize - OffsetInBlock), Size - Offset);
	}

	virtual IMemoryReadStreamRef ReadData(FGraphEventArray& OutCompletionEvents, int64 Offset, int64 BytesToRead, EAsyncIOPriorityAndFlags Priority) override;
	virtual FGraphEventRef PreloadData(const FFileCachePreloadEntry* PreloadEntries, int32 NumEntries, EAsyncIOPriorityAndFlags Priority) override;

	IMemoryReadStreamRef ReadDataUncached(FGraphEventArray& OutCompletionEvents, int64 Offset, int64 BytesToRead, EAsyncIOPriorityAndFlags Priority);

	void WaitAll() override;

	void Evict(CacheLineID Line);

private:
	struct FPendingRequest
	{
		FGraphEventRef Event;
	};

	void CheckForSizeRequestComplete();

	CacheSlotID AcquireSlotAndReadLine(FFileCache& Cache, CacheLineID LineID, EAsyncIOPriorityAndFlags Priority);
	void ReadLine(FFileCache& Cache, CacheSlotID SlotID, CacheLineID LineID, EAsyncIOPriorityAndFlags Priority, const FGraphEventRef& CompletionEvent);

	TArray<CacheSlotID> LineToSlot;
	TArray<FPendingRequest> LineToRequest;

	int64 NumSlots;
	int64 FileSize;
	IAsyncReadFileHandle* InnerHandle;
	FGraphEventRef SizeRequestEvent;
};

///////////////

FFileCache::FFileCache(int32 NumSlots)
	: EvictFileCacheCommand(TEXT("r.VT.EvictFileCache"), TEXT("Evict all the file caches in the VT system."),
		FConsoleCommandDelegate::CreateRaw(this, &FFileCache::EvictFileCacheFromConsole))
	, SizeInBytes(NumSlots * CacheSlotID::BlockSize)
	, NumFreeSlots(NumSlots)
{
	LLM_SCOPE(ELLMTag::FileSystem);

	Memory = (uint8*)FMemory::Malloc(SizeInBytes);

	SlotInfo.AddUninitialized(NumSlots + 1);
	for (int i = 0; i <= NumSlots; ++i)
	{
		FSlotInfo& Info = SlotInfo[i];
		Info.Handle = nullptr;
		Info.LineID = CacheLineID();
		Info.LockCount = 0;
		Info.NextSlotIndex = i + 1;
		Info.PrevSlotIndex = i - 1;
	}

	// list is circular
	SlotInfo[0].PrevSlotIndex = NumSlots;
	SlotInfo[NumSlots].NextSlotIndex = 0;
}

CacheSlotID FFileCache::AcquireAndLockSlot(FFileCacheHandle* InHandle, CacheLineID InLineID)
{
	check(NumFreeSlots > 0);
	--NumFreeSlots;

	const int32 SlotIndex = SlotInfo[0].NextSlotIndex;
	check(SlotIndex != 0);

	FSlotInfo& Info = SlotInfo[SlotIndex];
	check(Info.LockCount == 0); // slot should not be in free list if it's locked
	if (Info.Handle)
	{
		Info.Handle->Evict(Info.LineID);
	}

	Info.LockCount = 1;
	Info.Handle = InHandle;
	Info.LineID = InLineID;
	UnlinkSlot(SlotIndex);

	return CacheSlotID(SlotIndex - 1);
}

bool FFileCache::IsSlotLocked(CacheSlotID InSlotID) const
{
	const int32 SlotIndex = InSlotID.Get() + 1;
	const FSlotInfo& Info = SlotInfo[SlotIndex];
	return Info.LockCount > 0;
}

void FFileCache::LockSlot(CacheSlotID InSlotID)
{
	const int32 SlotIndex = InSlotID.Get() + 1;
	FSlotInfo& Info = SlotInfo[SlotIndex];
	const int32 PrevLockCount = Info.LockCount;
	if (PrevLockCount == 0)
	{
		check(NumFreeSlots > 0);
		--NumFreeSlots;
		UnlinkSlot(SlotIndex);
	}
	Info.LockCount = PrevLockCount + 1;
}

void FFileCache::UnlockSlot(CacheSlotID InSlotID)
{
	const int32 SlotIndex = InSlotID.Get() + 1;
	const int32 PrevLockCount = SlotInfo[SlotIndex].LockCount;
	check(PrevLockCount > 0);
	if (PrevLockCount == 1)
	{
		// move slot back to the free list when it's unlocked
		LinkSlotTail(SlotIndex);
		++NumFreeSlots;
		check(NumFreeSlots < SlotInfo.Num());
	}
	SlotInfo[SlotIndex].LockCount = PrevLockCount - 1;
}

bool FFileCache::EvictAll(FFileCacheHandle* InFile)
{
	SCOPE_CYCLE_COUNTER(STAT_SFC_EvictAll);

	FScopeLock Lock(&CriticalSection);

	bool bAllOK = true;
	for (int SlotIndex = 1; SlotIndex < SlotInfo.Num(); ++SlotIndex)
	{
		FSlotInfo& Info = SlotInfo[SlotIndex];
		if (Info.Handle && ((Info.Handle == InFile) || InFile == nullptr))
		{
			if (Info.LockCount == 0)
			{
				Info.Handle->Evict(Info.LineID);
				Info.Handle = nullptr;
				Info.LineID = CacheLineID();

				// move evicted slots to the front of list so they'll be re-used more quickly
				UnlinkSlot(SlotIndex);
				LinkSlotHead(SlotIndex);
			}
			else
			{
				bAllOK = false;
			}
		}
	}

	return bAllOK;
}

void FFileCache::FlushCompletedRequests()
{
	while (IAsyncReadRequest* Request = CompletedRequests.Pop())
	{
		Request->WaitCompletion();
		delete Request;
	}
}

FFileCacheHandle::~FFileCacheHandle()
{
	if (SizeRequestEvent)
	{
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(SizeRequestEvent);
		SizeRequestEvent.SafeRelease();
	}

	if (InnerHandle)
	{
		WaitAll();

		const bool result = GetCache().EvictAll(this);
		check(result);

		// Need to ensure any request created by our async handle is destroyed before destroying the handle
		GetCache().FlushCompletedRequests();

		delete InnerHandle;
	}
}

FFileCacheHandle::FFileCacheHandle(IAsyncReadFileHandle* InHandle)
	: NumSlots(0)
	, FileSize(-1)
	, InnerHandle(InHandle)
{
	FGraphEventRef CompletionEvent = FGraphEvent::CreateGraphEvent();
	FAsyncFileCallBack SizeCallbackFunction = [this, CompletionEvent](bool bWasCancelled, IAsyncReadRequest* Request)
	{
		this->FileSize = Request->GetSizeResults();
		check(this->FileSize > 0);

		TArray<FBaseGraphTask*> NewTasks;
		CompletionEvent->DispatchSubsequents(NewTasks);
		GetCache().PushCompletedRequest(Request);
	};

	SizeRequestEvent = CompletionEvent;
	IAsyncReadRequest* SizeRequest = InHandle->SizeRequest(&SizeCallbackFunction);
	check(SizeRequest);
}

class FMemoryReadStreamAsyncRequest : public IMemoryReadStream
{
public:
	FMemoryReadStreamAsyncRequest(IAsyncReadRequest* InRequest, int64 InSize)
		: Memory(nullptr), Request(InRequest), Size(InSize)
	{
	}

	uint8* GetReadResults()
	{
		if (Request)
		{
			// Event is triggered from read callback, so small window where event is triggered, but request isn't flagged as complete
			// Normally this wait won't be needed
			check(!Memory);
			Request->WaitCompletion();
			Memory = Request->GetReadResults(); // We now own the pointer returned from GetReadResults()
			delete Request; // no longer need to keep request alive
			Request = nullptr;
		}
		return Memory;
	}

	virtual const void* Read(int64& OutSize, int64 InOffset, int64 InSize) override
	{
		const uint8* ResultData = GetReadResults();
		check(InOffset < Size);
		OutSize = FMath::Min(InSize, Size - InOffset);
		return ResultData + InOffset;
	}

	virtual int64 GetSize() override
	{
		return Size;
	}

	virtual ~FMemoryReadStreamAsyncRequest()
	{
		uint8* ResultData = GetReadResults();
		if (ResultData)
		{
			FMemory::Free(ResultData);
		}
		check(!Request);
	}

	uint8* Memory;
	IAsyncReadRequest* Request;
	int64 Size;
};

IMemoryReadStreamRef FFileCacheHandle::ReadDataUncached(FGraphEventArray& OutCompletionEvents, int64 Offset, int64 BytesToRead, EAsyncIOPriorityAndFlags Priority)
{
	FGraphEventRef CompletionEvent = FGraphEvent::CreateGraphEvent();

	FAsyncFileCallBack ReadCallbackFunction = [CompletionEvent](bool bWasCancelled, IAsyncReadRequest* Request)
	{
		TArray<FBaseGraphTask*> NewTasks;
		CompletionEvent->DispatchSubsequents(NewTasks);
	};

	OutCompletionEvents.Add(CompletionEvent);
	IAsyncReadRequest* AsyncRequest = InnerHandle->ReadRequest(Offset, BytesToRead, Priority, &ReadCallbackFunction);
	return new FMemoryReadStreamAsyncRequest(AsyncRequest, BytesToRead);
}

class FMemoryReadStreamCache : public IMemoryReadStream
{
public:
	virtual const void* Read(int64& OutSize, int64 InOffset, int64 InSize) override
	{
		FFileCache& Cache = GetCache();

		const int64 Offset = InitialSlotOffset + InOffset;
		const int32 SlotIndex = (int32)FMath::DivideAndRoundDown(Offset, (int64)CacheSlotID::BlockSize);
		const int32 OffsetInSlot = (int32)(Offset - SlotIndex * CacheSlotID::BlockSize);
		checkSlow(SlotIndex >= 0 && SlotIndex < NumCacheSlots);
		const void* SlotMemory = Cache.GetSlotMemory(CacheSlots[SlotIndex]);

		OutSize = FMath::Min(InSize, (int64)CacheSlotID::BlockSize - OffsetInSlot);
		return (uint8*)SlotMemory + OffsetInSlot;
	}

	virtual int64 GetSize() override
	{
		return Size;
	}

	virtual ~FMemoryReadStreamCache()
	{
		FFileCache& Cache = GetCache();
		FScopeLock CacheLock(&Cache.CriticalSection);
		for (int i = 0; i < NumCacheSlots; ++i)
		{
			const CacheSlotID& SlotID = CacheSlots[i];
			check(SlotID.IsValid());
			Cache.UnlockSlot(SlotID);
		}
	}

	void operator delete(void* InMem)
	{
		FMemory::Free(InMem);
	}

	int64 InitialSlotOffset;
	int64 Size;
	int32 NumCacheSlots;
	CacheSlotID CacheSlots[1]; // variable length, sized by NumCacheSlots
};

void FFileCacheHandle::CheckForSizeRequestComplete()
{
	if (SizeRequestEvent && SizeRequestEvent->IsComplete())
	{
		SizeRequestEvent.SafeRelease();

		check(FileSize > 0);

		// Make sure we haven't lazily allocated more slots than are in the file, then allocate the final number of slots
		const int64 TotalNumSlots = FMath::DivideAndRoundUp(FileSize, (int64)CacheLineSize);
		check(NumSlots <= TotalNumSlots);
		NumSlots = TotalNumSlots;
		// TArray is max signed int
		check(TotalNumSlots < MAX_int32);
		LineToSlot.SetNum((int32)TotalNumSlots, false);
		LineToRequest.SetNum((int32)TotalNumSlots, false);
	}
}

void FFileCacheHandle::ReadLine(FFileCache& Cache, CacheSlotID SlotID, CacheLineID LineID, EAsyncIOPriorityAndFlags Priority, const FGraphEventRef& CompletionEvent)
{
	check(FileSize >= 0);
	const int64 LineSizeInFile = LineID.GetSize(FileSize);
	const int64 LineOffsetInFile = LineID.GetOffset();
	uint8* CacheSlotMemory = Cache.GetSlotMemory(SlotID);

	// callback triggered when async read operation is complete, used to signal task graph event
	FAsyncFileCallBack ReadCallbackFunction = [CompletionEvent](bool bWasCancelled, IAsyncReadRequest* Request)
	{
		TArray<FBaseGraphTask*> NewTasks;
		CompletionEvent->DispatchSubsequents(NewTasks);
		GetCache().PushCompletedRequest(Request);
	};

	InnerHandle->ReadRequest(LineOffsetInFile, LineSizeInFile, Priority, &ReadCallbackFunction, CacheSlotMemory);
}

CacheSlotID FFileCacheHandle::AcquireSlotAndReadLine(FFileCache& Cache, CacheLineID LineID, EAsyncIOPriorityAndFlags Priority)
{
	SCOPED_LOADTIMER(FFileCacheHandle_AcquireSlotAndReadLine);

	// no valid slot for this line, grab a new slot from cache and start a read request
	CacheSlotID SlotID = Cache.AcquireAndLockSlot(this, LineID);

	FPendingRequest& PendingRequest = LineToRequest[LineID.Get()];
	if (PendingRequest.Event)
	{
		// previous async request/event (if any) should be completed, if this is back in the free list
		check(PendingRequest.Event->IsComplete());
	}

	FGraphEventRef CompletionEvent = FGraphEvent::CreateGraphEvent();
	PendingRequest.Event = CompletionEvent;
	if (FileSize >= 0)
	{
		// If FileSize >= 0, that means the async file size request has completed, we can perform the read immediately
		ReadLine(Cache, SlotID, LineID, Priority, CompletionEvent);
	}
	else
	{
		// Here we don't know the FileSize yet, so we schedule an async task to kick the read once the size request has completed
		// It's important to know the size of the file before performing the read, to ensure that we don't read past end-of-file
		FFunctionGraphTask::CreateAndDispatchWhenReady([this, SlotID, LineID, Priority, CompletionEvent]
		{
			this->ReadLine(GetCache(), SlotID, LineID, Priority, CompletionEvent);
		},
			TStatId(), SizeRequestEvent);
	}

	return SlotID;
}

IMemoryReadStreamRef FFileCacheHandle::ReadData(FGraphEventArray& OutCompletionEvents, int64 Offset, int64 BytesToRead, EAsyncIOPriorityAndFlags Priority)
{
	SCOPE_CYCLE_COUNTER(STAT_SFC_ReadData);
	SCOPED_LOADTIMER(FFileCacheHandle_ReadData);

	const CacheLineID StartLine = GetBlock<CacheLineID>(Offset);
	const CacheLineID EndLine = GetBlock<CacheLineID>(Offset + BytesToRead - 1);

	const int32 NumSlotsNeeded = EndLine.Get() + 1 - StartLine.Get();

	FFileCache& Cache = GetCache();

	FScopeLock CacheLock(&Cache.CriticalSection);

	CheckForSizeRequestComplete();

	if (NumSlotsNeeded > Cache.NumFreeSlots)
	{
		// not enough free slots in the cache to service this request
		CacheLock.Unlock();

		UE_LOG(LogStreamingFileCache, Verbose, TEXT("ReadData(%d, %d) is skipping cache, cache is full"), Offset, BytesToRead);
		return ReadDataUncached(OutCompletionEvents, Offset, BytesToRead, Priority);
	}

	if (EndLine.Get() >= NumSlots)
	{
		// If we're still waiting on SizeRequest, may need to lazily allocate some slots to service this request
		// If this happens after SizeRequest has completed, that means something must have gone wrong
		check(SizeRequestEvent);
		NumSlots = EndLine.Get() + 1;
		// TArray is max signed int
		check(NumSlots < MAX_int32);
		LineToSlot.SetNum((int32)NumSlots, false);
		LineToRequest.SetNum((int32)NumSlots, false);
	}

	const int32 NumCacheSlots = EndLine.Get() + 1 - StartLine.Get();
	check(NumCacheSlots > 0);
	const uint32 AllocSize = sizeof(FMemoryReadStreamCache) + sizeof(CacheSlotID) * (NumCacheSlots - 1);
	void* ResultMemory = FMemory::Malloc(AllocSize, alignof(FMemoryReadStreamCache));
	FMemoryReadStreamCache* Result = new(ResultMemory) FMemoryReadStreamCache();
	Result->NumCacheSlots = NumCacheSlots;
	Result->InitialSlotOffset = GetBlockOffset<CacheLineID>(Offset);
	Result->Size = BytesToRead;

	bool bHasPendingSlot = false;
	for (CacheLineID LineID = StartLine; LineID.Get() <= EndLine.Get(); ++LineID)
	{
		CacheSlotID& SlotID = LineToSlot[LineID.Get()];
		if (!SlotID.IsValid())
		{
			// no valid slot for this line, grab a new slot from cache and start a read request
			SlotID = AcquireSlotAndReadLine(Cache, LineID, Priority);
		}
		else
		{
			Cache.LockSlot(SlotID);
		}

		check(SlotID.IsValid());
		Result->CacheSlots[LineID.Get() - StartLine.Get()] = SlotID;

		FPendingRequest& PendingRequest = LineToRequest[LineID.Get()];
		if (PendingRequest.Event && !PendingRequest.Event->IsComplete())
		{
			// this line has a pending async request to read data
			// will need to wait for this request to complete before data is valid
			OutCompletionEvents.Add(PendingRequest.Event);
			bHasPendingSlot = true;
		}
		else
		{
			PendingRequest.Event.SafeRelease();
		}
	}

	return Result;
}

struct FFileCachePreloadTask
{
	explicit FFileCachePreloadTask(TArray<CacheSlotID>&& InLockedSlots) : LockedSlots(InLockedSlots) {}
	TArray<CacheSlotID> LockedSlots;

	void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
	{
		FFileCache& Cache = GetCache();
		FScopeLock CacheLock(&Cache.CriticalSection);
		for (int i = 0; i < LockedSlots.Num(); ++i)
		{
			const CacheSlotID& SlotID = LockedSlots[i];
			check(SlotID.IsValid());
			Cache.UnlockSlot(SlotID);
		}
	}

	FORCEINLINE static ESubsequentsMode::Type GetSubsequentsMode() { return ESubsequentsMode::TrackSubsequents; }
	FORCEINLINE ENamedThreads::Type GetDesiredThread() { return ENamedThreads::AnyNormalThreadNormalTask; }
	FORCEINLINE TStatId GetStatId() const { return TStatId(); }
};

FGraphEventRef FFileCacheHandle::PreloadData(const FFileCachePreloadEntry* PreloadEntries, int32 NumEntries, EAsyncIOPriorityAndFlags Priority)
{
	SCOPED_LOADTIMER(FFileCacheHandle_PreloadData);

	check(NumEntries > 0);

	FFileCache& Cache = GetCache();

	FScopeLock CacheLock(&Cache.CriticalSection);

	CheckForSizeRequestComplete();

	{
		const FFileCachePreloadEntry& LastEntry = PreloadEntries[NumEntries - 1];
		const CacheLineID EndLine = GetBlock<CacheLineID>(LastEntry.Offset + LastEntry.Size - 1);
		if (EndLine.Get() >= NumSlots)
		{
			// If we're still waiting on SizeRequest, may need to lazily allocate some slots to service this request
			// If this happens after SizeRequest has completed, that means something must have gone wrong
			check(SizeRequestEvent);
			NumSlots = EndLine.Get() + 1;
			// TArray is max signed int
			check(NumSlots < MAX_int32);
			LineToSlot.SetNum((int32)NumSlots, false);
			LineToRequest.SetNum((int32)NumSlots, false);
		}
	}

	FGraphEventArray CompletionEvents;
	TArray<CacheSlotID> LockedSlots;
	LockedSlots.Empty(NumEntries);

	CacheLineID CurrentLine(0);
	int64 PrevOffset = -1;
	for (int32 EntryIndex = 0; EntryIndex < NumEntries && Cache.NumFreeSlots > 0; ++EntryIndex)
	{
		const FFileCachePreloadEntry& Entry = PreloadEntries[EntryIndex];
		const CacheLineID StartLine = GetBlock<CacheLineID>(Entry.Offset);
		const CacheLineID EndLine = GetBlock<CacheLineID>(Entry.Offset + Entry.Size - 1);

		checkf(Entry.Offset > PrevOffset, TEXT("Preload entries must be sorted by Offset [%lld, %lld), %lld"),
			Entry.Offset, Entry.Offset + Entry.Size, PrevOffset);
		PrevOffset = Entry.Offset;

		CurrentLine = CacheLineID(FMath::Max(CurrentLine.Get(), StartLine.Get()));
		while (CurrentLine.Get() <= EndLine.Get() && Cache.NumFreeSlots > 0)
		{
			CacheSlotID& SlotID = LineToSlot[CurrentLine.Get()];
			if (!SlotID.IsValid())
			{
				// no valid slot for this line, grab a new slot from cache and start a read request
				SlotID = AcquireSlotAndReadLine(Cache, CurrentLine, Priority);
				LockedSlots.Add(SlotID);
			}

			FPendingRequest& PendingRequest = LineToRequest[CurrentLine.Get()];
			if (PendingRequest.Event && !PendingRequest.Event->IsComplete())
			{
				// this line has a pending async request to read data
				// will need to wait for this request to complete before data is valid
				CompletionEvents.Add(PendingRequest.Event);
			}
			else
			{
				PendingRequest.Event.SafeRelease();
			}

			++CurrentLine;
		}
	}

	FGraphEventRef CompletionEvent;
	if (CompletionEvents.Num() > 0)
	{
		CompletionEvent = TGraphTask<FFileCachePreloadTask>::CreateTask(&CompletionEvents).ConstructAndDispatchWhenReady(MoveTemp(LockedSlots));
	}
	else if (LockedSlots.Num() > 0)
	{
		// Unusual case, we locked some slots, but the reads completed immediately, so we don't need to keep the slots locked
		for (const CacheSlotID& SlotID : LockedSlots)
		{
			Cache.UnlockSlot(SlotID);
		}
	}

	return CompletionEvent;
}

void FFileCacheHandle::Evict(CacheLineID LineID)
{
	LineToSlot[LineID.Get()] = CacheSlotID();

	FPendingRequest& PendingRequest = LineToRequest[LineID.Get()];
	if (PendingRequest.Event)
	{
		check(PendingRequest.Event->IsComplete());
		PendingRequest.Event.SafeRelease();
	}
}

void FFileCacheHandle::WaitAll()
{
	for (int i = 0; i < LineToRequest.Num(); ++i)
	{
		FPendingRequest& PendingRequest = LineToRequest[i];
		if (PendingRequest.Event)
		{
			check(PendingRequest.Event->IsComplete());
			PendingRequest.Event.SafeRelease();
		}
	}
}

void IFileCacheHandle::EvictAll()
{
	GetCache().EvictAll();
}

IFileCacheHandle* IFileCacheHandle::CreateFileCacheHandle(const TCHAR* InFileName)
{
	SCOPE_CYCLE_COUNTER(STAT_SFC_CreateHandle);

	IAsyncReadFileHandle* FileHandle = FPlatformFileManager::Get().GetPlatformFile().OpenAsyncRead(InFileName);
	if (!FileHandle)
	{
		return nullptr;
	}

	return new FFileCacheHandle(FileHandle);
}

IFileCacheHandle* IFileCacheHandle::CreateFileCacheHandle(IAsyncReadFileHandle* FileHandle)
{
	SCOPE_CYCLE_COUNTER(STAT_SFC_CreateHandle);

	if (FileHandle != nullptr)
	{
		return new FFileCacheHandle(FileHandle);
	}
	else
	{
		return nullptr;
	}
}

uint32 IFileCacheHandle::GetFileCacheSize()
{
	return GetCache().SizeInBytes;
}
