// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/ArrayView.h"
#include "Containers/StringView.h"

namespace UE
{
namespace String
{
	/**
	 * Search the view for the first occurrence of the search string.
	 *
	 * @param View The string to search within.
	 * @param Search The string to search for.
	 * @param SearchCase Whether the comparison should ignore case.
	 * @return The position at which the search string was found, or INDEX_NONE if not found.
	 */
	CORE_API FAnsiStringView::SizeType FindFirst(FAnsiStringView View, FAnsiStringView Search, ESearchCase::Type SearchCase = ESearchCase::CaseSensitive);
	CORE_API FWideStringView::SizeType FindFirst(FWideStringView View, FWideStringView Search, ESearchCase::Type SearchCase = ESearchCase::CaseSensitive);

	/**
	 * Search the view for the last occurrence of the search string.
	 *
	 * @param View The string to search within.
	 * @param Search The string to search for.
	 * @param SearchCase Whether the comparison should ignore case.
	 * @return The position at which the search string was found, or INDEX_NONE if not found.
	 */
	CORE_API FAnsiStringView::SizeType FindLast(FAnsiStringView View, FAnsiStringView Search, ESearchCase::Type SearchCase = ESearchCase::CaseSensitive);
	CORE_API FWideStringView::SizeType FindLast(FWideStringView View, FWideStringView Search, ESearchCase::Type SearchCase = ESearchCase::CaseSensitive);

	/**
	 * Search the view for the first occurrence of any search string.
	 *
	 * @param View The string to search within.
	 * @param Search The strings to search for.
	 * @param SearchCase Whether the comparison should ignore case.
	 * @return The position at which any search string was found, or INDEX_NONE if not found.
	 */
	CORE_API FAnsiStringView::SizeType FindFirstOfAny(FAnsiStringView View, TConstArrayView<FAnsiStringView> Search, ESearchCase::Type SearchCase = ESearchCase::CaseSensitive);
	CORE_API FWideStringView::SizeType FindFirstOfAny(FWideStringView View, TConstArrayView<FWideStringView> Search, ESearchCase::Type SearchCase = ESearchCase::CaseSensitive);

	/**
	 * Search the view for the last occurrence of any search string.
	 *
	 * @param View The string to search within.
	 * @param Search The strings to search for.
	 * @param SearchCase Whether the comparison should ignore case.
	 * @return The position at which any search string was found, or INDEX_NONE if not found.
	 */
	CORE_API FAnsiStringView::SizeType FindLastOfAny(FAnsiStringView View, TConstArrayView<FAnsiStringView> Search, ESearchCase::Type SearchCase = ESearchCase::CaseSensitive);
	CORE_API FWideStringView::SizeType FindLastOfAny(FWideStringView View, TConstArrayView<FWideStringView> Search, ESearchCase::Type SearchCase = ESearchCase::CaseSensitive);

	/**
	 * Search the view for the first occurrence of the search character.
	 *
	 * @param View The string to search within.
	 * @param Search The character to search for.
	 * @param SearchCase Whether the comparison should ignore case.
	 * @return The position at which the search character was found, or INDEX_NONE if not found.
	 */
	CORE_API FAnsiStringView::SizeType FindFirstChar(FAnsiStringView View, ANSICHAR Search, ESearchCase::Type SearchCase = ESearchCase::CaseSensitive);
	CORE_API FWideStringView::SizeType FindFirstChar(FWideStringView View, WIDECHAR Search, ESearchCase::Type SearchCase = ESearchCase::CaseSensitive);

	/**
	 * Search the view for the last occurrence of the search character.
	 *
	 * @param View The string to search within.
	 * @param Search The character to search for.
	 * @param SearchCase Whether the comparison should ignore case.
	 * @return The position at which the search character was found, or INDEX_NONE if not found.
	 */
	CORE_API FAnsiStringView::SizeType FindLastChar(FAnsiStringView View, ANSICHAR Search, ESearchCase::Type SearchCase = ESearchCase::CaseSensitive);
	CORE_API FWideStringView::SizeType FindLastChar(FWideStringView View, WIDECHAR Search, ESearchCase::Type SearchCase = ESearchCase::CaseSensitive);

	/**
	 * Search the view for the first occurrence of any search character.
	 *
	 * @param View The string to search within.
	 * @param Search The characters to search for.
	 * @param SearchCase Whether the comparison should ignore case.
	 * @return The position at which any search character was found, or INDEX_NONE if not found.
	 */
	CORE_API FAnsiStringView::SizeType FindFirstOfAnyChar(FAnsiStringView View, TConstArrayView<ANSICHAR> Search, ESearchCase::Type SearchCase = ESearchCase::CaseSensitive);
	CORE_API FWideStringView::SizeType FindFirstOfAnyChar(FWideStringView View, TConstArrayView<WIDECHAR> Search, ESearchCase::Type SearchCase = ESearchCase::CaseSensitive);

	/**
	 * Search the view for the last occurrence of any search character.
	 *
	 * @param View The string to search within.
	 * @param Search The characters to search for.
	 * @param SearchCase Whether the comparison should ignore case.
	 * @return The position at which any search character was found, or INDEX_NONE if not found.
	 */
	CORE_API FAnsiStringView::SizeType FindLastOfAnyChar(FAnsiStringView View, TConstArrayView<ANSICHAR> Search, ESearchCase::Type SearchCase = ESearchCase::CaseSensitive);
	CORE_API FWideStringView::SizeType FindLastOfAnyChar(FWideStringView View, TConstArrayView<WIDECHAR> Search, ESearchCase::Type SearchCase = ESearchCase::CaseSensitive);
}
}
