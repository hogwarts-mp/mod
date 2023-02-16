// Copyright Epic Games, Inc. All Rights Reserved.
#include "Stats/StatsTrace.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Templates/Atomic.h"
#include "Misc/CString.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformTLS.h"
#include "Trace/Trace.inl"
#include "UObject/NameTypes.h"

#if STATSTRACE_ENABLED

UE_TRACE_CHANNEL(StatsChannel)

UE_TRACE_EVENT_BEGIN(Stats, Spec, Important)
	UE_TRACE_EVENT_FIELD(uint32, Id)
	UE_TRACE_EVENT_FIELD(bool, IsFloatingPoint)
	UE_TRACE_EVENT_FIELD(bool, IsMemory)
	UE_TRACE_EVENT_FIELD(bool, ShouldClearEveryFrame)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Stats, EventBatch)
UE_TRACE_EVENT_END()

struct FStatsTraceInternal
{
public:
	enum
	{
		MaxBufferSize = 512,
		MaxEncodedEventSize = 30, // 10 + 10 + 10
		FullBufferThreshold = MaxBufferSize - MaxEncodedEventSize,
	};

	enum EOpType
	{
		Increment = 0,
		Decrement = 1,
		AddInteger = 2,
		SetInteger = 3,
		AddFloat = 4,
		SetFloat = 5,
	};

	struct FThreadState
	{
		uint64 LastCycle;
		uint16 BufferSize;
		uint8 Buffer[MaxBufferSize];
	};

	static FThreadState* GetThreadState() { return ThreadLocalThreadState; }
	FORCENOINLINE static FThreadState* InitThreadState();
	FORCENOINLINE static void FlushThreadBuffer(FThreadState* ThreadState);
	static void BeginEncodeOp(const FName& Stat, EOpType Op, FStatsTraceInternal::FThreadState*& OutThreadState, uint8*& OutBufferPtr);
	static void EndEncodeOp(FStatsTraceInternal::FThreadState* ThreadState, uint8* BufferPtr);

private:
	static thread_local FThreadState* ThreadLocalThreadState;
};

thread_local FStatsTraceInternal::FThreadState* FStatsTraceInternal::ThreadLocalThreadState = nullptr;

FStatsTraceInternal::FThreadState* FStatsTraceInternal::InitThreadState()
{
	ThreadLocalThreadState = new FThreadState();
	ThreadLocalThreadState->BufferSize = 0;
	ThreadLocalThreadState->LastCycle = 0;
	return ThreadLocalThreadState;
}

void FStatsTraceInternal::FlushThreadBuffer(FThreadState* ThreadState)
{
	UE_TRACE_LOG(Stats, EventBatch, StatsChannel, ThreadState->BufferSize)
		<< EventBatch.Attachment(ThreadState->Buffer, ThreadState->BufferSize);
	ThreadState->BufferSize = 0;
}

void FStatsTraceInternal::BeginEncodeOp(const FName& Stat, EOpType Op, FThreadState*& OutThreadState, uint8*& OutBufferPtr)
{
	uint64 Cycle = FPlatformTime::Cycles64();
	OutThreadState = GetThreadState();
	if (!OutThreadState)
	{
		OutThreadState = InitThreadState();
	}
	uint64 CycleDiff = Cycle - OutThreadState->LastCycle;
	OutThreadState->LastCycle = Cycle;
	if (OutThreadState->BufferSize >= FullBufferThreshold)
	{
		FStatsTraceInternal::FlushThreadBuffer(OutThreadState);
	}
	OutBufferPtr = OutThreadState->Buffer + OutThreadState->BufferSize;
	FTraceUtils::Encode7bit((uint64(Stat.GetComparisonIndex().ToUnstableInt()) << 3) | uint64(Op), OutBufferPtr);
	FTraceUtils::Encode7bit(CycleDiff, OutBufferPtr);
}

void FStatsTraceInternal::EndEncodeOp(FThreadState* ThreadState, uint8* BufferPtr)
{
	ThreadState->BufferSize = uint16(BufferPtr - ThreadState->Buffer);
}

void FStatsTrace::DeclareStat(const FName& Stat, const ANSICHAR* Name, const TCHAR* Description, bool IsFloatingPoint, bool IsMemory, bool ShouldClearEveryFrame)
{
	uint16 NameSize = (strlen(Name) + 1) * sizeof(ANSICHAR);
	uint16 DescriptionSize = (FCString::Strlen(Description) + 1) * sizeof(TCHAR);
	auto Attachment = [Name, NameSize, Description, DescriptionSize](uint8* Buffer)
	{
		memcpy(Buffer, Name, NameSize);
		memcpy(Buffer + NameSize, Description, DescriptionSize);
	};
	UE_TRACE_LOG(Stats, Spec, StatsChannel, NameSize + DescriptionSize)
		<< Spec.Id(Stat.GetComparisonIndex().ToUnstableInt())
		<< Spec.IsFloatingPoint(IsFloatingPoint)
		<< Spec.IsMemory(IsMemory)
		<< Spec.ShouldClearEveryFrame(ShouldClearEveryFrame)
		<< Spec.Attachment(Attachment);
}

void FStatsTrace::Increment(const FName& Stat)
{
	FStatsTraceInternal::FThreadState* ThreadState;
	uint8* BufferPtr;
	FStatsTraceInternal::BeginEncodeOp(Stat, FStatsTraceInternal::Increment, ThreadState, BufferPtr);
	ThreadState->BufferSize = uint16(BufferPtr - ThreadState->Buffer);
	FStatsTraceInternal::EndEncodeOp(ThreadState, BufferPtr);
}

void FStatsTrace::Decrement(const FName& Stat)
{
	FStatsTraceInternal::FThreadState* ThreadState;
	uint8* BufferPtr;
	FStatsTraceInternal::BeginEncodeOp(Stat, FStatsTraceInternal::Decrement, ThreadState, BufferPtr);
	ThreadState->BufferSize = uint16(BufferPtr - ThreadState->Buffer);
	FStatsTraceInternal::EndEncodeOp(ThreadState, BufferPtr);
}

void FStatsTrace::Add(const FName& Stat, int64 Amount)
{
	FStatsTraceInternal::FThreadState* ThreadState;
	uint8* BufferPtr;
	FStatsTraceInternal::BeginEncodeOp(Stat, FStatsTraceInternal::AddInteger, ThreadState, BufferPtr);
	FTraceUtils::EncodeZigZag(Amount, BufferPtr);
	FStatsTraceInternal::EndEncodeOp(ThreadState, BufferPtr);
}

void FStatsTrace::Add(const FName& Stat, double Amount)
{
	FStatsTraceInternal::FThreadState* ThreadState;
	uint8* BufferPtr;
	FStatsTraceInternal::BeginEncodeOp(Stat, FStatsTraceInternal::AddFloat, ThreadState, BufferPtr);
	memcpy(BufferPtr, &Amount, sizeof(double));
	BufferPtr += sizeof(double);
	FStatsTraceInternal::EndEncodeOp(ThreadState, BufferPtr);
}

void FStatsTrace::Set(const FName& Stat, int64 Value)
{
	FStatsTraceInternal::FThreadState* ThreadState;
	uint8* BufferPtr;
	FStatsTraceInternal::BeginEncodeOp(Stat, FStatsTraceInternal::SetInteger, ThreadState, BufferPtr);
	FTraceUtils::EncodeZigZag(Value, BufferPtr);
	FStatsTraceInternal::EndEncodeOp(ThreadState, BufferPtr);
}

void FStatsTrace::Set(const FName& Stat, double Value)
{
	FStatsTraceInternal::FThreadState* ThreadState;
	uint8* BufferPtr;
	FStatsTraceInternal::BeginEncodeOp(Stat, FStatsTraceInternal::SetFloat, ThreadState, BufferPtr);
	memcpy(BufferPtr, &Value, sizeof(double));
	BufferPtr += sizeof(double);
	FStatsTraceInternal::EndEncodeOp(ThreadState, BufferPtr);
}

#endif