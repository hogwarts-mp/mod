// Copyright Epic Games, Inc. All Rights Reserved.

#include "IO/IoDispatcher.h"
#include "IO/IoDispatcherPrivate.h"
#include "IO/IoStore.h"
#include "IO/IoDispatcherFileBackend.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/CoreDelegates.h"
#include "Math/RandomStream.h"
#include "Misc/ScopeLock.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "HAL/PlatformProcess.h"
#include "Serialization/LargeMemoryReader.h"
#include "GenericPlatform/GenericPlatformChunkInstall.h"
#include "HAL/Event.h"
#include "Async/MappedFileHandle.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "ProfilingDebugging/CsvProfiler.h"

DEFINE_LOG_CATEGORY(LogIoDispatcher);


const FIoChunkId FIoChunkId::InvalidChunkId = FIoChunkId::CreateEmptyId();

TUniquePtr<FIoDispatcher> GIoDispatcher;

#if UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG
//PRAGMA_DISABLE_OPTIMIZATION
#endif

TRACE_DECLARE_INT_COUNTER(PendingIoRequests, TEXT("IoDispatcher/PendingIoRequests"));

template <typename T, uint32 BlockSize = 128>
class TBlockAllocator
{
public:
	~TBlockAllocator()
	{
		FreeBlocks();
	}

	FORCEINLINE T* Alloc()
	{
		FScopeLock _(&CriticalSection);

		if (!NextFree)
		{
			//TODO: Virtual alloc
			FBlock* Block = new FBlock;

			for (int32 ElementIndex = 0; ElementIndex < BlockSize; ++ElementIndex)
			{
				FElement* Element = &Block->Elements[ElementIndex];
				Element->Next = NextFree;
				NextFree = Element;
			}

			Block->Next = Blocks;
			Blocks = Block;
		}

		FElement* Element = NextFree;
		NextFree = Element->Next;

		++NumElements;

		return Element->Buffer.GetTypedPtr();
	}

	FORCEINLINE void Free(T* Ptr)
	{
		FScopeLock _(&CriticalSection);

		FElement* Element = reinterpret_cast<FElement*>(Ptr);
		Element->Next = NextFree;
		NextFree = Element;

		--NumElements;
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

	void Trim()
	{
		FScopeLock _(&CriticalSection);
		if (!NumElements)
		{
			FreeBlocks();
		}
	}

private:
	void FreeBlocks()
	{
		FBlock* Block = Blocks;
		while (Block)
		{
			FBlock* Tmp = Block;
			Block = Block->Next;
			delete Tmp;
		}

		Blocks = nullptr;
		NextFree = nullptr;
		NumElements = 0;
	}

	struct FElement
	{
		TTypeCompatibleBytes<T> Buffer;
		FElement* Next;
	};

	struct FBlock
	{
		FElement Elements[BlockSize];
		FBlock* Next = nullptr;
	};

	FBlock*				Blocks = nullptr;
	FElement*			NextFree = nullptr;
	int32				NumElements = 0;
	FCriticalSection	CriticalSection;
};

class FIoDispatcherImpl
	: public FRunnable
{
public:
	FIoDispatcherImpl(bool bInIsMultithreaded)
		: bIsMultithreaded(bInIsMultithreaded)
		, FileIoStore(EventQueue, SignatureErrorEvent, bIsMultithreaded)
	{
		FCoreDelegates::GetMemoryTrimDelegate().AddLambda([this]()
		{
			RequestAllocator.Trim();
			BatchAllocator.Trim();
		});
	}

	~FIoDispatcherImpl()
	{
		delete Thread;
	}

	FIoStatus Initialize()
	{
		return FIoStatus::Ok;
	}

	bool InitializePostSettings()
	{
		FileIoStore.Initialize();
		Thread = FRunnableThread::Create(this, TEXT("IoDispatcher"), 0, TPri_AboveNormal, FPlatformAffinity::GetIoDispatcherThreadMask());
		return true;
	}

	FIoRequestImpl* AllocRequest(const FIoChunkId& ChunkId, FIoReadOptions Options)
	{
		LLM_SCOPE(ELLMTag::FileSystem);
		FIoRequestImpl* Request = RequestAllocator.Construct(*this);

		Request->ChunkId = ChunkId;
		Request->Options = Options;

		return Request;
	}

	void FreeRequest(FIoRequestImpl* Request)
	{
		RequestAllocator.Destroy(Request);
	}

	FIoBatchImpl* AllocBatch()
	{
		LLM_SCOPE(ELLMTag::FileSystem);
		FIoBatchImpl* Batch = BatchAllocator.Construct();

		return Batch;
	}

	void WakeUpDispatcherThread()
	{
		if (bIsMultithreaded)
		{
			EventQueue.DispatcherNotify();
		}
		else
		{
			ProcessIncomingRequests();
			while (PendingIoRequestsCount > 0)
			{
				ProcessCompletedRequests();
			}
		}
	}

	void Cancel(FIoRequestImpl* Request)
	{
		Request->AddRef();
		{
			FScopeLock _(&UpdateLock);
			RequestsToCancel.Add(Request);
		}
		WakeUpDispatcherThread();
	}

	void Reprioritize(FIoRequestImpl* Request)
	{
		Request->AddRef();
		{
			FScopeLock _(&UpdateLock);
			RequestsToReprioritize.Add(Request);
		}
		WakeUpDispatcherThread();
	}

	TIoStatusOr<FIoMappedRegion> OpenMapped(const FIoChunkId& ChunkId, const FIoReadOptions& Options)
	{
		if (ChunkId.IsValid())
		{
			return FileIoStore.OpenMapped(ChunkId, Options);
		}
		else
		{
			return FIoStatus(EIoErrorCode::InvalidParameter, TEXT("FIoChunkId is not valid"));
		}
	}

	FIoStatus Mount(const FIoStoreEnvironment& Environment, const FGuid& EncryptionKeyGuid, const FAES::FAESKey& EncryptionKey)
	{
		TIoStatusOr<FIoContainerId> ContainerId = FileIoStore.Mount(Environment, EncryptionKeyGuid, EncryptionKey);

		if (ContainerId.IsOk())
		{
			FIoDispatcherMountedContainer MountedContainer;
			MountedContainer.ContainerId = ContainerId.ValueOrDie();
			MountedContainer.Environment = Environment;
			FScopeLock Lock(&MountedContainersCritical);
			if (ContainerMountedEvent.IsBound())
			{
				ContainerMountedEvent.Broadcast(MountedContainer);
			}
			MountedContainers.Add(MoveTemp(MountedContainer));
			return FIoStatus::Ok;
		}

		return ContainerId.Status();
	}

	bool DoesChunkExist(const FIoChunkId& ChunkId) const
	{
		return FileIoStore.DoesChunkExist(ChunkId);
	}

	TIoStatusOr<uint64> GetSizeForChunk(const FIoChunkId& ChunkId) const
	{
		// Only attempt to find the size if the FIoChunkId is valid
		if (ChunkId.IsValid())
		{
			return FileIoStore.GetSizeForChunk(ChunkId);
		}
		else
		{
			return FIoStatus(EIoErrorCode::InvalidParameter, TEXT("FIoChunkId is not valid"));
		}	
	}

	TArray<FIoDispatcherMountedContainer> GetMountedContainers() const
	{
		FScopeLock Lock(&MountedContainersCritical);
		return MountedContainers;
	}

	FIoDispatcher::FIoContainerMountedEvent& OnContainerMounted()
	{
		return ContainerMountedEvent;
	}

	FIoSignatureErrorEvent& GetSignatureErrorEvent()
	{
		return SignatureErrorEvent;
	}

	void IssueBatchInternal(FIoBatch& Batch, FIoBatchImpl* BatchImpl)
	{
		if (!Batch.HeadRequest)
		{
			if (BatchImpl)
			{
				CompleteBatch(BatchImpl);
			}
			return;
		}
		check(Batch.TailRequest);
		uint32 RequestCount = 0;
		FIoRequestImpl* Request = Batch.HeadRequest;
		while (Request)
		{
			Request->Batch = BatchImpl;
			Request = Request->NextRequest;
			++RequestCount;
		}
		if (BatchImpl)
		{
			BatchImpl->UnfinishedRequestsCount += RequestCount;
		}
		{
			FScopeLock _(&WaitingLock);
			if (!WaitingRequestsHead)
			{
				WaitingRequestsHead = Batch.HeadRequest;
			}
			else
			{
				WaitingRequestsTail->NextRequest = Batch.HeadRequest;
			}
			WaitingRequestsTail = Batch.TailRequest;
		}
		Batch.HeadRequest = Batch.TailRequest = nullptr;
		WakeUpDispatcherThread();
	}

	void IssueBatch(FIoBatch& Batch)
	{
		IssueBatchInternal(Batch, nullptr);
	}

	void IssueBatchWithCallback(FIoBatch& Batch, TFunction<void()>&& Callback)
	{
		FIoBatchImpl* Impl = AllocBatch();
		Impl->Callback = MoveTemp(Callback);
		IssueBatchInternal(Batch, Impl);
	}

	void IssueBatchAndTriggerEvent(FIoBatch& Batch, FEvent* Event)
	{
		FIoBatchImpl* Impl = AllocBatch();
		Impl->Event = Event;
		IssueBatchInternal(Batch, Impl);
	}

	void IssueBatchAndDispatchSubsequents(FIoBatch& Batch, FGraphEventRef GraphEvent)
	{
		FIoBatchImpl* Impl = AllocBatch();
		Impl->GraphEvent = GraphEvent;
		IssueBatchInternal(Batch, Impl);
	}

	int64 GetTotalLoaded() const
	{
		return TotalLoaded;
	}

private:
	friend class FIoBatch;
	friend class FIoRequest;

	void ProcessCompletedRequests()
	{
		//TRACE_CPUPROFILER_EVENT_SCOPE(ProcessCompletedRequests);

		FIoRequestImpl* CompletedRequestsHead = FileIoStore.GetCompletedRequests();
		while (CompletedRequestsHead)
		{
			FIoRequestImpl* NextRequest = CompletedRequestsHead->NextRequest;
			if (CompletedRequestsHead->bCancelled)
			{
				CompleteRequest(CompletedRequestsHead, EIoErrorCode::Cancelled);
			}
			else if (CompletedRequestsHead->bFailed)
			{
				CompleteRequest(CompletedRequestsHead, EIoErrorCode::ReadError);
			}
			else
			{
				FPlatformAtomics::InterlockedAdd(&TotalLoaded, CompletedRequestsHead->IoBuffer.DataSize());
				CompleteRequest(CompletedRequestsHead, EIoErrorCode::Ok);
			}
			CompletedRequestsHead->ReleaseRef();
			CompletedRequestsHead = NextRequest;
			--PendingIoRequestsCount;
			TRACE_COUNTER_SET(PendingIoRequests, PendingIoRequestsCount);
		}
	}

	void CompleteBatch(FIoBatchImpl* Batch)
	{
		if (Batch->Callback)
		{
			Batch->Callback();
		}
		if (Batch->Event)
		{
			Batch->Event->Trigger();
		}
		if (Batch->GraphEvent)
		{
			TArray<FBaseGraphTask*> NewTasks;
			Batch->GraphEvent->DispatchSubsequents(NewTasks);
		}
		BatchAllocator.Destroy(Batch);
	}

	bool CompleteRequest(FIoRequestImpl* Request, EIoErrorCode Status)
	{
		//TRACE_CPUPROFILER_EVENT_SCOPE(CompleteRequest);

		EIoErrorCode ExpectedStatus = EIoErrorCode::Unknown;
		if (!Request->ErrorCode.CompareExchange(ExpectedStatus, Status))
		{
			return false;
		}

		FIoBatchImpl* Batch = Request->Batch;
		if (Request->Callback)
		{
			TIoStatusOr<FIoBuffer> Result;
			if (Status == EIoErrorCode::Ok)
			{
				Result = Request->IoBuffer;
			}
			else
			{
				Result = Status;
			}
			Request->Callback(Result);
		}
		if (Batch)
		{
			check(Batch->UnfinishedRequestsCount);
			if (--Batch->UnfinishedRequestsCount == 0)
			{
				CompleteBatch(Batch);
			}
		}
		return true;
	}

	void ProcessIncomingRequests()
	{
		FIoRequestImpl* RequestsToSubmitHead = nullptr;
		FIoRequestImpl* RequestsToSubmitTail = nullptr;
		//TRACE_CPUPROFILER_EVENT_SCOPE(ProcessIncomingRequests);
		for (;;)
		{
			{
				FScopeLock _(&WaitingLock);
				if (WaitingRequestsHead)
				{
					if (RequestsToSubmitTail)
					{
						RequestsToSubmitTail->NextRequest = WaitingRequestsHead;
						RequestsToSubmitTail = WaitingRequestsTail;
					}
					else
					{
						RequestsToSubmitHead = WaitingRequestsHead;
						RequestsToSubmitTail = WaitingRequestsTail;
					}
					WaitingRequestsHead = WaitingRequestsTail = nullptr;
				}
			}
			TArray<FIoRequestImpl*> LocalRequestsToCancel;
			TArray<FIoRequestImpl*> LocalRequestsToReprioritize;
			{
				FScopeLock _(&UpdateLock);
				Swap(LocalRequestsToCancel, RequestsToCancel);
				Swap(LocalRequestsToReprioritize, RequestsToReprioritize);
			}
			for (FIoRequestImpl* RequestToCancel : LocalRequestsToCancel)
			{
				if (!RequestToCancel->bCancelled)
				{
					RequestToCancel->bCancelled = true;
					if (RequestToCancel->bSubmitted)
					{
						FileIoStore.CancelIoRequest(RequestToCancel);
					}
				}
				RequestToCancel->ReleaseRef();
			}
			for (FIoRequestImpl* RequestToRePrioritize : LocalRequestsToReprioritize)
			{
				if (RequestToRePrioritize->bSubmitted)
				{
					FileIoStore.UpdatePriorityForIoRequest(RequestToRePrioritize);
				}
				RequestToRePrioritize->ReleaseRef();
			}
			if (!RequestsToSubmitHead)
			{
				return;
			}

			FIoRequestImpl* Request = RequestsToSubmitHead;
			RequestsToSubmitHead = RequestsToSubmitHead->NextRequest;
			Request->NextRequest = nullptr;
			if (!RequestsToSubmitHead)
			{
				RequestsToSubmitTail = nullptr;
			}

			if (Request->bCancelled)
			{
				CompleteRequest(Request, EIoErrorCode::Cancelled);
				Request->ReleaseRef();
				continue;
			}
			
			// Make sure that the FIoChunkId in the request is valid before we try to do anything with it.
			if (Request->ChunkId.IsValid())
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(ResolveRequest);
				EIoStoreResolveResult Result = FileIoStore.Resolve(Request);
				Request->bSubmitted = true;
				if (Result != IoStoreResolveResult_OK)
				{
					CompleteRequest(Request, EIoErrorCode::NotFound);
					Request->ReleaseRef();
					continue;
				}
			}
			else
			{
				CompleteRequest(Request, EIoErrorCode::InvalidParameter);
				Request->ReleaseRef();
				continue;
			}
			
			++PendingIoRequestsCount;
			TRACE_COUNTER_SET(PendingIoRequests, PendingIoRequestsCount);
			
			ProcessCompletedRequests();
		}
	}

	virtual bool Init()
	{
		return true;
	}

	virtual uint32 Run()
	{
		FMemory::SetupTLSCachesOnCurrentThread();
		while (!bStopRequested)
		{
			if (PendingIoRequestsCount)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(IoDispatcherWaitForIo);
				EventQueue.DispatcherWaitForIo();
			}
			else
			{
				EventQueue.DispatcherWait();
			}
			ProcessIncomingRequests();
			ProcessCompletedRequests();
		}
		return 0;
	}

	virtual void Stop()
	{
		bStopRequested = true;
		EventQueue.DispatcherNotify();
	}

	using FRequestAllocator = TBlockAllocator<FIoRequestImpl, 4096>;
	using FBatchAllocator = TBlockAllocator<FIoBatchImpl, 4096>;

	bool bIsMultithreaded;
	FIoDispatcherEventQueue EventQueue;

	FIoSignatureErrorEvent SignatureErrorEvent;
	FFileIoStore FileIoStore;
	FRequestAllocator RequestAllocator;
	FBatchAllocator BatchAllocator;
	FRunnableThread* Thread = nullptr;
	FCriticalSection WaitingLock;
	FIoRequestImpl* WaitingRequestsHead = nullptr;
	FIoRequestImpl* WaitingRequestsTail = nullptr;
	FCriticalSection UpdateLock;
	TArray<FIoRequestImpl*> RequestsToCancel;
	TArray<FIoRequestImpl*> RequestsToReprioritize;
	TAtomic<bool> bStopRequested { false };
	mutable FCriticalSection MountedContainersCritical;
	TArray<FIoDispatcherMountedContainer> MountedContainers;
	FIoDispatcher::FIoContainerMountedEvent ContainerMountedEvent;
	uint64 PendingIoRequestsCount = 0;
	int64 TotalLoaded = 0;
};

FIoDispatcher::FIoDispatcher()
	: Impl(new FIoDispatcherImpl(FGenericPlatformProcess::SupportsMultithreading()))
{
}

FIoDispatcher::~FIoDispatcher()
{
	delete Impl;
}

FIoStatus FIoDispatcher::Mount(const FIoStoreEnvironment& Environment, const FGuid& EncryptionKeyGuid, const FAES::FAESKey& EncryptionKey)
{
	LLM_SCOPE(ELLMTag::FileSystem);
	return Impl->Mount(Environment, EncryptionKeyGuid, EncryptionKey);
}

FIoBatch
FIoDispatcher::NewBatch()
{
	return FIoBatch(*Impl);
}

TIoStatusOr<FIoMappedRegion>
FIoDispatcher::OpenMapped(const FIoChunkId& ChunkId, const FIoReadOptions& Options)
{
	return Impl->OpenMapped(ChunkId, Options);
}

// Polling methods
bool
FIoDispatcher::DoesChunkExist(const FIoChunkId& ChunkId) const
{
	return Impl->DoesChunkExist(ChunkId);
}

TIoStatusOr<uint64>
FIoDispatcher::GetSizeForChunk(const FIoChunkId& ChunkId) const
{
	return Impl->GetSizeForChunk(ChunkId);
}

TArray<FIoDispatcherMountedContainer>
FIoDispatcher::GetMountedContainers() const
{
	return Impl->GetMountedContainers();
}

int64
FIoDispatcher::GetTotalLoaded() const
{
	return Impl->GetTotalLoaded();
}

FIoDispatcher::FIoContainerMountedEvent&
FIoDispatcher::OnContainerMounted()
{
	return Impl->OnContainerMounted();
}

FIoSignatureErrorEvent&
FIoDispatcher::GetSignatureErrorEvent()
{
	return Impl->GetSignatureErrorEvent();
}

bool
FIoDispatcher::IsInitialized()
{
	return GIoDispatcher.IsValid();
}

bool
FIoDispatcher::IsValidEnvironment(const FIoStoreEnvironment& Environment)
{
	return FFileIoStore::IsValidEnvironment(Environment);
}

FIoStatus
FIoDispatcher::Initialize()
{
	LLM_SCOPE(ELLMTag::FileSystem);
	GIoDispatcher = MakeUnique<FIoDispatcher>();

	return GIoDispatcher->Impl->Initialize();
}

void
FIoDispatcher::InitializePostSettings()
{
	LLM_SCOPE(ELLMTag::FileSystem);

	check(GIoDispatcher);
	bool bSuccess = GIoDispatcher->Impl->InitializePostSettings();
	UE_CLOG(!bSuccess, LogIoDispatcher, Fatal, TEXT("Failed to initialize IoDispatcher"));
}

void
FIoDispatcher::Shutdown()
{
	GIoDispatcher.Reset();
}

FIoDispatcher&
FIoDispatcher::Get()
{
	return *GIoDispatcher;
}

//////////////////////////////////////////////////////////////////////////

FIoBatch::FIoBatch(FIoDispatcherImpl& InDispatcher)
	: Dispatcher(&InDispatcher)
{
}

FIoBatch::FIoBatch()
	: Dispatcher(FIoDispatcher::Get().Impl)
{
}

FIoBatch::FIoBatch(FIoBatch&& Other)
{
	Dispatcher = Other.Dispatcher;
	HeadRequest = Other.HeadRequest;
	Other.HeadRequest = nullptr;
}

FIoBatch::~FIoBatch()
{
	FIoRequestImpl* Request = HeadRequest;
	while (Request)
	{
		FIoRequestImpl* NextRequest = Request->NextRequest;
		Request->ReleaseRef();
		Request = NextRequest;
	}
}

FIoBatch&
FIoBatch::operator=(const FIoBatch& Other)
{
	Dispatcher = Other.Dispatcher;
	check(!Other.HeadRequest);
	return *this;
}

FIoBatch&
FIoBatch::operator=(FIoBatch&& Other)
{
	FIoRequestImpl* Request = HeadRequest;
	while (Request)
	{
		FIoRequestImpl* NextRequest = Request->NextRequest;
		Request->ReleaseRef();
		Request = NextRequest;
	}
	Dispatcher = Other.Dispatcher;
	HeadRequest = Other.HeadRequest;
	Other.HeadRequest = nullptr;
	return *this;
}

FIoRequestImpl*
FIoBatch::ReadInternal(const FIoChunkId& ChunkId, const FIoReadOptions& Options, int32 Priority)
{
	FIoRequestImpl* Request = Dispatcher->AllocRequest(ChunkId, Options);
	Request->Priority = Priority;
	Request->AddRef();
	if (!HeadRequest)
	{
		check(!TailRequest);
		HeadRequest = TailRequest = Request;
	}
	else
	{
		check(TailRequest);
		TailRequest->NextRequest = Request;
		TailRequest = Request;
	}
	return Request;
}

FIoRequest
FIoBatch::Read(const FIoChunkId& ChunkId, FIoReadOptions Options, int32 Priority)
{
	FIoRequestImpl* Request = ReadInternal(ChunkId, Options, Priority);
	return FIoRequest(Request);
}

FIoRequest
FIoBatch::ReadWithCallback(const FIoChunkId& ChunkId, const FIoReadOptions& Options, int32 Priority, FIoReadCallback&& Callback)
{
	FIoRequestImpl* Request = ReadInternal(ChunkId, Options, Priority);
	Request->Callback = MoveTemp(Callback);
	return FIoRequest(Request);
}

void
FIoBatch::Issue()
{
	Dispatcher->IssueBatch(*this);
}

void
FIoBatch::Issue(int32 Priority)
{
	FIoRequestImpl* Request = HeadRequest;
	while (Request)
	{
		Request->Priority = Priority;
		Request = Request->NextRequest;
	}
	Issue();
}

void
FIoBatch::IssueWithCallback(TFunction<void()>&& Callback)
{
	Dispatcher->IssueBatchWithCallback(*this, MoveTemp(Callback));
}

void
FIoBatch::IssueAndTriggerEvent(FEvent* Event)
{
	Dispatcher->IssueBatchAndTriggerEvent(*this, Event);
}

void
FIoBatch::IssueAndDispatchSubsequents(FGraphEventRef Event)
{
	Dispatcher->IssueBatchAndDispatchSubsequents(*this, Event);
}

//////////////////////////////////////////////////////////////////////////

void FIoRequestImpl::FreeRequest()
{
	Dispatcher.FreeRequest(this);
}

FIoRequest::FIoRequest(FIoRequestImpl* InImpl)
	: Impl(InImpl)
{
	if (Impl)
	{
		Impl->AddRef();
	}
}

FIoRequest::FIoRequest(const FIoRequest& Other)
{
	if (Other.Impl)
	{
		Impl = Other.Impl;
		Impl->AddRef();
	}
}

FIoRequest::FIoRequest(FIoRequest&& Other)
{
	Impl = Other.Impl;
	Other.Impl = nullptr;
}

FIoRequest& FIoRequest::operator=(const FIoRequest& Other)
{
	if (Other.Impl)
	{
		Other.Impl->AddRef();
	}
	if (Impl)
	{
		Impl->ReleaseRef();
	}
	Impl = Other.Impl;
	return *this;
}

FIoRequest& FIoRequest::operator=(FIoRequest&& Other)
{
	Impl = Other.Impl;
	Other.Impl = nullptr;
	return *this;
}

FIoRequest::~FIoRequest()
{
	if (Impl)
	{
		Impl->ReleaseRef();
	}
}

FIoStatus
FIoRequest::Status() const
{
	if (Impl)
	{
		return Impl->ErrorCode.Load();
	}
	else
	{
		return FIoStatus::Invalid;
	}
}

TIoStatusOr<FIoBuffer>
FIoRequest::GetResult()
{
	if (!Impl)
	{
		return FIoStatus::Invalid;
	}
	FIoStatus Status(Impl->ErrorCode.Load());
	check(Status.IsCompleted());
	TIoStatusOr<FIoBuffer> Result;
	if (Status.IsOk())
	{
		return Impl->IoBuffer;
	}
	else
	{
		return Status;
	}
}

void
FIoRequest::Cancel()
{
	if (!Impl)
	{
		return;
	}
	//TRACE_BOOKMARK(TEXT("FIoRequest::Cancel()"));
	Impl->Dispatcher.Cancel(Impl);
}

void
FIoRequest::UpdatePriority(uint32 NewPriority)
{
	if (!Impl || Impl->Priority == NewPriority)
	{
		return;
	}
	Impl->Priority = NewPriority;
	Impl->Dispatcher.Reprioritize(Impl);
}

