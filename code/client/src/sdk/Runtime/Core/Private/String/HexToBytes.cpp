// Copyright Epic Games, Inc. All Rights Reserved.

#include "String/HexToBytes.h"

#include "Containers/StringView.h"
#include "Containers/UnrealString.h"

namespace UE
{
namespace String
{
	template <typename CharType>
	int32 HexToBytesImpl(const TStringView<CharType>& Hex, uint8* const OutBytes)
	{
		const int32 HexCount = Hex.Len();
		const CharType* HexPos = Hex.GetData();
		const CharType* HexEnd = HexPos + HexCount;
		uint8* OutPos = OutBytes;
		if (const bool bPadNibble = (HexCount % 2) == 1)
		{
			*OutPos++ = TCharToNibble(*HexPos++);
		}
		while (HexPos != HexEnd)
		{
			const uint8 HiNibble = uint8(TCharToNibble(*HexPos++) << 4);
			*OutPos++ = HiNibble | TCharToNibble(*HexPos++);
		}
		return static_cast<int32>(OutPos - OutBytes);
	}

	int32 HexToBytes(const FStringView& Hex, uint8* OutBytes)
	{
		return HexToBytesImpl<TCHAR>(Hex, OutBytes);
	}

	int32 HexToBytes(const FAnsiStringView& Hex, uint8* OutBytes)
	{
		return HexToBytesImpl<ANSICHAR>(Hex, OutBytes);
	}
}
}
