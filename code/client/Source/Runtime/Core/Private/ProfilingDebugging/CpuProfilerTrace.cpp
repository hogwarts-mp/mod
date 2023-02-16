// Copyright Epic Games, Inc. All Rights Reserved.
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Trace/Trace.inl"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformTLS.h"
#include "HAL/TlsAutoCleanup.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Misc/Parse.h"
#include "Containers/Map.h"
#include "Misc/MemStack.h"
#include "Misc/Crc.h"

#if CPUPROFILERTRACE_ENABLED

UE_TRACE_CHANNEL_DEFINE(CpuChannel)

UE_TRACE_EVENT_BEGIN(CpuProfiler, EventSpec, Important)
	UE_TRACE_EVENT_FIELD(uint32, Id)
	UE_TRACE_EVENT_FIELD(uint8, CharSize)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(CpuProfiler, EventBatch, NoSync)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(CpuProfiler, EndCapture, Important)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(CpuProfiler, EndThread, NoSync)
UE_TRACE_EVENT_END()

struct FCpuProfilerTraceInternal
{
	enum
	{
		MaxBufferSize = 256,
		MaxEncodedEventSize = 15, // 10 + 5
		FullBufferThreshold = MaxBufferSize - MaxEncodedEventSize,
	};

	template<typename CharType>
	struct FDynamicScopeNameMapKeyFuncs
	{
		typedef const CharType* KeyType;
		typedef const CharType* KeyInitType;
		typedef const TPairInitializer<const CharType*, uint32>& ElementInitType;

		enum { bAllowDuplicateKeys = false };

		static FORCEINLINE bool Matches(const CharType* A, const CharType* B)
		{
			return TCString<CharType>::Stricmp(A, B) == 0;
		}

		static FORCEINLINE uint32 GetKeyHash(const CharType* Key)
		{
			return FCrc::Strihash_DEPRECATED(Key);
		}

		static FORCEINLINE KeyInitType GetSetKey(ElementInitType Element)
		{
			return Element.Key;
		}
	};

	struct FThreadBuffer
		: public FTlsAutoCleanup
	{
		FThreadBuffer()
			: DynamicScopeNamesMemory(0)
		{
		}

		virtual ~FThreadBuffer()
		{
			UE_TRACE_LOG(CpuProfiler, EndThread, CpuChannel);
		}

		uint64 LastCycle = 0;
		uint16 BufferSize = 0;
		uint8 Buffer[MaxBufferSize];
		FMemStackBase DynamicScopeNamesMemory;
		TMap<const ANSICHAR*, uint32, FDefaultSetAllocator, FDynamicScopeNameMapKeyFuncs<ANSICHAR>> DynamicAnsiScopeNamesMap;
		TMap<const TCHAR*, uint32, FDefaultSetAllocator, FDynamicScopeNameMapKeyFuncs<TCHAR>> DynamicTCharScopeNamesMap;
	};

	uint32 static GetNextSpecId();
	FORCENOINLINE static FThreadBuffer* CreateThreadBuffer();
	FORCENOINLINE static void FlushThreadBuffer(FThreadBuffer* ThreadBuffer);
	FORCENOINLINE static void EndCapture(FThreadBuffer* ThreadBuffer);

	static thread_local uint32 ThreadDepth;
	static thread_local FThreadBuffer* ThreadBuffer;
};

thread_local uint32 FCpuProfilerTraceInternal::ThreadDepth = 0;
thread_local FCpuProfilerTraceInternal::FThreadBuffer* FCpuProfilerTraceInternal::ThreadBuffer = nullptr;

FCpuProfilerTraceInternal::FThreadBuffer* FCpuProfilerTraceInternal::CreateThreadBuffer()
{
	ThreadBuffer = new FThreadBuffer();
	ThreadBuffer->Register();
	return ThreadBuffer;
}

void FCpuProfilerTraceInternal::FlushThreadBuffer(FThreadBuffer* InThreadBuffer)
{
	UE_TRACE_LOG(CpuProfiler, EventBatch, true, InThreadBuffer->BufferSize)
		<< EventBatch.Attachment(InThreadBuffer->Buffer, InThreadBuffer->BufferSize);
	InThreadBuffer->BufferSize = 0;
	InThreadBuffer->LastCycle = 0;
}

void FCpuProfilerTraceInternal::EndCapture(FThreadBuffer* InThreadBuffer)
{
	UE_TRACE_LOG(CpuProfiler, EndCapture, true, InThreadBuffer->BufferSize)
		<< EndCapture.Attachment(InThreadBuffer->Buffer, InThreadBuffer->BufferSize);
	InThreadBuffer->BufferSize = 0;
	InThreadBuffer->LastCycle = 0;
}

#define CPUPROFILERTRACE_OUTPUTBEGINEVENT_PROLOGUE() \
	++FCpuProfilerTraceInternal::ThreadDepth; \
	FCpuProfilerTraceInternal::FThreadBuffer* ThreadBuffer = FCpuProfilerTraceInternal::ThreadBuffer; \
	if (!ThreadBuffer) \
	{ \
		ThreadBuffer = FCpuProfilerTraceInternal::CreateThreadBuffer(); \
	} \

#define CPUPROFILERTRACE_OUTPUTBEGINEVENT_EPILOGUE() \
	uint64 Cycle = FPlatformTime::Cycles64(); \
	uint64 CycleDiff = Cycle - ThreadBuffer->LastCycle; \
	ThreadBuffer->LastCycle = Cycle; \
	uint8* BufferPtr = ThreadBuffer->Buffer + ThreadBuffer->BufferSize; \
	FTraceUtils::Encode7bit((CycleDiff << 1) | 1ull, BufferPtr); \
	FTraceUtils::Encode7bit(SpecId, BufferPtr); \
	ThreadBuffer->BufferSize = (uint16)(BufferPtr - ThreadBuffer->Buffer); \
	if (ThreadBuffer->BufferSize >= FCpuProfilerTraceInternal::FullBufferThreshold) \
	{ \
		FCpuProfilerTraceInternal::FlushThreadBuffer(ThreadBuffer); \
	}

void FCpuProfilerTrace::OutputBeginEvent(uint32 SpecId)
{
	CPUPROFILERTRACE_OUTPUTBEGINEVENT_PROLOGUE();
	CPUPROFILERTRACE_OUTPUTBEGINEVENT_EPILOGUE();
}

void FCpuProfilerTrace::OutputBeginDynamicEvent(const ANSICHAR* Name)
{
	CPUPROFILERTRACE_OUTPUTBEGINEVENT_PROLOGUE();
	uint32 SpecId = ThreadBuffer->DynamicAnsiScopeNamesMap.FindRef(Name);
	if (!SpecId)
	{
		int32 NameSize = strlen(Name) + 1;
		ANSICHAR* NameCopy = reinterpret_cast<ANSICHAR*>(ThreadBuffer->DynamicScopeNamesMemory.Alloc(NameSize, alignof(ANSICHAR)));
		FMemory::Memmove(NameCopy, Name, NameSize);
		SpecId = OutputEventType(NameCopy);
		ThreadBuffer->DynamicAnsiScopeNamesMap.Add(NameCopy, SpecId);
	}
	CPUPROFILERTRACE_OUTPUTBEGINEVENT_EPILOGUE();
}

void FCpuProfilerTrace::OutputBeginDynamicEvent(const TCHAR* Name)
{
	CPUPROFILERTRACE_OUTPUTBEGINEVENT_PROLOGUE();
	uint32 SpecId = ThreadBuffer->DynamicTCharScopeNamesMap.FindRef(Name);
	if (!SpecId)
	{
		int32 NameSize = (FCString::Strlen(Name) + 1) * sizeof(TCHAR);
		TCHAR* NameCopy = reinterpret_cast<TCHAR*>(ThreadBuffer->DynamicScopeNamesMemory.Alloc(NameSize, alignof(TCHAR)));
		FMemory::Memmove(NameCopy, Name, NameSize);
		SpecId = OutputEventType(NameCopy);
		ThreadBuffer->DynamicTCharScopeNamesMap.Add(NameCopy, SpecId);
	}
	CPUPROFILERTRACE_OUTPUTBEGINEVENT_EPILOGUE();
}

#undef CPUPROFILERTRACE_OUTPUTBEGINEVENT_PROLOGUE
#undef CPUPROFILERTRACE_OUTPUTBEGINEVENT_EPILOGUE

void FCpuProfilerTrace::OutputEndEvent()
{
	--FCpuProfilerTraceInternal::ThreadDepth;
	FCpuProfilerTraceInternal::FThreadBuffer* ThreadBuffer = FCpuProfilerTraceInternal::ThreadBuffer;
	if (!ThreadBuffer)
	{
		ThreadBuffer = FCpuProfilerTraceInternal::CreateThreadBuffer();
	}
	uint64 Cycle = FPlatformTime::Cycles64();
	uint64 CycleDiff = Cycle - ThreadBuffer->LastCycle;
	ThreadBuffer->LastCycle = Cycle;
	uint8* BufferPtr = ThreadBuffer->Buffer + ThreadBuffer->BufferSize;
	FTraceUtils::Encode7bit(CycleDiff << 1, BufferPtr);
	ThreadBuffer->BufferSize = (uint16)(BufferPtr - ThreadBuffer->Buffer);
	if ((FCpuProfilerTraceInternal::ThreadDepth == 0) | (ThreadBuffer->BufferSize >= FCpuProfilerTraceInternal::FullBufferThreshold))
	{
		FCpuProfilerTraceInternal::FlushThreadBuffer(ThreadBuffer);
	}
}

uint32 FCpuProfilerTraceInternal::GetNextSpecId()
{
	static TAtomic<uint32> NextSpecId(1);
	return NextSpecId++;
}

uint32 FCpuProfilerTrace::OutputEventType(const TCHAR* Name)
{
	uint32 SpecId = FCpuProfilerTraceInternal::GetNextSpecId();
	uint16 NameSize = (uint16)((FCString::Strlen(Name) + 1) * sizeof(TCHAR));
	UE_TRACE_LOG(CpuProfiler, EventSpec, CpuChannel, NameSize)
		<< EventSpec.Id(SpecId)
		<< EventSpec.CharSize(uint8(sizeof(TCHAR)))
		<< EventSpec.Attachment(Name, NameSize);
	return SpecId;
}

uint32 FCpuProfilerTrace::OutputEventType(const ANSICHAR* Name)
{
	uint32 SpecId = FCpuProfilerTraceInternal::GetNextSpecId();
	uint16 NameSize = (uint16)(strlen(Name) + 1);
	UE_TRACE_LOG(CpuProfiler, EventSpec, CpuChannel, NameSize)
		<< EventSpec.Id(SpecId)
		<< EventSpec.CharSize(uint8(1))
		<< EventSpec.Attachment(Name, NameSize);
	return SpecId;
}

void FCpuProfilerTrace::Shutdown()
{
	if (FCpuProfilerTraceInternal::ThreadBuffer)
	{
		delete FCpuProfilerTraceInternal::ThreadBuffer;
		FCpuProfilerTraceInternal::ThreadBuffer = nullptr;
	}
}

#endif
