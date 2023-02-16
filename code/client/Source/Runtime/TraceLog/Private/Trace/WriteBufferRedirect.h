// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

namespace Trace {
namespace Private {

////////////////////////////////////////////////////////////////////////////////
extern thread_local FWriteBuffer* GTlsWriteBuffer;

////////////////////////////////////////////////////////////////////////////////
template <int BufferSize>
class TWriteBufferRedirect
{
public:
					TWriteBufferRedirect();
					~TWriteBufferRedirect();
	void			Close();
	uint8*			GetData();
	uint32			GetSize() const;
	uint32			GetCapacity() const;
	void			Reset();

private:
	FWriteBuffer*	PrevBuffer;
	uint8			Data[BufferSize];
	FWriteBuffer	Buffer;
};

////////////////////////////////////////////////////////////////////////////////
template <int BufferSize>
inline TWriteBufferRedirect<BufferSize>::TWriteBufferRedirect()
{
	Reset();
	PrevBuffer = GTlsWriteBuffer;
	GTlsWriteBuffer = &Buffer;
}

////////////////////////////////////////////////////////////////////////////////
template <int BufferSize>
inline TWriteBufferRedirect<BufferSize>::~TWriteBufferRedirect()
{
	Close();
}

////////////////////////////////////////////////////////////////////////////////
template <int BufferSize>
inline void TWriteBufferRedirect<BufferSize>::Close()
{
	if (PrevBuffer == nullptr)
	{
		return;
	}

	GTlsWriteBuffer = PrevBuffer;
	PrevBuffer = nullptr;
}

////////////////////////////////////////////////////////////////////////////////
template <int BufferSize>
inline uint8* TWriteBufferRedirect<BufferSize>::GetData()
{
	return Buffer.Reaped;
}

////////////////////////////////////////////////////////////////////////////////
template <int BufferSize>
inline uint32 TWriteBufferRedirect<BufferSize>::GetSize() const
{
	return uint32(Buffer.Committed - Buffer.Reaped);
}

////////////////////////////////////////////////////////////////////////////////
template <int BufferSize>
inline uint32 TWriteBufferRedirect<BufferSize>::GetCapacity() const
{
	return BufferSize;
}

////////////////////////////////////////////////////////////////////////////////
template <int BufferSize>
inline void TWriteBufferRedirect<BufferSize>::Reset()
{
	Buffer.Cursor = Data + sizeof(uint32);
	Buffer.Committed = Buffer.Cursor;
	Buffer.Reaped = Buffer.Cursor;
}

} // namespace Private
} // namespace Trace
