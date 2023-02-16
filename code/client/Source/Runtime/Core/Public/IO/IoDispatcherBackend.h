// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

class FIoRequestImpl
{
public:
	FIoRequestImpl* NextRequest = nullptr;
	void* BackendData = nullptr;
	FIoChunkId ChunkId;
	FIoReadOptions Options;
	FIoBuffer IoBuffer;
	int32 Priority = 0;

	FIoRequestImpl(FIoDispatcherImpl& InDispatcher)
		: Dispatcher(InDispatcher)
	{

	}

	bool IsCancelled() const
	{
		return bCancelled;
	}

	void SetFailed()
	{
		bFailed = true;
	}

private:
	friend class FIoDispatcherImpl;
	friend class FIoRequest;
	friend class FIoBatch;

	void AddRef()
	{
		RefCount.IncrementExchange();
	}

	void ReleaseRef()
	{
		if (RefCount.DecrementExchange() == 1)
		{
			FreeRequest();
		}
	}

	void FreeRequest();

	FIoDispatcherImpl& Dispatcher;
	FIoBatchImpl* Batch = nullptr;
	FIoReadCallback Callback;
	TAtomic<uint32> RefCount{ 0 };
	TAtomic<EIoErrorCode> ErrorCode{ EIoErrorCode::Unknown };
	bool bSubmitted = false;
	bool bCancelled = false;
	bool bFailed = false;
};

