// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/ArrayView.h"
#include "Containers/StringFwd.h"
#include "Templates/Function.h"

namespace UE
{
namespace String
{
	/**
	 * Visit every token in the input view as separated by any of the delimiters.
	 *
	 * Comparisons with delimiters are case-sensitive and empty tokens are visited.
	 * Behavior is undefined when delimiters overlap each other, such as the delimiters
	 * ("AB, "BC") and the input view "1ABC2".
	 *
	 * @param View The string view to split into tokens.
	 * @param Delimiters An array of non-overlapping non-empty delimiters to split the input string on.
	 * @param Visitor A function that is called for each token.
	 */
	CORE_API void ParseTokensMultiple(const FStringView& View, TArrayView<const FStringView> Delimiters, TFunctionRef<void(FStringView)> Visitor);

	/**
	 * Visit every token in the input view as separated by any of the delimiters.
	 *
	 * Comparisons with delimiters are case-sensitive and empty tokens are visited.
	 *
	 * @param View The string view to split into tokens.
	 * @param Delimiters An array of delimiter characters to split the input string on.
	 * @param Visitor A function that is called for each token.
	 */
	CORE_API void ParseTokensMultiple(const FStringView& View, TArrayView<const TCHAR> Delimiters, TFunctionRef<void(FStringView)> Visitor);

	/**
	 * Visit every token in the input view as separated by the delimiter.
	 *
	 * Comparisons with the delimiter are case-sensitive and empty tokens are visited.
	 *
	 * @param View The string view to split into tokens.
	 * @param Delimiter The non-empty delimiter to split the input string on.
	 * @param Visitor A function that is called for each token.
	 */
	inline void ParseTokens(const FStringView& View, const FStringView& Delimiter, TFunctionRef<void(FStringView)> Visitor)
	{
		return ParseTokensMultiple(View, MakeArrayView(&Delimiter, 1), Visitor);
	}

	/**
	 * Visit every token in the input view as separated by the delimiter.
	 *
	 * Comparisons with the delimiter are case-sensitive and empty tokens are visited.
	 *
	 * @param View The string view to split into tokens.
	 * @param Delimiter The delimiter character to split the input string on.
	 * @param Visitor A function that is called for each token.
	 */
	inline void ParseTokens(const FStringView& View, TCHAR Delimiter, TFunctionRef<void(FStringView)> Visitor)
	{
		return ParseTokensMultiple(View, MakeArrayView(&Delimiter, 1), Visitor);
	}
}
}
