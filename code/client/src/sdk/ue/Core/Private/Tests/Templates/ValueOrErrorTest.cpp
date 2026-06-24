// Copyright Epic Games, Inc. All Rights Reserved.

#include "Templates/ValueOrError.h"

#include "Misc/AutomationTest.h"

#include <type_traits>

static_assert(!std::is_constructible<TValueOrError<int, int>>::value, "Expected no default constructor.");

static_assert(std::is_copy_constructible<TValueOrError<int, int>>::value, "Missing copy constructor.");
static_assert(std::is_move_constructible<TValueOrError<int, int>>::value, "Missing move constructor.");

static_assert(std::is_copy_assignable<TValueOrError<int, int>>::value, "Missing copy assignment.");
static_assert(std::is_move_assignable<TValueOrError<int, int>>::value, "Missing move assignment.");

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(TValueOrErrorTest, "System.Core.Templates.TValueOrError", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)
bool TValueOrErrorTest::RunTest(const FString& Parameters)
{
	static int ValueCount = 0;
	static int ErrorCount = 0;

	struct FTestValue
	{
		FTestValue() { Value = ++ValueCount; }
		FTestValue(const FTestValue&) { Value = ++ValueCount; }
		FTestValue(FTestValue&& Other) { Value = Other.Value; ++ValueCount; }
		FTestValue(int InValue1, int InValue2, int InValue3) { Value = InValue1 + InValue2 + InValue3; ++ValueCount; }
		~FTestValue() { --ValueCount; }
		FTestValue& operator=(const FTestValue& Other) = delete;
		FTestValue& operator=(FTestValue&& Other) = delete;
		int Value;
	};

	struct FTestError
	{
		FTestError() { Error = ++ErrorCount; }
		FTestError(const FTestError&) { Error = ++ErrorCount; }
		FTestError(FTestError&& Other) { Error = Other.Error; ++ErrorCount; }
		FTestError(int InError1, int InError2) { Error = InError1 + InError2; ++ErrorCount; }
		~FTestError() { --ErrorCount; }
		FTestError& operator=(const FTestError& Other) = delete;
		FTestError& operator=(FTestError&& Other) = delete;
		int Error;
	};

	struct FTestMoveOnly
	{
		FTestMoveOnly() = default;
		FTestMoveOnly(const FTestMoveOnly&) = delete;
		FTestMoveOnly(FTestMoveOnly&&) = default;
		int Value = 0;
	};

	using FTestType = TValueOrError<FTestValue, FTestError>;

	// Test MakeValue Move
	{
		FTestType ValueOrError = MakeValue(FTestValue());
		TestEqual(TEXT("TValueOrError MakeValue Move Construct Count"), ValueCount, 1);
		TestEqual(TEXT("TValueOrError MakeValue Move TryGetValue"), ValueOrError.TryGetValue(), &ValueOrError.GetValue());
		TestEqual(TEXT("TValueOrError MakeValue Move GetValue"), ValueOrError.GetValue().Value, 1);
		TestFalse(TEXT("TValueOrError MakeValue Move HasError"), ValueOrError.HasError());
		TestTrue(TEXT("TValueOrError MakeValue Move HasValue"), ValueOrError.HasValue());
		TestTrue(TEXT("TValueOrError MakeValue Move TryGetError"), ValueOrError.TryGetError() == nullptr);
	}
	TestEqual(TEXT("TValueOrError MakeValue Move Destruct Count"), ValueCount, 0);

	// Test MakeValue Proxy
	{
		FTestType ValueOrError = MakeValue(2, 6, 8);
		TestEqual(TEXT("TValueOrError MakeValue Proxy Construct Count"), ValueCount, 1);
		TestEqual(TEXT("TValueOrError MakeValue Proxy TryGetValue"), ValueOrError.TryGetValue(), &ValueOrError.GetValue());
		TestEqual(TEXT("TValueOrError MakeValue Proxy GetValue"), ValueOrError.GetValue().Value, 16);
		TestFalse(TEXT("TValueOrError MakeValue Proxy HasError"), ValueOrError.HasError());
		TestTrue(TEXT("TValueOrError MakeValue Proxy HasValue"), ValueOrError.HasValue());
		TestTrue(TEXT("TValueOrError MakeValue Proxy TryGetError"), ValueOrError.TryGetError() == nullptr);
	}
	TestEqual(TEXT("TValueOrError MakeValue Proxy Destruct Count"), ValueCount, 0);

	// Test StealValue
	{
		FTestType ValueOrError = MakeValue(FTestValue());
		FTestValue Value = ValueOrError.StealValue();
		TestEqual(TEXT("TValueOrError StealValue Construct Count"), ValueCount, 1);
		TestEqual(TEXT("TValueOrError StealValue GetValue"), Value.Value, 1);
		TestFalse(TEXT("TValueOrError StealValue HasError"), ValueOrError.HasError());
		TestFalse(TEXT("TValueOrError StealValue HasValue"), ValueOrError.HasValue());
	}
	TestEqual(TEXT("TValueOrError StealValue Destruct Count"), ValueCount, 0);

	// Test MakeError Move
	{
		FTestType ValueOrError = MakeError(FTestError());
		TestEqual(TEXT("TValueOrError MakeError Move Construct Count"), ErrorCount, 1);
		TestEqual(TEXT("TValueOrError MakeError Move TryGetError"), ValueOrError.TryGetError(), &ValueOrError.GetError());
		TestEqual(TEXT("TValueOrError MakeError Move GetError"), ValueOrError.GetError().Error, 1);
		TestFalse(TEXT("TValueOrError MakeError Move HasValue"), ValueOrError.HasValue());
		TestTrue(TEXT("TValueOrError MakeError Move HasError"), ValueOrError.HasError());
		TestTrue(TEXT("TValueOrError MakeError Move TryGetValue"), ValueOrError.TryGetValue() == nullptr);
	}
	TestEqual(TEXT("TValueOrError MakeError Move Destruct Count"), ErrorCount, 0);

	// Test MakeError Proxy
	{
		FTestType ValueOrError = MakeError(4, 12);
		TestEqual(TEXT("TValueOrError MakeError Proxy Construct Count"), ErrorCount, 1);
		TestEqual(TEXT("TValueOrError MakeError Proxy TryGetError"), ValueOrError.TryGetError(), &ValueOrError.GetError());
		TestEqual(TEXT("TValueOrError MakeError Proxy GetError"), ValueOrError.GetError().Error, 16);
		TestFalse(TEXT("TValueOrError MakeError Proxy HasValue"), ValueOrError.HasValue());
		TestTrue(TEXT("TValueOrError MakeError Proxy HasError"), ValueOrError.HasError());
		TestTrue(TEXT("TValueOrError MakeError Proxy TryGetValue"), ValueOrError.TryGetValue() == nullptr);
	}
	TestEqual(TEXT("TValueOrError MakeError Proxy Destruct Count"), ErrorCount, 0);

	// Test StealError
	{
		FTestType ValueOrError = MakeError();
		FTestError Error = ValueOrError.StealError();
		TestEqual(TEXT("TValueOrError StealError Construct Count"), ErrorCount, 1);
		TestEqual(TEXT("TValueOrError StealError GetError"), Error.Error, 1);
		TestFalse(TEXT("TValueOrError StealError HasValue"), ValueOrError.HasValue());
		TestFalse(TEXT("TValueOrError StealError HasError"), ValueOrError.HasError());
	}
	TestEqual(TEXT("TValueOrError StealError Destruct Count"), ErrorCount, 0);

	// Test Assignment
	{
		FTestType ValueOrError = MakeValue();
		ValueOrError = MakeValue();
		TestEqual(TEXT("TValueOrError Assignment Value Count 1"), ValueCount, 1);
		TestEqual(TEXT("TValueOrError Assignment Value GetValue 2"), ValueOrError.GetValue().Value, 2);
		TestEqual(TEXT("TValueOrError Assignment Error Count 0"), ErrorCount, 0);
		ValueOrError = MakeError();
		TestEqual(TEXT("TValueOrError Assignment Value Count 0"), ValueCount, 0);
		TestEqual(TEXT("TValueOrError Assignment Error Count 1"), ErrorCount, 1);
		ValueOrError = MakeError();
		TestEqual(TEXT("TValueOrError Assignment Value Count 0"), ValueCount, 0);
		TestEqual(TEXT("TValueOrError Assignment Value GetError 2"), ValueOrError.GetError().Error, 2);
		TestEqual(TEXT("TValueOrError Assignment Error Count 1"), ErrorCount, 1);
		ValueOrError = MakeValue();
		TestEqual(TEXT("TValueOrError Assignment Value Count 1"), ValueCount, 1);
		TestEqual(TEXT("TValueOrError Assignment Error Count 0"), ErrorCount, 0);
		FTestType UnsetValueOrError = MakeValue();
		UnsetValueOrError.StealValue();
		ValueOrError = MoveTemp(UnsetValueOrError);
		TestEqual(TEXT("TValueOrError Assignment Value Count 0"), ValueCount, 0);
		TestEqual(TEXT("TValueOrError Assignment Error Count 0"), ErrorCount, 0);
		TestFalse(TEXT("TValueOrError Assignment HasValue"), ValueOrError.HasValue());
		TestFalse(TEXT("TValueOrError Assignment HasError"), ValueOrError.HasError());
	}
	TestEqual(TEXT("TValueOrError Assignment Value Count 0"), ValueCount, 0);
	TestEqual(TEXT("TValueOrError Assignment Error Count 0"), ErrorCount, 0);

	// Test Move-Only Value/Error
	{
		TValueOrError<FTestMoveOnly, FTestMoveOnly> Value = MakeValue(FTestMoveOnly());
		TValueOrError<FTestMoveOnly, FTestMoveOnly> Error = MakeError(FTestMoveOnly());
		FTestMoveOnly MovedValue = MoveTemp(Value).GetValue();
		FTestMoveOnly MovedError = MoveTemp(Error).GetError();
	}

	// Test Integer Value/Error
	{
		TValueOrError<int32, int32> ValueOrError = MakeValue();
		TestEqual(TEXT("TValueOrError Integer Error Zero"), ValueOrError.GetValue(), 0);
		ValueOrError = MakeValue(1);
		TestEqual(TEXT("TValueOrError Integer Value One"), ValueOrError.GetValue(), 1);
		ValueOrError = MakeError();
		TestEqual(TEXT("TValueOrError Integer Error Zero"), ValueOrError.GetError(), 0);
		ValueOrError = MakeError(1);
		TestEqual(TEXT("TValueOrError Integer Error One"), ValueOrError.GetError(), 1);
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
