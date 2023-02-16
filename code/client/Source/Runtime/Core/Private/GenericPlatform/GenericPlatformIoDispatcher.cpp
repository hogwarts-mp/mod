// Copyright Epic Games, Inc. All Rights Reserved.

#include "GenericPlatformIoDispatcher.h"
#include "IO/IoDispatcherFileBackend.h"
#include "Async/AsyncFileHandle.h"
#include "HAL/Event.h"
#include "HAL/PlatformFilemanager.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "HAL/RunnableThread.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeLock.h"

//PRAGMA_DISABLE_OPTIMIZATION

TRACE_DECLARE_INT_COUNTER(IoDispatcherSequentialReads, TEXT("IoDispatcher/SequentialReads"));
TRACE_DECLARE_INT_COUNTER(IoDispatcherForwardSeeks, TEXT("IoDispatcher/ForwardSeeks"));
TRACE_DECLARE_INT_COUNTER(IoDispatcherBackwardSeeks, TEXT("IoDispatcher/BackwardSeeks"));
TRACE_DECLARE_MEMORY_COUNTER(IoDispatcherTotalSeekDistance, TEXT("IoDispatcher/TotalSeekDistance"));

FGenericIoDispatcherEventQueue::FGenericIoDispatcherEventQueue()
	: DispatcherEvent(FPlatformProcess::GetSynchEventFromPool())
	, ServiceEvent(FPlatformProcess::GetSynchEventFromPool())
{
}

FGenericIoDispatcherEventQueue::~FGenericIoDispatcherEventQueue()
{
	FPlatformProcess::ReturnSynchEventToPool(ServiceEvent);
	FPlatformProcess::ReturnSynchEventToPool(DispatcherEvent);
}

void FGenericIoDispatcherEventQueue::DispatcherNotify()
{
	DispatcherEvent->Trigger();
}

void FGenericIoDispatcherEventQueue::DispatcherWait()
{
	DispatcherEvent->Wait();
}

void FGenericIoDispatcherEventQueue::ServiceNotify()
{
	ServiceEvent->Trigger();
}

void FGenericIoDispatcherEventQueue::ServiceWait()
{
	ServiceEvent->Wait();
}

FGenericFileIoStoreImpl::FGenericFileIoStoreImpl(FGenericIoDispatcherEventQueue& InEventQueue, FFileIoStoreBufferAllocator& InBufferAllocator, FFileIoStoreBlockCache& InBlockCache)
	: EventQueue(InEventQueue)
	, BufferAllocator(InBufferAllocator)
	, BlockCache(InBlockCache)
{
}

FGenericFileIoStoreImpl::~FGenericFileIoStoreImpl()
{
}

bool FGenericFileIoStoreImpl::OpenContainer(const TCHAR* ContainerFilePath, uint64& ContainerFileHandle, uint64& ContainerFileSize)
{
	IPlatformFile& Ipf = IPlatformFile::GetPlatformPhysical();
	int64 FileSize = Ipf.FileSize(ContainerFilePath);
	if (FileSize < 0)
	{
		return false;
	}
	IFileHandle* FileHandle = Ipf.OpenReadNoBuffering(ContainerFilePath);
	if (!FileHandle)
	{
		return false;
	}
	ContainerFileHandle = reinterpret_cast<UPTRINT>(FileHandle);
	ContainerFileSize = uint64(FileSize);
	return true;
}

bool FGenericFileIoStoreImpl::StartRequests(FFileIoStoreRequestQueue& RequestQueue)
{
	FFileIoStoreReadRequest* NextRequest = RequestQueue.Pop();
	if (!NextRequest)
	{
		return false;
	}

	if (NextRequest->bCancelled)
	{
		{
			FScopeLock _(&CompletedRequestsCritical);
			CompletedRequests.Add(NextRequest);
		}
		EventQueue.DispatcherNotify();
		return true;
	}

	uint8* Dest;
	if (!NextRequest->ImmediateScatter.Request)
	{
		NextRequest->Buffer = BufferAllocator.AllocBuffer();
		if (!NextRequest->Buffer)
		{
			RequestQueue.Push(*NextRequest);
			return false;
		}
		Dest = NextRequest->Buffer->Memory;
	}
	else
	{
		Dest = NextRequest->ImmediateScatter.Request->GetIoBuffer().Data() + NextRequest->ImmediateScatter.DstOffset;
	}
	
	if (!BlockCache.Read(NextRequest))
	{
		IFileHandle* FileHandle = reinterpret_cast<IFileHandle*>(static_cast<UPTRINT>(NextRequest->FileHandle));
		if (FileHandle->Tell() != NextRequest->Offset)
		{
			if (uint64(FileHandle->Tell()) > NextRequest->Offset)
			{
				TRACE_COUNTER_INCREMENT(IoDispatcherBackwardSeeks);
			}
			else
			{
				TRACE_COUNTER_INCREMENT(IoDispatcherForwardSeeks);
			}
			TRACE_COUNTER_ADD(IoDispatcherTotalSeekDistance, FMath::Abs(FileHandle->Tell() - int64(NextRequest->Offset)));
		}
		else
		{
			TRACE_COUNTER_INCREMENT(IoDispatcherSequentialReads);
		}
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ReadBlockFromFile);
			NextRequest->bFailed = true;
			int32 RetryCount = 0;
			while (RetryCount++ < 10)
			{
				if (!FileHandle->Seek(NextRequest->Offset))
				{
					UE_LOG(LogIoDispatcher, Warning, TEXT("Failed seeking to offset %lld (Retries: %d)"), NextRequest->Offset, (RetryCount - 1));
					continue;
				}
				if (!FileHandle->Read(Dest, NextRequest->Size))
				{
					UE_LOG(LogIoDispatcher, Warning, TEXT("Failed reading %lld bytes at offset %lld (Retries: %d)"), NextRequest->Size, NextRequest->Offset, (RetryCount - 1));
					continue;
				}
				NextRequest->bFailed = false;
				BlockCache.Store(NextRequest);
				break;
			}
		}
	}
	{
		FScopeLock _(&CompletedRequestsCritical);
		CompletedRequests.Add(NextRequest);
	}
	EventQueue.DispatcherNotify();
	return true;
}

void FGenericFileIoStoreImpl::GetCompletedRequests(FFileIoStoreReadRequestList& OutRequests)
{
	FScopeLock _(&CompletedRequestsCritical);
	OutRequests.Append(CompletedRequests);
	CompletedRequests.Clear();
}
