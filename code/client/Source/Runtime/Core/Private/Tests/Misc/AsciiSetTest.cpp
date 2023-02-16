// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AsciiSet.h"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS 

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TAsciiSetTest, "System.Core.Misc.AsciiSet", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)

bool TAsciiSetTest::RunTest(const FString& Parameters)
{
	constexpr FAsciiSet Whitespaces(" \v\f\t\r\n");
	TestTrue(TEXT("Contains"), Whitespaces.Contains(' '));
	TestTrue(TEXT("Contains"), Whitespaces.Contains('\n'));
	TestFalse(TEXT("Contains"), Whitespaces.Contains('a'));
	TestFalse(TEXT("Contains no extended ASCII"), Whitespaces.Contains('\x80'));
	TestFalse(TEXT("Contains no extended ASCII"), Whitespaces.Contains('\xA0'));
	TestFalse(TEXT("Contains no extended ASCII"), Whitespaces.Contains('\xFF'));
	
	constexpr FAsciiSet Aa("Aa");
	uint32 ANum = 0;
	for (char32_t C = 0; C < 512; ++C)
	{
		ANum += Aa.Contains(C);
	}
	TestEqual("Contains no wide", ANum, 2u);

	constexpr FAsciiSet NonWhitespaces = ~Whitespaces;
	uint32 WhitespaceNum = 0;
	for (uint8 Char = 0; Char < 128; ++Char)
	{
		WhitespaceNum += !!Whitespaces.Test(Char);
		TestEqual(TEXT("Inverse"), !!Whitespaces.Test(Char), !NonWhitespaces.Test(Char));
	}
	TestEqual(TEXT("Num"), WhitespaceNum, 6);

	TestEqual(TEXT("Skip"), FAsciiSet::Skip(TEXT("  \t\tHello world!"), Whitespaces), TEXT("Hello world!"));
	TestEqual(TEXT("Skip"), FAsciiSet::Skip(TEXT("Hello world!"), Whitespaces), TEXT("Hello world!"));
	TestEqual(TEXT("Skip to extended ASCII"), FAsciiSet::Skip(" " "\xA0" " abc", Whitespaces), "\xA0" " abc");
	TestEqual(TEXT("Skip to wide"), FAsciiSet::Skip(TEXT(" 变 abc"), Whitespaces), TEXT("变 abc"));
	TestEqual(TEXT("AdvanceToFirst"),	*FAsciiSet::FindFirstOrEnd("NonWhitespace\t \nNonWhitespace", Whitespaces), '\t');
	TestEqual(TEXT("AdvanceToLast"),	*FAsciiSet::FindLastOrEnd("NonWhitespace\t \nNonWhitespace", Whitespaces), '\n');
	TestEqual(TEXT("AdvanceToLast"),	*FAsciiSet::FindLastOrEnd("NonWhitespace\t NonWhitespace\n", Whitespaces), '\n');
	TestEqual(TEXT("AdvanceToFirst"),	*FAsciiSet::FindFirstOrEnd("NonWhitespaceNonWhitespace", Whitespaces), '\0');
	TestEqual(TEXT("AdvanceToLast"),	*FAsciiSet::FindLastOrEnd("NonWhitespaceNonWhitespace", Whitespaces), '\0');

	constexpr FAsciiSet XmlEscapeChars("&<>\"'");
	TestTrue(TEXT("None"), FAsciiSet::HasNone("No escape chars", XmlEscapeChars));
	TestFalse(TEXT("Any"), FAsciiSet::HasAny("No escape chars", XmlEscapeChars));
	TestFalse(TEXT("Only"), FAsciiSet::HasOnly("No escape chars", XmlEscapeChars));

	TestTrue(TEXT("None"), FAsciiSet::HasNone("", XmlEscapeChars));
	TestFalse(TEXT("Any"), FAsciiSet::HasAny("", XmlEscapeChars));
	TestTrue(TEXT("Only"), FAsciiSet::HasOnly("", XmlEscapeChars));

	TestFalse(TEXT("None"), FAsciiSet::HasNone("&<>\"'", XmlEscapeChars));
	TestTrue(TEXT("Any"), FAsciiSet::HasAny("&<>\"'", XmlEscapeChars));
	TestTrue(TEXT("Only"), FAsciiSet::HasOnly("&<>\"'", XmlEscapeChars));

	TestFalse(TEXT("None"), FAsciiSet::HasNone("&<>\"' and more", XmlEscapeChars));
	TestTrue(TEXT("Any"), FAsciiSet::HasAny("&<>\"' and more", XmlEscapeChars));
	TestFalse(TEXT("Only"), FAsciiSet::HasOnly("&<>\"' and more", XmlEscapeChars));

	constexpr FAsciiSet Abc("abc");
	constexpr FAsciiSet Abcd = Abc + 'd';
	TestTrue(TEXT("Add"), Abcd.Contains('a'));
	TestTrue(TEXT("Add"), Abcd.Contains('b'));
	TestTrue(TEXT("Add"), Abcd.Contains('c'));
	TestTrue(TEXT("Add"), Abcd.Contains('d'));
	TestFalse(TEXT("Add"), Abcd.Contains('e'));

	return true;
}

#endif //WITH_DEV_AUTOMATION_TESTS