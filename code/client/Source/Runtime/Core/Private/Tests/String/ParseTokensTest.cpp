// Copyright Epic Games, Inc. All Rights Reserved.

#include "String/ParseTokens.h"

#include "Algo/Compare.h"
#include "Misc/AutomationTest.h"
#include "Misc/StringBuilder.h"
#include "Templates/EqualTo.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(StringParseTokensByStringTest, "System.Core.String.ParseTokens.ByString", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool StringParseTokensByStringTest::RunTest(const FString& Parameters)
{
	auto RunParseTokensTest = [this](const FStringView& View, std::initializer_list<FStringView> Delimiters, std::initializer_list<FStringView> ExpectedTokens)
	{
		TArray<FStringView, TInlineAllocator<8>> ResultTokens;
		UE::String::ParseTokensMultiple(View, Delimiters, [&ResultTokens](FStringView Token) { ResultTokens.Add(Token); });
		if (!Algo::CompareByPredicate(ResultTokens, ExpectedTokens, TEqualTo<>()))
		{
			TStringBuilder<512> Error;
			Error << TEXT("UE::String::ParseTokensMultiple failed to parse \"") << View << TEXT("\" with delimiters {");
			Error.JoinQuoted(Delimiters, TEXT(", "_SV), TEXT("\""_SV));
			Error << TEXT("} result {");
			Error.JoinQuoted(ResultTokens, TEXT(", "_SV), TEXT("\""_SV));
			Error << TEXT("} expected {");
			Error.JoinQuoted(ExpectedTokens, TEXT(", "_SV), TEXT("\""_SV));
			Error << TEXT("}");
			AddError(Error.ToString());
		}
	};

	RunParseTokensTest(TEXT(""),         {},                       {TEXT("")});
	RunParseTokensTest(TEXT("ABC"),      {},                       {TEXT("ABC")});

	RunParseTokensTest(TEXT(""),         {TEXT(",")},              {TEXT("")});
	RunParseTokensTest(TEXT(","),        {TEXT(",")},              {TEXT(""), TEXT("")});
	RunParseTokensTest(TEXT(",,"),       {TEXT(",")},              {TEXT(""), TEXT(""), TEXT("")});
	RunParseTokensTest(TEXT("ABC"),      {TEXT(",")},              {TEXT("ABC")});
	RunParseTokensTest(TEXT("A,,C"),     {TEXT(",")},              {TEXT("A"), TEXT(""), TEXT("C")});
	RunParseTokensTest(TEXT("A,B,C"),    {TEXT(",")},              {TEXT("A"), TEXT("B"), TEXT("C")});
	RunParseTokensTest(TEXT(",A,B,C,"),  {TEXT(",")},              {TEXT(""), TEXT("A"), TEXT("B"), TEXT("C"), TEXT("")});
	RunParseTokensTest(TEXT("A\u2022B\u2022C"), {TEXT("\u2022")},  {TEXT("A"), TEXT("B"), TEXT("C")});

	RunParseTokensTest(TEXT("ABCDABCD"), {TEXT("AB")},             {TEXT(""), TEXT("CD"), TEXT("CD")});
	RunParseTokensTest(TEXT("ABCDABCD"), {TEXT("ABCD")},           {TEXT(""), TEXT(""), TEXT("")});
	RunParseTokensTest(TEXT("ABCDABCD"), {TEXT("DA")},             {TEXT("ABC"), TEXT("BCD")});

	RunParseTokensTest(TEXT("ABCDABCD"), {TEXT("B"),  TEXT("D")},  {TEXT("A"), TEXT("C"), TEXT("A"), TEXT("C"), TEXT("")});
	RunParseTokensTest(TEXT("ABCDABCD"), {TEXT("BC"), TEXT("DA")}, {TEXT("A"), TEXT(""), TEXT(""), TEXT("D")});

	RunParseTokensTest(TEXT("A\u2022\u2022B,,C"), {TEXT(",,"), TEXT("\u2022\u2022")},                      {TEXT("A"), TEXT("B"), TEXT("C")});
	RunParseTokensTest(TEXT("A\u2022\u2022B\u0085\u0085C"), {TEXT("\u0085\u0085"), TEXT("\u2022\u2022")},  {TEXT("A"), TEXT("B"), TEXT("C")});

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(StringParseTokensByCharTest, "System.Core.String.ParseTokens.ByChar", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool StringParseTokensByCharTest::RunTest(const FString& Parameters)
{
	auto RunParseTokensTest = [this](const FStringView& View, std::initializer_list<TCHAR> Delimiters, std::initializer_list<FStringView> ExpectedTokens)
	{
		TArray<FStringView, TInlineAllocator<8>> ResultTokens;
		UE::String::ParseTokensMultiple(View, Delimiters, [&ResultTokens](FStringView Token) { ResultTokens.Add(Token); });
		if (!Algo::CompareByPredicate(ResultTokens, ExpectedTokens, TEqualTo<>()))
		{
			TStringBuilder<512> Error;
			Error << TEXT("UE::String::ParseTokensMultiple failed to parse \"") << View << TEXT("\" with delimiters {");
			Error.JoinQuoted(Delimiters, TEXT(", "_SV), TEXT("'"_SV));
			Error << TEXT("} result {");
			Error.JoinQuoted(ResultTokens, TEXT(", "_SV), TEXT("\""_SV));
			Error << TEXT("} expected {");
			Error.JoinQuoted(ExpectedTokens, TEXT(", "_SV), TEXT("\""_SV));
			Error << TEXT("}");
			AddError(Error.ToString());
		}
	};

	RunParseTokensTest(TEXT(""),         {},                       {TEXT("")});
	RunParseTokensTest(TEXT("ABC"),      {},                       {TEXT("ABC")});

	RunParseTokensTest(TEXT(""),         {TEXT(',')},              {TEXT("")});
	RunParseTokensTest(TEXT(","),        {TEXT(',')},              {TEXT(""), TEXT("")});
	RunParseTokensTest(TEXT(",,"),       {TEXT(',')},              {TEXT(""), TEXT(""), TEXT("")});
	RunParseTokensTest(TEXT("ABC"),      {TEXT(',')},              {TEXT("ABC")});
	RunParseTokensTest(TEXT("A,,C"),     {TEXT(',')},              {TEXT("A"), TEXT(""), TEXT("C")});
	RunParseTokensTest(TEXT("A,B,C"),    {TEXT(',')},              {TEXT("A"), TEXT("B"), TEXT("C")});
	RunParseTokensTest(TEXT(",A,B,C,"),  {TEXT(',')},              {TEXT(""), TEXT("A"), TEXT("B"), TEXT("C"), TEXT("")});
	RunParseTokensTest(TEXT("A\u2022B\u2022C"), {TEXT('\u2022')},  {TEXT("A"), TEXT("B"), TEXT("C")});

	RunParseTokensTest(TEXT("ABCDABCD"), {TEXT('B'),  TEXT('D')},                  {TEXT("A"), TEXT("C"), TEXT("A"), TEXT("C"), TEXT("")});
	RunParseTokensTest(TEXT("A\u2022B,C"), {TEXT(','), TEXT('\u2022')},            {TEXT("A"), TEXT("B"), TEXT("C")});
	RunParseTokensTest(TEXT("A\u2022B\u0085C"), {TEXT('\u0085'), TEXT('\u2022')},  {TEXT("A"), TEXT("B"), TEXT("C")});

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
