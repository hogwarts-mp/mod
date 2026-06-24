// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/IoStore.h"
#include "Containers/Map.h"
#include "HAL/FileManager.h"
#include "Templates/UniquePtr.h"
#include "Misc/Paths.h"
#include "Misc/Compression.h"
#include "Serialization/BufferWriter.h"
#include "Async/Async.h"
#include "Async/Future.h"
#include "Async/ParallelFor.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/StringBuilder.h"
#include "Features/IModularFeatures.h"
#include "Modules/ModuleManager.h"
#include "Misc/CoreDelegates.h"
#include "Serialization/MemoryWriter.h"
#include "Async/AsyncFileHandle.h"

//////////////////////////////////////////////////////////////////////////

constexpr char FIoStoreTocHeader::TocMagicImg[];

//////////////////////////////////////////////////////////////////////////

template<typename ArrayType>
bool WriteArray(IFileHandle* FileHandle, const ArrayType& Array)
{
	return FileHandle->Write(reinterpret_cast<const uint8*>(Array.GetData()), Array.GetTypeSize() * Array.Num());
}

static IEngineCrypto* GetEngineCrypto()
{
	static TArray<IEngineCrypto*> Features = IModularFeatures::Get().GetModularFeatureImplementations<IEngineCrypto>(IEngineCrypto::GetFeatureName());
	checkf(Features.Num() > 0, TEXT("RSA functionality was used but no modular feature was registered to provide it. Please make sure your project has the PlatformCrypto plugin enabled!"));
	return Features[0];
}

static bool IsSigningEnabled()
{
	return FCoreDelegates::GetPakSigningKeysDelegate().IsBound();
}

static FRSAKeyHandle GetPublicSigningKey()
{
	static FRSAKeyHandle PublicKey = InvalidRSAKeyHandle;
	static bool bInitializedPublicKey = false;
	if (!bInitializedPublicKey)
	{
		FCoreDelegates::FPakSigningKeysDelegate& Delegate = FCoreDelegates::GetPakSigningKeysDelegate();
		if (Delegate.IsBound())
		{
			TArray<uint8> Exponent;
			TArray<uint8> Modulus;
			Delegate.Execute(Exponent, Modulus);
			PublicKey = GetEngineCrypto()->CreateRSAKey(Exponent, TArray<uint8>(), Modulus);
		}
		bInitializedPublicKey = true;
	}

	return PublicKey;
}

static FIoStatus CreateContainerSignature(
	const FRSAKeyHandle PrivateKey,
	const FIoStoreTocHeader& TocHeader,
	TArrayView<const FSHAHash> BlockSignatureHashes,
	TArray<uint8>& OutTocSignature,
	TArray<uint8>& OutBlockSignature)
{
	if (PrivateKey == InvalidRSAKeyHandle)
	{
		return FIoStatus(EIoErrorCode::SignatureError, TEXT("Invalid signing key"));
	}

	FSHAHash TocHash, BlocksHash;

	FSHA1::HashBuffer(reinterpret_cast<const uint8*>(&TocHeader), sizeof(FIoStoreTocHeader), TocHash.Hash);
	FSHA1::HashBuffer(BlockSignatureHashes.GetData(), BlockSignatureHashes.Num() * sizeof(FSHAHash), BlocksHash.Hash);

	int32 BytesEncrypted = GetEngineCrypto()->EncryptPrivate(TArrayView<const uint8>(TocHash.Hash, UE_ARRAY_COUNT(FSHAHash::Hash)), OutTocSignature, PrivateKey);

	if (BytesEncrypted < 1)
	{
		return FIoStatus(EIoErrorCode::SignatureError, TEXT("Failed to encrypt TOC signature"));
	}

	BytesEncrypted = GetEngineCrypto()->EncryptPrivate(TArrayView<const uint8>(BlocksHash.Hash, UE_ARRAY_COUNT(FSHAHash::Hash)), OutBlockSignature, PrivateKey);

	return BytesEncrypted > 0 ? FIoStatus::Ok : FIoStatus(EIoErrorCode::SignatureError, TEXT("Failed to encrypt block signature"));
}

static FIoStatus ValidateContainerSignature(
	const FRSAKeyHandle PublicKey,
	const FIoStoreTocHeader& TocHeader,
	TArrayView<const FSHAHash> BlockSignatureHashes,
	TArrayView<const uint8> TocSignature,
	TArrayView<const uint8> BlockSignature)
{
	if (PublicKey == InvalidRSAKeyHandle)
	{
		return FIoStatus(EIoErrorCode::SignatureError, TEXT("Invalid signing key"));
	}

	TArray<uint8> DecryptedTocHash, DecryptedBlocksHash;

	int32 BytesDecrypted = GetEngineCrypto()->DecryptPublic(TocSignature, DecryptedTocHash, PublicKey);
	if (BytesDecrypted != UE_ARRAY_COUNT(FSHAHash::Hash))
	{
		return FIoStatus(EIoErrorCode::SignatureError, TEXT("Failed to decrypt TOC signature"));
	}

	BytesDecrypted = GetEngineCrypto()->DecryptPublic(BlockSignature, DecryptedBlocksHash, PublicKey);
	if (BytesDecrypted != UE_ARRAY_COUNT(FSHAHash::Hash))
	{
		return FIoStatus(EIoErrorCode::SignatureError, TEXT("Failed to decrypt block signature"));
	}

	FSHAHash TocHash, BlocksHash;
	FSHA1::HashBuffer(reinterpret_cast<const uint8*>(&TocHeader), sizeof(FIoStoreTocHeader), TocHash.Hash);
	FSHA1::HashBuffer(BlockSignatureHashes.GetData(), BlockSignatureHashes.Num() * sizeof(FSHAHash), BlocksHash.Hash);

	if (FMemory::Memcmp(DecryptedTocHash.GetData(), TocHash.Hash, DecryptedTocHash.Num()) != 0)
	{
		return FIoStatus(EIoErrorCode::SignatureError, TEXT("Invalid TOC signature"));
	}

	if (FMemory::Memcmp(DecryptedBlocksHash.GetData(), BlocksHash.Hash, DecryptedBlocksHash.Num()) != 0)
	{
		return FIoStatus(EIoErrorCode::SignatureError, TEXT("Invalid block signature"));
	}

	return FIoStatus::Ok;
}

FIoStoreEnvironment::FIoStoreEnvironment()
{
}

FIoStoreEnvironment::~FIoStoreEnvironment()
{
}

void FIoStoreEnvironment::InitializeFileEnvironment(FStringView InPath, int32 InOrder)
{
	Path = InPath;
	Order = InOrder;
}

//////////////////////////////////////////////////////////////////////////

struct FChunkBlock
{
	uint64 Offset = 0;
	uint64 Size = 0;
	uint64 CompressedSize = 0;
	uint64 UncompressedSize = 0;
	FName CompressionMethod = NAME_None;
	FSHAHash Signature;
};

struct FIoStoreWriteQueueEntry
{
	FIoStoreWriteQueueEntry* Next = nullptr;
	IIoStoreWriteRequest* Request = nullptr;
	FIoChunkId ChunkId;
	FIoChunkHash ChunkHash;
	FIoBuffer ChunkBuffer;
	uint64 Sequence = 0;
	uint64 UncompressedSize = 0;
	uint64 CompressedSize = 0;
	uint64 Padding = 0;
	uint64 Offset = 0;
	FArchive* ContainerArchive = nullptr;
	TArray<FChunkBlock> ChunkBlocks;
	FIoWriteOptions Options;
	FGraphEventRef HashBarrier;
	FGraphEventRef HashTask;
	FGraphEventRef CreateChunkBlocksBarrier;
	FGraphEventRef CreateChunkBlocksTask;
	FEvent* WriteCompletedEvent = nullptr;
	int32 PartitionIndex = -1;
	bool bAdded = false;
	bool bModified = false;
};

class FIoStoreWriteQueue
{
public:
	FIoStoreWriteQueue()
		: Event(FPlatformProcess::GetSynchEventFromPool(false))
	{ }
	
	~FIoStoreWriteQueue()
	{
		check(Head == nullptr && Tail == nullptr);
		FPlatformProcess::ReturnSynchEventToPool(Event);
	}

	void Enqueue(FIoStoreWriteQueueEntry* Entry)
	{
		check(!bIsDoneAdding);
		{
			FScopeLock _(&CriticalSection);

			if (!Tail)
			{
				Head = Tail = Entry;
			}
			else
			{
				Tail->Next = Entry;
				Tail = Entry;
			}
			Entry->Next = nullptr;
		}

		Event->Trigger();
	}

	FIoStoreWriteQueueEntry* DequeueOrWait()
	{
		for (;;)
		{
			{
				FScopeLock _(&CriticalSection);
				if (Head)
				{
					FIoStoreWriteQueueEntry* Entry = Head;
					Head = Tail = nullptr;
					return Entry;
				}
			}

			if (bIsDoneAdding)
			{
				break;
			}

			Event->Wait();
		}

		return nullptr;
	}

	void CompleteAdding()
	{
		bIsDoneAdding = true;
		Event->Trigger();
	}

	bool IsEmpty() const
	{
		FScopeLock _(&CriticalSection);
		return Head == nullptr;
	}

private:
	mutable FCriticalSection CriticalSection;
	FEvent* Event = nullptr;
	FIoStoreWriteQueueEntry* Head = nullptr;
	FIoStoreWriteQueueEntry* Tail = nullptr;
	TAtomic<bool> bIsDoneAdding { false };
};

class FIoStoreWriterContextImpl
{
	static constexpr uint64 DefaultMemoryLimit = 5ull * (2ull << 30ull);
public:
	FIoStoreWriterContextImpl()
	{
	}

	~FIoStoreWriterContextImpl()
	{
		CompressionQueue.CompleteAdding();
		WriteQueue.CompleteAdding();
		CompressorThread.Wait();
		WriterThread.Wait();
		if (MemoryFreedEvent)
		{
			FPlatformProcess::ReturnSynchEventToPool(MemoryFreedEvent);
		}
	}

	UE_NODISCARD FIoStatus Initialize(const FIoStoreWriterSettings& InWriterSettings)
	{
		WriterSettings = InWriterSettings;
		MemoryFreedEvent = FPlatformProcess::GetSynchEventFromPool(false);

		PaddingBuffer.SetNumZeroed(int32(WriterSettings.CompressionBlockSize));
		CompressorThread = Async(EAsyncExecution::Thread, [this]() { CompressorThreadFunc(); });
		WriterThread = Async(EAsyncExecution::Thread, [this]() { WriterThreadFunc(); });

		return FIoStatus::Ok;
	}

	FIoStoreWriterContext::FProgress GetProgress() const
	{
		FIoStoreWriterContext::FProgress Progress;
		Progress.TotalChunksCount = TotalChunksCount.Load();
		Progress.HashedChunksCount = HashedChunksCount.Load();
		Progress.CompressedChunksCount = CompressedChunksCount.Load();
		Progress.SerializedChunksCount = SerializedChunksCount.Load();
		return Progress;
	}

	const FIoStoreWriterSettings& GetSettings() const
	{
		return WriterSettings;
	}

	void BeginCompress(FIoStoreWriteQueueEntry* QueueEntry)
	{
		CompressionQueue.Enqueue(QueueEntry);
	}

	void BeginWrite(FIoStoreWriteQueueEntry* QueueEntry)
	{
		WriteQueue.Enqueue(QueueEntry);
	}

private:
	void BeginCompressEntry(FIoStoreWriteQueueEntry* Entry)
	{
		check(!Entry->CompressedSize);
		check(!Entry->ChunkBuffer.DataSize());
		uint64 LocalUsedBufferMemory = UsedBufferMemory.Load();
		while (LocalUsedBufferMemory > 0 && LocalUsedBufferMemory + Entry->UncompressedSize > DefaultMemoryLimit)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(WaitForBufferMemory);
			MemoryFreedEvent->Wait();
			LocalUsedBufferMemory = UsedBufferMemory.Load();
		}
		UsedBufferMemory.AddExchange(Entry->UncompressedSize);
		Entry->Request->PrepareSourceBufferAsync(Entry->CreateChunkBlocksBarrier);
	}

	void WriteEntry(FIoStoreWriteQueueEntry* Entry)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(WriteToContainerFile);
		if (Entry->Padding > 0)
		{
			if (PaddingBuffer.Num() < Entry->Padding)
			{
				PaddingBuffer.SetNumZeroed(int32(Entry->Padding));
			}
			Entry->ContainerArchive->Serialize(PaddingBuffer.GetData(), Entry->Padding);
		}
		check(Entry->Offset == Entry->ContainerArchive->Tell());
		Entry->ContainerArchive->Serialize(Entry->ChunkBuffer.Data(), Entry->ChunkBuffer.DataSize());
		Entry->ChunkBuffer = FIoBuffer();
		uint64 UsedBefore = UsedBufferMemory.SubExchange(Entry->UncompressedSize);
		check(UsedBefore >= Entry->UncompressedSize);
		MemoryFreedEvent->Trigger();
		if (Entry->WriteCompletedEvent)
		{
			Entry->WriteCompletedEvent->Trigger();
		}
		SerializedChunksCount.IncrementExchange();
	}

	void CompressorThreadFunc()
	{
		for (;;)
		{
			FIoStoreWriteQueueEntry* Entry = CompressionQueue.DequeueOrWait();
			if (!Entry)
			{
				return;
			}
			while (Entry)
			{
				FIoStoreWriteQueueEntry* Next = Entry->Next;
				BeginCompressEntry(Entry);
				Entry = Next;
			}
		}
	}

	void WriterThreadFunc()
	{
		for (;;)
		{
			FIoStoreWriteQueueEntry* Entry = WriteQueue.DequeueOrWait();
			if (!Entry)
			{
				return;
			}
			while (Entry)
			{
				FIoStoreWriteQueueEntry* Next = Entry->Next;
				WriteEntry(Entry);
				Entry = Next;
			}
		}
	}

	FIoStoreWriterSettings WriterSettings;
	FCriticalSection CriticalSection;
	FEvent* MemoryFreedEvent = nullptr;
	TAtomic<uint64> UsedBufferMemory{ 0 };
	TFuture<void> CompressorThread;
	TFuture<void> WriterThread;
	FIoStoreWriteQueue CompressionQueue;
	FIoStoreWriteQueue WriteQueue;
	TArray<uint8> PaddingBuffer;
	TAtomic<uint64> TotalChunksCount{ 0 };
	TAtomic<uint64> HashedChunksCount{ 0 };
	TAtomic<uint64> CompressedChunksCount{ 0 };
	TAtomic<uint64> SerializedChunksCount{ 0 };
	
	friend class FIoStoreWriterImpl;
};

FIoStoreWriterContext::FIoStoreWriterContext()
	: Impl(new FIoStoreWriterContextImpl())
{

}

FIoStoreWriterContext::~FIoStoreWriterContext()
{
	delete Impl;
}

UE_NODISCARD FIoStatus FIoStoreWriterContext::Initialize(const FIoStoreWriterSettings& InWriterSettings)
{
	return Impl->Initialize(InWriterSettings);
}

FIoStoreWriterContext::FProgress FIoStoreWriterContext::GetProgress() const
{
	return Impl->GetProgress();
}

class FIoStoreToc
{
public:
	FIoStoreToc()
	{
		FMemory::Memzero(&Toc.Header, sizeof(FIoStoreTocHeader));
	}

	void Initialize()
	{
		ChunkIdToIndex.Empty(false);

		for (int32 ChunkIndex = 0; ChunkIndex < Toc.ChunkIds.Num(); ++ChunkIndex)
		{
			ChunkIdToIndex.Add(Toc.ChunkIds[ChunkIndex], ChunkIndex);
		}
	}

	int32 AddChunkEntry(const FIoChunkId& ChunkId, const FIoOffsetAndLength& OffsetLength, const FIoStoreTocEntryMeta& Meta)
	{
		int32& Index = ChunkIdToIndex.FindOrAdd(ChunkId);

		if (!Index)
		{
			Index = Toc.ChunkIds.Add(ChunkId);
			Toc.ChunkOffsetLengths.Add(OffsetLength);
			Toc.ChunkMetas.Add(Meta);

			return Index;
		}

		return INDEX_NONE;
	}

	FIoStoreTocCompressedBlockEntry& AddCompressionBlockEntry()
	{
		return Toc.CompressionBlocks.AddDefaulted_GetRef();
	}

	FSHAHash& AddBlockSignatureEntry()
	{
		return Toc.ChunkBlockSignatures.AddDefaulted_GetRef();
	}

	uint8 AddCompressionMethodEntry(FName CompressionMethod)
	{
		if (CompressionMethod == NAME_None)
		{
			return 0;
		}

		uint8 Index = 1;
		for (const FName& Name : Toc.CompressionMethods)
		{
			if (Name == CompressionMethod)
			{
				return Index;
			}
			++Index;
		}

		return 1 + uint8(Toc.CompressionMethods.Add(CompressionMethod));
	}

	void AddToFileIndex(FString FileName, int32 TocEntryIndex)
	{
		FilesToIndex.Emplace(MoveTemp(FileName));
		FileTocEntryIndices.Add(TocEntryIndex);
	}

	FIoStoreTocResource& GetTocResource()
	{
		return Toc;
	}

	const FIoStoreTocResource& GetTocResource() const
	{
		return Toc;
	}

	const int32* GetTocEntryIndex(const FIoChunkId& ChunkId) const
	{
		return ChunkIdToIndex.Find(ChunkId);
	}

	const FIoOffsetAndLength* GetOffsetAndLength(const FIoChunkId& ChunkId) const
	{
		if (const int32* Index = ChunkIdToIndex.Find(ChunkId))
		{
			return &Toc.ChunkOffsetLengths[*Index];
		}

		return nullptr;
	}

	const TArray<FString>& GetFilesToIndex() const
	{
		return FilesToIndex;
	}

	const TArray<uint32>& GetFileTocEntryIndices() const
	{
		return FileTocEntryIndices;
	}

private:
	TMap<FIoChunkId, int32> ChunkIdToIndex;
	FIoStoreTocResource Toc;
	TArray<FString> FilesToIndex;
	TArray<uint32> FileTocEntryIndices;
};

class FIoStoreWriterImpl
{
public:
	FIoStoreWriterImpl(FIoStoreEnvironment& InEnvironment)
		: Environment(InEnvironment)
	{
	}

	UE_NODISCARD FIoStatus Initialize(FIoStoreWriterContextImpl& InContext, const FIoContainerSettings& InContainerSettings, const TArray<TUniquePtr<FIoStoreReader>>& PatchSourceReaders)
	{
		WriterContext = &InContext;
		ContainerSettings = InContainerSettings;

		TocFilePath = Environment.GetPath() + TEXT(".utoc");
		
		IPlatformFile& Ipf = IPlatformFile::GetPlatformPhysical();
		Ipf.CreateDirectoryTree(*FPaths::GetPath(TocFilePath));

		FIoStatus Status = FIoStatus::Ok;
		if (InContext.GetSettings().bEnableCsvOutput)
		{
			Status = EnableCsvOutput();
		}

		PrepareLayout(PatchSourceReaders);

		return Status;
	}

	FIoStatus EnableCsvOutput()
	{
		FString CsvFilePath = Environment.GetPath() + TEXT(".csv");
		CsvArchive.Reset(IFileManager::Get().CreateFileWriter(*CsvFilePath));
		if (!CsvArchive)
		{
			return FIoStatusBuilder(EIoErrorCode::FileOpenFailed) << TEXT("Failed to open IoStore CSV file '") << *CsvFilePath << TEXT("'");
		}
		ANSICHAR Header[] = "Name,Offset,Size\n";
		CsvArchive->Serialize(Header, sizeof(Header) - 1);

		return FIoStatus::Ok;
	}

	void Append(const FIoChunkId& ChunkId, IIoStoreWriteRequest* Request, const FIoWriteOptions& WriteOptions)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(AppendWriteRequest);
		checkf(ChunkId.IsValid(), TEXT("ChunkId is not valid!"));
		IsMetadataDirty = true;

		FIoStoreWriteQueueEntry* Entry = new FIoStoreWriteQueueEntry();
		Entry->Sequence = Entries.Num();
		WriterContext->TotalChunksCount.IncrementExchange();
		Entries.Add(Entry);
		Entry->ChunkId = ChunkId;
		Entry->Options = WriteOptions;
		Entry->Request = Request;
		Entry->HashBarrier = FGraphEvent::CreateGraphEvent();
		FGraphEventArray HashPrereqs;
		HashPrereqs.Add(Entry->HashBarrier);
		Entry->HashTask = FFunctionGraphTask::CreateAndDispatchWhenReady([this, Entry]()
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(HashChunk);
			FIoBuffer ChunkBuffer = Entry->Request->ConsumeSourceBuffer();
			Entry->UncompressedSize = ChunkBuffer.DataSize();
			Entry->ChunkHash = FIoChunkHash::HashBuffer(ChunkBuffer.Data(), ChunkBuffer.DataSize());
			WriterContext->HashedChunksCount.IncrementExchange();
		}, TStatId(), &HashPrereqs, ENamedThreads::AnyHiPriThreadHiPriTask);
		
		Entry->CreateChunkBlocksBarrier = FGraphEvent::CreateGraphEvent();
		FGraphEventArray CreateBlocksPrereqs;
		CreateBlocksPrereqs.Add(Entry->CreateChunkBlocksBarrier);
		Entry->CreateChunkBlocksTask = FFunctionGraphTask::CreateAndDispatchWhenReady([this, Entry]()
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(CreateChunkBlocks);
			Entry->ChunkBuffer = Entry->Request->ConsumeSourceBuffer();
			CreateChunkBlocks(Entry, ContainerSettings, WriterContext->GetSettings());
			WriterContext->CompressedChunksCount.IncrementExchange();
		}, TStatId(), &CreateBlocksPrereqs, ENamedThreads::AnyHiPriThreadHiPriTask);
		
		Entry->Request->PrepareSourceBufferAsync(Entry->HashBarrier);
	}

	UE_NODISCARD TIoStatusOr<FIoStoreWriterResult> Flush()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FlushContainer);

		if (!IsMetadataDirty)
		{
			return Result;
		}

		IsMetadataDirty = false;

		const FIoStoreWriterSettings& Settings = WriterContext->GetSettings();
		uint64 UncompressedFileOffset = 0;

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(WaitForChunkHashes);
			Algo::Reverse(Entries);
			for (FIoStoreWriteQueueEntry* Entry : Entries)
			{
				FTaskGraphInterface::Get().WaitUntilTaskCompletes(Entry->HashTask);
				Entry = Entry->Next;
			}
		}
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FinalizeLayout);
			FinalizeLayout(Entries);
		}

		for (FIoStoreWriteQueueEntry* Entry : Entries)
		{
			WriterContext->BeginCompress(Entry);
		}

		const uint64 MaxPartitionSize = Settings.MaxPartitionSize > 0 ? Settings.MaxPartitionSize : MAX_uint64;
		uint64 TotalEntryUncompressedSize = 0;
		int32 CurrentPartitionIndex = 0;
		FIoStoreWriteQueueEntry* LastEntry = Entries.Num() ? Entries.Last() : nullptr;
		if (LastEntry)
		{
			LastEntry->WriteCompletedEvent = FPlatformProcess::GetSynchEventFromPool(false);
		}
		bool bHasMemoryMappedEntry = false;
		for (FIoStoreWriteQueueEntry* Entry : Entries)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(WriteChunk);
			FTaskGraphInterface::Get().WaitUntilTaskCompletes(Entry->CreateChunkBlocksTask);
			
			uint64 EntryWriteSize = Entry->ChunkBuffer.DataSize();
			FPartition* TargetPartition = &Partitions[CurrentPartitionIndex];
			int32 NextPartitionIndexToTry = CurrentPartitionIndex + 1;
			if (Entry->PartitionIndex >= 0)
			{
				TargetPartition = &Partitions[Entry->PartitionIndex];
				if (TargetPartition->ReservedSpace > Entry->CompressedSize)
				{
					TargetPartition->ReservedSpace -= Entry->CompressedSize;
				}
				else
				{
					TargetPartition->ReservedSpace = 0;
				}
				NextPartitionIndexToTry = CurrentPartitionIndex;
			}
			bHasMemoryMappedEntry |= Entry->Options.bIsMemoryMapped;
			const uint64 ChunkAlignment = Entry->Options.bIsMemoryMapped ? Settings.MemoryMappingAlignment : 0;
			checkf(EntryWriteSize <= MaxPartitionSize, TEXT("Chunk is too large, increase max partition size!"));
			for (;;)
			{
				uint64 OffsetBeforePadding = TargetPartition->Offset;
				if (ChunkAlignment)
				{
					TargetPartition->Offset = Align(TargetPartition->Offset, ChunkAlignment);
				}
				if (Settings.CompressionBlockAlignment)
				{
					bool bCrossesBlockBoundary = Align(TargetPartition->Offset, Settings.CompressionBlockAlignment) != Align(TargetPartition->Offset + EntryWriteSize - 1, Settings.CompressionBlockAlignment);
					if (bCrossesBlockBoundary)
					{
						TargetPartition->Offset = Align(TargetPartition->Offset, Settings.CompressionBlockAlignment);
					}
				}

				if (TargetPartition->Offset + EntryWriteSize + TargetPartition->ReservedSpace > MaxPartitionSize)
				{
					TargetPartition->Offset = OffsetBeforePadding;
					while (Partitions.Num() <= NextPartitionIndexToTry)
					{
						FPartition& NewPartition = Partitions.AddDefaulted_GetRef();
						NewPartition.Index = Partitions.Num() - 1;
					}
					CurrentPartitionIndex = NextPartitionIndexToTry;
					TargetPartition = &Partitions[CurrentPartitionIndex];
					++NextPartitionIndexToTry;
				}
				else
				{
					Entry->Padding = TargetPartition->Offset - OffsetBeforePadding;
					TotalPaddedBytes += Entry->Padding;
					break;
				}
			}

			if (!TargetPartition->ContainerFileHandle)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(CreatePartitionContainerFile);
				CreatePartitionContainerFile(*TargetPartition);
			}
			Entry->ContainerArchive = TargetPartition->ContainerFileHandle.Get();
			Entry->Offset = TargetPartition->Offset;
			WriterContext->BeginWrite(Entry);
			
			FIoOffsetAndLength OffsetLength;
			OffsetLength.SetOffset(UncompressedFileOffset);
			OffsetLength.SetLength(Entry->UncompressedSize);

			FIoStoreTocEntryMeta ChunkMeta{ Entry->ChunkHash, FIoStoreTocEntryMetaFlags::None };
			if (Entry->Options.bIsMemoryMapped)
			{
				ChunkMeta.Flags |= FIoStoreTocEntryMetaFlags::MemoryMapped;
			}

			for (const FChunkBlock& ChunkBlock : Entry->ChunkBlocks)
			{
				check(ChunkBlock.Offset + ChunkBlock.Size <= EntryWriteSize);

				FIoStoreTocCompressedBlockEntry& BlockEntry = Toc.AddCompressionBlockEntry();
				BlockEntry.SetOffset(TargetPartition->Index * Settings.MaxPartitionSize + TargetPartition->Offset + ChunkBlock.Offset);
				BlockEntry.SetCompressedSize(uint32(ChunkBlock.CompressedSize));
				BlockEntry.SetUncompressedSize(uint32(ChunkBlock.UncompressedSize));
				BlockEntry.SetCompressionMethodIndex(Toc.AddCompressionMethodEntry(ChunkBlock.CompressionMethod));

				if (!ChunkBlock.CompressionMethod.IsNone())
				{
					ChunkMeta.Flags |= FIoStoreTocEntryMetaFlags::Compressed;
				}

				if (ContainerSettings.IsSigned())
				{
					FSHAHash& Signature = Toc.AddBlockSignatureEntry();
					Signature = ChunkBlock.Signature;
				}
			}

			const int32 TocEntryIndex = Toc.AddChunkEntry(Entry->ChunkId, OffsetLength, ChunkMeta);
			check(TocEntryIndex != INDEX_NONE);

			if (ContainerSettings.IsIndexed() && Entry->Options.FileName.Len() > 0)
			{
				Toc.AddToFileIndex(Entry->Options.FileName, TocEntryIndex);
			}

			const uint64 RegionStartOffset = TargetPartition->Offset;
			TargetPartition->Offset += EntryWriteSize;
			UncompressedFileOffset += Align(Entry->UncompressedSize, Settings.CompressionBlockSize);
			TotalEntryUncompressedSize += Entry->UncompressedSize;

			if (Settings.bEnableFileRegions)
			{
				FFileRegion::AccumulateFileRegions(TargetPartition->AllFileRegions, RegionStartOffset, RegionStartOffset, TargetPartition->Offset, Entry->Request->GetRegions());
			}
		}
		if (LastEntry)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(WaitForWritesToComplete);
			LastEntry->WriteCompletedEvent->Wait();
			FPlatformProcess::ReturnSynchEventToPool(LastEntry->WriteCompletedEvent);
		}
		
		UncompressedContainerSize = TotalEntryUncompressedSize + TotalPaddedBytes;
		CompressedContainerSize = 0;
		for (FPartition& Partition : Partitions)
		{
			CompressedContainerSize += Partition.Offset;

			if (bHasMemoryMappedEntry)
			{
				uint64 ExtraPaddingBytes = Align(Partition.Offset, Settings.MemoryMappingAlignment) - Partition.Offset;
				if (ExtraPaddingBytes)
				{
					TArray<uint8> Padding;
					Padding.SetNumZeroed(int32(ExtraPaddingBytes));
					Partition.ContainerFileHandle->Serialize(Padding.GetData(), ExtraPaddingBytes);
					CompressedContainerSize += ExtraPaddingBytes;
					UncompressedContainerSize += ExtraPaddingBytes;
					Partition.Offset += ExtraPaddingBytes;
				}
			}
			
			if (Partition.ContainerFileHandle)
			{
				Partition.ContainerFileHandle->Flush();
				check(Partition.ContainerFileHandle->Tell() == Partition.Offset);
			}

			if (Partition.RegionsArchive)
			{
				FFileRegion::SerializeFileRegions(*Partition.RegionsArchive.Get(), Partition.AllFileRegions);
				Partition.RegionsArchive->Flush();
			}
		}

		FIoStoreTocResource& TocResource = Toc.GetTocResource();

		if (ContainerSettings.IsIndexed())
		{
			const TArray<FString>& FilesToIndex = Toc.GetFilesToIndex();
			const TArray<uint32>& FileTocEntryIndices = Toc.GetFileTocEntryIndices();

			FString MountPoint = IoDirectoryIndexUtils::GetCommonRootPath(FilesToIndex);
			FIoDirectoryIndexWriter DirectoryIndexWriter;
			DirectoryIndexWriter.SetMountPoint(MountPoint);

			check(FilesToIndex.Num() == FileTocEntryIndices.Num());
			for (int32 FileIndex = 0; FileIndex < FilesToIndex.Num(); ++FileIndex)
			{
				const uint32 FileEntryIndex = DirectoryIndexWriter.AddFile(FilesToIndex[FileIndex]);
				check(FileEntryIndex != ~uint32(0));
				DirectoryIndexWriter.SetFileUserData(FileEntryIndex, FileTocEntryIndices[FileIndex]);
			}

			DirectoryIndexWriter.Flush(
				TocResource.DirectoryIndexBuffer,
				ContainerSettings.IsEncrypted() ? ContainerSettings.EncryptionKey : FAES::FAESKey());
		}

		TIoStatusOr<uint64> TocSize = FIoStoreTocResource::Write(*TocFilePath, TocResource, ContainerSettings, WriterContext->GetSettings());
		if (!TocSize.IsOk())
		{
			return TocSize.Status();
		}

		Result.ContainerId = ContainerSettings.ContainerId;
		Result.ContainerName = FPaths::GetBaseFilename(TocFilePath);
		Result.ContainerFlags = ContainerSettings.ContainerFlags;
		Result.TocSize = TocSize.ConsumeValueOrDie();
		Result.TocEntryCount = TocResource.Header.TocEntryCount;
		Result.PaddingSize = TotalPaddedBytes;
		Result.UncompressedContainerSize = UncompressedContainerSize;
		Result.CompressedContainerSize = CompressedContainerSize;
		Result.DirectoryIndexSize = TocResource.Header.DirectoryIndexSize;
		Result.CompressionMethod = EnumHasAnyFlags(ContainerSettings.ContainerFlags, EIoContainerFlags::Compressed)
			? WriterContext->GetSettings().CompressionMethod
			: NAME_None;
		Result.ModifiedChunksCount = 0;
		Result.AddedChunksCount = 0;
		Result.ModifiedChunksSize= 0;
		Result.AddedChunksSize = 0;
		for (FIoStoreWriteQueueEntry* Entry : Entries)
		{
			if (Entry->bModified)
			{
				++Result.ModifiedChunksCount;
				Result.ModifiedChunksSize += Entry->CompressedSize;
			}
			else if (Entry->bAdded)
			{
				++Result.AddedChunksCount;
				Result.AddedChunksSize += Entry->CompressedSize;
			}
			delete Entry->Request;
			delete Entry;
		}

		Entries.Empty();

		return Result;
	}

private:
	struct FPartition
	{
		TUniquePtr<FArchive> ContainerFileHandle;
		TUniquePtr<FArchive> RegionsArchive;
		uint64 Offset = 0;
		uint64 ReservedSpace = 0;
		TArray<FFileRegion> AllFileRegions;
		int32 Index = -1;
	};

	struct FLayoutEntry
	{
		FLayoutEntry* Prev = nullptr;
		FLayoutEntry* Next = nullptr;
		uint64 IdealOrder = 0;
		uint64 CompressedSize = uint64(-1);
		FIoChunkHash Hash;
		FIoStoreWriteQueueEntry* QueueEntry = nullptr;
		int32 PartitionIndex = -1;
	};

	void PrepareLayout(const TArray<TUniquePtr<FIoStoreReader>>& PatchSourceReaders)
	{
		LayoutEntriesHead = new FLayoutEntry();
		LayoutEntries.Add(LayoutEntriesHead);
		FLayoutEntry* PrevEntryLink = LayoutEntriesHead;

		for (const TUniquePtr<FIoStoreReader>& PatchSourceReader : PatchSourceReaders)
		{
			PatchSourceReader->EnumerateChunks([this, &PrevEntryLink](const FIoStoreTocChunkInfo& ChunkInfo)
			{
				FLayoutEntry* PreviousBuildEntry = new FLayoutEntry();
				LayoutEntries.Add(PreviousBuildEntry);

				PreviousBuildEntry->Hash = ChunkInfo.Hash;
				PreviousBuildEntry->PartitionIndex = ChunkInfo.PartitionIndex;
				PreviousBuildEntry->CompressedSize = ChunkInfo.CompressedSize;
				PrevEntryLink->Next = PreviousBuildEntry;
				PreviousBuildEntry->Prev = PrevEntryLink;
				PrevEntryLink = PreviousBuildEntry;
				PreviousBuildLayoutEntryByChunkId.Add(ChunkInfo.Id, PreviousBuildEntry);
				return true;
			});

			if (!ContainerSettings.bGenerateDiffPatch)
			{
				break;
			}
		}

		LayoutEntriesTail = new FLayoutEntry();
		LayoutEntries.Add(LayoutEntriesTail);
		PrevEntryLink->Next = LayoutEntriesTail;
		LayoutEntriesTail->Prev = PrevEntryLink;
	}

	void FinalizeLayout(TArray<FIoStoreWriteQueueEntry*>& WriteQueueEntries)
	{
		FPartition& Partition = Partitions.AddDefaulted_GetRef();
		Partition.Index = 0;

		Algo::Sort(WriteQueueEntries, [](const FIoStoreWriteQueueEntry* A, const FIoStoreWriteQueueEntry* B)
		{
			uint64 AOrderHint = A->Request->GetOrderHint();
			uint64 BOrderHint = B->Request->GetOrderHint();
			if (AOrderHint != BOrderHint)
			{
				return AOrderHint < BOrderHint;
			}
			return A->Sequence < B->Sequence;
		});

		TMap<int64, FLayoutEntry*> LayoutEntriesByOrderMap;
		int64 IdealOrder = 0;
		TArray<FLayoutEntry*> UnassignedEntries;
		for (FIoStoreWriteQueueEntry* WriteQueueEntry : WriteQueueEntries)
		{
			FLayoutEntry* FindPreviousEntry = PreviousBuildLayoutEntryByChunkId.FindRef(WriteQueueEntry->ChunkId);
			if (FindPreviousEntry)
			{
				if (FindPreviousEntry->Hash != WriteQueueEntry->ChunkHash)
				{
					WriteQueueEntry->bModified = true;
				}
				else
				{
					FindPreviousEntry->QueueEntry = WriteQueueEntry;
					FindPreviousEntry->IdealOrder = IdealOrder;
					WriteQueueEntry->PartitionIndex = FindPreviousEntry->PartitionIndex;
				}
			}
			else
			{
				WriteQueueEntry->bAdded = true;
			}
			if (WriteQueueEntry->bModified | WriteQueueEntry->bAdded)
			{
				FLayoutEntry* NewLayoutEntry = new FLayoutEntry();
				NewLayoutEntry->QueueEntry = WriteQueueEntry;
				NewLayoutEntry->IdealOrder = IdealOrder;
				LayoutEntries.Add(NewLayoutEntry);
				UnassignedEntries.Add(NewLayoutEntry);
			}
			++IdealOrder;
		}
			
		if (ContainerSettings.bGenerateDiffPatch)
		{
			LayoutEntriesHead->Next = LayoutEntriesTail;
			LayoutEntriesTail->Prev = LayoutEntriesHead;
		}
		else
		{
			for (FLayoutEntry* EntryIt = LayoutEntriesHead->Next; EntryIt != LayoutEntriesTail; EntryIt = EntryIt->Next)
			{
				if (!EntryIt->QueueEntry)
				{
					EntryIt->Prev->Next = EntryIt->Next;
					EntryIt->Next->Prev = EntryIt->Prev;
				}
				else
				{
					LayoutEntriesByOrderMap.Add(EntryIt->IdealOrder, EntryIt);
				}
			}
		}
		FLayoutEntry* LastAddedEntry = LayoutEntriesHead;
		for (FLayoutEntry* UnassignedEntry : UnassignedEntries)
		{
			check(UnassignedEntry->QueueEntry);
			FLayoutEntry* PutAfterEntry = LayoutEntriesByOrderMap.FindRef(UnassignedEntry->IdealOrder - 1);
			if (!PutAfterEntry)
			{
				PutAfterEntry = LastAddedEntry;
			}

			UnassignedEntry->Prev = PutAfterEntry;
			UnassignedEntry->Next = PutAfterEntry->Next;
			PutAfterEntry->Next->Prev = UnassignedEntry;
			PutAfterEntry->Next = UnassignedEntry;
			LayoutEntriesByOrderMap.Add(UnassignedEntry->IdealOrder, UnassignedEntry);
			LastAddedEntry = UnassignedEntry;
		}

		TArray<FIoStoreWriteQueueEntry*> IncludedQueueEntries;
		for (FLayoutEntry* EntryIt = LayoutEntriesHead->Next; EntryIt != LayoutEntriesTail; EntryIt = EntryIt->Next)
		{
			check(EntryIt->QueueEntry);
			IncludedQueueEntries.Add(EntryIt->QueueEntry);
			int32 ReserveInPartitionIndex = EntryIt->QueueEntry->PartitionIndex;
			if (ReserveInPartitionIndex >= 0)
			{
				while (Partitions.Num() <= ReserveInPartitionIndex)
				{
					FPartition& NewPartition = Partitions.AddDefaulted_GetRef();
					NewPartition.Index = Partitions.Num() - 1;
				}
				FPartition& ReserveInPartition = Partitions[ReserveInPartitionIndex];
				check(EntryIt->CompressedSize != uint64(-1));
				ReserveInPartition.ReservedSpace += EntryIt->CompressedSize;
			}
		}
		Swap(WriteQueueEntries, IncludedQueueEntries);

		LayoutEntriesHead = nullptr;
		LayoutEntriesTail = nullptr;
		PreviousBuildLayoutEntryByChunkId.Empty();
		for (FLayoutEntry* Entry : LayoutEntries)
		{
			delete Entry;
		}
		LayoutEntries.Empty();
	}

	FIoStatus CreatePartitionContainerFile(FPartition& Partition)
	{
		check(!Partition.ContainerFileHandle);
		FString ContainerFilePath = Environment.GetPath();
		if (Partition.Index > 0)
		{
			ContainerFilePath += FString::Printf(TEXT("_s%d"), Partition.Index);
		}
		ContainerFilePath += TEXT(".ucas");
		
		Partition.ContainerFileHandle.Reset(IFileManager::Get().CreateFileWriter(*ContainerFilePath));
		//IPlatformFile& Ipf = IPlatformFile::GetPlatformPhysical();
		//Partition.ContainerFileHandle.Reset(Ipf.OpenWrite(*ContainerFilePath, /* append */ false, /* allowread */ true));
		if (!Partition.ContainerFileHandle)
		{
			return FIoStatusBuilder(EIoErrorCode::FileOpenFailed) << TEXT("Failed to open IoStore container file '") << *ContainerFilePath << TEXT("'");
		}
		if (WriterContext->GetSettings().bEnableFileRegions)
		{
			FString RegionsFilePath = ContainerFilePath + FFileRegion::RegionsFileExtension;
			Partition.RegionsArchive.Reset(IFileManager::Get().CreateFileWriter(*RegionsFilePath));
			if (!Partition.RegionsArchive)
			{
				return FIoStatusBuilder(EIoErrorCode::FileOpenFailed) << TEXT("Failed to open IoStore regions file '") << *RegionsFilePath << TEXT("'");
			}
		}

		return FIoStatus::Ok;
	}

	static void CreateChunkBlocks(
		FIoStoreWriteQueueEntry* Entry,
		const FIoContainerSettings& ContainerSettings,
		const FIoStoreWriterSettings& WriterSettings)
	{
		check(WriterSettings.CompressionBlockSize > 0);

		const uint64 NumChunkBlocks = Align(Entry->ChunkBuffer.DataSize(), WriterSettings.CompressionBlockSize) / WriterSettings.CompressionBlockSize;
		Entry->ChunkBlocks.Reserve(int32(NumChunkBlocks));

		auto CreateUncompressedBlocks = [](FIoStoreWriteQueueEntry* UncompressedEntry, const uint64 BlockSize) -> void
		{
			UncompressedEntry->ChunkBlocks.Empty();

			FIoBuffer& ChunkBuffer = UncompressedEntry->ChunkBuffer;

			uint64 UncompressedSize = ChunkBuffer.DataSize();
			uint64 RawSize = UncompressedSize;
			if (!IsAligned(RawSize, FAES::AESBlockSize))
			{
				RawSize = Align(RawSize, FAES::AESBlockSize);
				FIoBuffer AlignedBuffer(RawSize);
				FMemory::Memcpy(AlignedBuffer.Data(), ChunkBuffer.Data(), UncompressedSize);

				for (uint64 FillIndex = UncompressedSize; FillIndex < RawSize ; ++FillIndex)
				{
					AlignedBuffer.Data()[FillIndex] = AlignedBuffer.Data()[(FillIndex - UncompressedSize) % UncompressedSize];
				}

				ChunkBuffer = AlignedBuffer;
			}

			UncompressedEntry->CompressedSize = ChunkBuffer.DataSize();

			uint64 UncompressedOffset = 0;
			uint64 RemainingSize = UncompressedSize;
			while (RemainingSize)
			{
				const uint64 UncompressedBlockSize = FMath::Min<uint64>(RemainingSize, BlockSize);
				const uint64 RawBlockSize = Align(UncompressedBlockSize, FAES::AESBlockSize);
				UncompressedEntry->ChunkBlocks.Add(FChunkBlock { UncompressedOffset, RawBlockSize, UncompressedBlockSize, UncompressedBlockSize, NAME_None });
				RemainingSize -= UncompressedBlockSize;
				UncompressedOffset += RawBlockSize;
			}
		};

		if (ContainerSettings.IsCompressed() && !Entry->Options.bForceUncompressed && !Entry->Options.bIsMemoryMapped)
		{
			check(!WriterSettings.CompressionMethod.IsNone());

			const uint8* UncompressedBlock = Entry->ChunkBuffer.Data();
			TArray<TUniquePtr<uint8[]>> CompressedBlocks;
			CompressedBlocks.Reserve(int32(NumChunkBlocks));

			uint64 BytesToProcess	= Entry->ChunkBuffer.DataSize();
			uint64 BlockOffset		= 0;

			while (BytesToProcess > 0)
			{
				const int32 UncompressedBlockSize = static_cast<int32>(FMath::Min(BytesToProcess, WriterSettings.CompressionBlockSize));
				int32 CompressedBlockSize = FCompression::CompressMemoryBound(WriterSettings.CompressionMethod, UncompressedBlockSize);
				TUniquePtr<uint8[]>& CompressedBlock = CompressedBlocks.AddDefaulted_GetRef();
				CompressedBlock = MakeUnique<uint8[]>(CompressedBlockSize);

				FName CompressionMethod = WriterSettings.CompressionMethod;
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(CompressMemory);
					const bool bCompressed = FCompression::CompressMemory(
						CompressionMethod,
						CompressedBlock.Get(),
						CompressedBlockSize,
						UncompressedBlock,
						UncompressedBlockSize);
					check(bCompressed);
				}
				check(CompressedBlockSize > 0);

				if (CompressedBlockSize >= UncompressedBlockSize)
				{
					memcpy(CompressedBlock.Get(), UncompressedBlock, UncompressedBlockSize);
					CompressedBlockSize = UncompressedBlockSize;
					CompressionMethod = NAME_None;
				}

				// Always align each compressed block to AES block size but store the compressed block size in the TOC
				uint64 AlignedCompressedBlockSize = CompressedBlockSize;
				if (!IsAligned(CompressedBlockSize, FAES::AESBlockSize))
				{
					AlignedCompressedBlockSize = Align(CompressedBlockSize, FAES::AESBlockSize);
					TUniquePtr<uint8[]> AlignedBlock = MakeUnique<uint8[]>(AlignedCompressedBlockSize);

					FMemory::Memcpy(AlignedBlock.Get(), CompressedBlock.Get(), CompressedBlockSize);

					for (uint64 FillIndex = CompressedBlockSize; FillIndex < AlignedCompressedBlockSize ; ++FillIndex)
					{
						AlignedBlock.Get()[FillIndex] = AlignedBlock.Get()[(FillIndex - CompressedBlockSize) % CompressedBlockSize];
					}

					CompressedBlock.Reset(AlignedBlock.Release());
				}

				Entry->ChunkBlocks.Add(FChunkBlock { BlockOffset, AlignedCompressedBlockSize, uint64(CompressedBlockSize), uint64(UncompressedBlockSize), CompressionMethod });

				BytesToProcess		-= UncompressedBlockSize;
				BlockOffset			+= AlignedCompressedBlockSize;
				UncompressedBlock	+= UncompressedBlockSize;
			}

			Entry->CompressedSize = BlockOffset;
			
			Entry->ChunkBuffer = FIoBuffer(Entry->CompressedSize);
			
			uint8* CompressedChunkBuffer = Entry->ChunkBuffer.Data();
			FMemory::Memzero(CompressedChunkBuffer, Entry->CompressedSize); 

			for (int32 BlockIndex = 0; BlockIndex < CompressedBlocks.Num(); ++BlockIndex)
			{
				TUniquePtr<uint8[]>& CompressedBlock = CompressedBlocks[BlockIndex];
				const FChunkBlock& ChunkBlock = Entry->ChunkBlocks[BlockIndex];
				FMemory::Memcpy(CompressedChunkBuffer, CompressedBlock.Get(), ChunkBlock.Size);
				CompressedChunkBuffer += ChunkBlock.Size;
			}
		}
		else
		{
			CreateUncompressedBlocks(Entry, WriterSettings.CompressionBlockSize);
		}

		if (ContainerSettings.IsEncrypted())
		{
			for (FChunkBlock& ChunkBlock : Entry->ChunkBlocks)
			{
				check(IsAligned(ChunkBlock.Size, FAES::AESBlockSize));
				FAES::EncryptData(Entry->ChunkBuffer.Data() + ChunkBlock.Offset, static_cast<uint32>(ChunkBlock.Size), ContainerSettings.EncryptionKey);
			}
		}

		if (ContainerSettings.IsSigned())
		{
			for (FChunkBlock& ChunkBlock : Entry->ChunkBlocks)
			{
				FSHA1::HashBuffer(Entry->ChunkBuffer.Data() + ChunkBlock.Offset, ChunkBlock.Size, ChunkBlock.Signature.Hash);
			}
		}
	}

	FIoStoreEnvironment&		Environment;
	FIoStoreWriterContextImpl*	WriterContext = nullptr;
	FIoContainerSettings		ContainerSettings;
	FString						TocFilePath;
	FIoStoreToc					Toc;
	TArray<FPartition>			Partitions;
	TArray<FIoStoreWriteQueueEntry*> Entries;
	TArray<FLayoutEntry*>		LayoutEntries;
	FLayoutEntry*				LayoutEntriesHead = nullptr;
	FLayoutEntry*				LayoutEntriesTail = nullptr;
	TMap<FIoChunkId, FLayoutEntry*> PreviousBuildLayoutEntryByChunkId;
	TUniquePtr<FArchive>		CsvArchive;
	FIoStoreWriterResult		Result;
	uint64						TotalPaddedBytes = 0;
	uint64						UncompressedContainerSize = 0;
	uint64						CompressedContainerSize = 0;
	bool						IsMetadataDirty = true;
};

FIoStoreWriter::FIoStoreWriter(FIoStoreEnvironment& InEnvironment)
:	Impl(new FIoStoreWriterImpl(InEnvironment))
{
}

FIoStoreWriter::~FIoStoreWriter()
{
	(void)Impl->Flush();
}

FIoStatus FIoStoreWriter::Initialize(const FIoStoreWriterContext& Context, const FIoContainerSettings& ContainerSettings, const TArray<TUniquePtr<FIoStoreReader>>& PatchSourceReaders)
{
	return Impl->Initialize(*Context.Impl, ContainerSettings, PatchSourceReaders);
}

void FIoStoreWriter::Append(const FIoChunkId& ChunkId, FIoBuffer Chunk, const FIoWriteOptions& WriteOptions)
{
	struct FWriteRequest
		: IIoStoreWriteRequest
	{
		FWriteRequest(FIoBuffer InSourceBuffer)
		{
			SourceBuffer = InSourceBuffer;
			SourceBuffer.MakeOwned();
		}

		virtual ~FWriteRequest() = default;

		void PrepareSourceBufferAsync(FGraphEventRef CompletionEvent) override
		{
			TArray<FBaseGraphTask*> NewTasks;
			CompletionEvent->DispatchSubsequents(NewTasks);
		}

		FIoBuffer ConsumeSourceBuffer() override
		{
			return SourceBuffer;
		}

		uint64 GetOrderHint() override
		{
			return MAX_uint64;
		}

		TArrayView<const FFileRegion> GetRegions()
		{
			return TArrayView<const FFileRegion>();
		}

		FIoBuffer SourceBuffer;
	};

	Append(ChunkId, new FWriteRequest(Chunk), WriteOptions);
}

void FIoStoreWriter::Append(const FIoChunkId& ChunkId, IIoStoreWriteRequest* Request, const FIoWriteOptions& WriteOptions)
{
	Impl->Append(ChunkId, Request, WriteOptions);
}

TIoStatusOr<FIoStoreWriterResult> FIoStoreWriter::Flush()
{
	return Impl->Flush();
}

class FIoStoreReaderImpl
{
public:
	FIoStoreReaderImpl()
	{

	}

	UE_NODISCARD FIoStatus Initialize(const FIoStoreEnvironment& InEnvironment, const TMap<FGuid, FAES::FAESKey>& InDecryptionKeys)
	{
		TStringBuilder<256> TocFilePath;
		TocFilePath.Append(InEnvironment.GetPath());
		TocFilePath.Append(TEXT(".utoc"));

		FIoStoreTocResource& TocResource = Toc.GetTocResource();
		FIoStatus TocStatus = FIoStoreTocResource::Read(*TocFilePath, EIoStoreTocReadOptions::ReadAll, TocResource);
		if (!TocStatus.IsOk())
		{
			return TocStatus;
		}

		Toc.Initialize();

		IPlatformFile& Ipf = FPlatformFileManager::Get().GetPlatformFile();
		ContainerFileHandles.Reserve(TocResource.Header.PartitionCount);
		for (uint32 PartitionIndex = 0; PartitionIndex < TocResource.Header.PartitionCount; ++PartitionIndex)
		{
			TStringBuilder<256> ContainerFilePath;
			ContainerFilePath.Append(InEnvironment.GetPath());
			if (PartitionIndex > 0)
			{
				ContainerFilePath.Append(FString::Printf(TEXT("_s%d"), PartitionIndex));
			}
			ContainerFilePath.Append(TEXT(".ucas"));
			ContainerFileHandles.Emplace(Ipf.OpenAsyncRead(*ContainerFilePath));
			if (!ContainerFileHandles.Last())
			{
				return FIoStatusBuilder(EIoErrorCode::FileOpenFailed) << TEXT("Failed to open IoStore container file '") << *TocFilePath << TEXT("'");
			}
		}

		if (EnumHasAnyFlags(TocResource.Header.ContainerFlags, EIoContainerFlags::Encrypted))
		{
			const FAES::FAESKey* FindKey = InDecryptionKeys.Find(TocResource.Header.EncryptionKeyGuid);
			if (!FindKey)
			{
				return FIoStatusBuilder(EIoErrorCode::FileOpenFailed) << TEXT("Missing decryption key for IoStore container file '") << *TocFilePath << TEXT("'");
			}
			DecryptionKey = *FindKey;
		}

		if (EnumHasAnyFlags(TocResource.Header.ContainerFlags, EIoContainerFlags::Indexed) &&
			TocResource.DirectoryIndexBuffer.Num() > 0)
		{
			return DirectoryIndexReader.Initialize(TocResource.DirectoryIndexBuffer, DecryptionKey);
		}

		return FIoStatus::Ok;
	}

	FIoContainerId GetContainerId() const
	{
		return Toc.GetTocResource().Header.ContainerId;
	}

	EIoContainerFlags GetContainerFlags() const
	{
		return Toc.GetTocResource().Header.ContainerFlags;
	}

	FGuid GetEncryptionKeyGuid() const
	{
		return Toc.GetTocResource().Header.EncryptionKeyGuid;
	}

	void EnumerateChunks(TFunction<bool(const FIoStoreTocChunkInfo&)>&& Callback) const
	{
		const FIoStoreTocResource& TocResource = Toc.GetTocResource();

		for (int32 ChunkIndex = 0; ChunkIndex < TocResource.ChunkIds.Num(); ++ChunkIndex)
		{
			FIoStoreTocChunkInfo ChunkInfo = GetTocChunkInfo(ChunkIndex);
			if (!Callback(ChunkInfo))
			{
				break;
			}
		}
	}

	TIoStatusOr<FIoStoreTocChunkInfo> GetChunkInfo(const FIoChunkId& ChunkId) const
	{
		const int32* TocEntryIndex = Toc.GetTocEntryIndex(ChunkId);
		if (TocEntryIndex)
		{
			return GetTocChunkInfo(*TocEntryIndex);
		}
		else
		{
			return FIoStatus(EIoErrorCode::NotFound, TEXT("Not found"));
		}
	}

	TIoStatusOr<FIoStoreTocChunkInfo> GetChunkInfo(const uint32 TocEntryIndex) const
	{
		const FIoStoreTocResource& TocResource = Toc.GetTocResource();

		if (TocEntryIndex < uint32(TocResource.ChunkIds.Num()))
		{
			return GetTocChunkInfo(TocEntryIndex);
		}
		else
		{
			return FIoStatus(EIoErrorCode::InvalidParameter, TEXT("Invalid TocEntryIndex"));
		}
	}

	TIoStatusOr<FIoBuffer> Read(const FIoChunkId& ChunkId, const FIoReadOptions& Options) const
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ReadChunk);

		const FIoOffsetAndLength* OffsetAndLength = Toc.GetOffsetAndLength(ChunkId);
		if (!OffsetAndLength )
		{
			return FIoStatus(EIoErrorCode::NotFound, TEXT("Unknown chunk ID"));
		}

		if (!ThreadBuffers)
		{
			ThreadBuffers = new FThreadBuffers();
			ThreadBuffers->Register();
		}
		TArray<uint8>& CompressedBuffer = ThreadBuffers->CompressedBuffer;
		TArray<uint8>& UncompressedBuffer = ThreadBuffers->UncompressedBuffer;

		const FIoStoreTocResource& TocResource = Toc.GetTocResource();
		const uint64 CompressionBlockSize = TocResource.Header.CompressionBlockSize;
		FIoBuffer IoBuffer = FIoBuffer(OffsetAndLength->GetLength());
		int32 FirstBlockIndex = int32(OffsetAndLength->GetOffset() / CompressionBlockSize);
		int32 LastBlockIndex = int32((Align(OffsetAndLength->GetOffset() + OffsetAndLength->GetLength(), CompressionBlockSize) - 1) / CompressionBlockSize);
		uint64 OffsetInBlock = OffsetAndLength->GetOffset() % CompressionBlockSize;
		uint8* Dst = IoBuffer.Data();
		uint8* Src = nullptr;
		uint64 RemainingSize = OffsetAndLength->GetLength();
		for (int32 BlockIndex = FirstBlockIndex; BlockIndex <= LastBlockIndex; ++BlockIndex)
		{
			const FIoStoreTocCompressedBlockEntry& CompressionBlock = TocResource.CompressionBlocks[BlockIndex];
			uint32 RawSize = Align(CompressionBlock.GetCompressedSize(), FAES::AESBlockSize);
			if (uint32(CompressedBuffer.Num()) < RawSize)
			{
				CompressedBuffer.SetNumUninitialized(RawSize);
			}
			uint32 UncompressedSize = CompressionBlock.GetUncompressedSize();
			if (uint32(UncompressedBuffer.Num()) < UncompressedSize)
			{
				UncompressedBuffer.SetNumUninitialized(UncompressedSize);
			}
		
			int32 PartitionIndex = int32(CompressionBlock.GetOffset() / TocResource.Header.PartitionSize);
			int64 PartitionOffset = int64(CompressionBlock.GetOffset() % TocResource.Header.PartitionSize);
			TUniquePtr<IAsyncReadRequest> ReadRequest(ContainerFileHandles[PartitionIndex]->ReadRequest(PartitionOffset, RawSize, AIOP_Normal, nullptr, CompressedBuffer.GetData()));
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(WaitForIo);
				ReadRequest->WaitCompletion();
			}
			if (EnumHasAnyFlags(TocResource.Header.ContainerFlags, EIoContainerFlags::Encrypted))
			{
				FAES::DecryptData(CompressedBuffer.GetData(), RawSize, DecryptionKey);
			}
			if (CompressionBlock.GetCompressionMethodIndex() == 0)
			{
				Src = CompressedBuffer.GetData();
			}
			else
			{
				FName CompressionMethod = TocResource.CompressionMethods[CompressionBlock.GetCompressionMethodIndex()];
				bool bUncompressed = FCompression::UncompressMemory(CompressionMethod, UncompressedBuffer.GetData(), UncompressedSize, CompressedBuffer.GetData(), CompressionBlock.GetCompressedSize());
				if (!bUncompressed)
				{
					return FIoStatus(EIoErrorCode::CorruptToc, TEXT("Failed uncompressing block"));
				}
				Src = UncompressedBuffer.GetData();
			}
			uint64 SizeInBlock = FMath::Min(CompressionBlockSize - OffsetInBlock, RemainingSize);
			FMemory::Memcpy(Dst, Src + OffsetInBlock, SizeInBlock);
			OffsetInBlock = 0;
			RemainingSize -= SizeInBlock;
			Dst += SizeInBlock;
		}
		
		return IoBuffer;
	}

	const FIoDirectoryIndexReader& GetDirectoryIndexReader() const
	{
		return DirectoryIndexReader;
	}

	bool TocChunkContainsBlockIndex(const int32 TocEntryIndex, const int32 BlockIndex) const
	{
		const FIoStoreTocResource& TocResource = Toc.GetTocResource();
		const FIoOffsetAndLength& OffsetLength = TocResource.ChunkOffsetLengths[TocEntryIndex];

		const uint64 CompressionBlockSize = TocResource.Header.CompressionBlockSize;
		int32 FirstBlockIndex = int32(OffsetLength.GetOffset() / CompressionBlockSize);
		int32 LastBlockIndex = int32((Align(OffsetLength.GetOffset() + OffsetLength.GetLength(), CompressionBlockSize) - 1) / CompressionBlockSize);

		return BlockIndex >= FirstBlockIndex && BlockIndex <= LastBlockIndex;
	}

private:
	FIoStoreTocChunkInfo GetTocChunkInfo(const int32 TocEntryIndex) const
	{
		const FIoStoreTocResource& TocResource = Toc.GetTocResource();
		const FIoStoreTocEntryMeta& Meta = TocResource.ChunkMetas[TocEntryIndex];
		const FIoOffsetAndLength& OffsetLength = TocResource.ChunkOffsetLengths[TocEntryIndex];

		const bool bIsContainerCompressed = EnumHasAnyFlags(TocResource.Header.ContainerFlags, EIoContainerFlags::Compressed);

		FIoStoreTocChunkInfo ChunkInfo;
		ChunkInfo.Id = TocResource.ChunkIds[TocEntryIndex];
		ChunkInfo.Hash = Meta.ChunkHash;
		ChunkInfo.bIsCompressed = EnumHasAnyFlags(Meta.Flags, FIoStoreTocEntryMetaFlags::Compressed);
		ChunkInfo.bIsMemoryMapped = EnumHasAnyFlags(Meta.Flags, FIoStoreTocEntryMetaFlags::MemoryMapped);
		ChunkInfo.bForceUncompressed = bIsContainerCompressed && !EnumHasAnyFlags(Meta.Flags, FIoStoreTocEntryMetaFlags::Compressed);
		ChunkInfo.Offset = OffsetLength.GetOffset();
		ChunkInfo.Size = OffsetLength.GetLength();
		
		const uint64 CompressionBlockSize = TocResource.Header.CompressionBlockSize;
		int32 FirstBlockIndex = int32(OffsetLength.GetOffset() / CompressionBlockSize);
		int32 LastBlockIndex = int32((Align(OffsetLength.GetOffset() + OffsetLength.GetLength(), CompressionBlockSize) - 1) / CompressionBlockSize);

		ChunkInfo.CompressedSize = 0;
		ChunkInfo.PartitionIndex = -1;
		for (int32 BlockIndex = FirstBlockIndex; BlockIndex <= LastBlockIndex; ++BlockIndex)
		{
			const FIoStoreTocCompressedBlockEntry& CompressionBlock = TocResource.CompressionBlocks[BlockIndex];
			ChunkInfo.CompressedSize += CompressionBlock.GetCompressedSize();
			if (ChunkInfo.PartitionIndex < 0)
			{
				ChunkInfo.PartitionIndex = int32(CompressionBlock.GetOffset() / TocResource.Header.PartitionSize);
			}
		}


		return ChunkInfo;
	}

	struct FThreadBuffers
		: public FTlsAutoCleanup
	{
		TArray<uint8> CompressedBuffer;
		TArray<uint8> UncompressedBuffer;
	};

	FIoStoreToc Toc;
	FAES::FAESKey DecryptionKey;
	TArray<TUniquePtr<IAsyncReadFileHandle>> ContainerFileHandles;
	FIoDirectoryIndexReader DirectoryIndexReader;
	static thread_local FThreadBuffers* ThreadBuffers;
};

thread_local FIoStoreReaderImpl::FThreadBuffers* FIoStoreReaderImpl::ThreadBuffers = nullptr;

FIoStoreReader::FIoStoreReader()
	: Impl(new FIoStoreReaderImpl())
{
}

FIoStoreReader::~FIoStoreReader()
{
	delete Impl;
}

FIoStatus FIoStoreReader::Initialize(const FIoStoreEnvironment& InEnvironment, const TMap<FGuid, FAES::FAESKey>& InDecryptionKeys)
{
	return Impl->Initialize(InEnvironment, InDecryptionKeys);
}

FIoContainerId FIoStoreReader::GetContainerId() const
{
	return Impl->GetContainerId();
}

EIoContainerFlags FIoStoreReader::GetContainerFlags() const
{
	return Impl->GetContainerFlags();
}

FGuid FIoStoreReader::GetEncryptionKeyGuid() const
{
	return Impl->GetEncryptionKeyGuid();
}

void FIoStoreReader::EnumerateChunks(TFunction<bool(const FIoStoreTocChunkInfo&)>&& Callback) const
{
	Impl->EnumerateChunks(MoveTemp(Callback));
}

TIoStatusOr<FIoStoreTocChunkInfo> FIoStoreReader::GetChunkInfo(const FIoChunkId& Chunk) const
{
	return Impl->GetChunkInfo(Chunk);
}

TIoStatusOr<FIoStoreTocChunkInfo> FIoStoreReader::GetChunkInfo(const uint32 TocEntryIndex) const
{
	return Impl->GetChunkInfo(TocEntryIndex);
}

TIoStatusOr<FIoBuffer> FIoStoreReader::Read(const FIoChunkId& Chunk, const FIoReadOptions& Options) const
{
	return Impl->Read(Chunk, Options);
}

const FIoDirectoryIndexReader& FIoStoreReader::GetDirectoryIndexReader() const
{
	return Impl->GetDirectoryIndexReader();
}

FIoStatus FIoStoreTocResource::Read(const TCHAR* TocFilePath, EIoStoreTocReadOptions ReadOptions, FIoStoreTocResource& OutTocResource)
{
	check(TocFilePath != nullptr);

	IPlatformFile& Ipf = FPlatformFileManager::Get().GetPlatformFile();
	TUniquePtr<IFileHandle>	TocFileHandle(Ipf.OpenRead(TocFilePath, /* allowwrite */ false));

	if (!TocFileHandle)
	{
		return FIoStatusBuilder(EIoErrorCode::FileOpenFailed) << TEXT("Failed to open IoStore TOC file '") << TocFilePath << TEXT("'");
	}

	// Header
	FIoStoreTocHeader& Header = OutTocResource.Header;
	if (!TocFileHandle->Read(reinterpret_cast<uint8*>(&Header), sizeof(FIoStoreTocHeader)))
	{
		return FIoStatusBuilder(EIoErrorCode::CorruptToc) << TEXT("Failed to read IoStore TOC file '") << TocFilePath << TEXT("'");
	}

	if (!Header.CheckMagic())
	{
		return FIoStatusBuilder(EIoErrorCode::CorruptToc) << TEXT("TOC header magic mismatch while reading '") << TocFilePath << TEXT("'");
	}

	if (Header.TocHeaderSize != sizeof(FIoStoreTocHeader))
	{
		return FIoStatusBuilder(EIoErrorCode::CorruptToc) << TEXT("TOC header size mismatch while reading '") << TocFilePath << TEXT("'");
	}

	if (Header.TocCompressedBlockEntrySize != sizeof(FIoStoreTocCompressedBlockEntry))
	{
		return FIoStatusBuilder(EIoErrorCode::CorruptToc) << TEXT("TOC compressed block entry size mismatch while reading '") << TocFilePath << TEXT("'");
	}

	if (Header.Version < static_cast<uint8>(EIoStoreTocVersion::DirectoryIndex))
	{
		return FIoStatusBuilder(EIoErrorCode::CorruptToc) << TEXT("Outdated TOC header version while reading '") << TocFilePath << TEXT("'");
	}

	const uint64 TotalTocSize = TocFileHandle->Size() - sizeof(FIoStoreTocHeader);
	const uint64 TocMetaSize = Header.TocEntryCount * sizeof(FIoStoreTocEntryMeta);
	const uint64 DefaultTocSize = TotalTocSize - Header.DirectoryIndexSize - TocMetaSize;
	uint64 TocSize = DefaultTocSize;

	if (EnumHasAnyFlags(ReadOptions, EIoStoreTocReadOptions::ReadTocMeta))
	{
		TocSize = TotalTocSize; // Meta data is at the end of the TOC file
	}
	else if (EnumHasAnyFlags(ReadOptions, EIoStoreTocReadOptions::ReadDirectoryIndex))
	{
		TocSize = DefaultTocSize + Header.DirectoryIndexSize;
	}

	TUniquePtr<uint8[]> TocBuffer = MakeUnique<uint8[]>(TocSize);

	if (!TocFileHandle->Read(TocBuffer.Get(), TocSize))
	{
		return FIoStatusBuilder(EIoErrorCode::CorruptToc) << TEXT("Failed to read IoStore TOC file '") << TocFilePath << TEXT("'");
	}

	// Chunk IDs
	const FIoChunkId* ChunkIds = reinterpret_cast<const FIoChunkId*>(TocBuffer.Get());
	OutTocResource.ChunkIds = MakeArrayView<FIoChunkId const>(ChunkIds, Header.TocEntryCount);

	// Chunk offsets
	const FIoOffsetAndLength* ChunkOffsetLengths = reinterpret_cast<const FIoOffsetAndLength*>(ChunkIds + Header.TocEntryCount);
	OutTocResource.ChunkOffsetLengths = MakeArrayView<FIoOffsetAndLength const>(ChunkOffsetLengths, Header.TocEntryCount);

	// Compression blocks
	const FIoStoreTocCompressedBlockEntry* CompressionBlocks = reinterpret_cast<const FIoStoreTocCompressedBlockEntry*>(ChunkOffsetLengths + Header.TocEntryCount);
	OutTocResource.CompressionBlocks = MakeArrayView<FIoStoreTocCompressedBlockEntry const>(CompressionBlocks, Header.TocCompressedBlockEntryCount);

	// Compression methods
	OutTocResource.CompressionMethods.Reserve(Header.CompressionMethodNameCount + 1);
	OutTocResource.CompressionMethods.Add(NAME_None);

	const ANSICHAR* AnsiCompressionMethodNames = reinterpret_cast<const ANSICHAR*>(CompressionBlocks + Header.TocCompressedBlockEntryCount);
	for (uint32 CompressonNameIndex = 0; CompressonNameIndex < Header.CompressionMethodNameCount; CompressonNameIndex++)
	{
		const ANSICHAR* AnsiCompressionMethodName = AnsiCompressionMethodNames + CompressonNameIndex * Header.CompressionMethodNameLength;
		OutTocResource.CompressionMethods.Add(FName(AnsiCompressionMethodName));
	}

	// Chunk block signatures
	const uint8* SignatureBuffer = reinterpret_cast<const uint8*>(AnsiCompressionMethodNames + Header.CompressionMethodNameCount * Header.CompressionMethodNameLength);
	const uint8* DirectoryIndexBuffer = SignatureBuffer;

	const bool bIsSigned = EnumHasAnyFlags(Header.ContainerFlags, EIoContainerFlags::Signed);
	if (IsSigningEnabled() || bIsSigned)
	{
		if (!bIsSigned)
		{
			return FIoStatus(EIoErrorCode::SignatureError, TEXT("Missing signature"));
		}

		const int32* HashSize = reinterpret_cast<const int32*>(SignatureBuffer);
		TArrayView<const uint8> TocSignature = MakeArrayView<const uint8>(reinterpret_cast<const uint8*>(HashSize + 1), *HashSize);
		TArrayView<const uint8> BlockSignature = MakeArrayView<const uint8>(TocSignature.GetData() + *HashSize, *HashSize);
		TArrayView<const FSHAHash> ChunkBlockSignatures = MakeArrayView<const FSHAHash>(reinterpret_cast<const FSHAHash*>(BlockSignature.GetData() + *HashSize), Header.TocCompressedBlockEntryCount);

		// Adjust address to meta data
		DirectoryIndexBuffer = reinterpret_cast<const uint8*>(ChunkBlockSignatures.GetData() + ChunkBlockSignatures.Num());

		OutTocResource.ChunkBlockSignatures = ChunkBlockSignatures;

		if (IsSigningEnabled())
		{
			FIoStatus SignatureStatus = ValidateContainerSignature(GetPublicSigningKey(), Header, OutTocResource.ChunkBlockSignatures, TocSignature, BlockSignature);
			if (!SignatureStatus.IsOk())
			{
				return SignatureStatus;
			}
		}
	}

	// Directory index
	if (EnumHasAnyFlags(ReadOptions, EIoStoreTocReadOptions::ReadDirectoryIndex) &&
		EnumHasAnyFlags(Header.ContainerFlags, EIoContainerFlags::Indexed) &&
		Header.DirectoryIndexSize > 0)
	{
		OutTocResource.DirectoryIndexBuffer = MakeArrayView<const uint8>(DirectoryIndexBuffer, Header.DirectoryIndexSize);
	}

	// Meta
	const uint8* TocMeta = DirectoryIndexBuffer + Header.DirectoryIndexSize;
	if (EnumHasAnyFlags(ReadOptions, EIoStoreTocReadOptions::ReadTocMeta))
	{
		const FIoStoreTocEntryMeta* ChunkMetas = reinterpret_cast<const FIoStoreTocEntryMeta*>(TocMeta);
		OutTocResource.ChunkMetas = MakeArrayView<FIoStoreTocEntryMeta const>(ChunkMetas, Header.TocEntryCount);
	}

	if (Header.Version < static_cast<uint8>(EIoStoreTocVersion::PartitionSize))
	{
		Header.PartitionCount = 1;
		Header.PartitionSize = MAX_uint64;
	}

	return FIoStatus::Ok;
}

TIoStatusOr<uint64> FIoStoreTocResource::Write(
	const TCHAR* TocFilePath,
	FIoStoreTocResource& TocResource,
	const FIoContainerSettings& ContainerSettings,
	const FIoStoreWriterSettings& WriterSettings)
{
	check(TocFilePath != nullptr);

	IPlatformFile& Ipf = FPlatformFileManager::Get().GetPlatformFile();
	TUniquePtr<IFileHandle> TocFileHandle(Ipf.OpenWrite(TocFilePath, /* append */ false, /* allowread */ true));

	if (!TocFileHandle)
	{
		FIoStatus Status = FIoStatusBuilder(EIoErrorCode::FileOpenFailed) << TEXT("Failed to open IoStore TOC file '") << TocFilePath << TEXT("'");
		return Status;
	}

	if (TocResource.ChunkIds.Num() != TocResource.ChunkOffsetLengths.Num())
	{
		return FIoStatus(EIoErrorCode::InvalidParameter, TEXT("Number of TOC chunk IDs doesn't match the number of offsets"));
	}

	if (TocResource.ChunkIds.Num() != TocResource.ChunkMetas.Num())
	{
		return FIoStatus(EIoErrorCode::InvalidParameter, TEXT("Number of TOC chunk IDs doesn't match the number of chunk meta data"));
	}

	FMemory::Memzero(&TocResource.Header, sizeof(FIoStoreTocHeader));

	FIoStoreTocHeader& TocHeader = TocResource.Header;
	TocHeader.MakeMagic();
	TocHeader.Version = static_cast<uint8>(EIoStoreTocVersion::Latest);
	TocHeader.TocHeaderSize = sizeof(TocHeader);
	TocHeader.TocEntryCount = TocResource.ChunkIds.Num();
	TocHeader.TocCompressedBlockEntryCount = TocResource.CompressionBlocks.Num();
	TocHeader.TocCompressedBlockEntrySize = sizeof(FIoStoreTocCompressedBlockEntry);
	TocHeader.CompressionBlockSize = uint32(WriterSettings.CompressionBlockSize);
	TocHeader.CompressionMethodNameCount = TocResource.CompressionMethods.Num();
	TocHeader.CompressionMethodNameLength = FIoStoreTocResource::CompressionMethodNameLen;
	TocHeader.DirectoryIndexSize = TocResource.DirectoryIndexBuffer.Num();
	TocHeader.ContainerId = ContainerSettings.ContainerId;
	TocHeader.EncryptionKeyGuid = ContainerSettings.EncryptionKeyGuid;
	TocHeader.ContainerFlags = ContainerSettings.ContainerFlags;
	if (TocHeader.TocEntryCount == 0)
	{
		TocHeader.PartitionCount = 0;
		TocHeader.PartitionSize = MAX_uint64;
	}
	else if (WriterSettings.MaxPartitionSize)
	{
		TocHeader.PartitionCount = uint32(Align(TocResource.CompressionBlocks.Last().GetOffset(), WriterSettings.MaxPartitionSize) / WriterSettings.MaxPartitionSize);
		TocHeader.PartitionSize = WriterSettings.MaxPartitionSize;
	}
	else
	{
		TocHeader.PartitionCount = 1;
		TocHeader.PartitionSize = MAX_uint64;
	}

	TocFileHandle->Seek(0);

	// Header
	if (!TocFileHandle->Write(reinterpret_cast<const uint8*>(&TocResource.Header), sizeof(FIoStoreTocHeader)))
	{
		return FIoStatus(EIoErrorCode::WriteError, TEXT("Failed to write TOC header"));
	}

	// Chunk IDs
	if (!WriteArray(TocFileHandle.Get(), TocResource.ChunkIds))
	{
		return FIoStatus(EIoErrorCode::WriteError, TEXT("Failed to write chunk ids"));
	}

	// Chunk offsets
	if (!WriteArray(TocFileHandle.Get(), TocResource.ChunkOffsetLengths))
	{
		return FIoStatus(EIoErrorCode::WriteError, TEXT("Failed to write chunk offsets"));
	}

	// Compression blocks
	if (!WriteArray(TocFileHandle.Get(), TocResource.CompressionBlocks))
	{
		return FIoStatus(EIoErrorCode::WriteError, TEXT("Failed to write chunk block entries"));
	}

	// Compression methods
	ANSICHAR AnsiMethodName[FIoStoreTocResource::CompressionMethodNameLen];

	for (FName MethodName : TocResource.CompressionMethods)
	{
		FMemory::Memzero(AnsiMethodName, FIoStoreTocResource::CompressionMethodNameLen);
		FCStringAnsi::Strcpy(AnsiMethodName, FIoStoreTocResource::CompressionMethodNameLen, TCHAR_TO_ANSI(*MethodName.ToString()));

		if (!TocFileHandle->Write(reinterpret_cast<const uint8*>(AnsiMethodName), FIoStoreTocResource::CompressionMethodNameLen))
		{
			return FIoStatus(EIoErrorCode::WriteError, TEXT("Failed to write compression method TOC entry"));
		}
	}

	// Chunk block signatures
	if (EnumHasAnyFlags(TocHeader.ContainerFlags, EIoContainerFlags::Signed))
	{
		TArray<uint8> TocSignature, BlockSignature;
		check(TocResource.ChunkBlockSignatures.Num() == TocResource.CompressionBlocks.Num());

		FIoStatus SignatureStatus = CreateContainerSignature(
			ContainerSettings.SigningKey,
			TocHeader,
			TocResource.ChunkBlockSignatures,
			TocSignature,
			BlockSignature);

		if (!SignatureStatus .IsOk())
		{
			return SignatureStatus;
		}

		check(TocSignature.Num() == BlockSignature.Num());

		const int32 HashSize = TocSignature.Num();
		TocFileHandle->Write(reinterpret_cast<const uint8*>(&HashSize), sizeof(int32));
		TocFileHandle->Write(TocSignature.GetData(), HashSize);
		TocFileHandle->Write(BlockSignature.GetData(), HashSize);

		if (!WriteArray(TocFileHandle.Get(), TocResource.ChunkBlockSignatures))
		{
			return FIoStatus(EIoErrorCode::WriteError, TEXT("Failed to write chunk block signatures"));
		}
	}

	// Directory index
	if (EnumHasAnyFlags(TocHeader.ContainerFlags, EIoContainerFlags::Indexed))
	{
		TocFileHandle->Write(TocResource.DirectoryIndexBuffer.GetData(), TocResource.DirectoryIndexBuffer.Num());
	}

	// Meta
	if (!WriteArray(TocFileHandle.Get(), TocResource.ChunkMetas))
	{
		return FIoStatus(EIoErrorCode::WriteError, TEXT("Failed to write chunk meta data"));
	}

	TocFileHandle->Flush(true);

	return TocFileHandle->Tell();
}

void FIoStoreReader::GetFilenames(TArray<FString>& OutFileList) const
{
	const FIoDirectoryIndexReader& DirectoryIndex = GetDirectoryIndexReader();

	DirectoryIndex.IterateDirectoryIndex(
		FIoDirectoryIndexHandle::RootDirectory(),
		TEXT(""),
		[&OutFileList](FString Filename, uint32 TocEntryIndex) -> bool
		{
			OutFileList.AddUnique(Filename);
			return true;
		});
}

void FIoStoreReader::GetFilenamesByBlockIndex(const TArray<int32>& InBlockIndexList, TArray<FString>& OutFileList) const
{
	const FIoDirectoryIndexReader& DirectoryIndex = GetDirectoryIndexReader();

	DirectoryIndex.IterateDirectoryIndex(FIoDirectoryIndexHandle::RootDirectory(), TEXT(""),
		[this, &InBlockIndexList, &OutFileList](FString Filename, uint32 TocEntryIndex) -> bool
		{
			for (int32 BlockIndex : InBlockIndexList)
			{
				if (Impl->TocChunkContainsBlockIndex(TocEntryIndex, BlockIndex))
				{
					OutFileList.AddUnique(Filename);
					break;
				}
			}

			return true;
		});
}