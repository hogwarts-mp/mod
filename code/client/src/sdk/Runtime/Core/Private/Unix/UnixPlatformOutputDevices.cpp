// Copyright Epic Games, Inc. All Rights Reserved.

#include "Unix/UnixPlatformOutputDevices.h"
#include "Containers/StringConv.h"
#include "Logging/LogMacros.h"
#include "CoreGlobals.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/OutputDeviceHelper.h"
#include "Misc/CoreDelegates.h"
#include "Misc/App.h"
#include "Misc/OutputDeviceConsole.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Unix/UnixErrorOutputDevice.h"

void FUnixOutputDevices::SetupOutputDevices()
{
	check(GLog);

	ResetCachedAbsoluteFilename();

	// add file log
	GLog->AddOutputDevice(FPlatformOutputDevices::GetLog());

	// @todo: set to false for minor utils?
	if (GLogConsole != nullptr)
	{
		bool bLogToConsole = !NO_LOGGING && !FParse::Param(FCommandLine::Get(), TEXT("NOCONSOLE"));
		if (bLogToConsole)
		{
			GLog->AddOutputDevice(GLogConsole);
		}
	}

	// debug and event logging is not really supported on Unix. 
}

FString FUnixOutputDevices::GetAbsoluteLogFilename()
{
	// FIXME: this function should not exist once FGenericPlatformOutputDevices::GetAbsoluteLogFilename() returns absolute filename (see UE-25650)
	return FPaths::ConvertRelativePathToFull(FGenericPlatformOutputDevices::GetAbsoluteLogFilename());
}

class FOutputDevice* FUnixOutputDevices::GetEventLog()
{
	return NULL; // @TODO No event logging
}

FOutputDeviceError* FUnixOutputDevices::GetError()
{
	static FUnixErrorOutputDevice Singleton;
	return &Singleton;
}

