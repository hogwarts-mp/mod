// Copyright Epic Games, Inc. All Rights Reserved.

#include "String/BytesToHex.h"

#include "Misc/StringBuilder.h"

namespace UE
{
namespace String
{
	template <typename CharType>
	void BytesToHexImpl(TArrayView<const uint8> Bytes, CharType* OutHex)
	{
		auto NibbleToHex = [](uint8 Value) -> CharType { return Value + (Value > 9 ? 'A' - 10 : '0'); };
		const uint8* Data = Bytes.GetData();
		
		for (const uint8* DataEnd = Data + Bytes.Num(); Data != DataEnd; ++Data)
		{
			*OutHex++ = NibbleToHex(*Data >> 4);
			*OutHex++ = NibbleToHex(*Data & 15);
		}
	}

	void BytesToHex(TArrayView<const uint8> Bytes, WIDECHAR* OutHex)
	{
		BytesToHexImpl(Bytes, OutHex);
	}

	void BytesToHex(TArrayView<const uint8> Bytes, ANSICHAR* OutHex)
	{
		BytesToHexImpl(Bytes, OutHex);
	}

	void BytesToHex(TArrayView<const uint8> Bytes, FStringBuilderBase& Builder)
	{
		const int32 Offset = Builder.AddUninitialized(Bytes.Num() * 2);
		BytesToHexImpl(Bytes, GetData(Builder) + Offset);
	}

	void BytesToHex(TArrayView<const uint8> Bytes, FAnsiStringBuilderBase& Builder)
	{
		const int32 Offset = Builder.AddUninitialized(Bytes.Num() * 2);
		BytesToHexImpl(Bytes, GetData(Builder) + Offset);
	}
}
}
