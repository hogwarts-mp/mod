// Copyright Epic Games, Inc. All Rights Reserved.

#include "String/ParseTokens.h"

#include "Algo/AllOf.h"
#include "Algo/Find.h"
#include "Algo/NoneOf.h"
#include "Containers/BitArray.h"
#include "Containers/StringView.h"

namespace UE
{
namespace String
{
	/** Parse tokens with one single-character delimiter. */
	inline static void ParseTokens1Delim1Char(const FStringView& View, const TCHAR Delimiter, TFunctionRef<void(FStringView)> Visitor)
	{
		const TCHAR* ViewIt = View.GetData();
		const TCHAR* const ViewEnd = ViewIt + View.Len();
		const TCHAR* NextToken = ViewIt;

		for (;;)
		{
			if (ViewIt == ViewEnd)
			{
				break;
			}
			if (*ViewIt != Delimiter)
			{
				++ViewIt;
				continue;
			}
			Visitor(FStringView(NextToken, static_cast<FStringView::SizeType>(ViewIt - NextToken)));
			NextToken = ++ViewIt;
		}

		Visitor(FStringView(NextToken, static_cast<FStringView::SizeType>(ViewIt - NextToken)));
	}

	/** Parse tokens with multiple single-character Basic Latin delimiters. */
	inline static void ParseTokensNDelim1CharBasicLatin(const FStringView& View, TArrayView<const TCHAR> Delimiters, TFunctionRef<void(FStringView)> Visitor)
	{
		TBitArray<> DelimiterMask(false, 128);
		for (uint32 Delimiter : Delimiters)
		{
			DelimiterMask[Delimiter] = true;
		}

		const TCHAR* ViewIt = View.GetData();
		const TCHAR* const ViewEnd = ViewIt + View.Len();
		const TCHAR* NextToken = ViewIt;

		for (;;)
		{
			if (ViewIt == ViewEnd)
			{
				break;
			}
			const uint32 CodePoint = *ViewIt;
			if (CodePoint >= 128 || !DelimiterMask[CodePoint])
			{
				++ViewIt;
				continue;
			}
			Visitor(FStringView(NextToken, static_cast<FStringView::SizeType>(ViewIt - NextToken)));
			NextToken = ++ViewIt;
		}

		Visitor(FStringView(NextToken, static_cast<FStringView::SizeType>(ViewIt - NextToken)));
	}

	/** Parse tokens with multiple single-character delimiters in the Basic Multilingual Plane. */
	inline static void ParseTokensNDelim1Char(const FStringView& View, TArrayView<const TCHAR> Delimiters, TFunctionRef<void(FStringView)> Visitor)
	{
		if (Algo::AllOf(Delimiters, [](TCHAR Delimiter) { return Delimiter < 128; }))
		{
			return ParseTokensNDelim1CharBasicLatin(View, Delimiters, Visitor);
		}

		const TCHAR* ViewIt = View.GetData();
		const TCHAR* const ViewEnd = ViewIt + View.Len();
		const TCHAR* NextToken = ViewIt;

		for (;;)
		{
			if (ViewIt == ViewEnd)
			{
				break;
			}
			if (!Algo::Find(Delimiters, *ViewIt))
			{
				++ViewIt;
				continue;
			}
			Visitor(FStringView(NextToken, static_cast<FStringView::SizeType>(ViewIt - NextToken)));
			NextToken = ++ViewIt;
		}

		Visitor(FStringView(NextToken, static_cast<FStringView::SizeType>(ViewIt - NextToken)));
	}

	/** Parse tokens with multiple multi-character delimiters. */
	inline static void ParseTokensNDelimNChar(const FStringView& View, TArrayView<const FStringView> Delimiters, TFunctionRef<void(FStringView)> Visitor)
	{
		// This is a naive implementation that takes time proportional to View.Len() * TotalDelimiterLen.
		// If this function becomes a bottleneck, it can be specialized separately for one and many delimiters.
		// There are algorithms for each are linear or sub-linear in the length of string to search.

		const FStringView::SizeType ViewLen = View.Len();
		FStringView::SizeType NextTokenIndex = 0;

		for (FStringView::SizeType ViewIndex = 0; ViewIndex != ViewLen;)
		{
			const FStringView RemainingView(View.GetData() + ViewIndex, ViewLen - ViewIndex);
			auto MatchDelimiter = [&RemainingView](const FStringView& Delimiter) { return RemainingView.StartsWith(Delimiter, ESearchCase::CaseSensitive); };
			if (const FStringView* Delimiter = Algo::FindByPredicate(Delimiters, MatchDelimiter))
			{
				Visitor(FStringView(View.GetData() + NextTokenIndex, ViewIndex - NextTokenIndex));
				ViewIndex += Delimiter->Len();
				NextTokenIndex = ViewIndex;
			}
			else
			{
				++ViewIndex;
			}
		}

		Visitor(FStringView(View.GetData() + NextTokenIndex, ViewLen - NextTokenIndex));
	}

	void ParseTokensMultiple(const FStringView& View, TArrayView<const FStringView> Delimiters, TFunctionRef<void(FStringView)> Visitor)
	{
		check(Algo::NoneOf(Delimiters, &FStringView::IsEmpty));
		switch (Delimiters.Num())
		{
		case 0:
			return Visitor(View);
		case 1:
			if (Delimiters[0].Len() == 1)
			{
				return ParseTokens1Delim1Char(View, Delimiters[0][0], MoveTemp(Visitor));
			}
			return ParseTokensNDelimNChar(View, Delimiters, MoveTemp(Visitor));
		default:
			if (Algo::AllOf(Delimiters, [](const FStringView& Delimiter) { return Delimiter.Len() == 1; }))
			{
				TArray<TCHAR, TInlineAllocator<32>> DelimiterChars;
				DelimiterChars.Reserve(Delimiters.Num());
				for (const FStringView& Delimiter : Delimiters)
				{
					DelimiterChars.Add(Delimiter[0]);
				}
				return ParseTokensNDelim1Char(View, DelimiterChars, MoveTemp(Visitor));
			}
			else
			{
				return ParseTokensNDelimNChar(View, Delimiters, MoveTemp(Visitor));
			}
		}
	}

	void ParseTokensMultiple(const FStringView& View, TArrayView<const TCHAR> Delimiters, TFunctionRef<void(FStringView)> Visitor)
	{
		switch (Delimiters.Num())
		{
		case 0:
			return Visitor(View);
		case 1:
			return ParseTokens1Delim1Char(View, Delimiters[0], MoveTemp(Visitor));
		default:
			return ParseTokensNDelim1Char(View, Delimiters, MoveTemp(Visitor));
		}
	}
}
}
