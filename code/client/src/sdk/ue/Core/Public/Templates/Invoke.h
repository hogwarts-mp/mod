// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Templates/PointerIsConvertibleFromTo.h"
#include "Templates/UnrealTemplate.h"
#include "Templates/Decay.h"


namespace UE4Invoke_Private
{
	template <
		typename BaseType,
		typename CallableType,
		typename TEnableIf<TPointerIsConvertibleFromTo<typename TDecay<CallableType>::Type, typename TDecay<BaseType>::Type>::Value>::Type* = nullptr
	>
	FORCEINLINE auto DereferenceIfNecessary(CallableType&& Callable) -> decltype((CallableType&&)Callable)
	{
		return (CallableType&&)Callable;
	}

	template <
		typename BaseType,
		typename CallableType,
		typename TEnableIf<!TPointerIsConvertibleFromTo<typename TDecay<CallableType>::Type, typename TDecay<BaseType>::Type>::Value>::Type* = nullptr
	>
	FORCEINLINE auto DereferenceIfNecessary(CallableType&& Callable) -> decltype(*(CallableType&&)Callable)
	{
		return *(CallableType&&)Callable;
	}
}


/**
 * Invokes a callable with a set of arguments.  Allows the following:
 *
 * - Calling a functor object given a set of arguments.
 * - Calling a function pointer given a set of arguments.
 * - Calling a member function given a reference to an object and a set of arguments.
 * - Calling a member function given a pointer (including smart pointers) to an object and a set of arguments.
 * - Projecting via a data member pointer given a reference to an object.
 * - Projecting via a data member pointer given a pointer (including smart pointers) to an object.
 *
 * See: http://en.cppreference.com/w/cpp/utility/functional/invoke
 */
template <typename FuncType, typename... ArgTypes>
FORCEINLINE auto Invoke(FuncType&& Func, ArgTypes&&... Args)
	-> decltype(Forward<FuncType>(Func)(Forward<ArgTypes>(Args)...))
{
	return Forward<FuncType>(Func)(Forward<ArgTypes>(Args)...);
}

template <typename ReturnType, typename ObjType, typename CallableType>
FORCEINLINE auto Invoke(ReturnType ObjType::*pdm, CallableType&& Callable)
	-> decltype(UE4Invoke_Private::DereferenceIfNecessary<ObjType>(Forward<CallableType>(Callable)).*pdm)
{
	return UE4Invoke_Private::DereferenceIfNecessary<ObjType>(Forward<CallableType>(Callable)).*pdm;
}

template <typename ReturnType, typename ObjType, typename... PMFArgTypes, typename CallableType, typename... ArgTypes>
FORCEINLINE auto Invoke(ReturnType (ObjType::*PtrMemFun)(PMFArgTypes...), CallableType&& Callable, ArgTypes&&... Args)
	-> decltype((UE4Invoke_Private::DereferenceIfNecessary<ObjType>(Forward<CallableType>(Callable)).*PtrMemFun)(Forward<ArgTypes>(Args)...))
{
	return (UE4Invoke_Private::DereferenceIfNecessary<ObjType>(Forward<CallableType>(Callable)).*PtrMemFun)(Forward<ArgTypes>(Args)...);
}

template <typename ReturnType, typename ObjType, typename... PMFArgTypes, typename CallableType, typename... ArgTypes>
FORCEINLINE auto Invoke(ReturnType (ObjType::*PtrMemFun)(PMFArgTypes...) const, CallableType&& Callable, ArgTypes&&... Args)
	-> decltype((UE4Invoke_Private::DereferenceIfNecessary<ObjType>(Forward<CallableType>(Callable)).*PtrMemFun)(Forward<ArgTypes>(Args)...))
{
	return (UE4Invoke_Private::DereferenceIfNecessary<ObjType>(Forward<CallableType>(Callable)).*PtrMemFun)(Forward<ArgTypes>(Args)...);
}


/**
 * Wraps up a named non-member function so that it can easily be passed as a callable.
 * This allows functions with overloads or default arguments to be treated correctly.
 *
 * Example:
 *
 * TArray<FMyType> Array = ...;
 *
 * // Doesn't compile, because you can't take the address of an overloaded function when its type needs to be deduced. 
 * Algo::SortBy(Array, &LexToString);
 *
 * // Works as expected
 * Algo::SortBy(Array, UE_PROJECTION(LexToString));
 */
#define UE_PROJECTION(FuncName) \
	[](auto&&... Args) -> decltype(auto) \
	{ \
		return FuncName(Forward<decltype(Args)>(Args)...); \
	}

/**
 * Wraps up a named member function so that it can easily be passed as a callable.
 * This allows functions with overloads or default arguments to be treated correctly.
 *
 * Example:
 *
 * TArray<UObject*> Array = ...;
 *
 * // Doesn't compile, because &UObject::GetFullName loses the default argument and passes
 * // FString (UObject::*)(const UObject*) to Algo::SortBy<>(), which is not a valid projection.
 * Algo::SortBy(Array, &UObject::GetFullName);
 *
 * // Works as expected
 * Algo::SortBy(Array, UE_PROJECTION_MEMBER(UObject, GetFullName));
 */
#define UE_PROJECTION_MEMBER(Type, FuncName) \
	[](auto&& Obj, auto&&... Args) -> decltype(auto) \
	{ \
		return UE4Invoke_Private::DereferenceIfNecessary<Type>(Forward<decltype(Obj)>(Obj)).FuncName(Forward<decltype(Args)>(Args)...); \
	}

#define PROJECTION(FuncName)              DEPRECATED_MACRO(4.26, "The PROJECTION macro is deprecated, please use UE_PROJECTION instead.") UE_PROJECTION(FuncName)
#define PROJECTION_MEMBER(Type, FuncName) DEPRECATED_MACRO(4.26, "The PROJECTION_MEMBER macro is deprecated, please use UE_PROJECTION_MEMBER instead.") UE_PROJECTION_MEMBER(Type, FuncName)

namespace UE4Invoke_Private
{
	template <typename, typename FuncType, typename... ArgTypes>
	struct TInvokeResult_Impl
	{
	};

	template <typename FuncType, typename... ArgTypes>
	struct TInvokeResult_Impl<decltype((void)Invoke(DeclVal<FuncType>(), DeclVal<ArgTypes>()...)), FuncType, ArgTypes...>
	{
		using Type = decltype(Invoke(DeclVal<FuncType>(), DeclVal<ArgTypes>()...));
	};
}

/**
 * Trait for the type of the result when invoking a callable with the given argument types.
 * Not defined (as thus usable in SFINAE contexts) when the callable cannot be invoked with the given argument types.
 */
template <typename FuncType, typename... ArgTypes>
struct TInvokeResult : UE4Invoke_Private::TInvokeResult_Impl<void, FuncType, ArgTypes...>
{
};

template <typename FuncType, typename... ArgTypes>
using TInvokeResult_T = typename TInvokeResult<FuncType, ArgTypes...>::Type;
