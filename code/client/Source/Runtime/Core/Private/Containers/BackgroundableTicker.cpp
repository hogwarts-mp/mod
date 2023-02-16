// Copyright Epic Games, Inc. All Rights Reserved.

#include "Containers/BackgroundableTicker.h"
#include "Misc/CoreDelegates.h"
#include "Stats/Stats.h"

FBackgroundableTicker& FBackgroundableTicker::GetCoreTicker()
{
	static FBackgroundableTicker Singleton;
	return Singleton;
}

FBackgroundableTicker::FBackgroundableTicker()
{
	CoreTickerHandle = FTicker::GetCoreTicker().AddTicker(TEXT("FBackgroundableTicker"), 0.0f, [this](float DeltaTime) -> bool
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FBackgroundableTicker_ForegroundTick);
		if (bWasBackgrounded)
		{
			// When returning to the foreground, ensure we do not report enormous delta time values coming from the foreground ticker.
			bWasBackgrounded = false;
			DeltaTime = FMath::Clamp(DeltaTime, 0.0f, 1.0f / 60);
		}
		Tick(DeltaTime);
		return true;
	});
	BackgroundTickerHandle = FCoreDelegates::MobileBackgroundTickDelegate.AddLambda([this](float DeltaTime)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FBackgroundableTicker_BackgroundTick);
		bWasBackgrounded = true;
		Tick(DeltaTime);
	});
}

FBackgroundableTicker::~FBackgroundableTicker()
{
	FTicker::GetCoreTicker().RemoveTicker(CoreTickerHandle);
	FCoreDelegates::MobileBackgroundTickDelegate.Remove(BackgroundTickerHandle);
}