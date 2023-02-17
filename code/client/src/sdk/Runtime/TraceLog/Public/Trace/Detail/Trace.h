// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Config.h"

#if UE_TRACE_ENABLED

namespace Trace
{
	class FChannel;
};

#define TRACE_PRIVATE_CHANNEL_DEFAULT_ARGS false, "None"

#define TRACE_PRIVATE_CHANNEL_DECLARE(LinkageType, ChannelName) \
	static Trace::FChannel ChannelName##Object; \
	LinkageType Trace::FChannel& ChannelName = ChannelName##Object;

#define TRACE_PRIVATE_CHANNEL_IMPL(ChannelName, ...) \
	struct F##ChannelName##Registrator \
	{ \
		F##ChannelName##Registrator() \
		{ \
			ChannelName##Object.Setup(#ChannelName, { __VA_ARGS__ } ); \
		} \
	}; \
	static F##ChannelName##Registrator ChannelName##Reg = F##ChannelName##Registrator();

#define TRACE_PRIVATE_CHANNEL(ChannelName, ...) \
	TRACE_PRIVATE_CHANNEL_DECLARE(static, ChannelName) \
	TRACE_PRIVATE_CHANNEL_IMPL(ChannelName, ##__VA_ARGS__)

#define TRACE_PRIVATE_CHANNEL_MODULE_EXTERN(ModuleApi, ChannelName) \
	TRACE_PRIVATE_CHANNEL_DECLARE(ModuleApi extern, ChannelName)

#define TRACE_PRIVATE_CHANNEL_DEFINE(ChannelName, ...) \
	TRACE_PRIVATE_CHANNEL_DECLARE(, ChannelName) \
	TRACE_PRIVATE_CHANNEL_IMPL(ChannelName, ##__VA_ARGS__)

#define TRACE_PRIVATE_CHANNEL_EXTERN(ChannelName, ...) \
	__VA_ARGS__ extern Trace::FChannel& ChannelName;

#define TRACE_PRIVATE_CHANNELEXPR_IS_ENABLED(ChannelsExpr) \
	bool(ChannelsExpr)

#define TRACE_PRIVATE_EVENT_DEFINE(LoggerName, EventName) \
	Trace::Private::FEventNode LoggerName##EventName##Event;

#define TRACE_PRIVATE_EVENT_BEGIN(LoggerName, EventName, ...) \
	TRACE_PRIVATE_EVENT_BEGIN_IMPL(static, LoggerName, EventName, ##__VA_ARGS__)

#define TRACE_PRIVATE_EVENT_BEGIN_EXTERN(LoggerName, EventName, ...) \
	TRACE_PRIVATE_EVENT_BEGIN_IMPL(extern, LoggerName, EventName, ##__VA_ARGS__)

#define TRACE_PRIVATE_EVENT_BEGIN_IMPL(LinkageType, LoggerName, EventName, ...) \
	LinkageType TRACE_PRIVATE_EVENT_DEFINE(LoggerName, EventName) \
	struct F##LoggerName##EventName##Fields \
	{ \
		enum \
		{ \
			Important			= Trace::Private::FEventInfo::Flag_Important, \
			NoSync				= Trace::Private::FEventInfo::Flag_NoSync, \
			PartialEventFlags	= (0, ##__VA_ARGS__) & ~Important, \
		}; \
		enum : bool { bIsImportant = ((0, ##__VA_ARGS__) & Important) != 0, }; \
		static constexpr uint32 GetSize() { return decltype(EventProps_Private)::Size; } \
		static uint32 GetUid() { static uint32 Uid = 0; return (Uid = Uid ? Uid : Initialize()); } \
		static uint32 FORCENOINLINE Initialize() \
		{ \
			static const uint32 Uid_ThreadSafeInit = [] () \
			{ \
				using namespace Trace; \
				static F##LoggerName##EventName##Fields Fields; \
				static Trace::Private::FEventInfo Info = \
				{ \
					FLiteralName(#LoggerName), \
					FLiteralName(#EventName), \
					(FFieldDesc*)(&Fields), \
					uint16(sizeof(Fields) / sizeof(FFieldDesc)), \
					uint16(EventFlags), \
				}; \
				return LoggerName##EventName##Event.Initialize(&Info); \
			}(); \
			return Uid_ThreadSafeInit; \
		} \
		Trace::TField<0 /*Index*/, 0 /*Offset*/,

#define TRACE_PRIVATE_EVENT_FIELD(FieldType, FieldName) \
		FieldType> const FieldName##_Field = Trace::FLiteralName(#FieldName); \
		template <typename... Ts> auto FieldName(Ts... ts) const { FieldName##_Field.Set((uint8*)this, Forward<Ts>(ts)...); return true; } \
		Trace::TField< \
			decltype(FieldName##_Field)::Index + 1, \
			decltype(FieldName##_Field)::Offset + decltype(FieldName##_Field)::Size,

#define TRACE_PRIVATE_EVENT_END() \
		Trace::EventProps> const EventProps_Private = {}; \
		Trace::TField<0, decltype(EventProps_Private)::Size, Trace::Attachment> const Attachment_Field = {}; \
		template <typename... Ts> auto Attachment(Ts... ts) const { Attachment_Field.Set((uint8*)this, Forward<Ts>(ts)...); return true; } \
		explicit operator bool () const { return true; } \
		enum { EventFlags = PartialEventFlags|(decltype(EventProps_Private)::MaybeHasAux ? Trace::Private::FEventInfo::Flag_MaybeHasAux : 0), }; \
	};

#define TRACE_PRIVATE_LOG_PRELUDE(EnterFunc, LoggerName, EventName, ChannelsExpr, ...) \
	if (TRACE_PRIVATE_CHANNELEXPR_IS_ENABLED(ChannelsExpr)) \
		if (auto LogScope = Trace::Private::TLogScope<F##LoggerName##EventName##Fields>::EnterFunc(__VA_ARGS__)) \
			if (const auto& __restrict EventName = *(F##LoggerName##EventName##Fields*)LogScope.GetPointer())

#define TRACE_PRIVATE_LOG_EPILOG() \
				LogScope += LogScope

#define TRACE_PRIVATE_LOG(LoggerName, EventName, ChannelsExpr, ...) \
	TRACE_PRIVATE_LOG_PRELUDE(Enter, LoggerName, EventName, ChannelsExpr, ##__VA_ARGS__) \
		TRACE_PRIVATE_LOG_EPILOG()

#define TRACE_PRIVATE_LOG_SCOPED(LoggerName, EventName, ChannelsExpr, ...) \
	Trace::Private::FScopedLogScope PREPROCESSOR_JOIN(TheScope, __LINE__); \
	TRACE_PRIVATE_LOG_PRELUDE(ScopedEnter, LoggerName, EventName, ChannelsExpr, ##__VA_ARGS__) \
		PREPROCESSOR_JOIN(TheScope, __LINE__).SetActive(), \
		TRACE_PRIVATE_LOG_EPILOG()

#define TRACE_PRIVATE_LOG_SCOPED_T(LoggerName, EventName, ChannelsExpr, ...) \
	Trace::Private::FScopedStampedLogScope PREPROCESSOR_JOIN(TheScope, __LINE__); \
	TRACE_PRIVATE_LOG_PRELUDE(ScopedStampedEnter, LoggerName, EventName, ChannelsExpr, ##__VA_ARGS__) \
		PREPROCESSOR_JOIN(TheScope, __LINE__).SetActive(), \
		TRACE_PRIVATE_LOG_EPILOG()

#else

#define TRACE_PRIVATE_CHANNEL(ChannelName, ...)

#define TRACE_PRIVATE_CHANNEL_EXTERN(ChannelName, ...)

#define TRACE_PRIVATE_CHANNEL_MODULE_EXTERN(ModuleApi, ChannelName)

#define TRACE_PRIVATE_CHANNEL_DEFINE(ChannelName, ...)

#define TRACE_PRIVATE_CHANNELEXPR_IS_ENABLED(ChannelsExpr) \
	false

#define TRACE_PRIVATE_EVENT_DEFINE(LoggerName, EventName)

#define TRACE_PRIVATE_EVENT_BEGIN(LoggerName, EventName, ...) \
	TRACE_PRIVATE_EVENT_BEGIN_IMPL(LoggerName, EventName)

#define TRACE_PRIVATE_EVENT_BEGIN_EXTERN(LoggerName, EventName) \
	TRACE_PRIVATE_EVENT_BEGIN_IMPL(LoggerName, EventName)

#define TRACE_PRIVATE_EVENT_BEGIN_IMPL(LoggerName, EventName) \
	struct F##LoggerName##EventName##Dummy \
	{ \
		struct FTraceDisabled \
		{ \
			const FTraceDisabled& operator () (...) const { return *this; } \
		}; \
		const F##LoggerName##EventName##Dummy& operator << (const FTraceDisabled&) const \
		{ \
			return *this; \
		} \
		explicit operator bool () const { return false; }

#define TRACE_PRIVATE_EVENT_FIELD(FieldType, FieldName) \
		const FTraceDisabled& FieldName;

#define TRACE_PRIVATE_EVENT_END() \
		const FTraceDisabled& Attachment; \
	};

#define TRACE_PRIVATE_LOG(LoggerName, EventName, ...) \
	if (const auto& EventName = *(F##LoggerName##EventName##Dummy*)1) \
		EventName

#define TRACE_PRIVATE_LOG_SCOPED(LoggerName, EventName, ...) \
	if (const auto& EventName = *(F##LoggerName##EventName##Dummy*)1) \
		EventName

#define TRACE_PRIVATE_LOG_SCOPED_T(LoggerName, EventName, ...) \
	if (const auto& EventName = *(F##LoggerName##EventName##Dummy*)1) \
		EventName

#endif // UE_TRACE_ENABLED
