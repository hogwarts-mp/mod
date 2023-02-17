// Copyright Epic Games, Inc. All Rights Reserved.

#include "Trace/Detail/Field.h" // :(

#if UE_TRACE_ENABLED

#include "Trace/Detail/Writer.inl"

namespace Trace {
namespace Private {

////////////////////////////////////////////////////////////////////////////////
template <typename CallbackType>
static void Field_WriteAuxData(uint32 Index, int32 Size, CallbackType&& Callback)
{
	static_assert(sizeof(Private::FWriteBuffer::Overflow) >= sizeof(FAuxHeader), "FWriteBuffer::Overflow is not large enough");

	// Header
	const int bMaybeHasAux = true;
	FWriteBuffer* Buffer = Writer_GetBuffer();
	Buffer->Cursor += sizeof(FAuxHeader) - bMaybeHasAux;

	auto* Header = (FAuxHeader*)(Buffer->Cursor - sizeof(FAuxHeader));
	Header->Size = Size << 8;
	Header->FieldIndex = uint8(0x80 | (Index & int(EIndexPack::FieldCountMask)));

	bool bCommit = ((uint8*)Header + bMaybeHasAux == Buffer->Committed);

	// Array data
	while (true)
	{
		if (Buffer->Cursor >= (uint8*)Buffer)
		{
			if (bCommit)
			{
				AtomicStoreRelease(&(uint8* volatile&)(Buffer->Committed), Buffer->Cursor);
			}

			Buffer = Writer_NextBuffer(0);
			bCommit = true;
		}

		int32 Remaining = int32((uint8*)Buffer - Buffer->Cursor);
		int32 SegmentSize = (Remaining < Size) ? Remaining : Size;
		Callback(Buffer->Cursor, SegmentSize);
		Buffer->Cursor += SegmentSize;

		Size -= SegmentSize;
		if (Size <= 0)
		{
			break;
		}
	}

	// The auxilary data null terminator.
	Buffer->Cursor[0] = 0;
	Buffer->Cursor++;

	if (bCommit)
	{
		AtomicStoreRelease(&(uint8* volatile&)(Buffer->Committed), Buffer->Cursor);
	}
}

////////////////////////////////////////////////////////////////////////////////
void Field_WriteAuxData(uint32 Index, const uint8* Data, int32 Size)
{
	auto MemcpyLambda = [&Data] (uint8* Cursor, int32 NumBytes)
	{
		memcpy(Cursor, Data, NumBytes);
		Data += NumBytes;
	};
	return Field_WriteAuxData(Index, Size, MemcpyLambda);
}

////////////////////////////////////////////////////////////////////////////////
void Field_WriteStringAnsi(uint32 Index, const TCHAR* String, int32 Length)
{
	int32 Size = Length;
	Size &= (FAuxHeader::SizeLimit - 1);

	auto WriteLambda = [&String] (uint8* Cursor, int32 NumBytes)
	{
		for (int32 i = 0; i < NumBytes; ++i)
		{
			*Cursor = uint8(*String & 0x7f);
			Cursor++;
			String++;
		}
	};

	return Field_WriteAuxData(Index, Size, WriteLambda);
}

////////////////////////////////////////////////////////////////////////////////
void Field_WriteStringAnsi(uint32 Index, const ANSICHAR* String, int32 Length)
{
	int32 Size = Length * sizeof(String[0]);
	Size &= (FAuxHeader::SizeLimit - 1); // a very crude "clamp"
	return Field_WriteAuxData(Index, (const uint8*)String, Size);
}

////////////////////////////////////////////////////////////////////////////////
void Field_WriteStringWide(uint32 Index, const TCHAR* String, int32 Length)
{
	int32 Size = Length * sizeof(String[0]);
	Size &= (FAuxHeader::SizeLimit - 1); // (see above)
	return Field_WriteAuxData(Index, (const uint8*)String, Size);
}

} // namespace Private
} // namespace Trace

#endif // UE_TRACE_ENABLED
