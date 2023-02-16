// Copyright Epic Games, Inc. All Rights Reserved.
#include "ProfilingDebugging/MiscTrace.h"

#if MISCTRACE_ENABLED

#include "Trace/Trace.inl"
#include "Misc/CString.h"
#include "HAL/PlatformTLS.h"
#include "HAL/PlatformTime.h"

UE_TRACE_CHANNEL(FrameChannel)
UE_TRACE_CHANNEL(BookmarkChannel)

UE_TRACE_EVENT_BEGIN(Misc, BookmarkSpec, Important)
	UE_TRACE_EVENT_FIELD(const void*, BookmarkPoint)
	UE_TRACE_EVENT_FIELD(int32, Line)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Misc, Bookmark)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(const void*, BookmarkPoint)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Misc, BeginGameFrame)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Misc, EndGameFrame)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Misc, BeginRenderFrame)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(Misc, EndRenderFrame)
UE_TRACE_EVENT_END()


struct FMiscTraceInternal
{
	static uint64 LastFrameCycle[TraceFrameType_Count];
};

uint64 FMiscTraceInternal::LastFrameCycle[TraceFrameType_Count] = { 0, 0 };

void FMiscTrace::OutputBookmarkSpec(const void* BookmarkPoint, const ANSICHAR* File, int32 Line, const TCHAR* Format)
{
	uint16 FileNameSize = (uint16)(strlen(File) + 1);
	uint16 FormatStringSize = (uint16)((FCString::Strlen(Format) + 1) * sizeof(TCHAR));
	auto StringCopyFunc = [FileNameSize, FormatStringSize, File, Format](uint8* Out) {
		memcpy(Out, File, FileNameSize);
		memcpy(Out + FileNameSize, Format, FormatStringSize);
	};
	UE_TRACE_LOG(Misc, BookmarkSpec, BookmarkChannel, FileNameSize + FormatStringSize)
		<< BookmarkSpec.BookmarkPoint(BookmarkPoint)
		<< BookmarkSpec.Line(Line)
		<< BookmarkSpec.Attachment(StringCopyFunc);
}

void FMiscTrace::OutputBookmarkInternal(const void* BookmarkPoint, uint16 EncodedFormatArgsSize, uint8* EncodedFormatArgs)
{
	UE_TRACE_LOG(Misc, Bookmark, BookmarkChannel, EncodedFormatArgsSize)
		<< Bookmark.Cycle(FPlatformTime::Cycles64())
		<< Bookmark.BookmarkPoint(BookmarkPoint)
		<< Bookmark.Attachment(EncodedFormatArgs, EncodedFormatArgsSize);
}

void FMiscTrace::OutputBeginFrame(ETraceFrameType FrameType)
{
	if (!UE_TRACE_CHANNELEXPR_IS_ENABLED(FrameChannel))
	{
		return;
	}
	uint64 Cycle = FPlatformTime::Cycles64();
	uint64 CycleDiff = Cycle - FMiscTraceInternal::LastFrameCycle[FrameType];
	FMiscTraceInternal::LastFrameCycle[FrameType] = Cycle;
	uint8 Buffer[10];
	uint8* BufferPtr = Buffer;
	FTraceUtils::Encode7bit(CycleDiff, BufferPtr);
	uint16 BufferSize = (uint16)(BufferPtr - Buffer);
	if (FrameType == TraceFrameType_Game)
	{
		UE_TRACE_LOG(Misc, BeginGameFrame, FrameChannel, BufferSize)
			<< BeginGameFrame.Attachment(&Buffer, BufferSize);
	}
	else if (FrameType == TraceFrameType_Rendering)
	{
		UE_TRACE_LOG(Misc, BeginRenderFrame, FrameChannel, BufferSize)
			<< BeginRenderFrame.Attachment(&Buffer, BufferSize);
	}
}

void FMiscTrace::OutputEndFrame(ETraceFrameType FrameType)
{
	if (!UE_TRACE_CHANNELEXPR_IS_ENABLED(FrameChannel))
	{
		return;
	}
	uint64 Cycle = FPlatformTime::Cycles64();
	uint64 CycleDiff = Cycle - FMiscTraceInternal::LastFrameCycle[FrameType];
	FMiscTraceInternal::LastFrameCycle[FrameType] = Cycle;
	uint8 Buffer[10];
	uint8* BufferPtr = Buffer;
	FTraceUtils::Encode7bit(CycleDiff, BufferPtr);
	uint16 BufferSize = (uint16)(BufferPtr - Buffer);
	if (FrameType == TraceFrameType_Game)
	{
		UE_TRACE_LOG(Misc, EndGameFrame, FrameChannel, BufferSize)
			<< EndGameFrame.Attachment(&Buffer, BufferSize);
	}
	else if (FrameType == TraceFrameType_Rendering)
	{
		UE_TRACE_LOG(Misc, EndRenderFrame, FrameChannel, BufferSize)
			<< EndRenderFrame.Attachment(&Buffer, BufferSize);
	}
}

#endif
