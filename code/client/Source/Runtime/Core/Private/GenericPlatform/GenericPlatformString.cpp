// Copyright Epic Games, Inc. All Rights Reserved.

#include "GenericPlatform/GenericPlatformString.h"
#include "HAL/UnrealMemory.h"
#include "Misc/Char.h"
#include "Containers/UnrealString.h"
#include "Logging/LogMacros.h"


DEFINE_LOG_CATEGORY_STATIC(LogGenericPlatformString, Log, All);

template <> const TCHAR* FGenericPlatformString::GetEncodingTypeName<ANSICHAR>() { return TEXT("ANSICHAR"); }
template <> const TCHAR* FGenericPlatformString::GetEncodingTypeName<WIDECHAR>() { return TEXT("WIDECHAR"); }
template <> const TCHAR* FGenericPlatformString::GetEncodingTypeName<UCS2CHAR>() { return TEXT("UCS2CHAR"); }
#if PLATFORM_TCHAR_IS_CHAR16
template <> const TCHAR* FGenericPlatformString::GetEncodingTypeName<wchar_t>() { return TEXT("WCHAR_T"); }
#endif

void* FGenericPlatformString::Memcpy(void* Dest, const void* Src, SIZE_T Count)
{
	return FMemory::Memcpy(Dest, Src, Count);
}

namespace
{
	void TrimStringAndLogBogusCharsError(FString& SrcStr, const TCHAR* SourceCharName, const TCHAR* DestCharName)
	{
		SrcStr.TrimStartInline();
		// @todo: Put this back in once GLog becomes a #define, or is replaced with GLog::GetLog()
		//UE_LOG(LogGenericPlatformString, Warning, TEXT("Bad chars found when trying to convert \"%s\" from %s to %s"), *SrcStr, SourceCharName, DestCharName);
	}
}

template <typename DestEncoding, typename SourceEncoding>
void FGenericPlatformString::LogBogusChars(const SourceEncoding* Src, int32 SrcSize)
{
	FString SrcStr;
	bool    bFoundBogusChars = false;
	for (; SrcSize; --SrcSize)
	{
		SourceEncoding SrcCh = *Src++;
		if (!CanConvertChar<DestEncoding>(SrcCh))
		{
			SrcStr += FString::Printf(TEXT("[0x%X]"), (int32)SrcCh);
			bFoundBogusChars = true;
		}
		else if (CanConvertChar<TCHAR>(SrcCh))
		{
			if (TChar<SourceEncoding>::IsLinebreak(SrcCh))
			{
				if (bFoundBogusChars)
				{
					TrimStringAndLogBogusCharsError(SrcStr, GetEncodingTypeName<SourceEncoding>(), GetEncodingTypeName<DestEncoding>());
					bFoundBogusChars = false;
				}
				SrcStr.Empty();
			}
			else
			{
				SrcStr.AppendChar((TCHAR)SrcCh);
			}
		}
		else
		{
			SrcStr.AppendChar((TCHAR)'?');
		}
	}

	if (bFoundBogusChars)
	{
		TrimStringAndLogBogusCharsError(SrcStr, GetEncodingTypeName<SourceEncoding>(), GetEncodingTypeName<DestEncoding>());
	}
}

namespace GenericPlatformStringPrivate
{

template<typename CharType1, typename CharType2>
int32 StrncmpImpl(const CharType1* String1, const CharType2* String2, SIZE_T Count)
{
	for (; Count > 0; --Count)
	{
		CharType1 C1 = *String1++;
		CharType2 C2 = *String2++;

		if (C1 != C2)
		{
			return TChar<CharType1>::ToUnsigned(C1) - TChar<CharType2>::ToUnsigned(C2);
		}
		if (C1 == 0)
		{
			return 0;
		}
	}

	return 0;
}

}

int32 FGenericPlatformString::Strncmp(const ANSICHAR* Str1, const ANSICHAR* Str2, SIZE_T Count) { return GenericPlatformStringPrivate::StrncmpImpl(Str1, Str2, Count); }
int32 FGenericPlatformString::Strncmp(const WIDECHAR* Str1, const WIDECHAR* Str2, SIZE_T Count) { return GenericPlatformStringPrivate::StrncmpImpl(Str1, Str2, Count); }
int32 FGenericPlatformString::Strncmp(const ANSICHAR* Str1, const WIDECHAR* Str2, SIZE_T Count) { return GenericPlatformStringPrivate::StrncmpImpl(Str1, Str2, Count); }
int32 FGenericPlatformString::Strncmp(const WIDECHAR* Str1, const ANSICHAR* Str2, SIZE_T Count) { return GenericPlatformStringPrivate::StrncmpImpl(Str1, Str2, Count); }

#if !UE_BUILD_DOCS
template CORE_API void FGenericPlatformString::LogBogusChars<ANSICHAR, WIDECHAR>(const WIDECHAR* Src, int32 SrcSize);
template CORE_API void FGenericPlatformString::LogBogusChars<ANSICHAR, UCS2CHAR>(const UCS2CHAR* Src, int32 SrcSize);
template CORE_API void FGenericPlatformString::LogBogusChars<WIDECHAR, ANSICHAR>(const ANSICHAR* Src, int32 SrcSize);
template CORE_API void FGenericPlatformString::LogBogusChars<WIDECHAR, UCS2CHAR>(const UCS2CHAR* Src, int32 SrcSize);
template CORE_API void FGenericPlatformString::LogBogusChars<UCS2CHAR, ANSICHAR>(const ANSICHAR* Src, int32 SrcSize);
template CORE_API void FGenericPlatformString::LogBogusChars<UCS2CHAR, WIDECHAR>(const WIDECHAR* Src, int32 SrcSize);
#if PLATFORM_TCHAR_IS_CHAR16
template CORE_API void FGenericPlatformString::LogBogusChars<wchar_t, char16_t>(const char16_t* Src, int32 SrcSize);
template CORE_API void FGenericPlatformString::LogBogusChars<char16_t, wchar_t>(const wchar_t* Src, int32 SrcSize);
#endif
#endif
