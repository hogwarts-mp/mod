// Copyright Epic Games, Inc. All Rights Reserved.

#include "String/ParseLines.h"

#include "Algo/Compare.h"
#include "Containers/StringView.h"
#include "Misc/AutomationTest.h"
#include "Misc/StringBuilder.h"
#include "Templates/EqualTo.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(StringParseLinesTest, "System.Core.String.ParseLines", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool StringParseLinesTest::RunTest(const FString& Parameters)
{
	auto RunParseLinesTest = [this](const FStringView& View, std::initializer_list<FStringView> ExpectedLines)
	{
		TArray<FStringView, TInlineAllocator<8>> ResultLines;
		UE::String::ParseLines(View, [&ResultLines](FStringView Line) { ResultLines.Add(Line); });
		if (!Algo::CompareByPredicate(ResultLines, ExpectedLines, TEqualTo<>()))
		{
			TStringBuilder<512> Error;
			Error << TEXT("UE::String::ParseLines failed to parse \"") << FString(View).ReplaceCharWithEscapedChar() << TEXT("\" result {");
			Error.JoinQuoted(ResultLines, TEXT(", "_SV), TEXT("\""_SV));
			Error << TEXT("} expected {");
			Error.JoinQuoted(ExpectedLines, TEXT(", "_SV), TEXT("\""_SV));
			Error << TEXT("}");
			AddError(Error.ToString());
		}
	};

	RunParseLinesTest(TEXT(""_SV),                        {TEXT(""_SV)});
	RunParseLinesTest(TEXT("\n"_SV),                      {TEXT(""_SV)});
	RunParseLinesTest(TEXT("\r"_SV),                      {TEXT(""_SV)});
	RunParseLinesTest(TEXT("\r\n"_SV),                    {TEXT(""_SV)});
	RunParseLinesTest(TEXT("\n\n"_SV),                    {TEXT(""_SV), TEXT(""_SV)});
	RunParseLinesTest(TEXT("\r\r"_SV),                    {TEXT(""_SV), TEXT(""_SV)});
	RunParseLinesTest(TEXT("\r\n\r\n"_SV),                {TEXT(""_SV), TEXT(""_SV)});
	RunParseLinesTest(TEXT("\r\nABC"_SV).Left(2),         {TEXT(""_SV)});
	RunParseLinesTest(TEXT("\r\nABC\r\nDEF"_SV).Left(5),  {TEXT(""_SV), TEXT("ABC"_SV)});
	RunParseLinesTest(TEXT("ABC DEF"_SV),                 {TEXT("ABC DEF"_SV)});
	RunParseLinesTest(TEXT("\nABC DEF\n"_SV),             {TEXT(""_SV), TEXT("ABC DEF"_SV)});
	RunParseLinesTest(TEXT("\rABC DEF\r"_SV),             {TEXT(""_SV), TEXT("ABC DEF"_SV)});
	RunParseLinesTest(TEXT("\r\nABC DEF\r\n"_SV),         {TEXT(""_SV), TEXT("ABC DEF"_SV)});
	RunParseLinesTest(TEXT("\r\n\r\nABC DEF\r\n\r\n"_SV), {TEXT(""_SV), TEXT(""_SV), TEXT("ABC DEF"_SV), TEXT(""_SV)});
	RunParseLinesTest(TEXT("ABC\nDEF"_SV),                {TEXT("ABC"_SV), TEXT("DEF"_SV)});
	RunParseLinesTest(TEXT("ABC\rDEF"_SV),                {TEXT("ABC"_SV), TEXT("DEF"_SV)});
	RunParseLinesTest(TEXT("\r\nABC\r\nDEF\r\n"_SV),      {TEXT(""_SV), TEXT("ABC"_SV), TEXT("DEF"_SV)});
	RunParseLinesTest(TEXT("\r\nABC\r\n\r\nDEF\r\n"_SV),  {TEXT(""_SV), TEXT("ABC"_SV), TEXT(""_SV), TEXT("DEF"_SV)});

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
