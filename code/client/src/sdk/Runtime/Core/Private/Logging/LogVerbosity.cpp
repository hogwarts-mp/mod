// Copyright Epic Games, Inc. All Rights Reserved.

#include "Logging/LogVerbosity.h"

const TCHAR* ToString(ELogVerbosity::Type Verbosity)
{
	switch (Verbosity & ELogVerbosity::VerbosityMask)
	{
	case ELogVerbosity::NoLogging:
		return TEXT("NoLogging");
	case ELogVerbosity::Fatal:
		return TEXT("Fatal");
	case ELogVerbosity::Error:
		return TEXT("Error");
	case ELogVerbosity::Warning:
		return TEXT("Warning");
	case ELogVerbosity::Display:
		return TEXT("Display");
	case ELogVerbosity::Log:
		return TEXT("Log");
	case ELogVerbosity::Verbose:
		return TEXT("Verbose");
	case ELogVerbosity::VeryVerbose:
		return TEXT("VeryVerbose");
	}
	return TEXT("UnknownVerbosity");
}