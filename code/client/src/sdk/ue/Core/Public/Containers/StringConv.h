// Copyright Epic Games, Inc. All Rights Reserved.

// This file contains the classes used when converting strings between
// standards (ANSI, UNICODE, etc.)

#pragma once

#include "CoreTypes.h"
#include "Misc/AssertionMacros.h"
#include "Containers/ContainerAllocationPolicies.h"
#include "Containers/Array.h"
#include "Misc/CString.h"

#define DEFAULT_STRING_CONVERSION_SIZE 128u
#define UNICODE_BOGUS_CHAR_CODEPOINT '?'
static_assert(sizeof(UNICODE_BOGUS_CHAR_CODEPOINT) <= sizeof(ANSICHAR) && (UNICODE_BOGUS_CHAR_CODEPOINT) >= 32 && (UNICODE_BOGUS_CHAR_CODEPOINT) <= 127, "The Unicode Bogus character point is expected to fit in a single ANSICHAR here");

template <typename From, typename To>
class TStringConvert
{
public:
	typedef From FromType;
	typedef To   ToType;

	FORCEINLINE static void Convert(To* Dest, int32 DestLen, const From* Source, int32 SourceLen)
	{
		To* Result = FPlatformString::Convert(Dest, DestLen, Source, SourceLen, (To)UNICODE_BOGUS_CHAR_CODEPOINT);
		check(Result);
	}

	static int32 ConvertedLength(const From* Source, int32 SourceLen)
	{
		return FPlatformString::ConvertedLength<To>(Source, SourceLen);
	}
};

namespace StringConv
{
	constexpr const uint16 HIGH_SURROGATE_START_CODEPOINT = 0xD800;
	constexpr const uint16 HIGH_SURROGATE_END_CODEPOINT   = 0xDBFF;
	constexpr const uint16 LOW_SURROGATE_START_CODEPOINT  = 0xDC00;
	constexpr const uint16 LOW_SURROGATE_END_CODEPOINT    = 0xDFFF;
	constexpr const uint32 ENCODED_SURROGATE_START_CODEPOINT = 0x10000;
	constexpr const uint32 ENCODED_SURROGATE_END_CODEPOINT   = 0x10FFFF;

	/** Is the provided Codepoint within the range of valid codepoints? */
	static FORCEINLINE bool IsValidCodepoint(const uint32 Codepoint)
	{
		if ((Codepoint > 0x10FFFF) ||						// No Unicode codepoints above 10FFFFh, (for now!)
			(Codepoint == 0xFFFE) || (Codepoint == 0xFFFF)) // illegal values.
		{
			return false;
		}
		return true;
	}

	/** Is the provided Codepoint within the range of the high-surrogates? */
	static FORCEINLINE bool IsHighSurrogate(const uint32 Codepoint)
	{
		return Codepoint >= HIGH_SURROGATE_START_CODEPOINT && Codepoint <= HIGH_SURROGATE_END_CODEPOINT;
	}

	/** Is the provided Codepoint within the range of the low-surrogates? */
	static FORCEINLINE bool IsLowSurrogate(const uint32 Codepoint)
	{
		return Codepoint >= LOW_SURROGATE_START_CODEPOINT && Codepoint <= LOW_SURROGATE_END_CODEPOINT;
	}

	static FORCEINLINE uint32 EncodeSurrogate(const uint16 HighSurrogate, const uint16 LowSurrogate)
	{
		return ((HighSurrogate - HIGH_SURROGATE_START_CODEPOINT) << 10) + (LowSurrogate - LOW_SURROGATE_START_CODEPOINT) + 0x10000;
	}

	static FORCEINLINE void DecodeSurrogate(const uint32 Codepoint, uint16& OutHighSurrogate, uint16& OutLowSurrogate)
	{
		const uint32 TmpCodepoint = Codepoint - 0x10000;
		OutHighSurrogate = (uint16)((TmpCodepoint >> 10) + HIGH_SURROGATE_START_CODEPOINT);
		OutLowSurrogate = (TmpCodepoint & 0x3FF) + LOW_SURROGATE_START_CODEPOINT;
	}

	/** Is the provided Codepoint outside of the range of the basic multilingual plane, but within the valid range of UTF8/16? */
	static FORCEINLINE bool IsEncodedSurrogate(const uint32 Codepoint)
	{
		return Codepoint >= ENCODED_SURROGATE_START_CODEPOINT && Codepoint <= ENCODED_SURROGATE_END_CODEPOINT;
	}

	/** Inline combine any UTF-16 surrogate pairs in the given null-terminated character buffer, returning the updated length */
	template<typename CharType>
	static FORCEINLINE int32 InlineCombineSurrogates_Buffer(CharType* StrBuffer, int32 StrLen)
	{
		static_assert(sizeof(CharType) == 4, "CharType must be 4-bytes!");

		for (int32 Index = 0; Index < StrLen; ++Index)
		{
			CharType Codepoint = StrBuffer[Index];

			// Check if this character is a high-surrogate
			if (StringConv::IsHighSurrogate(Codepoint))
			{
				// Ensure our string has enough characters to read from
				if ((Index + 1) >= StrLen)
				{
					// Unpaired surrogate - replace with bogus char
					StrBuffer[Index] = UNICODE_BOGUS_CHAR_CODEPOINT;
					break;
				}

				const uint32 HighCodepoint = Codepoint;
				Codepoint = StrBuffer[Index + 1];

				// If our High Surrogate is set, check if this character is the matching low-surrogate
				if (StringConv::IsLowSurrogate(Codepoint))
				{
					const uint32 LowCodepoint = Codepoint;

					// Combine our high and low surrogates together to a single Unicode codepoint
					Codepoint = StringConv::EncodeSurrogate(HighCodepoint, LowCodepoint);

					StrBuffer[Index] = Codepoint;
					{
						const int32 ArrayNum = StrLen + 1;
						const int32 IndexToRemove = Index + 1;
						const int32 NumToMove = ArrayNum - IndexToRemove - 1;
						if (NumToMove > 0)
						{
							FMemory::Memmove(StrBuffer + IndexToRemove, StrBuffer + IndexToRemove + 1, NumToMove * sizeof(CharType));
						}
					}
					--StrLen;
					continue;
				}

				// Unpaired surrogate - replace with bogus char
				StrBuffer[Index] = UNICODE_BOGUS_CHAR_CODEPOINT;
			}
			else if (StringConv::IsLowSurrogate(Codepoint))
			{
				// Unpaired surrogate - replace with bogus char
				StrBuffer[Index] = UNICODE_BOGUS_CHAR_CODEPOINT;
			}
		}

		return StrLen;
	}

	/** Inline combine any UTF-16 surrogate pairs in the given null-terminated TCHAR array */
	template<typename AllocatorType>
	static FORCEINLINE void InlineCombineSurrogates_Array(TArray<TCHAR, AllocatorType>& StrBuffer)
	{
#if PLATFORM_TCHAR_IS_4_BYTES
		const int32 NewStrLen = InlineCombineSurrogates_Buffer(StrBuffer.GetData(), StrBuffer.Num() - 1);
		StrBuffer.SetNum(NewStrLen + 1, /*bAllowShrinking*/false);
#endif	// PLATFORM_TCHAR_IS_4_BYTES
	}
}

namespace UE4StringConv_Private
{
	/**
	 * This is a basic object which counts how many times it has been incremented
	 */
	struct FCountingOutputIterator
	{
		FCountingOutputIterator()
			: Counter(0)
		{
		}

		const FCountingOutputIterator& operator* () const { return *this; }
		const FCountingOutputIterator& operator++() { ++Counter; return *this; }
		const FCountingOutputIterator& operator++(int) { ++Counter; return *this; }
		const FCountingOutputIterator& operator+=(const int32 Amount) { Counter += Amount; return *this; }

		template <typename T>
		T operator=(T Val) const
		{
			return Val;
		}

		friend int32 operator-(FCountingOutputIterator Lhs, FCountingOutputIterator Rhs)
		{
			return Lhs.Counter - Rhs.Counter;
		}

		int32 GetCount() const { return Counter; }

	private:
		int32 Counter;
	};
}

// This should be replaced with Platform stuff when FPlatformString starts to know about UTF-8.
class FTCHARToUTF8_Convert
{
public:
	typedef TCHAR    FromType;
	typedef ANSICHAR ToType;

	/**
	 * Convert Codepoint into UTF-8 characters.
	 *
	 * @param Codepoint Codepoint to expand into UTF-8 bytes
	 * @param OutputIterator Output iterator to write UTF-8 bytes into
	 * @param OutputIteratorByteSizeRemaining Maximum number of ANSI characters that can be written to OutputIterator
	 * @return Number of characters written for Codepoint
	 */
	template <typename BufferType>
	static int32 Utf8FromCodepoint(uint32 Codepoint, BufferType OutputIterator, uint32 OutputIteratorByteSizeRemaining)
	{
		// Ensure we have at least one character in size to write
		if (OutputIteratorByteSizeRemaining < sizeof(ANSICHAR))
		{
			return 0;
		}

		const BufferType OutputIteratorStartPosition = OutputIterator;

		if (!StringConv::IsValidCodepoint(Codepoint))
		{
			Codepoint = UNICODE_BOGUS_CHAR_CODEPOINT;
		}
		else if (StringConv::IsHighSurrogate(Codepoint) || StringConv::IsLowSurrogate(Codepoint)) // UTF-8 Characters are not allowed to encode codepoints in the surrogate pair range
		{
			Codepoint = UNICODE_BOGUS_CHAR_CODEPOINT;
		}

		// Do the encoding...
		if (Codepoint < 0x80)
		{
			*(OutputIterator++) = (ANSICHAR)Codepoint;
		}
		else if (Codepoint < 0x800)
		{
			if (OutputIteratorByteSizeRemaining >= 2)
			{
				*(OutputIterator++) = (ANSICHAR)((Codepoint >> 6)         | 128 | 64);
				*(OutputIterator++) = (ANSICHAR)((Codepoint       & 0x3F) | 128);
			}
		}
		else if (Codepoint < 0x10000)
		{
			if (OutputIteratorByteSizeRemaining >= 3)
			{
				*(OutputIterator++) = (ANSICHAR) ((Codepoint >> 12)        | 128 | 64 | 32);
				*(OutputIterator++) = (ANSICHAR)(((Codepoint >> 6) & 0x3F) | 128);
				*(OutputIterator++) = (ANSICHAR) ((Codepoint       & 0x3F) | 128);
			}
		}
		else
		{
			if (OutputIteratorByteSizeRemaining >= 4)
			{
				*(OutputIterator++) = (ANSICHAR) ((Codepoint >> 18)         | 128 | 64 | 32 | 16);
				*(OutputIterator++) = (ANSICHAR)(((Codepoint >> 12) & 0x3F) | 128);
				*(OutputIterator++) = (ANSICHAR)(((Codepoint >> 6 ) & 0x3F) | 128);
				*(OutputIterator++) = (ANSICHAR) ((Codepoint        & 0x3F) | 128);
			}
		}

		return UE_PTRDIFF_TO_INT32(OutputIterator - OutputIteratorStartPosition);
	}

	/**
	 * Converts a Source string into UTF8 and stores it in Dest.
	 *
	 * @param Dest      The destination output iterator. Usually ANSICHAR*, but you can supply your own output iterator.
	 *                  One can determine the number of characters written by checking the offset of Dest when the function returns.
	 * @param DestLen   The length of the destination buffer.
	 * @param Source    The source string to convert.
	 * @param SourceLen The length of the source string.
	 * @return          The number of bytes written to Dest, up to DestLen, or -1 if the entire Source string could did not fit in DestLen bytes.
	 */
	template <typename DestBufferType>
	static FORCEINLINE int32 Convert(DestBufferType Dest, int32 DestLen, const TCHAR* Source, int32 SourceLen)
	{
		return Convert_Impl(Dest, DestLen, Source, SourceLen);
	}

	/**
	 * Determines the length of the converted string.
	 *
	 * @return The length of the string in UTF-8 code units.
	 */
	static FORCEINLINE int32 ConvertedLength(const TCHAR* Source, int32 SourceLen)
	{
		UE4StringConv_Private::FCountingOutputIterator Dest;
		const int32 DestLen = SourceLen * 4;
		Convert_Impl(Dest, DestLen, Source, SourceLen);

		return Dest.GetCount();
	}

private:
	template <typename DestBufferType>
	static int32 Convert_Impl(DestBufferType& Dest, int32 DestLen, const TCHAR* Source, const int32 SourceLen)
	{
		DestBufferType DestStartingPosition = Dest;
#if PLATFORM_TCHAR_IS_4_BYTES
		for (int32 i = 0; i < SourceLen; ++i)
		{
			uint32 Codepoint = static_cast<uint32>(Source[i]);

			if (!WriteCodepointToBuffer(Codepoint, Dest, DestLen))
			{
				// Could not write data, bail out
				return -1;
			}
		}
#else	// PLATFORM_TCHAR_IS_4_BYTES
		uint32 HighSurrogate = MAX_uint32;

		for (int32 i = 0; i < SourceLen; ++i)
		{
			const bool bHighSurrogateIsSet = HighSurrogate != MAX_uint32;
			uint32 Codepoint = static_cast<uint32>(Source[i]);

			// Check if this character is a high-surrogate
			if (StringConv::IsHighSurrogate(Codepoint))
			{
				// Ensure we don't already have a high-surrogate set or end without a matching low-surrogate
				if (bHighSurrogateIsSet || i == SourceLen - 1)
				{
					// Already have a high-surrogate in this pair or string ends with lone high-surrogate
					// Write our stored value (will be converted into bogus character)
					if (!WriteCodepointToBuffer(HighSurrogate, Dest, DestLen))
					{
						// Could not write data, bail out
						return -1;
					}
				}

				// Store our code point for our next character
				HighSurrogate = Codepoint;
				continue;
			}

			// If our High Surrogate is set, check if this character is the matching low-surrogate
			if (bHighSurrogateIsSet)
			{
				if (StringConv::IsLowSurrogate(Codepoint))
				{
					const uint32 LowSurrogate = Codepoint;
					// Combine our high and low surrogates together to a single Unicode codepoint
					Codepoint = StringConv::EncodeSurrogate((uint16)HighSurrogate, (uint16)LowSurrogate);
				}
				else
				{
					// Did not find matching low-surrogate, write out a bogus character for our stored HighSurrogate
					if (!WriteCodepointToBuffer(HighSurrogate, Dest, DestLen))
					{
						// Could not write data, bail out
						return -1;
					}
				}

				// Reset our high-surrogate now that we've used (or discarded) its value
				HighSurrogate = MAX_uint32;
			}

			if (!WriteCodepointToBuffer(Codepoint, Dest, DestLen))
			{
				// Could not write data, bail out
				return -1;
			}
		}
#endif	// PLATFORM_TCHAR_IS_4_BYTES
		return UE_PTRDIFF_TO_INT32(Dest - DestStartingPosition);
	}

	template <typename DestBufferType>
	static bool WriteCodepointToBuffer(const uint32 Codepoint, DestBufferType& Dest, int32& DestLen)
	{
		int32 WrittenChars = Utf8FromCodepoint(Codepoint, Dest, DestLen);
		if (WrittenChars < 1)
		{
			return false;
		}

		Dest += WrittenChars;
		DestLen -= WrittenChars;
		return true;
	}
};

class FUTF8ToTCHAR_Convert
{
public:
	typedef ANSICHAR FromType;
	typedef TCHAR    ToType;

	/**
	 * Converts the UTF-8 string to UTF-16 or UTF-32.
	 *
	 * @param Dest      The destination buffer of the converted string.
	 * @param DestLen   The length of the destination buffer.
	 * @param Source    The source string to convert.
	 * @param SourceLen The length of the source string.
	 */
	static FORCEINLINE void Convert(TCHAR* Dest, const int32 DestLen, const ANSICHAR* Source, const int32 SourceLen)
	{
		Convert_Impl(Dest, DestLen, Source, SourceLen);
	}

	/**
	 * Determines the length of the converted string.
	 *
	 * @param Source string to read and determine amount of TCHARs to represent
	 * @param SourceLen Length of source string; we will not read past this amount, even if the characters tell us to
	 * @return The length of the string in UTF-16 or UTF-32 characters.
	 */
	static int32 ConvertedLength(const ANSICHAR* Source, const int32 SourceLen)
	{
		UE4StringConv_Private::FCountingOutputIterator Dest;
		Convert_Impl(Dest, MAX_int32, Source, SourceLen);

		return Dest.GetCount();
	}

private:
	static uint32 CodepointFromUtf8(const ANSICHAR*& SourceString, const uint32 SourceLengthRemaining)
	{
		checkSlow(SourceLengthRemaining > 0);

		const ANSICHAR* OctetPtr = SourceString;

		uint32 Codepoint = 0;
		uint32 Octet = (uint32) ((uint8) *SourceString);
		uint32 Octet2, Octet3, Octet4;

		if (Octet < 128)  // one octet char: 0 to 127
		{
			++SourceString;  // skip to next possible start of codepoint.
			return Octet;
		}
		else if (Octet < 192)  // bad (starts with 10xxxxxx).
		{
			// Apparently each of these is supposed to be flagged as a bogus
			//  char, instead of just resyncing to the next valid codepoint.
			++SourceString;  // skip to next possible start of codepoint.
			return UNICODE_BOGUS_CHAR_CODEPOINT;
		}
		else if (Octet < 224)  // two octets
		{
			// Ensure our string has enough characters to read from
			if (SourceLengthRemaining < 2)
			{
				// Skip to end and write out a single char (we always have room for at least 1 char)
				SourceString += SourceLengthRemaining;
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			Octet -= (128+64);
			Octet2 = (uint32) ((uint8) *(++OctetPtr));
			if ((Octet2 & (128 + 64)) != 128)  // Format isn't 10xxxxxx?
			{
				++SourceString;  // Sequence was not valid UTF-8. Skip the first byte and continue.
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			Codepoint = ((Octet << 6) | (Octet2 - 128));
			if ((Codepoint >= 0x80) && (Codepoint <= 0x7FF))
			{
				SourceString += 2;  // skip to next possible start of codepoint.
				return Codepoint;
			}
		}
		else if (Octet < 240)  // three octets
		{
			// Ensure our string has enough characters to read from
			if (SourceLengthRemaining < 3)
			{
				// Skip to end and write out a single char (we always have room for at least 1 char)
				SourceString += SourceLengthRemaining;
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			Octet -= (128+64+32);
			Octet2 = (uint32) ((uint8) *(++OctetPtr));
			if ((Octet2 & (128+64)) != 128)  // Format isn't 10xxxxxx?
			{
				++SourceString;  // Sequence was not valid UTF-8. Skip the first byte and continue.
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			Octet3 = (uint32) ((uint8) *(++OctetPtr));
			if ((Octet3 & (128+64)) != 128)  // Format isn't 10xxxxxx?
			{
				++SourceString;  // Sequence was not valid UTF-8. Skip the first byte and continue.
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			Codepoint = ( ((Octet << 12)) | ((Octet2-128) << 6) | ((Octet3-128)) );

			// UTF-8 characters cannot be in the UTF-16 surrogates range
			if (StringConv::IsHighSurrogate(Codepoint) || StringConv::IsLowSurrogate(Codepoint))
			{
				++SourceString;  // Sequence was not valid UTF-8. Skip the first byte and continue.
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			// 0xFFFE and 0xFFFF are illegal, too, so we check them at the edge.
			if ((Codepoint >= 0x800) && (Codepoint <= 0xFFFD))
			{
				SourceString += 3;  // skip to next possible start of codepoint.
				return Codepoint;
			}
		}
		else if (Octet < 248)  // four octets
		{
			// Ensure our string has enough characters to read from
			if (SourceLengthRemaining < 4)
			{
				// Skip to end and write out a single char (we always have room for at least 1 char)
				SourceString += SourceLengthRemaining;
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			Octet -= (128+64+32+16);
			Octet2 = (uint32) ((uint8) *(++OctetPtr));
			if ((Octet2 & (128+64)) != 128)  // Format isn't 10xxxxxx?
			{
				++SourceString;  // Sequence was not valid UTF-8. Skip the first byte and continue.
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			Octet3 = (uint32) ((uint8) *(++OctetPtr));
			if ((Octet3 & (128+64)) != 128)  // Format isn't 10xxxxxx?
			{
				++SourceString;  // Sequence was not valid UTF-8. Skip the first byte and continue.
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			Octet4 = (uint32) ((uint8) *(++OctetPtr));
			if ((Octet4 & (128+64)) != 128)  // Format isn't 10xxxxxx?
			{
				++SourceString;  // Sequence was not valid UTF-8. Skip the first byte and continue.
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			Codepoint = ( ((Octet << 18)) | ((Octet2 - 128) << 12) |
						((Octet3 - 128) << 6) | ((Octet4 - 128)) );
			if ((Codepoint >= 0x10000) && (Codepoint <= 0x10FFFF))
			{
				SourceString += 4;  // skip to next possible start of codepoint.
				return Codepoint;
			}
		}
		// Five and six octet sequences became illegal in rfc3629.
		//  We throw the codepoint away, but parse them to make sure we move
		//  ahead the right number of bytes and don't overflow the buffer.
		else if (Octet < 252)  // five octets
		{
			// Ensure our string has enough characters to read from
			if (SourceLengthRemaining < 5)
			{
				// Skip to end and write out a single char (we always have room for at least 1 char)
				SourceString += SourceLengthRemaining;
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			Octet = (uint32) ((uint8) *(++OctetPtr));
			if ((Octet & (128+64)) != 128)  // Format isn't 10xxxxxx?
			{
				++SourceString;  // Sequence was not valid UTF-8. Skip the first byte and continue.
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			Octet = (uint32) ((uint8) *(++OctetPtr));
			if ((Octet & (128+64)) != 128)  // Format isn't 10xxxxxx?
			{
				++SourceString;  // Sequence was not valid UTF-8. Skip the first byte and continue.
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			Octet = (uint32) ((uint8) *(++OctetPtr));
			if ((Octet & (128+64)) != 128)  // Format isn't 10xxxxxx?
			{
				++SourceString;  // Sequence was not valid UTF-8. Skip the first byte and continue.
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			Octet = (uint32) ((uint8) *(++OctetPtr));
			if ((Octet & (128+64)) != 128)  // Format isn't 10xxxxxx?
			{
				++SourceString;  // Sequence was not valid UTF-8. Skip the first byte and continue.
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			SourceString += 5;  // skip to next possible start of codepoint.
			return UNICODE_BOGUS_CHAR_CODEPOINT;
		}

		else  // six octets
		{
			// Ensure our string has enough characters to read from
			if (SourceLengthRemaining < 6)
			{
				// Skip to end and write out a single char (we always have room for at least 1 char)
				SourceString += SourceLengthRemaining;
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			Octet = (uint32) ((uint8) *(++OctetPtr));
			if ((Octet & (128+64)) != 128)  // Format isn't 10xxxxxx?
			{
				++SourceString;  // Sequence was not valid UTF-8. Skip the first byte and continue.
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			Octet = (uint32) ((uint8) *(++OctetPtr));
			if ((Octet & (128+64)) != 128)  // Format isn't 10xxxxxx?
			{
				++SourceString;  // Sequence was not valid UTF-8. Skip the first byte and continue.
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			Octet = (uint32) ((uint8) *(++OctetPtr));
			if ((Octet & (128+64)) != 128)  // Format isn't 10xxxxxx?
			{
				++SourceString;  // Sequence was not valid UTF-8. Skip the first byte and continue.
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			Octet = (uint32) ((uint8) *(++OctetPtr));
			if ((Octet & (128+64)) != 128)  // Format isn't 10xxxxxx?
			{
				++SourceString;  // Sequence was not valid UTF-8. Skip the first byte and continue.
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			Octet = (uint32) ((uint8) *(++OctetPtr));
			if ((Octet & (128+64)) != 128)  // Format isn't 10xxxxxx?
			{
				++SourceString;  // Sequence was not valid UTF-8. Skip the first byte and continue.
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			SourceString += 6;  // skip to next possible start of codepoint.
			return UNICODE_BOGUS_CHAR_CODEPOINT;
		}

		++SourceString;  // Sequence was not valid UTF-8. Skip the first byte and continue.
		return UNICODE_BOGUS_CHAR_CODEPOINT;  // catch everything else.
	}

	/**
	 * Read Source string, converting the data from UTF-8 into UTF-16, and placing these in the Destination
	 */
	template <typename DestBufferType>
	static void Convert_Impl(DestBufferType& ConvertedBuffer, int32 DestLen, const ANSICHAR* Source, const int32 SourceLen)
	{
		const ANSICHAR* SourceEnd = Source + SourceLen;

		const uint64 ExtendedCharMask = 0x8080808080808080;
		while (Source < SourceEnd && DestLen > 0)
		{
			// In case we're given an unaligned pointer, we'll
			// fallback to the slow path until properly aligned.
			if (IsAligned(Source, 8))
			{
				// Fast path for most common case
				while (Source < SourceEnd - 8 && DestLen >= 8)
				{
					// Detect any extended characters 8 chars at a time
					if ((*(const uint64*)Source) & ExtendedCharMask)
					{
						// Move to slow path since we got extended characters to process
						break;
					}

					// This should get unrolled on most compiler
					// ROI of diminished return to vectorize this as we 
					// would have to deal with alignment, endianness and
					// rewrite the iterators to support bulk writes
					for (int32 Index = 0; Index < 8; ++Index)
					{
						*(ConvertedBuffer++) = (ToType)(uint8)*(Source++);
					}
					DestLen -= 8;
				}
			}

			// Slow path for extended characters
			while (Source < SourceEnd && DestLen > 0)
			{
				// Read our codepoint, advancing the source pointer
				uint32 Codepoint = CodepointFromUtf8(Source, UE_PTRDIFF_TO_UINT32(SourceEnd - Source));

	#if !PLATFORM_TCHAR_IS_4_BYTES
				// We want to write out two chars
				if (StringConv::IsEncodedSurrogate(Codepoint))
				{
					// We need two characters to write the surrogate pair
					if (DestLen >= 2)
					{
						uint16 HighSurrogate = 0;
						uint16 LowSurrogate = 0;
						StringConv::DecodeSurrogate(Codepoint, HighSurrogate, LowSurrogate);

						*(ConvertedBuffer++) = (ToType)HighSurrogate;
						*(ConvertedBuffer++) = (ToType)LowSurrogate;
						DestLen -= 2;
						continue;
					}

					// If we don't have space, write a bogus character instead (we should have space for it)
					Codepoint = UNICODE_BOGUS_CHAR_CODEPOINT;
				}
				else if (Codepoint > StringConv::ENCODED_SURROGATE_END_CODEPOINT)
				{
					// Ignore values higher than the supplementary plane range
					Codepoint = UNICODE_BOGUS_CHAR_CODEPOINT;
				}
	#endif	// !PLATFORM_TCHAR_IS_4_BYTES

				*(ConvertedBuffer++) = (ToType)Codepoint;
				--DestLen;

				// Return to the fast path once aligned and back to simple ASCII chars
				if (Codepoint < 128 && IsAligned(Source, 8))
				{
					break;
				}
			}
		}
	}
};

template<typename InFromType, typename InToType>
class TUTF32ToUTF16_Convert
{
	static_assert(sizeof(InFromType) == 4, "FromType must be 4 bytes!");
	static_assert(sizeof(InToType) == 2, "ToType must be 2 bytes!");

public:
	typedef InFromType FromType;
	typedef InToType   ToType;

	/**
	 * Convert Codepoint into UTF-16 characters.
	 *
	 * @param Codepoint Codepoint to expand into UTF-16 code units
	 * @param OutputIterator Output iterator to write UTF-16 code units into
	 * @param OutputIteratorNumRemaining Maximum number of UTF-16 code units that can be written to OutputIterator
	 * @return Number of characters written for Codepoint
	 */
	template <typename BufferType>
	static int32 Utf16FromCodepoint(uint32 Codepoint, BufferType OutputIterator, uint32 OutputIteratorNumRemaining)
	{
		// Ensure we have at least one character in size to write
		if (OutputIteratorNumRemaining < 1)
		{
			return 0;
		}

		const BufferType OutputIteratorStartPosition = OutputIterator;

		if (!StringConv::IsValidCodepoint(Codepoint))
		{
			Codepoint = UNICODE_BOGUS_CHAR_CODEPOINT;
		}
		else if (StringConv::IsHighSurrogate(Codepoint) || StringConv::IsLowSurrogate(Codepoint)) // Surrogate pairs range should not be present in UTF-32
		{
			Codepoint = UNICODE_BOGUS_CHAR_CODEPOINT;
		}

		// We want to write out two chars
		if (StringConv::IsEncodedSurrogate(Codepoint))
		{
			// We need two characters to write the surrogate pair
			if (OutputIteratorNumRemaining >= 2)
			{
				uint16 HighSurrogate = 0;
				uint16 LowSurrogate = 0;
				StringConv::DecodeSurrogate(Codepoint, HighSurrogate, LowSurrogate);

				*(OutputIterator++) = (ToType)HighSurrogate;
				*(OutputIterator++) = (ToType)LowSurrogate;
			}
		}
		else if (Codepoint > StringConv::ENCODED_SURROGATE_END_CODEPOINT)
		{
			// Ignore values higher than the supplementary plane range
			*(OutputIterator++) = UNICODE_BOGUS_CHAR_CODEPOINT;
		}
		else
		{
			// Normal codepoint
			*(OutputIterator++) = (ToType)Codepoint;
		}

		return UE_PTRDIFF_TO_INT32(OutputIterator - OutputIteratorStartPosition);
	}

	/**
	 * Converts the string to the desired format.
	 *
	 * @param Dest      The destination buffer of the converted string.
	 * @param DestLen   The length of the destination buffer.
	 * @param Source    The source string to convert.
	 * @param SourceLen The length of the source string.
	 */
	static FORCEINLINE void Convert(ToType* Dest, int32 DestLen, const FromType* Source, int32 SourceLen)
	{
		Convert_Impl(Dest, DestLen, Source, SourceLen);
	}

	/**
	 * Determines the length of the converted string.
	 *
	 * @return The length of the string in UTF-16 code units.
	 */
	static FORCEINLINE int32 ConvertedLength(const FromType* Source, int32 SourceLen)
	{
		UE4StringConv_Private::FCountingOutputIterator Dest;
		const int32 DestLen = SourceLen * 2;
		Convert_Impl(Dest, DestLen, Source, SourceLen);

		return Dest.GetCount();
	}

private:
	template <typename DestBufferType>
	static void Convert_Impl(DestBufferType& Dest, int32 DestLen, const FromType* Source, const int32 SourceLen)
	{
		for (int32 i = 0; i < SourceLen; ++i)
		{
			uint32 Codepoint = static_cast<uint32>(Source[i]);

			if (!WriteCodepointToBuffer(Codepoint, Dest, DestLen))
			{
				// Could not write data, bail out
				return;
			}
		}
	}

	template <typename DestBufferType>
	static bool WriteCodepointToBuffer(const uint32 Codepoint, DestBufferType& Dest, int32& DestLen)
	{
		int32 WrittenChars = Utf16FromCodepoint(Codepoint, Dest, DestLen);
		if (WrittenChars < 1)
		{
			return false;
		}

		Dest += WrittenChars;
		DestLen -= WrittenChars;
		return true;
	}
};

template<typename InFromType, typename InToType>
class TUTF16ToUTF32_Convert
{
	static_assert(sizeof(InFromType) == 2, "FromType must be 2 bytes!");
	static_assert(sizeof(InToType) == 4, "ToType must be 4 bytes!");

public:
	typedef InFromType FromType;
	typedef InToType   ToType;

	/**
	 * Converts the UTF-16 string to UTF-32.
	 *
	 * @param Dest      The destination buffer of the converted string.
	 * @param DestLen   The length of the destination buffer.
	 * @param Source    The source string to convert.
	 * @param SourceLen The length of the source string.
	 */
	static FORCEINLINE void Convert(ToType* Dest, const int32 DestLen, const FromType* Source, const int32 SourceLen)
	{
		Convert_Impl(Dest, DestLen, Source, SourceLen);
	}

	/**
	 * Determines the length of the converted string.
	 *
	 * @param Source string to read and determine amount of ToType chars to represent
	 * @param SourceLen Length of source string; we will not read past this amount, even if the characters tell us to
	 * @return The length of the string in UTF-32 characters.
	 */
	static int32 ConvertedLength(const FromType* Source, const int32 SourceLen)
	{
		UE4StringConv_Private::FCountingOutputIterator Dest;
		Convert_Impl(Dest, MAX_int32, Source, SourceLen);

		return Dest.GetCount();
	}

private:
	static uint32 CodepointFromUtf16(const FromType*& SourceString, const uint32 SourceLengthRemaining)
	{
		checkSlow(SourceLengthRemaining > 0);

		const FromType* CodeUnitPtr = SourceString;

		uint32 Codepoint = *CodeUnitPtr;

		// Check if this character is a high-surrogate
		if (StringConv::IsHighSurrogate(Codepoint))
		{
			// Ensure our string has enough characters to read from
			if (SourceLengthRemaining < 2)
			{
				// Skip to end and write out a single char (we always have room for at least 1 char)
				SourceString += SourceLengthRemaining;
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}

			const uint16 HighSurrogate = (uint16)Codepoint;
			Codepoint = *(++CodeUnitPtr);

			// If our High Surrogate is set, check if this character is the matching low-surrogate
			if (StringConv::IsLowSurrogate(Codepoint))
			{
				const uint16 LowSurrogate = (uint16)Codepoint;

				// Combine our high and low surrogates together to a single Unicode codepoint
				Codepoint = StringConv::EncodeSurrogate(HighSurrogate, LowSurrogate);

				SourceString += 2;  // skip to next possible start of codepoint.
				return Codepoint;
			}
			else
			{
				++SourceString; // Did not find matching low-surrogate, write out a bogus character and continue
				return UNICODE_BOGUS_CHAR_CODEPOINT;
			}
		}
		else if (StringConv::IsLowSurrogate(Codepoint))
		{
			// Unpaired low surrogate
			++SourceString;
			return UNICODE_BOGUS_CHAR_CODEPOINT;
		}
		else
		{
			// Single codepoint
			++SourceString;
			return Codepoint;
		}
	}

	/**
	 * Read Source string, converting the data from UTF-16 into UTF-32, and placing these in the Destination
	 */
	template <typename DestBufferType>
	static void Convert_Impl(DestBufferType& ConvertedBuffer, int32 DestLen, const FromType* Source, const int32 SourceLen)
	{
		const FromType* SourceEnd = Source + SourceLen;
		while (Source < SourceEnd && DestLen > 0)
		{
			// Read our codepoint, advancing the source pointer
			uint32 Codepoint = CodepointFromUtf16(Source, SourceEnd - Source);

			*(ConvertedBuffer++) = Codepoint;
			--DestLen;
		}
	}
};

struct ENullTerminatedString
{
	enum Type
	{
		No  = 0,
		Yes = 1
	};
};

/**
 * Class takes one type of string and converts it to another. The class includes a
 * chunk of presized memory of the destination type. If the presized array is
 * too small, it mallocs the memory needed and frees on the class going out of
 * scope.
 */
template<typename Converter, int32 DefaultConversionSize = DEFAULT_STRING_CONVERSION_SIZE>
class TStringConversion : private Converter, private TInlineAllocator<DefaultConversionSize>::template ForElementType<typename Converter::ToType>
{
	typedef typename TInlineAllocator<DefaultConversionSize>::template ForElementType<typename Converter::ToType> AllocatorType;

	typedef typename Converter::FromType FromType;
	typedef typename Converter::ToType   ToType;

	/**
	 * Converts the data by using the Convert() method on the base class
	 */
	void Init(const FromType* Source, int32 SourceLen, ENullTerminatedString::Type NullTerminated)
	{
		StringLength = Converter::ConvertedLength(Source, SourceLen);

		int32 BufferSize = StringLength + NullTerminated;

		AllocatorType::ResizeAllocation(0, BufferSize, sizeof(ToType));

		Ptr = (ToType*)AllocatorType::GetAllocation();
		Converter::Convert(Ptr, BufferSize, Source, SourceLen + NullTerminated);
	}

public:
	explicit TStringConversion(const FromType* Source)
	{
		if (Source)
		{
			Init(Source, TCString<FromType>::Strlen(Source), ENullTerminatedString::Yes);
		}
		else
		{
			Ptr = nullptr;
			StringLength = 0;
		}
	}

	TStringConversion(const FromType* Source, int32 SourceLen)
	{
		if (Source)
		{
			ENullTerminatedString::Type NullTerminated = ENullTerminatedString::No;
			if (SourceLen > 0 && Source[SourceLen-1] == 0)
			{
				// Given buffer is null-terminated
				NullTerminated = ENullTerminatedString::Yes;
				SourceLen -= 1;
			}

			Init(Source, SourceLen, NullTerminated);
		}
		else
		{
			Ptr = nullptr;
			StringLength = 0;
		}
	}

	/**
	 * Move constructor
	 */
	TStringConversion(TStringConversion&& Other)
		: Converter(MoveTemp(ImplicitConv<Converter&>(Other)))
		, Ptr(Other.Ptr)
		, StringLength(Other.StringLength)
	{
		AllocatorType::MoveToEmpty(Other);

		Other.Ptr          = nullptr;
		Other.StringLength = 0;
	}

	/**
	 * Accessor for the converted string.
	 * @note The string may not be null-terminated if constructed from an explicitly sized buffer that didn't include the null-terminator.
	 *
	 * @return A const pointer to the converted string.
	 */
	FORCEINLINE const ToType* Get() const
	{
		return Ptr;
	}

	/**
	 * Length of the converted string.
	 *
	 * @return The number of characters in the converted string, excluding any null terminator.
	 */
	FORCEINLINE int32 Length() const
	{
		return StringLength;
	}

private:
	// Non-copyable
	TStringConversion(const TStringConversion&) = delete;
	TStringConversion& operator=(const TStringConversion&) = delete;

	ToType* Ptr;
	int32   StringLength;
};


/**
 * Class takes one type of string and and stores it as-is.
 * For API compatibility with TStringConversion when the To and From types are already in the same format.
 */
template<typename InFromType, typename InToType = InFromType>
class TStringPointer
{
	static_assert(sizeof(InFromType) == sizeof(InToType), "FromType must be the same size as ToType!");

public:
	typedef InFromType FromType;
	typedef InToType   ToType;

public:
	explicit TStringPointer(const FromType* Source)
	{
		if (Source)
		{
			Ptr = (const ToType*)Source;
			StringLength = -1; // Length calculated on-demand
		}
		else
		{
			Ptr = nullptr;
			StringLength = 0;
		}
	}

	TStringPointer(const FromType* Source, int32 SourceLen)
	{
		if (Source)
		{
			if (SourceLen > 0 && Source[SourceLen-1] == 0)
			{
				// Given buffer is null-terminated
				SourceLen -= 1;
			}

			Ptr = (const ToType*)Source;
			StringLength = SourceLen;
		}
		else
		{
			Ptr = nullptr;
			StringLength = 0;
		}
	}

	/**
	 * Move constructor
	 */
	TStringPointer(TStringPointer&& Other) = default;

	/**
	 * Accessor for the string.
	 * @note The string may not be null-terminated if constructed from an explicitly sized buffer that didn't include the null-terminator.
	 *
	 * @return A const pointer to the string.
	 */
	FORCEINLINE const ToType* Get() const
	{
		return Ptr;
	}

	/**
	 * Length of the string.
	 *
	 * @return The number of characters in the string, excluding any null terminator.
	 */
	FORCEINLINE int32 Length() const
	{
		return StringLength == -1 ? TCString<ToType>::Strlen(Ptr) : StringLength;
	}

private:
	// Non-copyable
	TStringPointer(const TStringPointer&) = delete;
	TStringPointer& operator=(const TStringPointer&) = delete;

	const ToType* Ptr;
	int32 StringLength;
};


/**
 * NOTE: The objects these macros declare have very short lifetimes. They are
 * meant to be used as parameters to functions. You cannot assign a variable
 * to the contents of the converted string as the object will go out of
 * scope and the string released.
 *
 * NOTE: The parameter you pass in MUST be a proper string, as the parameter
 * is typecast to a pointer. If you pass in a char, not char* it will compile
 * and then crash at runtime.
 *
 * Usage:
 *
 *		SomeApi(TCHAR_TO_ANSI(SomeUnicodeString));
 *
 *		const char* SomePointer = TCHAR_TO_ANSI(SomeUnicodeString); <--- Bad!!!
 */

// These should be replaced with StringCasts when FPlatformString starts to know about UTF-8.
typedef TStringConversion<FTCHARToUTF8_Convert> FTCHARToUTF8;
typedef TStringConversion<FUTF8ToTCHAR_Convert> FUTF8ToTCHAR;

// Usage of these should be replaced with StringCasts.
#define TCHAR_TO_ANSI(str) (ANSICHAR*)StringCast<ANSICHAR>(static_cast<const TCHAR*>(str)).Get()
#define ANSI_TO_TCHAR(str) (TCHAR*)StringCast<TCHAR>(static_cast<const ANSICHAR*>(str)).Get()
#define TCHAR_TO_UTF8(str) (ANSICHAR*)FTCHARToUTF8((const TCHAR*)str).Get()
#define UTF8_TO_TCHAR(str) (TCHAR*)FUTF8ToTCHAR((const ANSICHAR*)str).Get()

// special handling for platforms still using a 32-bit TCHAR
#if PLATFORM_TCHAR_IS_4_BYTES

typedef TStringConversion<TUTF32ToUTF16_Convert<TCHAR, UTF16CHAR>> FTCHARToUTF16;
typedef TStringConversion<TUTF16ToUTF32_Convert<UTF16CHAR, TCHAR>> FUTF16ToTCHAR;
#define TCHAR_TO_UTF16(str) (UTF16CHAR*)FTCHARToUTF16((const TCHAR*)str).Get()
#define UTF16_TO_TCHAR(str) (TCHAR*)FUTF16ToTCHAR((const UTF16CHAR*)str).Get()

static_assert(sizeof(TCHAR) == sizeof(UTF32CHAR), "TCHAR and UTF32CHAR are expected to be the same size for inline conversion! PLATFORM_TCHAR_IS_4_BYTES is not configured correctly for this platform.");
typedef TStringPointer<TCHAR, UTF32CHAR> FTCHARToUTF32;
typedef TStringPointer<UTF32CHAR, TCHAR> FUTF32ToTCHAR;
#define TCHAR_TO_UTF32(str) (UTF32CHAR*)(str)
#define UTF32_TO_TCHAR(str) (TCHAR*)(str)

#else

static_assert(sizeof(TCHAR) == sizeof(UTF16CHAR), "TCHAR and UTF16CHAR are expected to be the same size for inline conversion! PLATFORM_TCHAR_IS_4_BYTES is not configured correctly for this platform.");
typedef TStringPointer<TCHAR, UTF16CHAR> FTCHARToUTF16;
typedef TStringPointer<UTF16CHAR, TCHAR> FUTF16ToTCHAR;
#define TCHAR_TO_UTF16(str) (UTF16CHAR*)(str)
#define UTF16_TO_TCHAR(str) (TCHAR*)(str)

typedef TStringConversion<TUTF16ToUTF32_Convert<TCHAR, UTF32CHAR>> FTCHARToUTF32;
typedef TStringConversion<TUTF32ToUTF16_Convert<UTF32CHAR, TCHAR>> FUTF32ToTCHAR;
#define TCHAR_TO_UTF32(str) (UTF32CHAR*)FTCHARToUTF32((const TCHAR*)str).Get()
#define UTF32_TO_TCHAR(str) (TCHAR*)FUTF32ToTCHAR((const UTF32CHAR*)str).Get()

#endif

// special handling for going from char16_t to wchar_t for third party libraries that need wchar_t
#if PLATFORM_TCHAR_IS_CHAR16 && PLATFORM_WCHAR_IS_4_BYTES
typedef TStringConversion<TUTF16ToUTF32_Convert<TCHAR, wchar_t>> FTCHARToWChar;
typedef TStringConversion<TUTF32ToUTF16_Convert<wchar_t, TCHAR>> FWCharToTCHAR;
#define TCHAR_TO_WCHAR(str) (wchar_t*)FTCHARToWChar((const TCHAR*)str).Get()
#define WCHAR_TO_TCHAR(str) (TCHAR*)FWCharToTCHAR((const wchar_t*)str).Get()
#else
static_assert(sizeof(TCHAR) == sizeof(wchar_t), "TCHAR and wchar_t are expected to be the same size for inline conversion! PLATFORM_WCHAR_IS_4_BYTES is not configured correctly for this platform.");
typedef TStringPointer<TCHAR, wchar_t> FTCHARToWChar;
typedef TStringPointer<wchar_t, TCHAR> FWCharToTCHAR;
#define TCHAR_TO_WCHAR(str) (wchar_t*)(str)
#define WCHAR_TO_TCHAR(str) (TCHAR*)(str)
#endif

/**
 * StringCast example usage:
 *
 * void Func(const FString& Str)
 * {
 *     auto Src = StringCast<ANSICHAR>();
 *     const ANSICHAR* Ptr = Src.Get(); // Ptr is a pointer to an ANSICHAR representing the potentially-converted string data.
 * }
 *
 */

/**
 * Creates an object which acts as a source of a given string type.  See example above.
 *
 * @param Str The null-terminated source string to convert.
 */
template <typename To, typename From>
FORCEINLINE typename TEnableIf<FPlatformString::TAreEncodingsCompatible<To, From>::Value, TStringPointer<To>>::Type StringCast(const From* Str)
{
	return TStringPointer<To>((const To*)Str);
}

/**
 * Creates an object which acts as a source of a given string type.  See example above.
 *
 * @param Str The null-terminated source string to convert.
 */
template <typename To, typename From>
FORCEINLINE typename TEnableIf<!FPlatformString::TAreEncodingsCompatible<To, From>::Value, TStringConversion<TStringConvert<From, To>>>::Type StringCast(const From* Str)
{
	return TStringConversion<TStringConvert<From, To>>(Str);
}

/**
 * Creates an object which acts as a source of a given string type.  See example above.
 *
 * @param Str The source string to convert, not necessarily null-terminated.
 * @param Len The number of From elements in Str.
 */
template <typename To, typename From>
FORCEINLINE typename TEnableIf<FPlatformString::TAreEncodingsCompatible<To, From>::Value, TStringPointer<To>>::Type StringCast(const From* Str, int32 Len)
{
	return TStringPointer<To>((const To*)Str, Len);
}

/**
 * Creates an object which acts as a source of a given string type.  See example above.
 *
 * @param Str The source string to convert, not necessarily null-terminated.
 * @param Len The number of From elements in Str.
 */
template <typename To, typename From>
FORCEINLINE typename TEnableIf<!FPlatformString::TAreEncodingsCompatible<To, From>::Value, TStringConversion<TStringConvert<From, To>>>::Type StringCast(const From* Str, int32 Len)
{
	return TStringConversion<TStringConvert<From, To>>(Str, Len);
}


/**
 * Casts one fixed-width char type into another.
 *
 * @param Ch The character to convert.
 * @return The converted character.
 */
template <typename To, typename From>
FORCEINLINE To CharCast(From Ch)
{
	To Result;
	FPlatformString::Convert(&Result, 1, &Ch, 1, (To)UNICODE_BOGUS_CHAR_CODEPOINT);
	return Result;
}

/**
 * This class is returned by StringPassthru and is not intended to be used directly.
 */
template <typename ToType, typename FromType, int32 DefaultConversionSize = DEFAULT_STRING_CONVERSION_SIZE>
class TStringPassthru : private TInlineAllocator<DefaultConversionSize>::template ForElementType<FromType>
{
	typedef typename TInlineAllocator<DefaultConversionSize>::template ForElementType<FromType> AllocatorType;

public:
	FORCEINLINE TStringPassthru(ToType* InDest, int32 InDestLen, int32 InSrcLen)
		: Dest   (InDest)
		, DestLen(InDestLen)
		, SrcLen (InSrcLen)
	{
		AllocatorType::ResizeAllocation(0, SrcLen, sizeof(FromType));
	}

	FORCEINLINE TStringPassthru(TStringPassthru&& Other)
	{
		AllocatorType::MoveToEmpty(Other);
	}

	FORCEINLINE void Apply() const
	{
		const FromType* Source = (const FromType*)AllocatorType::GetAllocation();
		check(FPlatformString::ConvertedLength<ToType>(Source, SrcLen) <= DestLen);
		FPlatformString::Convert(Dest, DestLen, Source, SrcLen);
	}

	FORCEINLINE FromType* Get()
	{
		return (FromType*)AllocatorType::GetAllocation();
	}

private:
	// Non-copyable
	TStringPassthru(const TStringPassthru&);
	TStringPassthru& operator=(const TStringPassthru&);

	ToType* Dest;
	int32   DestLen;
	int32   SrcLen;
};

// This seemingly-pointless class is intended to be API-compatible with TStringPassthru
// and is returned by StringPassthru when no string conversion is necessary.
template <typename T>
class TPassthruPointer
{
public:
	FORCEINLINE explicit TPassthruPointer(T* InPtr)
		: Ptr(InPtr)
	{
	}

	FORCEINLINE T* Get() const
	{
		return Ptr;
	}

	FORCEINLINE void Apply() const
	{
	}

private:
	T* Ptr;
};

/**
 * Allows the efficient conversion of strings by means of a temporary memory buffer only when necessary.  Intended to be used
 * when you have an API which populates a buffer with some string representation which is ultimately going to be stored in another
 * representation, but where you don't want to do a conversion or create a temporary buffer for that string if it's not necessary.
 *
 * Intended use:
 *
 * // Populates the buffer Str with StrLen characters.
 * void SomeAPI(APICharType* Str, int32 StrLen);
 *
 * void Func(DestChar* Buffer, int32 BufferSize)
 * {
 *     // Create a passthru.  This takes the buffer (and its size) which will ultimately hold the string, as well as the length of the
 *     // string that's being converted, which must be known in advance.
 *     // An explicit template argument is also passed to indicate the character type of the source string.
 *     // Buffer must be correctly typed for the destination string type.
 *     auto Passthru = StringMemoryPassthru<APICharType>(Buffer, BufferSize, SourceLength);
 *
 *     // Passthru.Get() returns an APICharType buffer pointer which is guaranteed to be SourceLength characters in size.
 *     // It's possible, and in fact intended, for Get() to return the same pointer as Buffer if DestChar and APICharType are
 *     // compatible string types.  If this is the case, SomeAPI will write directly into Buffer.  If the string types are not
 *     // compatible, Get() will return a pointer to some temporary memory which allocated by and owned by the passthru.
 *     SomeAPI(Passthru.Get(), SourceLength);
 *
 *     // If the string types were not compatible, then the passthru used temporary storage, and we need to write that back to Buffer.
 *     // We do that with the Apply call.  If the string types were compatible, then the data was already written to Buffer directly
 *     // and so Apply is a no-op.
 *     Passthru.Apply();
 *
 *     // Now Buffer holds the data output by SomeAPI, already converted if necessary.
 * }
 */
template <typename From, typename To, int32 DefaultConversionSize = DEFAULT_STRING_CONVERSION_SIZE>
FORCEINLINE typename TEnableIf<FPlatformString::TAreEncodingsCompatible<To, From>::Value, TPassthruPointer<From>>::Type StringMemoryPassthru(To* Buffer, int32 BufferSize, int32 SourceLength)
{
	check(SourceLength <= BufferSize);
	return TPassthruPointer<From>((From*)Buffer);
}

template <typename From, typename To, int32 DefaultConversionSize = DEFAULT_STRING_CONVERSION_SIZE>
FORCEINLINE typename TEnableIf<!FPlatformString::TAreEncodingsCompatible<To, From>::Value, TStringPassthru<To, From, DefaultConversionSize>>::Type StringMemoryPassthru(To* Buffer, int32 BufferSize, int32 SourceLength)
{
	return TStringPassthru<To, From, DefaultConversionSize>(Buffer, BufferSize, SourceLength);
}

template <typename ToType, typename FromType>
FORCEINLINE TArray<ToType> StringToArray(const FromType* Src, int32 SrcLen)
{
	int32 DestLen = FPlatformString::ConvertedLength<ToType>(Src, SrcLen);

	TArray<ToType> Result;
	Result.AddUninitialized(DestLen);
	FPlatformString::Convert(Result.GetData(), DestLen, Src, SrcLen);

	return Result;
}

template <typename ToType, typename FromType>
FORCEINLINE TArray<ToType> StringToArray(const FromType* Str)
{
	return ToArray(Str, TCString<FromType>::Strlen(Str) + 1);
}
