// Copyright Epic Games, Inc. All Rights Reserved.

#include "Trace/Config.h"

#if UE_TRACE_ENABLED

#include "Platform.h"
#include "Trace/Detail/Atomic.h"
#include "Trace/Detail/Writer.inl"
#include "Trace/Trace.inl"

namespace Trace {
namespace Private {

////////////////////////////////////////////////////////////////////////////////
uint32			Writer_SendData(uint32, uint8* __restrict, uint32);
FWriteBuffer*	Writer_AllocateBlockFromPool();
uint32			Writer_GetThreadId();
void			Writer_FreeBlockListToPool(FWriteBuffer*, FWriteBuffer*);
extern uint64	GStartCycle;



////////////////////////////////////////////////////////////////////////////////
UE_TRACE_EVENT_BEGIN($Trace, ThreadTiming, NoSync|Important)
	UE_TRACE_EVENT_FIELD(uint64, BaseTimestamp)
UE_TRACE_EVENT_END()

#define TRACE_PRIVATE_PERF 0
#if TRACE_PRIVATE_PERF
UE_TRACE_EVENT_BEGIN($Trace, WorkerThread)
	UE_TRACE_EVENT_FIELD(uint32, Cycles)
	UE_TRACE_EVENT_FIELD(uint32, BytesReaped)
	UE_TRACE_EVENT_FIELD(uint32, BytesSent)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN($Trace, Memory)
	UE_TRACE_EVENT_FIELD(uint32, AllocSize)
UE_TRACE_EVENT_END()
#endif // TRACE_PRIVATE_PERF



////////////////////////////////////////////////////////////////////////////////
#define T_ALIGN alignas(PLATFORM_CACHE_LINE_SIZE)
static FWriteBuffer						GNullWriteBuffer	= { 0, 0, 0, 0, nullptr, nullptr, (uint8*)&GNullWriteBuffer };
thread_local FWriteBuffer*				GTlsWriteBuffer		= &GNullWriteBuffer;
static FWriteBuffer* __restrict			GActiveThreadList;	// = nullptr;
T_ALIGN static FWriteBuffer* volatile	GNewThreadList;		// = nullptr;
#undef T_ALIGN

////////////////////////////////////////////////////////////////////////////////
#if !IS_MONOLITHIC
TRACELOG_API FWriteBuffer* Writer_GetBuffer()
{
	// Thread locals and DLLs don't mix so for modular builds we are forced to
	// export this function to access thread-local variables.
	return GTlsWriteBuffer;
}
#endif

////////////////////////////////////////////////////////////////////////////////
static FWriteBuffer* Writer_NextBufferInternal()
{
	FWriteBuffer* NextBuffer = Writer_AllocateBlockFromPool();

	NextBuffer->Cursor = (uint8*)NextBuffer - NextBuffer->Size;
	NextBuffer->Committed = NextBuffer->Cursor;
	NextBuffer->Reaped = NextBuffer->Cursor;
	NextBuffer->EtxOffset = UPTRINT(0) - sizeof(FWriteBuffer);
	NextBuffer->NextBuffer = nullptr;

	FWriteBuffer* CurrentBuffer = GTlsWriteBuffer;
	if (CurrentBuffer == &GNullWriteBuffer)
	{
		NextBuffer->ThreadId = uint16(Writer_GetThreadId());
		NextBuffer->PrevTimestamp = TimeGetTimestamp();

		GTlsWriteBuffer = NextBuffer;

		UE_TRACE_LOG($Trace, ThreadTiming, TraceLogChannel)
			<< ThreadTiming.BaseTimestamp(NextBuffer->PrevTimestamp - GStartCycle);

		// Add this next buffer to the active list.
		for (;; PlatformYield())
		{
			NextBuffer->NextThread = AtomicLoadRelaxed(&GNewThreadList);
			if (AtomicCompareExchangeRelease(&GNewThreadList, NextBuffer, NextBuffer->NextThread))
			{
				break;
			}
		}
	}
	else
	{
		CurrentBuffer->NextBuffer = NextBuffer;
		NextBuffer->ThreadId = CurrentBuffer->ThreadId;
		NextBuffer->PrevTimestamp = CurrentBuffer->PrevTimestamp;

		GTlsWriteBuffer = NextBuffer;

		// Retire current buffer.
		UPTRINT EtxOffset = UPTRINT((uint8*)(CurrentBuffer) - CurrentBuffer->Cursor);
		AtomicStoreRelease(&(CurrentBuffer->EtxOffset), EtxOffset);
	}

	return NextBuffer;
}

////////////////////////////////////////////////////////////////////////////////
TRACELOG_API FWriteBuffer* Writer_NextBuffer(int32 Size)
{
	FWriteBuffer* CurrentBuffer = GTlsWriteBuffer;
	if (CurrentBuffer != &GNullWriteBuffer)
	{
		CurrentBuffer->Cursor -= Size;
	}

	FWriteBuffer* NextBuffer = Writer_NextBufferInternal();

	if (Size >= NextBuffer->Size)
	{
		// Someone is trying to write an event that is far too large
		return nullptr;
	}

	NextBuffer->Cursor += Size;
	return NextBuffer;
}

////////////////////////////////////////////////////////////////////////////////
void Writer_DrainBuffers()
{
	struct FRetireList
	{
		FWriteBuffer* __restrict Head = nullptr;
		FWriteBuffer* __restrict Tail = nullptr;

		void Insert(FWriteBuffer* __restrict Buffer)
		{
			Buffer->NextBuffer = Head;
			Head = Buffer;
			Tail = (Tail != nullptr) ? Tail : Head;
		}
	};

#if TRACE_PRIVATE_PERF
	uint64 StartTsc = TimeGetTimestamp();
	uint32 BytesReaped = 0;
	uint32 BytesSent = 0;
#endif

	// Claim ownership of any new thread buffer lists
	FWriteBuffer* __restrict NewThreadList;
	for (;; PlatformYield())
	{
		NewThreadList = AtomicLoadRelaxed(&GNewThreadList);
		if (AtomicCompareExchangeAcquire(&GNewThreadList, (FWriteBuffer*)nullptr, NewThreadList))
		{
			break;
		}
	}

	// Reverse the new threads list so they're more closely ordered by age
	// when sent out.
	FWriteBuffer* __restrict NewThreadCursor = NewThreadList;
	NewThreadList = nullptr;
	while (NewThreadCursor != nullptr)
	{
		FWriteBuffer* __restrict NextThread = NewThreadCursor->NextThread;

		NewThreadCursor->NextThread = NewThreadList;
		NewThreadList = NewThreadCursor;

		NewThreadCursor = NextThread;
	}
	
	FRetireList RetireList;

	FWriteBuffer* __restrict ActiveThreadList = GActiveThreadList;
	GActiveThreadList = nullptr;

	// Now we've two lists of known and new threads. Each of these lists in turn is
	// a list of that thread's buffers (where it is writing trace events to).
	for (FWriteBuffer* __restrict Buffer : { ActiveThreadList, NewThreadList })
	{
		// For each thread...
		for (FWriteBuffer* __restrict NextThread; Buffer != nullptr; Buffer = NextThread)
		{
			NextThread = Buffer->NextThread;
			uint32 ThreadId = Buffer->ThreadId;

			// For each of the thread's buffers...
			for (FWriteBuffer* __restrict NextBuffer; Buffer != nullptr; Buffer = NextBuffer)
			{
				uint8* Committed = AtomicLoadRelaxed((uint8**)&Buffer->Committed);

				// Send as much as we can.
				if (uint32 SizeToReap = uint32(Committed - Buffer->Reaped))
				{
#if TRACE_PRIVATE_PERF
					BytesReaped += SizeToReap;
					BytesSent += /*...*/
#endif
					Writer_SendData(ThreadId, Buffer->Reaped, SizeToReap);
					Buffer->Reaped = Committed;
				}

				// Is this buffer still in use?
				int32 EtxOffset = int32(AtomicLoadAcquire(&Buffer->EtxOffset));
				if ((uint8*)Buffer - EtxOffset > Committed)
				{
					break;
				}

				// Retire the buffer
				NextBuffer = Buffer->NextBuffer;
				RetireList.Insert(Buffer);
			}

			if (Buffer != nullptr)
			{
				Buffer->NextThread = GActiveThreadList;
				GActiveThreadList = Buffer;
			}
		}
	}

#if TRACE_PRIVATE_PERF
	UE_TRACE_LOG($Trace, WorkerThread, TraceLogChannel)
		<< WorkerThread.Cycles(uint32(TimeGetTimestamp() - StartTsc))
		<< WorkerThread.BytesReaped(BytesReaped)
		<< WorkerThread.BytesSent(BytesSent);

	UE_TRACE_LOG($Trace, Memory, TraceLogChannel)
		<< Memory.AllocSize(GPoolUsage);
#endif // TRACE_PRIVATE_PERF

	// Put the retirees we found back into the system again.
	if (RetireList.Head != nullptr)
	{
		Writer_FreeBlockListToPool(RetireList.Head, RetireList.Tail);
	}
}

////////////////////////////////////////////////////////////////////////////////
void Writer_EndThreadBuffer()
{
	if (GTlsWriteBuffer == &GNullWriteBuffer)
	{
		return;
	}

	UPTRINT EtxOffset = UPTRINT((uint8*)GTlsWriteBuffer - GTlsWriteBuffer->Cursor);
	AtomicStoreRelaxed(&(GTlsWriteBuffer->EtxOffset), EtxOffset);
}

} // namespace Private
} // namespace Trace

#endif // UE_TRACE_ENABLED
