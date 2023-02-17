// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/Config.h"

#if UE_TRACE_ENABLED

#include "Atomic.h"
#include "Protocol.h"
#include "Templates/UnrealTemplate.h"
#include "Writer.inl"

namespace Trace {
namespace Private {

////////////////////////////////////////////////////////////////////////////////
UE_TRACE_API void Field_WriteAuxData(uint32, const uint8*, int32);
UE_TRACE_API void Field_WriteStringAnsi(uint32, const ANSICHAR*, int32);
UE_TRACE_API void Field_WriteStringAnsi(uint32, const TCHAR*, int32);
UE_TRACE_API void Field_WriteStringWide(uint32, const TCHAR*, int32);

} // namespace Private

////////////////////////////////////////////////////////////////////////////////
template <typename Type> struct TFieldType;

template <> struct TFieldType<bool>			{ enum { Tid = int(EFieldType::Bool),	Size = sizeof(bool) }; };
template <> struct TFieldType<int8>			{ enum { Tid = int(EFieldType::Int8),	Size = sizeof(int8) }; };
template <> struct TFieldType<int16>		{ enum { Tid = int(EFieldType::Int16),	Size = sizeof(int16) }; };
template <> struct TFieldType<int32>		{ enum { Tid = int(EFieldType::Int32),	Size = sizeof(int32) }; };
template <> struct TFieldType<int64>		{ enum { Tid = int(EFieldType::Int64),	Size = sizeof(int64) }; };
template <> struct TFieldType<uint8>		{ enum { Tid = int(EFieldType::Int8),	Size = sizeof(uint8) }; };
template <> struct TFieldType<uint16>		{ enum { Tid = int(EFieldType::Int16),	Size = sizeof(uint16) }; };
template <> struct TFieldType<uint32>		{ enum { Tid = int(EFieldType::Int32),	Size = sizeof(uint32) }; };
template <> struct TFieldType<uint64>		{ enum { Tid = int(EFieldType::Int64),	Size = sizeof(uint64) }; };
template <> struct TFieldType<float>		{ enum { Tid = int(EFieldType::Float32),Size = sizeof(float) }; };
template <> struct TFieldType<double>		{ enum { Tid = int(EFieldType::Float64),Size = sizeof(double) }; };
template <class T> struct TFieldType<T*>	{ enum { Tid = int(EFieldType::Pointer),Size = sizeof(void*) }; };

template <typename T>
struct TFieldType<T[]>
{
	enum
	{
		Tid  = int(TFieldType<T>::Tid)|int(EFieldType::Array),
		Size = 0,
	};
};

#if 0
template <typename T, int N>
struct TFieldType<T[N]>
{
	enum
	{
		Tid  = int(TFieldType<T>::Tid)|int(EFieldType::Array),
		Size = sizeof(T[N]),
	};
};
#endif // 0

template <> struct TFieldType<Trace::AnsiString> { enum { Tid  = int(EFieldType::AnsiString), Size = 0, }; };
template <> struct TFieldType<Trace::WideString> { enum { Tid  = int(EFieldType::WideString), Size = 0, }; };



////////////////////////////////////////////////////////////////////////////////
struct FLiteralName
{
	template <uint32 Size>
	explicit FLiteralName(const ANSICHAR (&Name)[Size])
	: Ptr(Name)
	, Length(Size - 1)
	{
		static_assert(Size < 256, "Field name is too large");
	}

	const ANSICHAR* Ptr;
	uint8 Length;
};

////////////////////////////////////////////////////////////////////////////////
struct FFieldDesc
{
	FFieldDesc(const FLiteralName& Name, uint8 Type, uint16 Offset, uint16 Size)
	: Name(Name.Ptr)
	, ValueOffset(Offset)
	, ValueSize(Size)
	, NameSize(Name.Length)
	, TypeInfo(Type)
	{
	}

	const ANSICHAR* Name;
	uint16			ValueOffset;
	uint16			ValueSize;
	uint8			NameSize;
	uint8			TypeInfo;
};



////////////////////////////////////////////////////////////////////////////////
template <int InIndex, int InOffset, typename Type> struct TField;

enum class EIndexPack
{
	FieldCountMask	= 0xff,
	MaybeHasAux		= 0x100,
};

////////////////////////////////////////////////////////////////////////////////
#define TRACE_PRIVATE_FIELD(InIndex, InOffset, Type) \
		enum \
		{ \
			Index	= InIndex, \
			Offset	= InOffset, \
			Tid		= TFieldType<Type>::Tid, \
			Size	= TFieldType<Type>::Size, \
		}; \
		static_assert((Index & int(EIndexPack::FieldCountMask)) <= 127, "Trace events may only have up to a maximum of 127 fields"); \
	private: \
		FFieldDesc FieldDesc; \
	public: \
		TField(const FLiteralName& Name) \
		: FieldDesc(Name, Tid, Offset, Size) \
		{ \
		}

////////////////////////////////////////////////////////////////////////////////
template <int InIndex, int InOffset, typename Type>
struct TField<InIndex, InOffset, Type[]>
{
	TRACE_PRIVATE_FIELD(InIndex|int(EIndexPack::MaybeHasAux), InOffset, Type[]);

	static void Set(uint8*, Type const* Data, int32 Count)
	{
		if (Count > 0)
		{
			int32 Size = (Count * sizeof(Type)) & (FAuxHeader::SizeLimit - 1) & ~(sizeof(Type) - 1);
			Private::Field_WriteAuxData(Index, (const uint8*)Data, Size);
		}
	}
};

#if 0
////////////////////////////////////////////////////////////////////////////////
template <int InIndex, int InOffset, typename Type, int Count>
struct TField<InIndex, InOffset, Type[Count]>
{
	TRACE_PRIVATE_FIELD(InIndex, InOffset, Type[Count]);

	static void Set(uint8*, Type const* Data, int Count)
	{
	}
};
#endif

////////////////////////////////////////////////////////////////////////////////
template <int InIndex, int InOffset>
struct TField<InIndex, InOffset, Trace::AnsiString>
{
	TRACE_PRIVATE_FIELD(InIndex|int(EIndexPack::MaybeHasAux), InOffset, Trace::AnsiString);

	FORCENOINLINE static void Set(uint8*, const TCHAR* String, int32 Length=-1)
	{
		if (Length < 0)
		{
			Length = 0;
			if (String)
			{
				for (const TCHAR* c = String; *c; ++c, ++Length);
			}
		}

		Private::Field_WriteStringAnsi(Index, String, Length);
	}

	FORCENOINLINE static void Set(uint8*, const ANSICHAR* String, int32 Length=-1)
	{
		if (Length < 0)
		{
			Length = String ? int32(strlen(String)) : 0;
		}

		if (Length)
		{
			Private::Field_WriteStringAnsi(Index, String, Length);
		}
	}
};

////////////////////////////////////////////////////////////////////////////////
template <int InIndex, int InOffset>
struct TField<InIndex, InOffset, Trace::WideString>
{
	TRACE_PRIVATE_FIELD(InIndex|int(EIndexPack::MaybeHasAux), InOffset, Trace::WideString);

	FORCENOINLINE static void Set(uint8*, const TCHAR* String, int32 Length=-1)
	{
		if (Length < 0)
		{
			Length = 0;
			for (const TCHAR* c = String; *c; ++c, ++Length);
		}

		if (Length)
		{
			Private::Field_WriteStringWide(Index, String, Length);
		}
	}
};

////////////////////////////////////////////////////////////////////////////////
template <int InIndex, int InOffset, typename Type>
struct TField
{
	TRACE_PRIVATE_FIELD(InIndex, InOffset, Type);

	static void Set(uint8* Dest, const Type& __restrict Value)
	{
		::memcpy(Dest + Offset, &Value, Size);
	}
};

#undef TRACE_PRIVATE_FIELD



////////////////////////////////////////////////////////////////////////////////
// Used to terminate the field list and determine an event's size.
enum EventProps {};
template <int InFieldCount, int InSize>
struct TField<InFieldCount, InSize, EventProps>
{
	enum : uint16
	{
		FieldCount	= (InFieldCount & int(EIndexPack::FieldCountMask)),
		Size		= InSize,
		MaybeHasAux	= !!(InFieldCount & int(EIndexPack::MaybeHasAux)),
	};
};

////////////////////////////////////////////////////////////////////////////////
// Access to additional data that can be included along with a logged event.
enum Attachment {};
template <int InOffset>
struct TField<0, InOffset, Attachment>
{
	template <typename LambdaType>
	static void Set(uint8* Dest, LambdaType&& Lambda)
	{
		Lambda(Dest + InOffset);
	}

	static void Set(uint8* Dest, const void* Data, uint32 Size)
	{
		::memcpy(Dest + InOffset, Data, Size);
	}
};

} // namespace Trace

#endif // UE_TRACE_ENABLED
