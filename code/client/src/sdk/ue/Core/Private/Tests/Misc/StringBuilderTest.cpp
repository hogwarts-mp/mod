// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/StringBuilder.h"

#include "Containers/UnrealString.h"
#include "Misc/AutomationTest.h"

static_assert(TIsSame<typename FStringBuilderBase::ElementType, TCHAR>::Value, "FStringBuilderBase must use TCHAR.");
static_assert(TIsSame<typename FAnsiStringBuilderBase::ElementType, ANSICHAR>::Value, "FAnsiStringBuilderBase must use ANSICHAR.");
static_assert(TIsSame<typename FWideStringBuilderBase::ElementType, WIDECHAR>::Value, "FWideStringBuilderBase must use WIDECHAR.");

static_assert(TIsSame<FStringBuilderBase, TStringBuilderBase<TCHAR>>::Value, "FStringBuilderBase must be the same as TStringBuilderBase<TCHAR>.");
static_assert(TIsSame<FAnsiStringBuilderBase, TStringBuilderBase<ANSICHAR>>::Value, "FAnsiStringBuilderBase must be the same as TStringBuilderBase<ANSICHAR>.");
static_assert(TIsSame<FWideStringBuilderBase, TStringBuilderBase<WIDECHAR>>::Value, "FWideStringBuilderBase must be the same as TStringBuilderBase<WIDECHAR>.");

static_assert(TIsContiguousContainer<FStringBuilderBase>::Value, "FStringBuilderBase must be a contiguous container.");
static_assert(TIsContiguousContainer<FAnsiStringBuilderBase>::Value, "FAnsiStringBuilderBase must be a contiguous container.");
static_assert(TIsContiguousContainer<FWideStringBuilderBase>::Value, "FWideStringBuilderBase must be a contiguous container.");

static_assert(TIsContiguousContainer<TStringBuilder<128>>::Value, "TStringBuilder<N> must be a contiguous container.");
static_assert(TIsContiguousContainer<TAnsiStringBuilder<128>>::Value, "TAnsiStringBuilder<N> must be a contiguous container.");
static_assert(TIsContiguousContainer<TWideStringBuilder<128>>::Value, "TWideStringBuilder<N> must be a contiguous container.");

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStringBuilderTestAppendString, "System.Core.StringBuilder.AppendString", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FStringBuilderTestAppendString::RunTest(const FString& Parameters)
{
	// Append Char
	{
		TStringBuilder<7> Builder;
		Builder << TEXT('A') << TEXT('B') << TEXT('C');
		Builder << 'D' << 'E' << 'F';
		TestEqual(TEXT("Append Char"), FStringView(Builder), TEXT("ABCDEF"_SV));

		TAnsiStringBuilder<4> AnsiBuilder;
		AnsiBuilder << 'A' << 'B' << 'C';
		TestEqual(TEXT("Append AnsiChar"), FAnsiStringView(AnsiBuilder), "ABC"_ASV);
	}

	// Append C String
	{
		TStringBuilder<7> Builder;
		Builder << TEXT("ABC");
		Builder << "DEF";
		TestEqual(TEXT("Append C String"), FStringView(Builder), TEXT("ABCDEF"_SV));

		TAnsiStringBuilder<4> AnsiBuilder;
		AnsiBuilder << "ABC";
		TestEqual(TEXT("Append Ansi C String"), FAnsiStringView(AnsiBuilder), "ABC"_ASV);
	}

	// Append FStringView
	{
		TStringBuilder<7> Builder;
		Builder << TEXT("ABC"_SV);
		Builder << "DEF"_ASV;
		TestEqual(TEXT("Append FStringView"), FStringView(Builder), TEXT("ABCDEF"_SV));

		TAnsiStringBuilder<4> AnsiBuilder;
		AnsiBuilder << "ABC"_ASV;
		TestEqual(TEXT("Append FAnsiStringView"), FAnsiStringView(AnsiBuilder), "ABC"_ASV);
	}

	// Append FStringBuilderBase
	{
		TStringBuilder<4> Builder;
		Builder << TEXT("ABC");
		TStringBuilder<4> BuilderCopy;
		BuilderCopy << Builder;
		TestEqual(TEXT("Append FStringBuilderBase"), FStringView(BuilderCopy), TEXT("ABC"_SV));

		TAnsiStringBuilder<4> AnsiBuilder;
		AnsiBuilder << "ABC";
		TAnsiStringBuilder<4> AnsiBuilderCopy;
		AnsiBuilderCopy << AnsiBuilder;
		TestEqual(TEXT("Append FAnsiStringBuilderBase"), FAnsiStringView(AnsiBuilderCopy), "ABC"_ASV);
	}

	// Append FString
	{
		TStringBuilder<4> Builder;
		Builder << FString(TEXT("ABC"));
		TestEqual(TEXT("Append FString"), FStringView(Builder), TEXT("ABC"_SV));
	}

	// Append Char Array
	{
		const TCHAR String[16] = TEXT("ABC");
		TStringBuilder<4> Builder;
		Builder << String;
		TestEqual(TEXT("Append Char Array"), FStringView(Builder), TEXT("ABC"_SV));

		const ANSICHAR AnsiString[16] = "ABC";
		TAnsiStringBuilder<4> AnsiBuilder;
		AnsiBuilder << AnsiString;
		TestEqual(TEXT("Append Char Array"), FAnsiStringView(AnsiBuilder), "ABC"_ASV);
	}

	return true;
}

#endif //WITH_DEV_AUTOMATION_TESTS
