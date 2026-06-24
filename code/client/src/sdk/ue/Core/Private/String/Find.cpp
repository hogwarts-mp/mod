// Copyright Epic Games, Inc. All Rights Reserved.

#include "String/Find.h"

#include "Algo/AllOf.h"
#include "Algo/Find.h"
#include "Algo/NoneOf.h"

namespace UE
{
namespace String
{
	namespace Private
	{
		// These are naive implementations that take time proportional to View.Len() * TotalSearchLen.
		// If these functions become a bottleneck, they can be specialized separately for one and many search patterns.
		// There are algorithms for each that are linear or sub-linear in the length of the string to search.

		template <typename CharType>
		static inline typename TStringView<CharType>::SizeType FindFirst(TStringView<CharType> View, TStringView<CharType> Search, ESearchCase::Type SearchCase)
		{
			check(!Search.IsEmpty());
			const typename TStringView<CharType>::SizeType SearchLen = Search.Len();
			if (SearchLen == 1)
			{
				return String::FindFirstChar(View, Search[0], SearchCase);
			}
			const CharType* const SearchData = Search.GetData();
			const CharType* const ViewBegin = View.GetData();
			const CharType* const ViewEnd = ViewBegin + View.Len() - SearchLen;
			if (SearchCase == ESearchCase::CaseSensitive)
			{
				for (const CharType* ViewIt = ViewBegin; ViewIt <= ViewEnd; ++ViewIt)
				{
					if (TCString<CharType>::Strncmp(ViewIt, SearchData, SearchLen) == 0)
					{
						return typename TStringView<CharType>::SizeType(ViewIt - ViewBegin);
					}
				}
			}
			else
			{
				for (const CharType* ViewIt = ViewBegin; ViewIt <= ViewEnd; ++ViewIt)
				{
					if (TCString<CharType>::Strnicmp(ViewIt, SearchData, SearchLen) == 0)
					{
						return typename TStringView<CharType>::SizeType(ViewIt - ViewBegin);
					}
				}
			}
			return INDEX_NONE;
		}

		template <typename CharType>
		static inline typename TStringView<CharType>::SizeType FindLast(TStringView<CharType> View, TStringView<CharType> Search, ESearchCase::Type SearchCase)
		{
			check(!Search.IsEmpty());
			const typename TStringView<CharType>::SizeType SearchLen = Search.Len();
			if (SearchLen == 1)
			{
				return String::FindLastChar(View, Search[0], SearchCase);
			}
			const CharType* const SearchData = Search.GetData();
			const CharType* const ViewBegin = View.GetData();
			const CharType* const ViewEnd = ViewBegin + View.Len() - SearchLen;
			if (SearchCase == ESearchCase::CaseSensitive)
			{
				for (const CharType* ViewIt = ViewEnd; ViewIt >= ViewBegin; --ViewIt)
				{
					if (TCString<CharType>::Strncmp(ViewIt, SearchData, SearchLen) == 0)
					{
						return typename TStringView<CharType>::SizeType(ViewIt - ViewBegin);
					}
				}
			}
			else
			{
				for (const CharType* ViewIt = ViewEnd; ViewIt >= ViewBegin; --ViewIt)
				{
					if (TCString<CharType>::Strnicmp(ViewIt, SearchData, SearchLen) == 0)
					{
						return typename TStringView<CharType>::SizeType(ViewIt - ViewBegin);
					}
				}
			}
			return INDEX_NONE;
		}

		template <typename CharType>
		static inline typename TStringView<CharType>::SizeType FindFirstOfAny(TStringView<CharType> View, TConstArrayView<TStringView<CharType>> Search, ESearchCase::Type SearchCase)
		{
			check(Algo::NoneOf(Search, &TStringView<CharType>::IsEmpty));
			switch (Search.Num())
			{
			case 0:
				return INDEX_NONE;
			case 1:
				return String::FindFirst(View, Search[0], SearchCase);
			default:
				if (Algo::AllOf(Search, [](const TStringView<CharType>& Pattern) { return Pattern.Len() == 1; }))
				{
					TArray<CharType, TInlineAllocator<32>> SearchChars;
					SearchChars.Reserve(Search.Num());
					for (const TStringView<CharType>& Pattern : Search)
					{
						SearchChars.Add(Pattern[0]);
					}
					return String::FindFirstOfAnyChar(View, SearchChars, SearchCase);
				}
				break;
			}

			const CharType* const ViewBegin = View.GetData();
			const typename TStringView<CharType>::SizeType ViewLen = View.Len();
			for (typename TStringView<CharType>::SizeType ViewIndex = 0; ViewIndex != ViewLen; ++ViewIndex)
			{
				const TStringView<CharType> RemainingView(ViewBegin + ViewIndex, ViewLen - ViewIndex);
				auto MatchPattern = [&RemainingView, SearchCase](const TStringView<CharType>& Pattern) { return RemainingView.StartsWith(Pattern, SearchCase); };
				if (Algo::FindByPredicate(Search, MatchPattern))
				{
					return ViewIndex;
				}
			}
			return INDEX_NONE;
		}

		template <typename CharType>
		static inline typename TStringView<CharType>::SizeType FindLastOfAny(TStringView<CharType> View, TConstArrayView<TStringView<CharType>> Search, ESearchCase::Type SearchCase)
		{
			check(Algo::NoneOf(Search, &TStringView<CharType>::IsEmpty));
			switch (Search.Num())
			{
			case 0:
				return INDEX_NONE;
			case 1:
				return String::FindLast(View, Search[0], SearchCase);
			default:
				if (Algo::AllOf(Search, [](const TStringView<CharType>& Pattern) { return Pattern.Len() == 1; }))
				{
					TArray<CharType, TInlineAllocator<32>> SearchChars;
					SearchChars.Reserve(Search.Num());
					for (const TStringView<CharType>& Pattern : Search)
					{
						SearchChars.Add(Pattern[0]);
					}
					return String::FindLastOfAnyChar(View, SearchChars, SearchCase);
				}
				break;
			}


			const CharType* const ViewBegin = View.GetData();
			const typename TStringView<CharType>::SizeType ViewLen = View.Len();
			for (typename TStringView<CharType>::SizeType ViewIndex = ViewLen - 1; ViewIndex >= 0; --ViewIndex)
			{
				const TStringView<CharType> RemainingView(ViewBegin + ViewIndex, ViewLen - ViewIndex);
				auto MatchPattern = [&RemainingView, SearchCase](const TStringView<CharType>& Pattern) { return RemainingView.StartsWith(Pattern, SearchCase); };
				if (Algo::FindByPredicate(Search, MatchPattern))
				{
					return ViewIndex;
				}
			}
			return INDEX_NONE;
		}

		template <typename CharType>
		static inline typename TStringView<CharType>::SizeType FindFirstChar(TStringView<CharType> View, CharType Search, ESearchCase::Type SearchCase)
		{
			const CharType* const ViewBegin = View.GetData();
			const CharType* const ViewEnd = ViewBegin + View.Len();
			if (SearchCase == ESearchCase::CaseSensitive)
			{
				for (const CharType* ViewIt = ViewBegin; ViewIt < ViewEnd; ++ViewIt)
				{
					if (*ViewIt == Search)
					{
						return typename TStringView<CharType>::SizeType(ViewIt - ViewBegin);
					}
				}
			}
			else
			{
				const CharType SearchUpper = TChar<CharType>::ToUpper(Search);
				for (const CharType* ViewIt = ViewBegin; ViewIt < ViewEnd; ++ViewIt)
				{
					if (TChar<CharType>::ToUpper(*ViewIt) == SearchUpper)
					{
						return typename TStringView<CharType>::SizeType(ViewIt - ViewBegin);
					}
				}
			}
			return INDEX_NONE;
		}

		template <typename CharType>
		static inline typename TStringView<CharType>::SizeType FindLastChar(TStringView<CharType> View, CharType Search, ESearchCase::Type SearchCase)
		{
			const CharType* const ViewBegin = View.GetData();
			const CharType* const ViewEnd = ViewBegin + View.Len();
			if (SearchCase == ESearchCase::CaseSensitive)
			{
				for (const CharType* ViewIt = ViewEnd - 1; ViewIt >= ViewBegin; --ViewIt)
				{
					if (*ViewIt == Search)
					{
						return typename TStringView<CharType>::SizeType(ViewIt - ViewBegin);
					}
				}
			}
			else
			{
				const CharType SearchUpper = TChar<CharType>::ToUpper(Search);
				for (const CharType* ViewIt = ViewEnd - 1; ViewIt >= ViewBegin; --ViewIt)
				{
					if (TChar<CharType>::ToUpper(*ViewIt) == SearchUpper)
					{
						return typename TStringView<CharType>::SizeType(ViewIt - ViewBegin);
					}
				}
			}
			return INDEX_NONE;
		}

		template <typename CharType>
		static inline typename TStringView<CharType>::SizeType FindFirstOfAnyChar(TStringView<CharType> View, TConstArrayView<CharType> Search, ESearchCase::Type SearchCase)
		{
			switch (Search.Num())
			{
			case 0:
				return INDEX_NONE;
			case 1:
				return String::FindFirstChar(View, Search[0], SearchCase);
			default:
				break;
			}

			const CharType* const ViewBegin = View.GetData();
			const CharType* const ViewEnd = ViewBegin + View.Len();
			if (SearchCase == ESearchCase::CaseSensitive)
			{
				for (const CharType* ViewIt = ViewBegin; ViewIt < ViewEnd; ++ViewIt)
				{
					if (Algo::Find(Search, *ViewIt))
					{
						return typename TStringView<CharType>::SizeType(ViewIt - ViewBegin);
					}
				}
				return INDEX_NONE;
			}
			else
			{
				TArray<CharType, TInlineAllocator<32>> SearchUpper;
				SearchUpper.Reserve(Search.Num());
				for (const CharType Pattern : Search)
				{
					SearchUpper.Add(TChar<CharType>::ToUpper(Pattern));
				}
				for (const CharType* ViewIt = ViewBegin; ViewIt < ViewEnd; ++ViewIt)
				{
					if (Algo::Find(SearchUpper, TChar<CharType>::ToUpper(*ViewIt)))
					{
						return typename TStringView<CharType>::SizeType(ViewIt - ViewBegin);
					}
				}
				return INDEX_NONE;
			}
		}

		template <typename CharType>
		static inline typename TStringView<CharType>::SizeType FindLastOfAnyChar(TStringView<CharType> View, TConstArrayView<CharType> Search, ESearchCase::Type SearchCase)
		{
			switch (Search.Num())
			{
			case 0:
				return INDEX_NONE;
			case 1:
				return String::FindLastChar(View, Search[0], SearchCase);
			default:
				break;
			}

			const CharType* const ViewBegin = View.GetData();
			const CharType* const ViewEnd = ViewBegin + View.Len();
			if (SearchCase == ESearchCase::CaseSensitive)
			{
				for (const CharType* ViewIt = ViewEnd - 1; ViewIt >= ViewBegin; --ViewIt)
				{
					if (Algo::Find(Search, *ViewIt))
					{
						return typename TStringView<CharType>::SizeType(ViewIt - ViewBegin);
					}
				}
				return INDEX_NONE;
			}
			else
			{
				TArray<CharType, TInlineAllocator<32>> SearchUpper;
				SearchUpper.Reserve(Search.Num());
				for (const CharType Pattern : Search)
				{
					SearchUpper.Add(TChar<CharType>::ToUpper(Pattern));
				}
				for (const CharType* ViewIt = ViewEnd - 1; ViewIt >= ViewBegin; --ViewIt)
				{
					if (Algo::Find(SearchUpper, TChar<CharType>::ToUpper(*ViewIt)))
					{
						return typename TStringView<CharType>::SizeType(ViewIt - ViewBegin);
					}
				}
				return INDEX_NONE;
			}
		}
	}

	FAnsiStringView::SizeType FindFirst(FAnsiStringView View, FAnsiStringView Search, ESearchCase::Type SearchCase)
	{
		return Private::FindFirst(View, Search, SearchCase);
	}

	FWideStringView::SizeType FindFirst(FWideStringView View, FWideStringView Search, ESearchCase::Type SearchCase)
	{
		return Private::FindFirst(View, Search, SearchCase);
	}

	FAnsiStringView::SizeType FindLast(FAnsiStringView View, FAnsiStringView Search, ESearchCase::Type SearchCase)
	{
		return Private::FindLast(View, Search, SearchCase);
	}

	FWideStringView::SizeType FindLast(FWideStringView View, FWideStringView Search, ESearchCase::Type SearchCase)
	{
		return Private::FindLast(View, Search, SearchCase);
	}

	FAnsiStringView::SizeType FindFirstOfAny(FAnsiStringView View, TConstArrayView<FAnsiStringView> Search, ESearchCase::Type SearchCase)
	{
		return Private::FindFirstOfAny(View, Search, SearchCase);
	}

	FWideStringView::SizeType FindFirstOfAny(FWideStringView View, TConstArrayView<FWideStringView> Search, ESearchCase::Type SearchCase)
	{
		return Private::FindFirstOfAny(View, Search, SearchCase);
	}

	FAnsiStringView::SizeType FindLastOfAny(FAnsiStringView View, TConstArrayView<FAnsiStringView> Search, ESearchCase::Type SearchCase)
	{
		return Private::FindLastOfAny(View, Search, SearchCase);
	}

	FWideStringView::SizeType FindLastOfAny(FWideStringView View, TConstArrayView<FWideStringView> Search, ESearchCase::Type SearchCase)
	{
		return Private::FindLastOfAny(View, Search, SearchCase);
	}

	FAnsiStringView::SizeType FindFirstChar(FAnsiStringView View, ANSICHAR Search, ESearchCase::Type SearchCase)
	{
		return Private::FindFirstChar(View, Search, SearchCase);
	}

	FWideStringView::SizeType FindFirstChar(FWideStringView View, WIDECHAR Search, ESearchCase::Type SearchCase)
	{
		return Private::FindFirstChar(View, Search, SearchCase);
	}

	FAnsiStringView::SizeType FindLastChar(FAnsiStringView View, ANSICHAR Search, ESearchCase::Type SearchCase)
	{
		return Private::FindLastChar(View, Search, SearchCase);
	}

	FWideStringView::SizeType FindLastChar(FWideStringView View, WIDECHAR Search, ESearchCase::Type SearchCase)
	{
		return Private::FindLastChar(View, Search, SearchCase);
	}

	FAnsiStringView::SizeType FindFirstOfAnyChar(FAnsiStringView View, TConstArrayView<ANSICHAR> Search, ESearchCase::Type SearchCase)
	{
		return Private::FindFirstOfAnyChar(View, Search, SearchCase);
	}

	FWideStringView::SizeType FindFirstOfAnyChar(FWideStringView View, TConstArrayView<WIDECHAR> Search, ESearchCase::Type SearchCase)
	{
		return Private::FindFirstOfAnyChar(View, Search, SearchCase);
	}

	FAnsiStringView::SizeType FindLastOfAnyChar(FAnsiStringView View, TConstArrayView<ANSICHAR> Search, ESearchCase::Type SearchCase)
	{
		return Private::FindLastOfAnyChar(View, Search, SearchCase);
	}

	FWideStringView::SizeType FindLastOfAnyChar(FWideStringView View, TConstArrayView<WIDECHAR> Search, ESearchCase::Type SearchCase)
	{
		return Private::FindLastOfAnyChar(View, Search, SearchCase);
	}
}
}
