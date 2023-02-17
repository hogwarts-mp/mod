// Copyright Epic Games, Inc. All Rights Reserved.
#include "ProfilingDebugging/CountersTrace.h"
#include "HAL/PlatformTime.h"
#include "Misc/Parse.h"
#include "Trace/Trace.inl"

#if COUNTERSTRACE_ENABLED

UE_TRACE_CHANNEL_DEFINE(CountersChannel)

UE_TRACE_EVENT_BEGIN(Counters, Spec, Important)
	UE_TRACE_EVENT_FIELD(uint16, Id)
	UE_TRACE_EVENT_FIELD(uint8, Type)
	UE_TRACE_EVENT_FIELD(uint8, DisplayHint)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Counters, SetValueInt)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(int64, Value)
	UE_TRACE_EVENT_FIELD(uint16, CounterId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Counters, SetValueFloat)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(double, Value)
	UE_TRACE_EVENT_FIELD(uint16, CounterId)
UE_TRACE_EVENT_END()


uint16 FCountersTrace::OutputInitCounter(const TCHAR* CounterName, ETraceCounterType CounterType, ETraceCounterDisplayHint CounterDisplayHint)
{
	static TAtomic<uint16> NextId(1);
	if (!UE_TRACE_CHANNELEXPR_IS_ENABLED(CountersChannel))
		return 0;
	uint16 CounterId = uint16(NextId++);
	uint16 NameSize = (uint16)((FCString::Strlen(CounterName) + 1) * sizeof(TCHAR));
	UE_TRACE_LOG(Counters, Spec, CountersChannel, NameSize)
		<< Spec.Id(CounterId)
		<< Spec.Type(uint8(CounterType))
		<< Spec.DisplayHint(uint8(CounterDisplayHint))
		<< Spec.Attachment(CounterName, NameSize);
	return CounterId;
}

void FCountersTrace::OutputSetValue(uint16 CounterId, int64 Value)
{
	UE_TRACE_LOG(Counters, SetValueInt, CountersChannel && CounterId)
		<< SetValueInt.Cycle(FPlatformTime::Cycles64())
		<< SetValueInt.Value(Value)
		<< SetValueInt.CounterId(CounterId);
}

void FCountersTrace::OutputSetValue(uint16 CounterId, double Value)
{
	UE_TRACE_LOG(Counters, SetValueFloat, CountersChannel && CounterId)
		<< SetValueFloat.Cycle(FPlatformTime::Cycles64())
		<< SetValueFloat.Value(Value)
		<< SetValueFloat.CounterId(CounterId);
}

#endif