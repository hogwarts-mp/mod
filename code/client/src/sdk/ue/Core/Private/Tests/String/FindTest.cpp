// Copyright Epic Games, Inc. All Rights Reserved.

#include "String/Find.h"

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(StringFindFirstTest, "System.Core.String.FindFirst", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool StringFindFirstTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("FindFirst"), UE::String::FindFirst(TEXT("AbCABCAbCABC"), TEXT("A")), 0);
	TestEqual(TEXT("FindFirst"), UE::String::FindFirst(TEXT("AbCABCAbCABC"), TEXT("a"), ESearchCase::IgnoreCase), 0);
	TestEqual(TEXT("FindFirst"), UE::String::FindFirst(TEXT("AbCABCAbCABC"), TEXT("b")), 1);
	TestEqual(TEXT("FindFirst"), UE::String::FindFirst(TEXT("AbCABCAbCABC"), TEXT("B")), 4);
	TestEqual(TEXT("FindFirst"), UE::String::FindFirst(TEXT("AbCABCAbCABC"), TEXT("B"), ESearchCase::IgnoreCase), 1);
	TestEqual(TEXT("FindFirst"), UE::String::FindFirst(TEXT("AbCABCAbCABC"), TEXT("a")), INDEX_NONE);
	TestEqual(TEXT("FindFirst"), UE::String::FindFirst(TEXT("AbCABCAbCABC"), TEXT("D"), ESearchCase::IgnoreCase), INDEX_NONE);
	TestEqual(TEXT("FindFirst"), UE::String::FindFirst(TEXT("AbCABCAbCABD"), TEXT("D")), 11);
	TestEqual(TEXT("FindFirst"), UE::String::FindFirst(TEXT("AbCABCAbCABD"), TEXT("d"), ESearchCase::IgnoreCase), 11);

	TestEqual(TEXT("FindFirst"), UE::String::FindFirst(TEXT("AbCABCAbCABC"), TEXT("AbC")), 0);
	TestEqual(TEXT("FindFirst"), UE::String::FindFirst(TEXT("AbCABCAbCABC"), TEXT("ABC")), 3);
	TestEqual(TEXT("FindFirst"), UE::String::FindFirst(TEXT("AbCABCAbCABC"), TEXT("Bc"), ESearchCase::IgnoreCase), 1);
	TestEqual(TEXT("FindFirst"), UE::String::FindFirst(TEXT("AbCABCAbCABC"), TEXT("ab")), INDEX_NONE);
	TestEqual(TEXT("FindFirst"), UE::String::FindFirst(TEXT("AbCABCAbCABC"), TEXT("CD"), ESearchCase::IgnoreCase), INDEX_NONE);
	TestEqual(TEXT("FindFirst"), UE::String::FindFirst(TEXT("AbCABCAbCABD"), TEXT("BD")), 10);
	TestEqual(TEXT("FindFirst"), UE::String::FindFirst(TEXT("AbCABCAbCABD"), TEXT("Bd"), ESearchCase::IgnoreCase), 10);

	TestEqual(TEXT("FindFirst"), UE::String::FindFirst(TEXT(""), TEXT("A")), INDEX_NONE);
	TestEqual(TEXT("FindFirst"), UE::String::FindFirst(TEXT("A"), TEXT("A")), 0);
	TestEqual(TEXT("FindFirst"), UE::String::FindFirst(TEXT("A"), TEXT("A"), ESearchCase::IgnoreCase), 0);
	TestEqual(TEXT("FindFirst"), UE::String::FindFirst(TEXT("ABC"), TEXT("ABC")), 0);
	TestEqual(TEXT("FindFirst"), UE::String::FindFirst(TEXT("ABC"), TEXT("abc"), ESearchCase::IgnoreCase), 0);
	TestEqual(TEXT("FindFirst"), UE::String::FindFirst(TEXT("AB"), TEXT("ABC")), INDEX_NONE);

	TestEqual(TEXT("FindFirst"), UE::String::FindFirst("AbCABCAbCABC", "ABC"), 3);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(StringFindLastTest, "System.Core.String.FindLast", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool StringFindLastTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("FindLast"), UE::String::FindLast(TEXT("AbCABCAbCABC"), TEXT("b")), 7);
	TestEqual(TEXT("FindLast"), UE::String::FindLast(TEXT("AbCABCAbCABC"), TEXT("B")), 10);
	TestEqual(TEXT("FindLast"), UE::String::FindLast(TEXT("AbCABCAbCABC"), TEXT("b"), ESearchCase::IgnoreCase), 10);
	TestEqual(TEXT("FindLast"), UE::String::FindLast(TEXT("AbCABCAbCABC"), TEXT("a")), INDEX_NONE);
	TestEqual(TEXT("FindLast"), UE::String::FindLast(TEXT("AbCABCAbCABC"), TEXT("D"), ESearchCase::IgnoreCase), INDEX_NONE);
	TestEqual(TEXT("FindLast"), UE::String::FindLast(TEXT("AbCABCAbCABD"), TEXT("D")), 11);
	TestEqual(TEXT("FindLast"), UE::String::FindLast(TEXT("AbCABCAbCABD"), TEXT("d"), ESearchCase::IgnoreCase), 11);

	TestEqual(TEXT("FindLast"), UE::String::FindLast(TEXT("AbCABCAbCABC"), TEXT("AbC")), 6);
	TestEqual(TEXT("FindLast"), UE::String::FindLast(TEXT("AbCABCAbCABC"), TEXT("ABC")), 9);
	TestEqual(TEXT("FindLast"), UE::String::FindLast(TEXT("AbCABCAbCABC"), TEXT("Bc"), ESearchCase::IgnoreCase), 10);
	TestEqual(TEXT("FindLast"), UE::String::FindLast(TEXT("AbCABCAbCABC"), TEXT("ab")), INDEX_NONE);
	TestEqual(TEXT("FindLast"), UE::String::FindLast(TEXT("AbCABCAbCABC"), TEXT("CD"), ESearchCase::IgnoreCase), INDEX_NONE);
	TestEqual(TEXT("FindLast"), UE::String::FindLast(TEXT("AbCABCAbCABC"), TEXT("BC")), 10);
	TestEqual(TEXT("FindLast"), UE::String::FindLast(TEXT("AbCABCAbCABC"), TEXT("Bc"), ESearchCase::IgnoreCase), 10);

	TestEqual(TEXT("FindLast"), UE::String::FindLast(TEXT(""), TEXT("A")), INDEX_NONE);
	TestEqual(TEXT("FindLast"), UE::String::FindLast(TEXT("A"), TEXT("A")), 0);
	TestEqual(TEXT("FindLast"), UE::String::FindLast(TEXT("A"), TEXT("A"), ESearchCase::IgnoreCase), 0);
	TestEqual(TEXT("FindLast"), UE::String::FindLast(TEXT("ABC"), TEXT("ABC")), 0);
	TestEqual(TEXT("FindLast"), UE::String::FindLast(TEXT("ABC"), TEXT("abc"), ESearchCase::IgnoreCase), 0);
	TestEqual(TEXT("FindLast"), UE::String::FindLast(TEXT("AB"), TEXT("ABC")), INDEX_NONE);

	TestEqual(TEXT("FindLast"), UE::String::FindLast("AbCABCAbCABC", "ABC"), 9);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(StringFindFirstOfAnyTest, "System.Core.String.FindFirstOfAny", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool StringFindFirstOfAnyTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("FindFirstOfAny"), UE::String::FindFirstOfAny(TEXT("AbCABCAbcABC"), {TEXT("A"), TEXT("B")}), 0);
	TestEqual(TEXT("FindFirstOfAny"), UE::String::FindFirstOfAny(TEXT("AbCABCAbcABC"), {TEXT("a"), TEXT("B")}, ESearchCase::IgnoreCase), 0);
	TestEqual(TEXT("FindFirstOfAny"), UE::String::FindFirstOfAny(TEXT("AbCABCAbcABC"), {TEXT("b")}), 1);
	TestEqual(TEXT("FindFirstOfAny"), UE::String::FindFirstOfAny(TEXT("AbCABCAbcABC"), {TEXT("c"), TEXT("B")}), 4);
	TestEqual(TEXT("FindFirstOfAny"), UE::String::FindFirstOfAny(TEXT("AbCABCAbcABC"), {TEXT("c"), TEXT("B")}, ESearchCase::IgnoreCase), 1);
	TestEqual(TEXT("FindFirstOfAny"), UE::String::FindFirstOfAny(TEXT("AbCABCAbcABC"), {TEXT("D"), TEXT("a")}), INDEX_NONE);
	TestEqual(TEXT("FindFirstOfAny"), UE::String::FindFirstOfAny(TEXT("AbCABCAbcABC"), {TEXT("E"), TEXT("D")}, ESearchCase::IgnoreCase), INDEX_NONE);
	TestEqual(TEXT("FindFirstOfAny"), UE::String::FindFirstOfAny(TEXT("AbCABCAbcABD"), {TEXT("E"), TEXT("D")}), 11);
	TestEqual(TEXT("FindFirstOfAny"), UE::String::FindFirstOfAny(TEXT("AbCABCAbcABD"), {TEXT("E"), TEXT("d")}, ESearchCase::IgnoreCase), 11);

	TestEqual(TEXT("FindFirstOfAny"), UE::String::FindFirstOfAny(TEXT("AbCABCAbCABC"), {TEXT("AbC")}), 0);
	TestEqual(TEXT("FindFirstOfAny"), UE::String::FindFirstOfAny(TEXT("AbCABCAbCABC"), {TEXT("CABc"), TEXT("ABC")}), 3);
	TestEqual(TEXT("FindFirstOfAny"), UE::String::FindFirstOfAny(TEXT("AbCABCAbCABC"), {TEXT("ABD"), TEXT("Bc")}, ESearchCase::IgnoreCase), 1);
	TestEqual(TEXT("FindFirstOfAny"), UE::String::FindFirstOfAny(TEXT("AbCABCAbCABC"), {TEXT("bc"), TEXT("ab")}), INDEX_NONE);
	TestEqual(TEXT("FindFirstOfAny"), UE::String::FindFirstOfAny(TEXT("AbCABCAbCABC"), {TEXT("DA"), TEXT("CD")}, ESearchCase::IgnoreCase), INDEX_NONE);
	TestEqual(TEXT("FindFirstOfAny"), UE::String::FindFirstOfAny(TEXT("AbCABCAbCABD"), {TEXT("BD"), TEXT("CABB")}), 10);
	TestEqual(TEXT("FindFirstOfAny"), UE::String::FindFirstOfAny(TEXT("AbCABCAbCABD"), {TEXT("Bd"), TEXT("CABB")}, ESearchCase::IgnoreCase), 10);

	TestEqual(TEXT("FindFirstOfAny"), UE::String::FindFirstOfAny(TEXT(""), {TEXT("A"), TEXT("B")}), INDEX_NONE);
	TestEqual(TEXT("FindFirstOfAny"), UE::String::FindFirstOfAny(TEXT("A"), {TEXT("A"), TEXT("B")}), 0);
	TestEqual(TEXT("FindFirstOfAny"), UE::String::FindFirstOfAny(TEXT("A"), {TEXT("A"), TEXT("B")}, ESearchCase::IgnoreCase), 0);
	TestEqual(TEXT("FindFirstOfAny"), UE::String::FindFirstOfAny(TEXT("ABC"), {TEXT("ABC"), TEXT("BC")}), 0);
	TestEqual(TEXT("FindFirstOfAny"), UE::String::FindFirstOfAny(TEXT("ABC"), {TEXT("abc"), TEXT("bc")}, ESearchCase::IgnoreCase), 0);
	TestEqual(TEXT("FindFirstOfAny"), UE::String::FindFirstOfAny(TEXT("AB"), {TEXT("ABC"), TEXT("ABD")}), INDEX_NONE);

	TestEqual(TEXT("FindFirstOfAny"), UE::String::FindFirstOfAny("AbCABCAbCABC", {"CABc", "ABC"}), 3);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(StringFindLastOfAnyTest, "System.Core.String.FindLastOfAny", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool StringFindLastOfAnyTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("FindLastOfAny"), UE::String::FindLastOfAny(TEXT("AbCABCAbcABC"), {TEXT("a"), TEXT("b")}), 7);
	TestEqual(TEXT("FindLastOfAny"), UE::String::FindLastOfAny(TEXT("AbCABCAbcABC"), {TEXT("a"), TEXT("b")}, ESearchCase::IgnoreCase), 10);
	TestEqual(TEXT("FindLastOfAny"), UE::String::FindLastOfAny(TEXT("AbCABCAbcABC"), {TEXT("b")}), 7);
	TestEqual(TEXT("FindLastOfAny"), UE::String::FindLastOfAny(TEXT("AbCABCAbcABC"), {TEXT("c"), TEXT("B")}), 10);
	TestEqual(TEXT("FindLastOfAny"), UE::String::FindLastOfAny(TEXT("AbCABCAbcABC"), {TEXT("c"), TEXT("B")}, ESearchCase::IgnoreCase), 11);
	TestEqual(TEXT("FindLastOfAny"), UE::String::FindLastOfAny(TEXT("AbCABCAbcABC"), {TEXT("D"), TEXT("a")}), INDEX_NONE);
	TestEqual(TEXT("FindLastOfAny"), UE::String::FindLastOfAny(TEXT("AbCABCAbcABC"), {TEXT("E"), TEXT("D")}, ESearchCase::IgnoreCase), INDEX_NONE);
	TestEqual(TEXT("FindLastOfAny"), UE::String::FindLastOfAny(TEXT("AbCABCAbcABD"), {TEXT("E"), TEXT("D")}), 11);
	TestEqual(TEXT("FindLastOfAny"), UE::String::FindLastOfAny(TEXT("AbCABCAbcABD"), {TEXT("E"), TEXT("d")}, ESearchCase::IgnoreCase), 11);

	TestEqual(TEXT("FindLastOfAny"), UE::String::FindLastOfAny(TEXT("AbCABCAbCABC"), {TEXT("AbC")}), 6);
	TestEqual(TEXT("FindLastOfAny"), UE::String::FindLastOfAny(TEXT("AbCABCAbCABC"), {TEXT("CABc"), TEXT("ABC")}), 9);
	TestEqual(TEXT("FindLastOfAny"), UE::String::FindLastOfAny(TEXT("AbCABCAbCABC"), {TEXT("ABD"), TEXT("Bc")}, ESearchCase::IgnoreCase), 10);
	TestEqual(TEXT("FindLastOfAny"), UE::String::FindLastOfAny(TEXT("AbCABCAbCABC"), {TEXT("bc"), TEXT("ab")}), INDEX_NONE);
	TestEqual(TEXT("FindLastOfAny"), UE::String::FindLastOfAny(TEXT("AbCABCAbCABC"), {TEXT("DA"), TEXT("CD")}, ESearchCase::IgnoreCase), INDEX_NONE);
	TestEqual(TEXT("FindLastOfAny"), UE::String::FindLastOfAny(TEXT("AbCABCAbCABD"), {TEXT("BD"), TEXT("CABB")}), 10);
	TestEqual(TEXT("FindLastOfAny"), UE::String::FindLastOfAny(TEXT("AbCABCAbCABD"), {TEXT("Bd"), TEXT("CABB")}, ESearchCase::IgnoreCase), 10);

	TestEqual(TEXT("FindLastOfAny"), UE::String::FindLastOfAny(TEXT(""), {TEXT("A"), TEXT("B")}), INDEX_NONE);
	TestEqual(TEXT("FindLastOfAny"), UE::String::FindLastOfAny(TEXT("A"), {TEXT("A"), TEXT("B")}), 0);
	TestEqual(TEXT("FindLastOfAny"), UE::String::FindLastOfAny(TEXT("A"), {TEXT("A"), TEXT("B")}, ESearchCase::IgnoreCase), 0);
	TestEqual(TEXT("FindLastOfAny"), UE::String::FindLastOfAny(TEXT("ABC"), {TEXT("ABC"), TEXT("BC")}), 1);
	TestEqual(TEXT("FindLastOfAny"), UE::String::FindLastOfAny(TEXT("ABC"), {TEXT("abc"), TEXT("bc")}, ESearchCase::IgnoreCase), 1);
	TestEqual(TEXT("FindLastOfAny"), UE::String::FindLastOfAny(TEXT("AB"), {TEXT("ABC"), TEXT("ABD")}), INDEX_NONE);

	TestEqual(TEXT("FindLastOfAny"), UE::String::FindLastOfAny("AbCABCAbCABC", {"CABc", "ABC"}), 9);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(StringFindFirstCharTest, "System.Core.String.FindFirstChar", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool StringFindFirstCharTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("FindFirstChar"), UE::String::FindFirstChar(TEXT("AbCABCAbCABC"), TEXT('b')), 1);
	TestEqual(TEXT("FindFirstChar"), UE::String::FindFirstChar(TEXT("AbCABCAbCABC"), TEXT('B')), 4);
	TestEqual(TEXT("FindFirstChar"), UE::String::FindFirstChar(TEXT("AbCABCAbCABC"), TEXT('B'), ESearchCase::IgnoreCase), 1);
	TestEqual(TEXT("FindFirstChar"), UE::String::FindFirstChar(TEXT("AbCABCAbCABC"), TEXT('a')), INDEX_NONE);
	TestEqual(TEXT("FindFirstChar"), UE::String::FindFirstChar(TEXT("AbCABCAbCABC"), TEXT('D'), ESearchCase::IgnoreCase), INDEX_NONE);
	TestEqual(TEXT("FindFirstChar"), UE::String::FindFirstChar(TEXT("AbCABCAbCABD"), TEXT('D')), 11);
	TestEqual(TEXT("FindFirstChar"), UE::String::FindFirstChar(TEXT("AbCABCAbCABD"), TEXT('d'), ESearchCase::IgnoreCase), 11);

	TestEqual(TEXT("FindFirstChar"), UE::String::FindFirstChar(TEXT(""), TEXT('A')), INDEX_NONE);
	TestEqual(TEXT("FindFirstChar"), UE::String::FindFirstChar(TEXT("A"), TEXT('A')), 0);
	TestEqual(TEXT("FindFirstChar"), UE::String::FindFirstChar(TEXT("A"), TEXT('A'), ESearchCase::IgnoreCase), 0);

	TestEqual(TEXT("FindFirstChar"), UE::String::FindFirstChar("AbCABCAbCABC", 'B'), 4);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(StringFindLastCharTest, "System.Core.String.FindLastChar", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool StringFindLastCharTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("FindLastChar"), UE::String::FindLastChar(TEXT("AbCABCAbCABC"), TEXT('b')), 7);
	TestEqual(TEXT("FindLastChar"), UE::String::FindLastChar(TEXT("AbCABCAbCABC"), TEXT('B')), 10);
	TestEqual(TEXT("FindLastChar"), UE::String::FindLastChar(TEXT("AbCABCAbCABC"), TEXT('b'), ESearchCase::IgnoreCase), 10);
	TestEqual(TEXT("FindLastChar"), UE::String::FindLastChar(TEXT("AbCABCAbCABC"), TEXT('a')), INDEX_NONE);
	TestEqual(TEXT("FindLastChar"), UE::String::FindLastChar(TEXT("AbCABCAbCABC"), TEXT('D'), ESearchCase::IgnoreCase), INDEX_NONE);
	TestEqual(TEXT("FindLastChar"), UE::String::FindLastChar(TEXT("AbCABCAbCABD"), TEXT('D')), 11);
	TestEqual(TEXT("FindLastChar"), UE::String::FindLastChar(TEXT("AbCABCAbCABD"), TEXT('d'), ESearchCase::IgnoreCase), 11);

	TestEqual(TEXT("FindLastChar"), UE::String::FindLastChar(TEXT(""), TEXT('A')), INDEX_NONE);
	TestEqual(TEXT("FindLastChar"), UE::String::FindLastChar(TEXT("A"), TEXT('A')), 0);
	TestEqual(TEXT("FindLastChar"), UE::String::FindLastChar(TEXT("A"), TEXT('A'), ESearchCase::IgnoreCase), 0);

	TestEqual(TEXT("FindLastChar"), UE::String::FindLastChar("AbCABCAbCABC", 'B'), 10);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(StringFindFirstOfAnyCharTest, "System.Core.String.FindFirstOfAnyChar", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool StringFindFirstOfAnyCharTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("FindFirstOfAnyChar"), UE::String::FindFirstOfAnyChar(TEXT("AbCABCAbcABC"), {TEXT('b')}), 1);
	TestEqual(TEXT("FindFirstOfAnyChar"), UE::String::FindFirstOfAnyChar(TEXT("AbCABCAbcABC"), {TEXT('c'), TEXT('B')}), 4);
	TestEqual(TEXT("FindFirstOfAnyChar"), UE::String::FindFirstOfAnyChar(TEXT("AbCABCAbcABC"), {TEXT('c'), TEXT('B')}, ESearchCase::IgnoreCase), 1);
	TestEqual(TEXT("FindFirstOfAnyChar"), UE::String::FindFirstOfAnyChar(TEXT("AbCABCAbcABC"), {TEXT('D'), TEXT('a')}), INDEX_NONE);
	TestEqual(TEXT("FindFirstOfAnyChar"), UE::String::FindFirstOfAnyChar(TEXT("AbCABCAbcABC"), {TEXT('E'), TEXT('D')}, ESearchCase::IgnoreCase), INDEX_NONE);
	TestEqual(TEXT("FindFirstOfAnyChar"), UE::String::FindFirstOfAnyChar(TEXT("AbCABCAbcABD"), {TEXT('E'), TEXT('D')}), 11);
	TestEqual(TEXT("FindFirstOfAnyChar"), UE::String::FindFirstOfAnyChar(TEXT("AbCABCAbcABD"), {TEXT('E'), TEXT('d')}, ESearchCase::IgnoreCase), 11);

	TestEqual(TEXT("FindFirstOfAnyChar"), UE::String::FindFirstOfAnyChar(TEXT(""), {TEXT('A'), TEXT('B')}), INDEX_NONE);
	TestEqual(TEXT("FindFirstOfAnyChar"), UE::String::FindFirstOfAnyChar(TEXT("A"), {TEXT('A'), TEXT('B')}), 0);
	TestEqual(TEXT("FindFirstOfAnyChar"), UE::String::FindFirstOfAnyChar(TEXT("A"), {TEXT('A'), TEXT('B')}, ESearchCase::IgnoreCase), 0);

	TestEqual(TEXT("FindFirstOfAnyChar"), UE::String::FindFirstOfAnyChar("AbCABCAbcABC", {'c', 'B'}), 4);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(StringFindLastOfAnyCharTest, "System.Core.String.FindLastOfAnyChar", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool StringFindLastOfAnyCharTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("FindLastOfAnyChar"), UE::String::FindLastOfAnyChar(TEXT("AbCABCAbcABC"), {TEXT('b')}), 7);
	TestEqual(TEXT("FindLastOfAnyChar"), UE::String::FindLastOfAnyChar(TEXT("AbCABCAbcABC"), {TEXT('c'), TEXT('B')}), 10);
	TestEqual(TEXT("FindLastOfAnyChar"), UE::String::FindLastOfAnyChar(TEXT("AbCABCAbcABC"), {TEXT('c'), TEXT('B')}, ESearchCase::IgnoreCase), 11);
	TestEqual(TEXT("FindLastOfAnyChar"), UE::String::FindLastOfAnyChar(TEXT("AbCABCAbcABC"), {TEXT('D'), TEXT('a')}), INDEX_NONE);
	TestEqual(TEXT("FindLastOfAnyChar"), UE::String::FindLastOfAnyChar(TEXT("AbCABCAbcABC"), {TEXT('E'), TEXT('D')}, ESearchCase::IgnoreCase), INDEX_NONE);
	TestEqual(TEXT("FindLastOfAnyChar"), UE::String::FindLastOfAnyChar(TEXT("AbCABCAbcABD"), {TEXT('E'), TEXT('D')}), 11);
	TestEqual(TEXT("FindLastOfAnyChar"), UE::String::FindLastOfAnyChar(TEXT("AbCABCAbcABD"), {TEXT('E'), TEXT('d')}, ESearchCase::IgnoreCase), 11);

	TestEqual(TEXT("FindLastOfAnyChar"), UE::String::FindLastOfAnyChar(TEXT(""), {TEXT('A'), TEXT('B')}), INDEX_NONE);
	TestEqual(TEXT("FindLastOfAnyChar"), UE::String::FindLastOfAnyChar(TEXT("A"), {TEXT('A'), TEXT('B')}), 0);
	TestEqual(TEXT("FindLastOfAnyChar"), UE::String::FindLastOfAnyChar(TEXT("A"), {TEXT('A'), TEXT('B')}, ESearchCase::IgnoreCase), 0);

	TestEqual(TEXT("FindLastOfAnyChar"), UE::String::FindLastOfAnyChar("AbCABCAbcABC", {'c', 'B'}), 10);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
