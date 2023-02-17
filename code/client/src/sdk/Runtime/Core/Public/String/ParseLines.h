// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/StringFwd.h"
#include "Templates/Function.h"

namespace UE
{
namespace String
{
	/**
	 * Visit every line in the input view as terminated by any of CRLF, CR, LF.
	 *
	 * Empty lines are visited by this function.
	 *
	 * @param View The string view to split into lines.
	 * @param Visitor A function that is called for each line.
	 */
	CORE_API void ParseLines(const FStringView& View, const TFunctionRef<void(FStringView)>& Visitor);
}
}
