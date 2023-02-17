// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/ErrorException.h"

//
// Throw a string exception with a message.
//
#if HACK_HEADER_GENERATOR
void VARARGS FError::ThrowfImpl(const TCHAR* Fmt, ...)
{
	static TCHAR TempStr[4096];
	GET_VARARGS( TempStr, UE_ARRAY_COUNT(TempStr), UE_ARRAY_COUNT(TempStr)-1, Fmt, Fmt );
#if !PLATFORM_EXCEPTIONS_DISABLED
	throw( TempStr );
#else
	UE_LOG(LogOutputDevice, Fatal, TEXT("THROW: %s"), TempStr);
#endif
#if PLATFORM_WINDOWS && defined(__clang__)
	abort();
#endif
}					
#endif
