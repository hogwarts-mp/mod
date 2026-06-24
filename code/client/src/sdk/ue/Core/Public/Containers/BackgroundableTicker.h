// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Ticker.h"

/**
 * This works the same as the core FTicker, but on supported mobile platforms 
 * it continues ticking while the app is running in the background.
 */
class FBackgroundableTicker
	: public FTicker
{
public:

	CORE_API static FBackgroundableTicker& GetCoreTicker();

	CORE_API FBackgroundableTicker();
	CORE_API ~FBackgroundableTicker();

private:
	
	FDelegateHandle CoreTickerHandle;
	FDelegateHandle BackgroundTickerHandle;
	bool bWasBackgrounded = false;
};