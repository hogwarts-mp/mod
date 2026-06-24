// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Config.h"

#if UE_TRACE_ENABLED

#include "Atomic.h"
#include "EventNode.h"
#include "LogScope.h"
#include "Protocol.h"
#include "Writer.inl"

namespace Trace {
namespace Private {

////////////////////////////////////////////////////////////////////////////////
extern TRACELOG_API uint32 volatile	GLogSerial;

////////////////////////////////////////////////////////////////////////////////
inline uint8* FLogScope::GetPointer() const
{
	return Instance.Ptr;
}

////////////////////////////////////////////////////////////////////////////////
inline void FLogScope::Commit() const
{
	FWriteBuffer* Buffer = Instance.Buffer;
	AtomicStoreRelease((uint8**) &(Buffer->Committed), Buffer->Cursor);
}

////////////////////////////////////////////////////////////////////////////////
inline void FLogScope::operator += (const FLogScope&) const
{
	Commit();
}

////////////////////////////////////////////////////////////////////////////////
template <uint32 Flags>
inline FLogScope FLogScope::Enter(uint32 Uid, uint32 Size)
{
	FLogScope Ret;
	bool bMaybeHasAux = (Flags & FEventInfo::Flag_MaybeHasAux) != 0;
	if ((Flags & FEventInfo::Flag_NoSync) != 0)
	{
		Ret.EnterNoSync(Uid, Size, bMaybeHasAux);
	}
	else
	{
		Ret.Enter(Uid, Size, bMaybeHasAux);
	}
	return Ret;
}

////////////////////////////////////////////////////////////////////////////////
template <class HeaderType>
inline void FLogScope::EnterPrelude(uint32 Size, bool bMaybeHasAux)
{
	uint32 AllocSize = sizeof(HeaderType) + Size + int(bMaybeHasAux);

	FWriteBuffer* Buffer = Writer_GetBuffer();
	Buffer->Cursor += AllocSize;
	if (UNLIKELY(Buffer->Cursor > (uint8*)Buffer))
	{
		Buffer = Writer_NextBuffer(AllocSize);
	}

	// The auxilary data null terminator.
	if (bMaybeHasAux)
	{
		Buffer->Cursor[-1] = 0;
	}

	uint8* Cursor = Buffer->Cursor - Size - int(bMaybeHasAux);
	Instance = {Cursor, Buffer};
}

////////////////////////////////////////////////////////////////////////////////
inline void FLogScope::Enter(uint32 Uid, uint32 Size, bool bMaybeHasAux)
{
	EnterPrelude<FEventHeaderSync>(Size, bMaybeHasAux);

	// Event header
	auto* Header = (uint16*)(Instance.Ptr - sizeof(FEventHeaderSync::SerialHigh)); // FEventHeader1
	*(uint32*)(Header - 1) = uint32(AtomicAddRelaxed(&GLogSerial, 1u));
	Header[-2] = uint16(Size);
	Header[-3] = uint16(Uid)|int(EKnownEventUids::Flag_TwoByteUid);
}

////////////////////////////////////////////////////////////////////////////////
inline void FLogScope::EnterNoSync(uint32 Uid, uint32 Size, bool bMaybeHasAux)
{
	EnterPrelude<FEventHeader>(Size, bMaybeHasAux);

	// Event header
	auto* Header = (uint16*)(Instance.Ptr);
	Header[-1] = uint16(Size);
	Header[-2] = uint16(Uid)|int(EKnownEventUids::Flag_TwoByteUid);
}



////////////////////////////////////////////////////////////////////////////////
inline FScopedLogScope::~FScopedLogScope()
{
	if (!bActive)
	{
		return;
	}

	uint8 LeaveUid = uint8(EKnownEventUids::LeaveScope << EKnownEventUids::_UidShift);

	FWriteBuffer* Buffer = Writer_GetBuffer();
	if (UNLIKELY(int32((uint8*)Buffer - Buffer->Cursor)) < int32(sizeof(LeaveUid)))
	{
		Buffer = Writer_NextBuffer(0);
	}

	Buffer->Cursor[0] = LeaveUid;
	Buffer->Cursor += sizeof(LeaveUid);

	AtomicStoreRelease((uint8**) &(Buffer->Committed), Buffer->Cursor);
}

////////////////////////////////////////////////////////////////////////////////
inline void FScopedLogScope::SetActive()
{
	bActive = true;
}



////////////////////////////////////////////////////////////////////////////////
inline FScopedStampedLogScope::~FScopedStampedLogScope()
{
	if (!bActive)
	{
		return;
	}

	FWriteBuffer* Buffer = Writer_GetBuffer();

	uint64 Stamp = Writer_GetTimestamp(Buffer);

	if (UNLIKELY(int32((uint8*)Buffer - Buffer->Cursor) < int32(sizeof(Stamp))))
	{
		Buffer = Writer_NextBuffer(0);
	}

	Stamp <<= 8;
	Stamp += uint8(EKnownEventUids::LeaveScope_T << EKnownEventUids::_UidShift);
	memcpy((uint64*)(Buffer->Cursor), &Stamp, sizeof(Stamp));
	Buffer->Cursor += sizeof(Stamp);

	AtomicStoreRelease((uint8**) &(Buffer->Committed), Buffer->Cursor);
}

////////////////////////////////////////////////////////////////////////////////
inline void FScopedStampedLogScope::SetActive()
{
	bActive = true;
}



////////////////////////////////////////////////////////////////////////////////
template <bool>	struct TLogScopeSelector;
template <>		struct TLogScopeSelector<false>	{ typedef FLogScope Type; };
template <>		struct TLogScopeSelector<true>	{ typedef FImportantLogScope Type; };

////////////////////////////////////////////////////////////////////////////////
template <class T>
auto TLogScope<T>::Enter(uint32 ExtraSize)
{
	uint32 Size = T::GetSize() + ExtraSize;
	uint32 Uid = T::GetUid();

	using LogScopeType = typename TLogScopeSelector<T::bIsImportant>::Type;
	return LogScopeType::template Enter<T::EventFlags>(Uid, Size);
}

////////////////////////////////////////////////////////////////////////////////
template <class T>
FLogScope TLogScope<T>::ScopedEnter(uint32 ExtraSize)
{
	static_assert(!T::bIsImportant, "Important events cannot be logged with scope");

	uint8 EnterUid = uint8(EKnownEventUids::EnterScope << EKnownEventUids::_UidShift);

	FWriteBuffer* Buffer = Writer_GetBuffer();
	if (UNLIKELY(int32((uint8*)Buffer - Buffer->Cursor)) < int32(sizeof(EnterUid)))
	{
		Buffer = Writer_NextBuffer(0);
	}

	Buffer->Cursor[0] = EnterUid;
	Buffer->Cursor += sizeof(EnterUid);

	AtomicStoreRelease((uint8**) &(Buffer->Committed), Buffer->Cursor);

	return Enter(ExtraSize);
}

////////////////////////////////////////////////////////////////////////////////
template <class T>
FLogScope TLogScope<T>::ScopedStampedEnter(uint32 ExtraSize)
{
	static_assert(!T::bIsImportant, "Important events cannot be logged with scope");

	uint64 Stamp;

	FWriteBuffer* Buffer = Writer_GetBuffer();
	if (UNLIKELY(int32((uint8*)Buffer - Buffer->Cursor) < int32(sizeof(Stamp))))
	{
		Buffer = Writer_NextBuffer(0);
	}

	Stamp = Writer_GetTimestamp(Buffer);
	Stamp <<= 8;
	Stamp += uint8(EKnownEventUids::EnterScope_T << EKnownEventUids::_UidShift);
	memcpy((uint64*)(Buffer->Cursor), &Stamp, sizeof(Stamp));
	Buffer->Cursor += sizeof(Stamp);

	AtomicStoreRelease((uint8**) &(Buffer->Committed), Buffer->Cursor);

	return Enter(ExtraSize);
}

} // namespace Private
} // namespace Trace

#endif // UE_TRACE_ENABLED
