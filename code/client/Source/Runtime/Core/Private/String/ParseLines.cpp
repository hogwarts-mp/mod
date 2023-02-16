// Copyright Epic Games, Inc. All Rights Reserved.

#include "String/ParseLines.h"

#include "Containers/StringView.h"

namespace UE
{
namespace String
{
	void ParseLines(const FStringView& View, const TFunctionRef<void(FStringView)>& Visitor)
	{
		const TCHAR* ViewIt = View.GetData();
		const TCHAR* ViewEnd = ViewIt + View.Len();
		do
		{
			const TCHAR* LineStart = ViewIt;
			const TCHAR* LineEnd = ViewEnd;
			for (; ViewIt != ViewEnd; ++ViewIt)
			{
				const TCHAR CurrentChar = *ViewIt;
				if (CurrentChar == TEXT('\n'))
				{
					LineEnd = ViewIt++;
					break;
				}
				if (CurrentChar == TEXT('\r'))
				{
					LineEnd = ViewIt++;
					if (ViewIt != ViewEnd && *ViewIt == TEXT('\n'))
					{
						++ViewIt;
					}
					break;
				}
			}

			Visitor(FStringView(LineStart, static_cast<FStringView::SizeType>(LineEnd - LineStart)));
		}
		while (ViewIt != ViewEnd);
	}
}
}
