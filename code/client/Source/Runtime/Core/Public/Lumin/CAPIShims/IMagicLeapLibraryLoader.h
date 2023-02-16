// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

/** Utility class to load the correct MLSDK libs depedning on the path set to the MLSDK package and whether or not we want to use MLremote / Zero Iteration*/
class IMagicLeapLibraryLoader : public IModuleInterface
{
public:
	virtual void* LoadDLL(const FString& LibName) const = 0;
};
