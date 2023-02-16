// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

namespace UE4HasOperatorEquals_Private
{
	struct FNotSpecified {};

	template <typename T>
	struct FReturnValueCheck
	{
		static char (&Func())[2];
	};

	template <>
	struct FReturnValueCheck<FNotSpecified>
	{
		static char (&Func())[1];
	};

	template <typename T>
	FNotSpecified operator==(const T&, const T&);

	template <typename T>
	FNotSpecified operator!=(const T&, const T&);

	template <typename T>
	const T& Make();

	template <typename T>
	struct Equals
	{
		enum { Value = sizeof(FReturnValueCheck<decltype(Make<T>() == Make<T>())>::Func()) == sizeof(char[2]) };
	};

	template <typename T>
	struct NotEquals
	{
		enum { Value = sizeof(FReturnValueCheck<decltype(Make<T>() != Make<T>())>::Func()) == sizeof(char[2]) };
	};
}

/**
 * Traits class which tests if a type has an operator== overload.
 */
template <typename T>
struct UE_DEPRECATED(4.23, "THasOperatorEquals has been deprecated, please use TModels<CEqualityComparable, T> instead.") THasOperatorEquals
{
	enum { Value = UE4HasOperatorEquals_Private::Equals<T>::Value };
};

/**
 * Traits class which tests if a type has an operator!= overload.
 */
template <typename T>
struct UE_DEPRECATED(4.23, "THasOperatorNotEquals has been deprecated, please use TModels<CEqualityComparable, T> instead.") THasOperatorNotEquals
{
	enum { Value = UE4HasOperatorEquals_Private::NotEquals<T>::Value };
};
