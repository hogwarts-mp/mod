// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Trace/Trace.h"
#include "Trace/Detail/Channel.h"
#include "Trace/Detail/Channel.inl"

#if !defined(CPUPROFILERTRACE_ENABLED)
#if UE_TRACE_ENABLED && !UE_BUILD_SHIPPING
#define CPUPROFILERTRACE_ENABLED 1
#else
#define CPUPROFILERTRACE_ENABLED 0
#endif
#endif

#if CPUPROFILERTRACE_ENABLED

UE_TRACE_CHANNEL_EXTERN(CpuChannel, CORE_API);

/*
 * Facilities for tracing timed cpu events. Two types of events are supported, static events where the identifier is
 * known at compile time, and dynamic event were identifiers can be constructed in runtime. Static events have lower overhead
 * so always prefer to use them if possible.
 *
 * Events are tracked per thread, so begin/end calls must be matched and called on the same thread. It is possible to use any channel
 * to emit the events, but both that channel and the CpuChannel must then be enabled.
 *
 * Usage of the scope macros is highly encouraged in order to avoid mistakes.
 */
struct FCpuProfilerTrace
{
	CORE_API static void Shutdown();
	/*
	 * Output cpu event definition (spec).
	 * @param Name Event name
	 * @return Event definition id
	 */
	FORCENOINLINE CORE_API static uint32 OutputEventType(const ANSICHAR* Name);
	/*
	 * Output cpu event definition (spec).
	 * @param Name Event name
	 * @return Event definition id
	 */
	FORCENOINLINE CORE_API static uint32 OutputEventType(const TCHAR* Name);
	/*
	 * Output begin event marker for a given spec. Must always be matched with an end event.
	 * @param SpecId Event definition id.
	 */
	CORE_API static void OutputBeginEvent(uint32 SpecId);
	/*
	 * Output begin event marker for a dynamic event name. This is more expensive than statically known event
	 * names using \ref OutputBeginEvent. Must always be matched with an end event.
	 * @param Name Name of event
	 */
	CORE_API static void OutputBeginDynamicEvent(const ANSICHAR* Name);
	/*
	 * Output begin event marker for a dynamic event name. This is more expensive than statically known event
	 * names using \ref OutputBeginEvent. Must always be matched with an end event.
	 * @param Name Name of event
	 */
	CORE_API static void OutputBeginDynamicEvent(const TCHAR* Name);
	/*
	 * Output end event marker for static or dynamic event for the currently open scope.
	 */
	CORE_API static void OutputEndEvent();

	struct FEventScope
	{
		FEventScope(uint32 InSpecId, const Trace::FChannel& Channel)
			: bEnabled(Channel | CpuChannel)
		{
			if (bEnabled)
			{
				OutputBeginEvent(InSpecId);
			}
		}

		~FEventScope()
		{
			if (bEnabled)
			{
				OutputEndEvent();
			}
		}

		bool bEnabled;
	};

	struct FDynamicEventScope
	{
		FDynamicEventScope(const ANSICHAR* InEventName, const Trace::FChannel& Channel)
			: bEnabled(Channel | CpuChannel)
		{
			if (bEnabled)
			{
				OutputBeginDynamicEvent(InEventName);
			}
		}

		FDynamicEventScope(const TCHAR* InEventName, const Trace::FChannel& Channel)
			: bEnabled(Channel | CpuChannel)
		{
			if (bEnabled)
			{
				OutputBeginDynamicEvent(InEventName);
			}
		}

		~FDynamicEventScope()
		{
			if (bEnabled)
			{
				OutputEndEvent();
			}
		}

		bool bEnabled;
	};
};

#define TRACE_CPUPROFILER_SHUTDOWN() \
	FCpuProfilerTrace::Shutdown();

// Trace a scoped cpu timing event providing a static string (const ANSICHAR* or const TCHAR*)
// as the scope name and a trace channel.
// Example: TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL_STR("My Scoped Timer A", CpuChannel)
// Note: The event will be emitted only if both the given channel and CpuChannel is enabled.
#define TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL_STR(NameStr, Channel) \
	static uint32 PREPROCESSOR_JOIN(__CpuProfilerEventSpecId, __LINE__); \
	if (bool(Channel|CpuChannel) && PREPROCESSOR_JOIN(__CpuProfilerEventSpecId, __LINE__) == 0) { \
		PREPROCESSOR_JOIN(__CpuProfilerEventSpecId, __LINE__) = FCpuProfilerTrace::OutputEventType(NameStr); \
	} \
	FCpuProfilerTrace::FEventScope PREPROCESSOR_JOIN(__CpuProfilerEventScope, __LINE__)(PREPROCESSOR_JOIN(__CpuProfilerEventSpecId, __LINE__), Channel);

// Trace a scoped cpu timing event providing a scope name (plain text) and a trace channel.
// Example: TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(MyScopedTimer::A, CpuChannel)
// Note: Do not use this macro with a static string because, in that case, additional quotes will
//       be added around the event scope name.
// Note: The event will be emitted only if both the given channel and CpuChannel is enabled.
#define TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(Name, Channel) \
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL_STR(#Name, Channel)

// Trace a scoped cpu timing event providing a static string (const ANSICHAR* or const TCHAR*)
// as the scope name. It will use the Cpu trace channel.
// Example: TRACE_CPUPROFILER_EVENT_SCOPE_STR("My Scoped Timer A")
#define TRACE_CPUPROFILER_EVENT_SCOPE_STR(NameStr) \
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL_STR(NameStr, CpuChannel)

// Trace a scoped cpu timing event providing a scope name (plain text) and a trace channel.
// It will use the Cpu trace channel.
// Example: TRACE_CPUPROFILER_EVENT_SCOPE(MyScopedTimer::A)
// Note: Do not use this macro with a static string because, in that case, additional quotes will
//       be added around the event scope name.
#define TRACE_CPUPROFILER_EVENT_SCOPE(Name) \
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(Name, CpuChannel)

// Trace a scoped cpu timing event providing a dynamic string (const ANSICHAR* or const TCHAR*)
// as the scope name and a trace channel.
// Example: TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(*MyScopedTimerNameString, CpuChannel)
// Note: This macro has a larger overhead compared to macro that accepts a plain text name
//       or a static string. Use it only if scope name really needs to be a dynamic string.
#define TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(Name, Channel) \
	FCpuProfilerTrace::FDynamicEventScope PREPROCESSOR_JOIN(__CpuProfilerEventScope, __LINE__)(Name, Channel);

// Trace a scoped cpu timing event providing a dynamic string (const ANSICHAR* or const TCHAR*)
// as the scope name. It will use the Cpu trace channel.
// Example: TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*MyScopedTimerNameString)
// Note: This macro has a larger overhead compared to macro that accepts a plain text name
//       or a static string. Use it only if scope name really needs to be a dynamic string.
#define TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(Name) \
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(Name, CpuChannel)

#else

#define TRACE_CPUPROFILER_SHUTDOWN()
#define TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL_STR(NameStr, Channel)
#define TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(Name, Channel)
#define TRACE_CPUPROFILER_EVENT_SCOPE_STR(NameStr)
#define TRACE_CPUPROFILER_EVENT_SCOPE(Name)
#define TRACE_CPUPROFILER_EVENT_SCOPE_TEXT_ON_CHANNEL(Name, Channel)
#define TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(Name)

#endif
