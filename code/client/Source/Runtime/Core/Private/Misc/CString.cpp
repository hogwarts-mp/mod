// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/CString.h"
#include "Containers/StringConv.h"
#include "Internationalization/Text.h"

// 4 lines of 64 chars each, plus a null terminator
template <>
CORE_API const ANSICHAR TCStringSpcHelper<ANSICHAR>::SpcArray[MAX_SPACES + 1] =
	"                                                                "
	"                                                                "
	"                                                                "
	"                                                               ";

template <>
CORE_API const WIDECHAR TCStringSpcHelper<WIDECHAR>::SpcArray[MAX_SPACES + 1] =
	TEXT("                                                                ")
	TEXT("                                                                ")
	TEXT("                                                                ")
	TEXT("                                                               ");

template <>
CORE_API const ANSICHAR TCStringSpcHelper<ANSICHAR>::TabArray[MAX_TABS + 1] = 
	"\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t"
	"\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t"
	"\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t"
	"\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t";

template <>
CORE_API const WIDECHAR TCStringSpcHelper<WIDECHAR>::TabArray[MAX_TABS + 1] = 
	TEXT("\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t")
	TEXT("\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t")
	TEXT("\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t")
	TEXT("\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t\t");

bool FToBoolHelper::FromCStringAnsi( const ANSICHAR* String )
{
	return FToBoolHelper::FromCStringWide( ANSI_TO_TCHAR(String) );
}

bool FToBoolHelper::FromCStringWide( const WIDECHAR* String )
{
	const FCoreTexts& CoreTexts = FCoreTexts::Get();

	if (
			FCStringWide::Stricmp(String, TEXT("True"))==0
		||	FCStringWide::Stricmp(String, TEXT("Yes"))==0
		||	FCStringWide::Stricmp(String, TEXT("On"))==0
		||	FCStringWide::Stricmp(String, *(CoreTexts.True.ToString()))==0
		||	FCStringWide::Stricmp(String, *(CoreTexts.Yes.ToString()))==0)
	{
		return true;
	}
	else if(
			FCStringWide::Stricmp(String, TEXT("False"))==0
		||	FCStringWide::Stricmp(String, TEXT("No"))==0
		||	FCStringWide::Stricmp(String, TEXT("Off"))==0
		||	FCStringWide::Stricmp(String, *(CoreTexts.False.ToString()))==0
		||	FCStringWide::Stricmp(String, *(CoreTexts.No.ToString()))==0)
	{
		return false;
	}
	else
	{
		return FCStringWide::Atoi(String) ? true : false;
	}
}
