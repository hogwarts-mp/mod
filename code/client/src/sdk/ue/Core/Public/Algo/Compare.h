// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Templates/UnrealTemplate.h"

namespace Algo
{
	/**
	* Compares two contiguous containers using operator== to compare pairs of elements.
	*
	* @param  InputA     Container of elements that are used as the first argument to operator==.
	* @param  InputB     Container of elements that are used as the second argument to operator==.
	*
	* @return Whether the containers are the same size and operator== returned true for every pair of elements.
	*/
	template <typename InAT, typename InBT>
	constexpr bool Compare(InAT&& InputA, InBT&& InputB)
	{
		const SIZE_T SizeA = GetNum(InputA);
		const SIZE_T SizeB = GetNum(InputB);

		if (SizeA != SizeB)
		{
			return false;
		}

		auto* A = GetData(InputA);
		auto* B = GetData(InputB);

		for (SIZE_T Count = SizeA; Count; --Count)
		{
			if (!(*A++ == *B++))
			{
				return false;
			}
		}

		return true;
	}

	/**
	* Compares two contiguous containers using a predicate to compare pairs of elements.
	*
	* @param  InputA     Container of elements that are used as the first argument to the predicate.
	* @param  InputB     Container of elements that are used as the second argument to the predicate.
	* @param  Predicate  Condition which returns true for elements which are deemed equal.
	*
	* @return Whether the containers are the same size and the predicate returned true for every pair of elements.
	*/
	template <typename InAT, typename InBT, typename PredicateT>
	constexpr bool CompareByPredicate(InAT&& InputA, InBT&& InputB, PredicateT Predicate)
	{
		const SIZE_T SizeA = GetNum(InputA);
		const SIZE_T SizeB = GetNum(InputB);

		if (SizeA != SizeB)
		{
			return false;
		}

		auto* A = GetData(InputA);
		auto* B = GetData(InputB);

		for (SIZE_T Count = SizeA; Count; --Count)
		{
			if (!Invoke(Predicate, *A++, *B++))
			{
				return false;
			}
		}

		return true;
	}
}
