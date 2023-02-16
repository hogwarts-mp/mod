// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IO/IoStore.h"
#include "IO/IoDispatcherBackend.h"

struct FFileIoStoreCompressionContext;

struct FFileIoStoreContainerFilePartition
{
	uint64 FileHandle = 0;
	uint64 FileSize = 0;
	uint32 ContainerFileIndex = 0;
	FString FilePath;
	TUniquePtr<IMappedFileHandle> MappedFileHandle;
};

struct FFileIoStoreContainerFile
{
	uint64 PartitionSize = 0;
	uint64 CompressionBlockSize = 0;
	TArray<FName> CompressionMethods;
	TArray<FIoStoreTocCompressedBlockEntry> CompressionBlocks;
	FString FilePath;
	FGuid EncryptionKeyGuid;
	FAES::FAESKey EncryptionKey;
	EIoContainerFlags ContainerFlags;
	TArray<FSHAHash> BlockSignatureHashes;
	TArray<FFileIoStoreContainerFilePartition> Partitions;

	void GetPartitionFileHandleAndOffset(uint64 TocOffset, uint64& OutFileHandle, uint64& OutOffset) const
	{
		int32 PartitionIndex = int32(TocOffset / PartitionSize);
		const FFileIoStoreContainerFilePartition& Partition = Partitions[PartitionIndex];
		OutFileHandle = Partition.FileHandle;
		OutOffset = TocOffset % PartitionSize;
	}
};

struct FFileIoStoreBuffer
{
	FFileIoStoreBuffer* Next = nullptr;
	uint8* Memory = nullptr;
};

struct FFileIoStoreBlockKey
{
	union
	{
		struct
		{
			uint32 FileIndex;
			uint32 BlockIndex;
		};
		uint64 Hash;
	};


	friend bool operator==(const FFileIoStoreBlockKey& A, const FFileIoStoreBlockKey& B)
	{
		return A.Hash == B.Hash;
	}

	friend uint32 GetTypeHash(const FFileIoStoreBlockKey& Key)
	{
		return GetTypeHash(Key.Hash);
	}
};

struct FFileIoStoreBlockScatter
{
	struct FFileIoStoreResolvedRequest* Request = nullptr;
	uint64 DstOffset = 0;
	uint64 SrcOffset = 0;
	uint64 Size = 0;
};

struct FFileIoStoreCompressedBlock
{
	FFileIoStoreCompressedBlock* Next = nullptr;
	FFileIoStoreBlockKey Key;
	FName CompressionMethod;
	uint64 RawOffset;
	uint32 UncompressedSize;
	uint32 CompressedSize;
	uint32 RawSize;
	uint32 RefCount = 0;
	uint32 UnfinishedRawBlocksCount = 0;
	TArray<struct FFileIoStoreReadRequest*, TInlineAllocator<2>> RawBlocks;
	TArray<FFileIoStoreBlockScatter, TInlineAllocator<2>> ScatterList;
	FFileIoStoreCompressionContext* CompressionContext = nullptr;
	uint8* CompressedDataBuffer = nullptr;
	FAES::FAESKey EncryptionKey;
	const FSHAHash* SignatureHash = nullptr;
	bool bFailed = false;
	bool bCancelled = false;
};

struct FFileIoStoreReadRequest
{
	FFileIoStoreReadRequest()
		: Sequence(NextSequence++)
	{

	}
	FFileIoStoreReadRequest* Next = nullptr;
	uint64 FileHandle = uint64(-1);
	uint64 Offset = uint64(-1);
	uint64 Size = uint64(-1);
	FFileIoStoreBlockKey Key;
	FFileIoStoreBuffer* Buffer = nullptr;
	uint32 RefCount = 0;
	uint32 BufferRefCount = 0;
	TArray<FFileIoStoreCompressedBlock*, TInlineAllocator<8>> CompressedBlocks;
	const uint32 Sequence;
	int32 Priority = 0;
	FFileIoStoreBlockScatter ImmediateScatter;
	bool bIsCacheable = false;
	bool bFailed = false;
	bool bCancelled = false;

private:
	static uint32 NextSequence;
};

class FFileIoStoreReadRequestList
{
public:
	bool IsEmpty() const
	{
		return Head == nullptr;
	}

	FFileIoStoreReadRequest* GetHead() const
	{
		return Head;
	}

	FFileIoStoreReadRequest* GetTail() const
	{
		return Tail;
	}

	void Add(FFileIoStoreReadRequest* Request)
	{
		if (Tail)
		{
			Tail->Next = Request;
		}
		else
		{
			Head = Request;
		}
		Tail = Request;
		Request->Next = nullptr;
	}

	void Append(FFileIoStoreReadRequest* ListHead, FFileIoStoreReadRequest* ListTail)
	{
		check(ListHead);
		check(ListTail);
		check(!ListTail->Next);
		if (Tail)
		{
			Tail->Next = ListHead;
		}
		else
		{
			Head = ListHead;
		}
		Tail = ListTail;
	}

	void Append(FFileIoStoreReadRequestList& List)
	{
		if (List.Head)
		{
			Append(List.Head, List.Tail);
		}
	}

	void Clear()
	{
		Head = Tail = nullptr;
	}

private:
	FFileIoStoreReadRequest* Head = nullptr;
	FFileIoStoreReadRequest* Tail = nullptr;
};

class FFileIoStoreBufferAllocator
{
public:
	void Initialize(uint64 MemorySize, uint64 BufferSize, uint32 BufferAlignment);
	FFileIoStoreBuffer* AllocBuffer();
	void FreeBuffer(FFileIoStoreBuffer* Buffer);

private:
	uint8* BufferMemory = nullptr;
	FCriticalSection BuffersCritical;
	FFileIoStoreBuffer* FirstFreeBuffer = nullptr;
};

class FFileIoStoreBlockCache
{
public:
	FFileIoStoreBlockCache();
	~FFileIoStoreBlockCache();

	void Initialize(uint64 CacheMemorySize, uint64 ReadBufferSize);
	bool Read(FFileIoStoreReadRequest* Block);
	void Store(const FFileIoStoreReadRequest* Block);

private:
	struct FCachedBlock
	{
		FCachedBlock* LruPrev = nullptr;
		FCachedBlock* LruNext = nullptr;
		uint64 Key = 0;
		uint8* Buffer = nullptr;
		bool bLocked = false;
	};

	FCriticalSection CriticalSection;
	uint8* CacheMemory = nullptr;
	TMap<uint64, FCachedBlock*> CachedBlocks;
	FCachedBlock CacheLruHead;
	FCachedBlock CacheLruTail;
	uint64 ReadBufferSize = 0;
};

class FFileIoStoreRequestQueue
{
public:
	FFileIoStoreReadRequest* Peek();
	FFileIoStoreReadRequest* Pop();
	void Push(FFileIoStoreReadRequest& Request);
	void Push(const FFileIoStoreReadRequestList& Requests);
	void UpdateOrder();

private:
	static bool QueueSortFunc(const FFileIoStoreReadRequest& A, const FFileIoStoreReadRequest& B)
	{
		if (A.Priority == B.Priority)
		{
			return A.Sequence < B.Sequence;
		}
		return A.Priority > B.Priority;
	}
	
	TArray<FFileIoStoreReadRequest*> Heap;
	FCriticalSection CriticalSection;
};

template <typename T, uint16 SlabSize = 4096>
class TIoDispatcherSingleThreadedSlabAllocator
{
public:
	TIoDispatcherSingleThreadedSlabAllocator()
	{
		CurrentSlab = new FSlab();
	}

	~TIoDispatcherSingleThreadedSlabAllocator()
	{
		check(CurrentSlab->Allocated == CurrentSlab->Freed);
		delete CurrentSlab;
	}

	template <typename... ArgsType>
	T* Construct(ArgsType&&... Args)
	{
		return new(Alloc()) T(Forward<ArgsType>(Args)...);
	}

	void Destroy(T* Ptr)
	{
		Ptr->~T();
		Free(Ptr);
	}

private:
	struct FSlab;

	struct FElement
	{
		TTypeCompatibleBytes<T> Data;
		FSlab* Slab = nullptr;
	};

	struct FSlab
	{
		uint16 Allocated = 0;
		uint16 Freed = 0;
		FElement Elements[SlabSize];
	};

	T* Alloc()
	{
		uint16 ElementIndex = CurrentSlab->Allocated++;
		check(ElementIndex < SlabSize);
		FElement* Element = CurrentSlab->Elements + ElementIndex;
		Element->Slab = CurrentSlab;
		if (CurrentSlab->Allocated == SlabSize)
		{
			//TRACE_CPUPROFILER_EVENT_SCOPE(AllocSlab);
			CurrentSlab = new FSlab();
		}
		return Element->Data.GetTypedPtr();
	}

	void Free(T* Ptr)
	{
		FElement* Element = reinterpret_cast<FElement*>(Ptr);
		FSlab* Slab = Element->Slab;
		if (++Slab->Freed == SlabSize)
		{
			//TRACE_CPUPROFILER_EVENT_SCOPE(FreeSlab);
			check(Slab->Freed == Slab->Allocated);
			delete Slab;
		}
	}

	FSlab* CurrentSlab = nullptr;
};

struct FFileIoStoreReadRequestLink
{
	FFileIoStoreReadRequestLink(FFileIoStoreReadRequest& InReadRequest)
		: ReadRequest(InReadRequest)
	{
	}

	FFileIoStoreReadRequestLink* Next = nullptr;
	FFileIoStoreReadRequest& ReadRequest;
};

class FFileIoStoreRequestAllocator
{
public:
	FFileIoStoreResolvedRequest* AllocResolvedRequest(
		FIoRequestImpl& InDispatcherRequest,
		const FFileIoStoreContainerFile& InContainerFile,
		uint32 InContainerFileIndex,
		uint64 InResolvedOffset,
		uint64 InResolvedSize)
	{
		return ResolvedRequestAllocator.Construct(
			InDispatcherRequest,
			InContainerFile,
			InContainerFileIndex,
			InResolvedOffset,
			InResolvedSize);
	}

	void Free(FFileIoStoreResolvedRequest* ResolvedRequest)
	{
		ResolvedRequestAllocator.Destroy(ResolvedRequest);
	}

	FFileIoStoreReadRequest* AllocReadRequest()
	{
		//TRACE_CPUPROFILER_EVENT_SCOPE(AllocReadRequest);
		return ReadRequestAllocator.Construct();
	}

	void Free(FFileIoStoreReadRequest* ReadRequest)
	{
		//TRACE_CPUPROFILER_EVENT_SCOPE(FreeReadRequest);
		ReadRequestAllocator.Destroy(ReadRequest);
	}

	FFileIoStoreCompressedBlock* AllocCompressedBlock()
	{
		//TRACE_CPUPROFILER_EVENT_SCOPE(AllocCompressedBlock);
		return CompressedBlockAllocator.Construct();
	}

	void Free(FFileIoStoreCompressedBlock* CompressedBlock)
	{
		//TRACE_CPUPROFILER_EVENT_SCOPE(FreeCompressedBlock);
		CompressedBlockAllocator.Destroy(CompressedBlock);
	}

	FFileIoStoreReadRequestLink* AllocRequestLink(FFileIoStoreReadRequest* ReadRequest)
	{
		check(ReadRequest);
		return RequestLinkAllocator.Construct(*ReadRequest);
	}

	void Free(FFileIoStoreReadRequestLink* RequestLink)
	{
		RequestLinkAllocator.Destroy(RequestLink);
	}

private:
	TIoDispatcherSingleThreadedSlabAllocator<FFileIoStoreResolvedRequest> ResolvedRequestAllocator;
	TIoDispatcherSingleThreadedSlabAllocator<FFileIoStoreReadRequest> ReadRequestAllocator;
	TIoDispatcherSingleThreadedSlabAllocator<FFileIoStoreCompressedBlock> CompressedBlockAllocator;
	TIoDispatcherSingleThreadedSlabAllocator<FFileIoStoreReadRequestLink> RequestLinkAllocator;
};

struct FFileIoStoreResolvedRequest
{
public:
	FFileIoStoreResolvedRequest(
		FIoRequestImpl& InDispatcherRequest,
		const FFileIoStoreContainerFile& InContainerFile,
		uint32 InContainerFileIndex,
		uint64 InResolvedOffset,
		uint64 InResolvedSize);

	const FFileIoStoreContainerFile& GetContainerFile() const
	{
		return ContainerFile;
	}

	uint32 GetContainerFileIndex() const
	{
		return ContainerFileIndex;
	}

	uint64 GetResolvedOffset() const
	{
		return ResolvedOffset;
	}

	uint64 GetResolvedSize() const
	{
		return ResolvedSize;
	}

	int32 GetPriority() const
	{
		return DispatcherRequest.Priority;
	}

	FIoBuffer& GetIoBuffer()
	{
		return DispatcherRequest.IoBuffer;
	}

	void AddReadRequestLink(FFileIoStoreReadRequestLink* ReadRequestLink);

private:
	FIoRequestImpl& DispatcherRequest;
	const FFileIoStoreContainerFile& ContainerFile;
	FFileIoStoreReadRequestLink* ReadRequestsHead = nullptr;
	FFileIoStoreReadRequestLink* ReadRequestsTail = nullptr;
	const uint64 ResolvedOffset;
	const uint64 ResolvedSize;
	const uint32 ContainerFileIndex;
	uint32 UnfinishedReadsCount = 0;
	bool bFailed = false;

	friend class FFileIoStore;
	friend class FFileIoStoreRequestTracker;
};
