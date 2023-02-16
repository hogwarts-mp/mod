// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreTypes.h"
#include "Misc/AutomationTest.h"
#include "Misc/AssertionMacros.h"
#include "Containers/StringView.h"
#include "Containers/UnrealString.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStringSanitizeFloatTest, "System.Core.String.SanitizeFloat", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FStringSanitizeFloatTest::RunTest(const FString& Parameters)
{
	auto DoTest = [this](const double InVal, const int32 InMinFractionalDigits, const FString& InExpected)
	{
		const FString Result = FString::SanitizeFloat(InVal, InMinFractionalDigits);
		if (!Result.Equals(InExpected, ESearchCase::CaseSensitive))
		{
			AddError(FString::Printf(TEXT("%f (%d digits) failure: result '%s' (expected '%s')"), InVal, InMinFractionalDigits, *Result, *InExpected));
		}
	};

	DoTest(+0.0, 0, TEXT("0"));
	DoTest(-0.0, 0, TEXT("0"));

	DoTest(+100.0000, 0, TEXT("100"));
	DoTest(+100.1000, 0, TEXT("100.1"));
	DoTest(+100.1010, 0, TEXT("100.101"));
	DoTest(-100.0000, 0, TEXT("-100"));
	DoTest(-100.1000, 0, TEXT("-100.1"));
	DoTest(-100.1010, 0, TEXT("-100.101"));

	DoTest(+100.0000, 1, TEXT("100.0"));
	DoTest(+100.1000, 1, TEXT("100.1"));
	DoTest(+100.1010, 1, TEXT("100.101"));
	DoTest(-100.0000, 1, TEXT("-100.0"));
	DoTest(-100.1000, 1, TEXT("-100.1"));
	DoTest(-100.1010, 1, TEXT("-100.101"));

	DoTest(+100.0000, 4, TEXT("100.0000"));
	DoTest(+100.1000, 4, TEXT("100.1000"));
	DoTest(+100.1010, 4, TEXT("100.1010"));
	DoTest(-100.0000, 4, TEXT("-100.0000"));
	DoTest(-100.1000, 4, TEXT("-100.1000"));
	DoTest(-100.1010, 4, TEXT("-100.1010"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStringAppendIntTest, "System.Core.String.AppendInt", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FStringAppendIntTest::RunTest(const FString& Parameters)
{
	auto DoTest = [this](const TCHAR* Call, const FString& Result, const TCHAR* InExpected)
	{
		if (!Result.Equals(InExpected, ESearchCase::CaseSensitive))
		{
			AddError(FString::Printf(TEXT("'%s' failure: result '%s' (expected '%s')"), Call, *Result, InExpected));
		}
	};

	{
		FString Zero;
		Zero.AppendInt(0);
		DoTest(TEXT("AppendInt(0)"), Zero, TEXT("0"));
	}

	{
		FString IntMin;
		IntMin.AppendInt(MIN_int32);
		DoTest(TEXT("AppendInt(MIN_int32)"), IntMin, TEXT("-2147483648"));
	}

	{
		FString IntMin;
		IntMin.AppendInt(MAX_int32);
		DoTest(TEXT("AppendInt(MAX_int32)"), IntMin, TEXT("2147483647"));
	}

	{
		FString AppendMultipleInts;
		AppendMultipleInts.AppendInt(1);
		AppendMultipleInts.AppendInt(-2);
		AppendMultipleInts.AppendInt(3);
		DoTest(TEXT("AppendInt(1);AppendInt(-2);AppendInt(3)"), AppendMultipleInts, TEXT("1-23"));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStringUnicodeTest, "System.Core.String.Unicode", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FStringUnicodeTest::RunTest(const FString& Parameters)
{
	auto DoTest = [this](const TCHAR* Call, const FString& Result, const TCHAR* InExpected)
	{
		if (!Result.Equals(InExpected, ESearchCase::CaseSensitive))
		{
			AddError(FString::Printf(TEXT("'%s' failure: result '%s' (expected '%s')"), Call, *Result, InExpected));
		}
	};

	// Test data used to verify basic processing of a Unicode character outside the BMP
	FString TestStr;
	if (FUnicodeChar::CodepointToString(128512, TestStr))
	{
		// Verify that the string can be serialized and deserialized without losing any data
		{
			TArray<uint8> StringData;
			FString FromArchive = TestStr;

			FMemoryWriter Writer(StringData);
			Writer << FromArchive;

			FromArchive.Reset();
			FMemoryReader Reader(StringData);
			Reader << FromArchive;

			DoTest(TEXT("FromArchive"), FromArchive, *TestStr);
		}

		// Verify that the string can be converted from/to UTF-8 without losing any data
		{
			const FString FromUtf8 = UTF8_TO_TCHAR(TCHAR_TO_UTF8(*TestStr));
			DoTest(TEXT("FromUtf8"), FromUtf8, *TestStr);
		}

		// Verify that the string can be converted from/to UTF-16 without losing any data
		{
			const FString FromUtf16 = UTF16_TO_TCHAR(TCHAR_TO_UTF16(*TestStr));
			DoTest(TEXT("FromUtf16"), FromUtf16, *TestStr);
		}
	}


	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLexTryParseStringTest, "System.Core.Misc.LexTryParseString", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)

bool FLexTryParseStringTest::RunTest(const FString& Parameters)
{
	// Test that LexFromString can intepret all the numerical formats we expect it to
	{
		// Test float values

		float Value;

		// Basic numbers
		TestTrue(TEXT("(float conversion from string) basic numbers"), LexTryParseString(Value, (TEXT("1"))) && Value == 1);
		TestTrue(TEXT("(float conversion from string) basic numbers"), LexTryParseString(Value, (TEXT("1.0"))) && Value == 1);
		TestTrue(TEXT("(float conversion from string) basic numbers"), LexTryParseString(Value, (TEXT(".5"))) && Value == 0.5);
		TestTrue(TEXT("(float conversion from string) basic numbers"), LexTryParseString(Value, (TEXT("1."))) && Value == 1);

		// Variations of 0
		TestTrue(TEXT("(float conversion from string) variations of 0"), LexTryParseString(Value, (TEXT("0"))) && Value == 0);
		TestTrue(TEXT("(float conversion from string) variations of 0"), LexTryParseString(Value, (TEXT("-0"))) && Value == 0);
		TestTrue(TEXT("(float conversion from string) variations of 0"), LexTryParseString(Value, (TEXT("0.0"))) && Value == 0);
		TestTrue(TEXT("(float conversion from string) variations of 0"), LexTryParseString(Value, (TEXT(".0"))) && Value == 0);
		TestTrue(TEXT("(float conversion from string) variations of 0"), LexTryParseString(Value, (TEXT("0."))) && Value == 0);
		TestTrue(TEXT("(float conversion from string) variations of 0"), LexTryParseString(Value, (TEXT("0. 111"))) && Value == 0);

		// Scientific notation
		TestTrue(TEXT("(float conversion from string) scientific notation"), LexTryParseString(Value, (TEXT("1.0e+10"))) && Value == 1.0e+10f);
		TestTrue(TEXT("(float conversion from string) scientific notation"), LexTryParseString(Value, (TEXT("1.99999999e-11"))) && Value == 1.99999999e-11f);
		TestTrue(TEXT("(float conversion from string) scientific notation"), LexTryParseString(Value, (TEXT("1e+10"))) && Value == 1e+10f);

		// Non-finite special numbers
		TestTrue(TEXT("(float conversion from string) inf"), LexTryParseString(Value, (TEXT("inf"))));
		TestTrue(TEXT("(float conversion from string) nan"), LexTryParseString(Value, (TEXT("nan"))));
		TestTrue(TEXT("(float conversion from string) nan(ind)"), LexTryParseString(Value, (TEXT("nan(ind)"))));

		// nan/inf etc. are detected from the start of the string, regardless of any other characters that come afterwards
		TestTrue(TEXT("(float conversion from string) nananananananana"), LexTryParseString(Value, (TEXT("nananananananana"))));
		TestTrue(TEXT("(float conversion from string) nan(ind)!"), LexTryParseString(Value, (TEXT("nan(ind)!"))));
		TestTrue(TEXT("(float conversion from string) infinity"), LexTryParseString(Value, (TEXT("infinity"))));

		// Some numbers with whitespace
		TestTrue(TEXT("(float conversion from string) whitespace"), LexTryParseString(Value, (TEXT("   2.5   "))) && Value == 2.5);
		TestTrue(TEXT("(float conversion from string) whitespace"), LexTryParseString(Value, (TEXT("\t3.0\t"))) && Value == 3.0);
		TestTrue(TEXT("(float conversion from string) whitespace"), LexTryParseString(Value, (TEXT("4.0   \t"))) && Value == 4.0);
		TestTrue(TEXT("(float conversion from string) whitespace"), LexTryParseString(Value, (TEXT("\r\n5.25"))) && Value == 5.25);
		TestTrue(TEXT("(float conversion from string) whitespace"), LexTryParseString(Value, (TEXT(" 6 . 2 "))) && Value == 6.0);
		TestTrue(TEXT("(float conversion from string) whitespace"), LexTryParseString(Value, (TEXT(" 56 . 2 "))) && Value == 56.0);
		TestTrue(TEXT("(float conversion from string) whitespace"), LexTryParseString(Value, (TEXT(" 5 6 . 2 "))) && Value == 5.0);

		// Failure cases
		TestFalse(TEXT("(float no conversion from string) not a number"), LexTryParseString(Value, (TEXT("not a number"))));
		TestFalse(TEXT("(float no conversion from string) <empty string>"), LexTryParseString(Value, (TEXT(""))));
		TestFalse(TEXT("(float conversion from string) ."), LexTryParseString(Value, (TEXT("."))));
	}

	{
		// Test integer values

		int32 Value;

		// Basic numbers
		TestTrue(TEXT("(int32 conversion from string) basic numbers"), LexTryParseString(Value, (TEXT("1"))) && Value == 1);
		TestTrue(TEXT("(int32 conversion from string) basic numbers"), LexTryParseString(Value, (TEXT("1.0"))) && Value == 1);
		TestTrue(TEXT("(int32 conversion from string) basic numbers"), LexTryParseString(Value, (TEXT("3.1"))) && Value == 3);
		TestTrue(TEXT("(int32 conversion from string) basic numbers"), LexTryParseString(Value, (TEXT("0.5"))) && Value == 0);
		TestTrue(TEXT("(int32 conversion from string) basic numbers"), LexTryParseString(Value, (TEXT("1."))) && Value == 1);

		// Variations of 0
		TestTrue(TEXT("(int32 conversion from string) variations of 0"), LexTryParseString(Value, (TEXT("0"))) && Value == 0);
		TestTrue(TEXT("(int32 conversion from string) variations of 0"), LexTryParseString(Value, (TEXT("0.0"))) && Value == 0);
		TestFalse(TEXT("(int32 conversion from string) variations of 0"), LexTryParseString(Value, (TEXT(".0"))) && Value == 0);
		TestTrue(TEXT("(int32 conversion from string) variations of 0"), LexTryParseString(Value, (TEXT("0."))) && Value == 0);

		// Scientific notation
		TestTrue(TEXT("(int32 conversion from string) scientific notation"), LexTryParseString(Value, (TEXT("1.0e+10"))) && Value == 1);
		TestTrue(TEXT("(int32 conversion from string) scientific notation"), LexTryParseString(Value, (TEXT("6.0e-10"))) && Value == 6);
		TestTrue(TEXT("(int32 conversion from string) scientific notation"), LexTryParseString(Value, (TEXT("0.0e+10"))) && Value == 0);
		TestTrue(TEXT("(int32 conversion from string) scientific notation"), LexTryParseString(Value, (TEXT("0.0e-10"))) && Value == 0);
		TestTrue(TEXT("(int32 conversion from string) scientific notation"), LexTryParseString(Value, (TEXT("3e+10"))) && Value == 3);
		TestTrue(TEXT("(int32 conversion from string) scientific notation"), LexTryParseString(Value, (TEXT("4e-10"))) && Value == 4);

		// Some numbers with whitespace
		TestTrue(TEXT("(int32 conversion from string) whitespace"), LexTryParseString(Value, (TEXT("   2.5   "))) && Value == 2);
		TestTrue(TEXT("(int32 conversion from string) whitespace"), LexTryParseString(Value, (TEXT("\t3.0\t"))) && Value == 3);
		TestTrue(TEXT("(int32 conversion from string) whitespace"), LexTryParseString(Value, (TEXT("4.0   \t"))) && Value == 4);
		TestTrue(TEXT("(int32 conversion from string) whitespace"), LexTryParseString(Value, (TEXT("\r\n5.25"))) && Value == 5);
		TestTrue(TEXT("(int32 conversion from string) whitespace"), LexTryParseString(Value, (TEXT(" 6 . 2 "))) && Value == 6);
		TestTrue(TEXT("(int32 conversion from string) whitespace"), LexTryParseString(Value, (TEXT(" 56 . 2 "))) && Value == 56);
		TestTrue(TEXT("(int32 conversion from string) whitespace"), LexTryParseString(Value, (TEXT(" 5 6 . 2 "))) && Value == 5);

		// Non-finite special numbers. All shouldn't parse into an int
		TestFalse(TEXT("(int32 no conversion from string) inf"), LexTryParseString(Value, (TEXT("inf"))));
		TestFalse(TEXT("(int32 no conversion from string) nan"), LexTryParseString(Value, (TEXT("nan"))));
		TestFalse(TEXT("(int32 no conversion from string) nan(ind)"), LexTryParseString(Value, (TEXT("nan(ind)"))));
		TestFalse(TEXT("(int32 no conversion from string) nananananananana"), LexTryParseString(Value, (TEXT("nananananananana"))));
		TestFalse(TEXT("(int32 no conversion from string) nan(ind)!"), LexTryParseString(Value, (TEXT("nan(ind)!"))));
		TestFalse(TEXT("(int32 no conversion from string) infinity"), LexTryParseString(Value, (TEXT("infinity"))));
		TestFalse(TEXT("(float no conversion from string) ."), LexTryParseString(Value, (TEXT("."))));
		TestFalse(TEXT("(float no conversion from string) <empyty string>"), LexTryParseString(Value, (TEXT(""))));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStringSubstringTest, "System.Core.String.Substring", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FStringSubstringTest::RunTest(const FString& Parameters)
{
	auto DoTest = [this](const TCHAR* Call, const FString& Result, const TCHAR* InExpected)
	{
		if (!Result.Equals(InExpected, ESearchCase::CaseSensitive))
		{
			AddError(FString::Printf(TEXT("'%s' failure: result '%s' (expected '%s')"), Call, *Result, InExpected));
		}
	};

	const FString TestString(TEXT("0123456789"));

#define SUBSTRINGTEST(TestName, ExpectedResult, Operation, ...) \
	FString TestName = TestString.Operation(__VA_ARGS__); \
	DoTest(TEXT(#TestName), TestName, ExpectedResult); \
	\
	FString Inline##TestName = TestString; \
	Inline##TestName.Operation##Inline(__VA_ARGS__); \
	DoTest(TEXT("Inline" #TestName), Inline##TestName, ExpectedResult); 

	// Left
	SUBSTRINGTEST(Left, TEXT("0123"), Left, 4);
	SUBSTRINGTEST(ExactLengthLeft, *TestString, Left, 10);
	SUBSTRINGTEST(LongerThanLeft, *TestString, Left, 20);
	SUBSTRINGTEST(ZeroLeft, TEXT(""), Left, 0);
	SUBSTRINGTEST(NegativeLeft, TEXT(""), Left, -1);

	// LeftChop
	SUBSTRINGTEST(LeftChop, TEXT("012345"), LeftChop, 4);
	SUBSTRINGTEST(ExactLengthLeftChop, TEXT(""), LeftChop, 10);
	SUBSTRINGTEST(LongerThanLeftChop, TEXT(""), LeftChop, 20);
	SUBSTRINGTEST(ZeroLeftChop, *TestString, LeftChop, 0);
	SUBSTRINGTEST(NegativeLeftChop, *TestString, LeftChop, -1);

	// Right
	SUBSTRINGTEST(Right, TEXT("6789"), Right, 4);
	SUBSTRINGTEST(ExactLengthRight, *TestString, Right, 10);
	SUBSTRINGTEST(LongerThanRight, *TestString, Right, 20);
	SUBSTRINGTEST(ZeroRight, TEXT(""), Right, 0);
	SUBSTRINGTEST(NegativeRight, TEXT(""), Right, -1);

	// RightChop
	SUBSTRINGTEST(RightChop, TEXT("456789"), RightChop, 4);
	SUBSTRINGTEST(ExactLengthRightChop, TEXT(""), RightChop, 10);
	SUBSTRINGTEST(LongerThanRightChop, TEXT(""), RightChop, 20);
	SUBSTRINGTEST(ZeroRightChop, *TestString, RightChop, 0);
	SUBSTRINGTEST(NegativeRightChop, *TestString, RightChop, -1);

	// Mid
	SUBSTRINGTEST(Mid, TEXT("456789"), Mid, 4);
	SUBSTRINGTEST(MidCount, TEXT("4567"), Mid, 4, 4);
	SUBSTRINGTEST(MidCountFullLength, *TestString, Mid, 0, 10);
	SUBSTRINGTEST(MidCountOffEnd, TEXT("89"), Mid, 8, 4);
	SUBSTRINGTEST(MidStartAfterEnd, TEXT(""), Mid, 20);
	SUBSTRINGTEST(MidZeroCount, TEXT(""), Mid, 5, 0);
	SUBSTRINGTEST(MidNegativeCount, TEXT(""), Mid, 5, -1);
	SUBSTRINGTEST(MidNegativeStartNegativeEnd, TEXT(""), Mid, -5, 1);
	SUBSTRINGTEST(MidNegativeStartPositiveEnd, TEXT("012"), Mid, -1, 4);
	SUBSTRINGTEST(MidNegativeStartBeyondEnd, *TestString, Mid, -1, 15);

#undef SUBSTRINGTEST

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStringFromStringViewTest, "System.Core.String.FromStringView", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FStringFromStringViewTest::RunTest(const FString& Parameters)
{
	// Verify basic construction and assignment from a string view.
	{
		const TCHAR* Literal = TEXT("Literal");
		TestEqual(TEXT("String(StringView)"), FString(FStringView(Literal)), Literal);
		TestEqual(TEXT("String = StringView"), FString(TEXT("Temp")) = FStringView(Literal), Literal);

		FStringView EmptyStringView;
		FString EmptyString(EmptyStringView);
		TestTrue(TEXT("String(EmptyStringView)"), EmptyString.IsEmpty());
		TestTrue(TEXT("String(EmptyStringView) (No Allocation)"), EmptyString.GetAllocatedSize() == 0);

		EmptyString = TEXT("Temp");
		EmptyString = EmptyStringView;
		TestTrue(TEXT("String = EmptyStringView"), EmptyString.IsEmpty());
		TestTrue(TEXT("String = EmptyStringView (No Allocation)"), EmptyString.GetAllocatedSize() == 0);
	}

	// Verify assignment from a view of itself.
	{
		FString AssignEntireString(TEXT("AssignEntireString"));
		AssignEntireString = FStringView(AssignEntireString);
		TestEqual(TEXT("String = StringView(String)"), AssignEntireString, TEXT("AssignEntireString"));

		FString AssignStartOfString(TEXT("AssignStartOfString"));
		AssignStartOfString = FStringView(AssignStartOfString).Left(11);
		TestEqual(TEXT("String = StringView(String).Left"), AssignStartOfString, TEXT("AssignStart"));

		FString AssignEndOfString(TEXT("AssignEndOfString"));
		AssignEndOfString = FStringView(AssignEndOfString).Right(11);
		TestEqual(TEXT("String = StringView(String).Right"), AssignEndOfString, TEXT("EndOfString"));

		FString AssignMiddleOfString(TEXT("AssignMiddleOfString"));
		AssignMiddleOfString = FStringView(AssignMiddleOfString).Mid(6, 6);
		TestEqual(TEXT("String = StringView(String).Mid"), AssignMiddleOfString, TEXT("Middle"));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStringConstructWithSlackTest, "System.Core.String.ConstructWithSlack", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FStringConstructWithSlackTest::RunTest(const FString& Parameters)
{
	// Note that the total capacity of a string might be greater than the string length + slack + a null terminator due to
	// underlying malloc implementations which is why we poll FMemory to see what size of allocation we should be expecting.

	// Test creating from a valid string with various valid slack value
	{
		const TCHAR* TestString = TEXT("FooBar");
		const char* TestAsciiString = "FooBar";
		const int32 LengthOfString = TCString<TCHAR>::Strlen(TestString);
		const int32 ExtraSlack = 32;
		const int32 NumElements = LengthOfString + ExtraSlack + 1;

		const SIZE_T ExpectedCapacity = FMemory::QuantizeSize(NumElements * sizeof(TCHAR));

		FString StringFromTChar(TestString, ExtraSlack);
		TestEqual(TEXT("(TCHAR: Valid string with valid slack) resulting capacity"), StringFromTChar.GetAllocatedSize(), ExpectedCapacity);

		FString StringFromAscii(TestAsciiString, ExtraSlack);
		TestEqual(TEXT("(ASCII: Valid string with valid slack) resulting capacity"), StringFromAscii.GetAllocatedSize(), ExpectedCapacity);

		FString StringFromFStringView(FStringView(TestString), ExtraSlack);
		TestEqual(TEXT("(FStringView: Valid string with valid slack) resulting capacity"), StringFromFStringView.GetAllocatedSize(), ExpectedCapacity);

		FString StringFromFString(FString(TestString), ExtraSlack);
		TestEqual(TEXT("(FString: Valid string with valid slack"), StringFromFString.GetAllocatedSize(), ExpectedCapacity);
	}

	// Test creating from a valid string with a zero slack value
	{
		const TCHAR* TestString = TEXT("FooBar");
		const char* TestAsciiString = "FooBar";
		const int32 LengthOfString = TCString<TCHAR>::Strlen(TestString);
		const int32 ExtraSlack = 0;
		const int32 NumElements = LengthOfString + ExtraSlack + 1;

		const SIZE_T ExpectedCapacity = FMemory::QuantizeSize(NumElements * sizeof(TCHAR));

		FString StringFromTChar(TestString, ExtraSlack);
		TestEqual(TEXT("(TCHAR: Valid string with zero slack) resulting capacity"), StringFromTChar.GetAllocatedSize(), ExpectedCapacity);

		FString StringFromAscii(TestAsciiString, ExtraSlack);
		TestEqual(TEXT("(ASCII: Valid string with zero slack) resulting capacity"), StringFromAscii.GetAllocatedSize(), ExpectedCapacity);

		FString StringFromFStringView(FStringView(TestString), ExtraSlack);
		TestEqual(TEXT("(FStringView: Valid string with zero slack) resulting capacity"), StringFromFStringView.GetAllocatedSize(), ExpectedCapacity);

		FString StringFromFString(FString(TestString), ExtraSlack);
		TestEqual(TEXT("(FString: Valid string with zero slack"), StringFromFString.GetAllocatedSize(), ExpectedCapacity);
	}

	// Test creating from an empty string with a valid slack value
	{
		const TCHAR* TestString = TEXT("");
		const char* TestAsciiString = "";
		const int32 LengthOfString = TCString<TCHAR>::Strlen(TestString);
		const int32 ExtraSlack = 32;
		const int32 NumElements = LengthOfString + ExtraSlack + 1;

		const SIZE_T ExpectedCapacity = FMemory::QuantizeSize(NumElements * sizeof(TCHAR));

		FString StringFromTChar(TestString, ExtraSlack);
		TestEqual(TEXT("(TCHAR: Empty string with slack) resulting capacity"), StringFromTChar.GetAllocatedSize(), ExpectedCapacity);

		FString StringFromAscii(TestAsciiString, ExtraSlack);
		TestEqual(TEXT("(ASCII: Empty string with slack) resulting capacity"), StringFromAscii.GetAllocatedSize(), ExpectedCapacity);

		FString StringFromFStringView(FStringView(TestString), ExtraSlack);
		TestEqual(TEXT("(FStringView: Empty string with slack) resulting capacity"), StringFromFStringView.GetAllocatedSize(), ExpectedCapacity);

		FString StringFromFString(FString(TestString), ExtraSlack);
		TestEqual(TEXT("(FString: Empty string with slack) resulting capacity"), StringFromFString.GetAllocatedSize(), ExpectedCapacity);
	}

	// Test creating from an empty string with a zero slack value
	{
		const TCHAR* TestString = TEXT("");
		const char* TestAsciiString = "";
		const int32 ExtraSlack = 0;

		const SIZE_T ExpectedCapacity = 0u;

		FString StringFromTChar(TestString, ExtraSlack);
		TestEqual(TEXT("(TCHAR: Empty string with zero slack) resulting capacity"), StringFromTChar.GetAllocatedSize(), ExpectedCapacity);

		FString StringFromAscii(TestAsciiString, ExtraSlack);
		TestEqual(TEXT("(ASCII: Empty string with zero slack) resulting capacity"), StringFromAscii.GetAllocatedSize(), ExpectedCapacity);

		FString StringFromFStringView(FStringView(TestString), ExtraSlack);
		TestEqual(TEXT("(FStringView: Empty string with zero slack) resulting capacity"), StringFromFStringView.GetAllocatedSize(), ExpectedCapacity);

		FString StringFromFString(FString(TestString), ExtraSlack);
		TestEqual(TEXT("(FString: Empty string with zero slack) resulting capacity"), StringFromFString.GetAllocatedSize(), ExpectedCapacity);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStringEqualityTest, "System.Core.String.Equality", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool FStringEqualityTest::RunTest(const FString& Parameters)
{
	auto TestSelfEquality = [this](const TCHAR* A)
	{
		TestTrue(TEXT("Self Equality C string"), FString(A) == A);
		TestTrue(TEXT("Self Equality C string"), A == FString(A));
		TestTrue(TEXT("Self Equality CaseSensitive"), FString(A).Equals(FString(A), ESearchCase::CaseSensitive));
		TestTrue(TEXT("Self Equality IgnoreCase"), FString(A).Equals(FString(A), ESearchCase::IgnoreCase));

		FString Slacker(A);
		Slacker.Reserve(100);
		TestTrue(TEXT("Self Equality slack"), Slacker == FString(A));
	};

	auto TestPairEquality = [this](const TCHAR* A, const TCHAR* B)
	{
		TestEqual(TEXT("Equals CaseSensitive"), FCString::Strcmp(A, B)  == 0, FString(A).Equals(FString(B), ESearchCase::CaseSensitive));
		TestEqual(TEXT("Equals CaseSensitive"), FCString::Strcmp(B, A)  == 0, FString(B).Equals(FString(A), ESearchCase::CaseSensitive));
		TestEqual(TEXT("Equals IgnoreCase"),	FCString::Stricmp(A, B) == 0, FString(A).Equals(FString(B), ESearchCase::IgnoreCase));
		TestEqual(TEXT("Equals IgnoreCase"),	FCString::Stricmp(B, A) == 0, FString(B).Equals(FString(A), ESearchCase::IgnoreCase));
	};

	const TCHAR* Pairs[][2] =	{ {TEXT(""),	TEXT(" ")}
								, {TEXT("a"),	TEXT("A")}
								, {TEXT("aa"),	TEXT("aA")}
								, {TEXT("az"),	TEXT("AZ")}
								, {TEXT("@["),	TEXT("@]")} };

	for (const TCHAR** Pair : Pairs)
	{
		TestSelfEquality(Pair[0]);
		TestSelfEquality(Pair[1]);
		TestPairEquality(Pair[0], Pair[1]);
	}

	return true;	
}

#endif // WITH_DEV_AUTOMATION_TESTS
