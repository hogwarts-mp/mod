// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	HoloLensOutputDevices.cpp: HoloLens implementations of OutputDevices functions
=============================================================================*/

#include "HoloLens/HoloLensPlatformOutputDevices.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"

#include "HAL/FeedbackContextAnsi.h"
#include "HoloLens/HoloLensOutputDevicesPrivate.h"

DEFINE_LOG_CATEGORY(LogHoloLensOutputDevices);

//////////////////////////////////
// FHoloLensOutputDevices
//////////////////////////////////
class FOutputDevice* FHoloLensOutputDevices::GetEventLog()
{
	static FOutputDeviceEventLog Singleton;
	return &Singleton;
}