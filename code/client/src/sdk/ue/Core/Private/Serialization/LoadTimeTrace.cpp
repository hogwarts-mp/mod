// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/LoadTimeTrace.h"

#if LOADTIMEPROFILERTRACE_ENABLED

#include "Trace/Trace.inl"
#include "Misc/CString.h"
#include "HAL/PlatformTLS.h"

UE_TRACE_CHANNEL_DEFINE(LoadTimeChannel)

UE_TRACE_EVENT_BEGIN(LoadTime, BeginRequestGroup)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(LoadTime, EndRequestGroup)
UE_TRACE_EVENT_END()

FLoadTimeProfilerTrace::FRequestGroupScope::~FRequestGroupScope()
{
	UE_TRACE_LOG(LoadTime, EndRequestGroup, LoadTimeChannel);
}

void FLoadTimeProfilerTrace::FRequestGroupScope::OutputBegin()
{
	uint16 FormatStringSize = (uint16)((FCString::Strlen(FormatString) + 1) * sizeof(TCHAR));
	auto Attachment = [this, FormatStringSize](uint8* Out)
	{
		memcpy(Out, FormatString, FormatStringSize);
		memcpy(Out + FormatStringSize, FormatArgsBuffer, FormatArgsSize);
	};
	UE_TRACE_LOG(LoadTime, BeginRequestGroup, LoadTimeChannel, FormatStringSize + FormatArgsSize)
		<< BeginRequestGroup.Attachment(Attachment);
}


#endif

