// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_platform.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_platform, MLResult, MLPlatformGetAPILevel)
#define MLPlatformGetAPILevel ::LUMIN_MLSDK_API::MLPlatformGetAPILevelShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
