// Copyright Epic Games, Inc. All Rights Reserved.

#include "Trace/Trace.inl" // should be Config.h :(

#if UE_TRACE_ENABLED

#include "Trace/Detail/Channel.h"

namespace Trace
{

namespace Private
{

////////////////////////////////////////////////////////////////////////////////
void	Writer_MemorySetHooks(AllocFunc, FreeFunc);
void	Writer_Initialize(const FInitializeDesc&);
void	Writer_Shutdown();
void	Writer_Update();
bool	Writer_SendTo(const ANSICHAR*, uint32);
bool	Writer_WriteTo(const ANSICHAR*);
bool	Writer_IsTracing();

} // namespace Private



////////////////////////////////////////////////////////////////////////////////
template <int DestSize>
static uint32 ToAnsiCheap(ANSICHAR (&Dest)[DestSize], const WIDECHAR* Src)
{
	const WIDECHAR* Cursor = Src;
	for (ANSICHAR& Out : Dest)
	{
		Out = ANSICHAR(*Cursor++ & 0x7f);
		if (Out == '\0')
		{
			break;
		}
	}
	Dest[DestSize - 1] = '\0';
	return uint32(UPTRINT(Cursor - Src));
};

////////////////////////////////////////////////////////////////////////////////
void SetMemoryHooks(AllocFunc Alloc, FreeFunc Free)
{
	Private::Writer_MemorySetHooks(Alloc, Free);
}

////////////////////////////////////////////////////////////////////////////////
void Initialize(const FInitializeDesc& Desc)
{
	Private::Writer_Initialize(Desc);
	FChannel::Initialize();
}

////////////////////////////////////////////////////////////////////////////////
void Shutdown()
{
	Private::Writer_Shutdown();
}

////////////////////////////////////////////////////////////////////////////////
void Update()
{
	Private::Writer_Update();
}

////////////////////////////////////////////////////////////////////////////////
bool SendTo(const TCHAR* InHost, uint32 Port)
{
	char Host[256];
	ToAnsiCheap(Host, InHost);
	return Private::Writer_SendTo(Host, Port);
}

////////////////////////////////////////////////////////////////////////////////
bool WriteTo(const TCHAR* InPath)
{
	char Path[512];
	ToAnsiCheap(Path, InPath);
	return Private::Writer_WriteTo(Path);
}

////////////////////////////////////////////////////////////////////////////////
bool IsTracing()
{
	return Private::Writer_IsTracing();
}

////////////////////////////////////////////////////////////////////////////////
bool IsChannel(const TCHAR* ChannelName)
{
	ANSICHAR ChannelNameA[64];
	ToAnsiCheap(ChannelNameA, ChannelName);
	return FChannel::FindChannel(ChannelNameA) != nullptr;
}

////////////////////////////////////////////////////////////////////////////////
bool ToggleChannel(const TCHAR* ChannelName, bool bEnabled)
{
	ANSICHAR ChannelNameA[64];
	ToAnsiCheap(ChannelNameA, ChannelName);
	return FChannel::Toggle(ChannelNameA, bEnabled);
}



////////////////////////////////////////////////////////////////////////////////
UE_TRACE_CHANNEL_EXTERN(TraceLogChannel)

UE_TRACE_EVENT_BEGIN($Trace, ThreadInfo)
	UE_TRACE_EVENT_FIELD(uint32, SystemId)
	UE_TRACE_EVENT_FIELD(int32, SortHint)
	UE_TRACE_EVENT_FIELD(Trace::AnsiString, Name)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN($Trace, ThreadGroupBegin)
	UE_TRACE_EVENT_FIELD(Trace::AnsiString, Name)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN($Trace, ThreadGroupEnd)
UE_TRACE_EVENT_END()

////////////////////////////////////////////////////////////////////////////////
void ThreadRegister(const TCHAR* Name, uint32 SystemId, int32 SortHint)
{
	UE_TRACE_LOG($Trace, ThreadInfo, TraceLogChannel)
		<< ThreadInfo.SystemId(SystemId)
		<< ThreadInfo.SortHint(SortHint)
		<< ThreadInfo.Name(Name);
}

////////////////////////////////////////////////////////////////////////////////
void ThreadGroupBegin(const TCHAR* Name)
{
	UE_TRACE_LOG($Trace, ThreadGroupBegin, TraceLogChannel)
		<< ThreadGroupBegin.Name(Name);
}

////////////////////////////////////////////////////////////////////////////////
void ThreadGroupEnd()
{
	UE_TRACE_LOG($Trace, ThreadGroupEnd, TraceLogChannel);
}

} // namespace Trace

#else

// Workaround for module not having any exported symbols
TRACELOG_API int TraceLogExportedSymbol = 0;

#endif // UE_TRACE_ENABLED
