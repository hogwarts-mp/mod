// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_input.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_input, MLResult, MLInputCreate)
#define MLInputCreate ::LUMIN_MLSDK_API::MLInputCreateShim
CREATE_FUNCTION_SHIM(ml_input, MLResult, MLInputSetControllerCallbacks)
#define MLInputSetControllerCallbacks ::LUMIN_MLSDK_API::MLInputSetControllerCallbacksShim
CREATE_FUNCTION_SHIM(ml_input, MLResult, MLInputGetControllerState)
#define MLInputGetControllerState ::LUMIN_MLSDK_API::MLInputGetControllerStateShim
CREATE_FUNCTION_SHIM(ml_input, MLResult, MLInputStartControllerFeedbackPatternVibe)
#define MLInputStartControllerFeedbackPatternVibe ::LUMIN_MLSDK_API::MLInputStartControllerFeedbackPatternVibeShim
CREATE_FUNCTION_SHIM(ml_input, MLResult, MLInputStartControllerFeedbackPatternLED)
#define MLInputStartControllerFeedbackPatternLED ::LUMIN_MLSDK_API::MLInputStartControllerFeedbackPatternLEDShim
CREATE_FUNCTION_SHIM(ml_input, MLResult, MLInputStartControllerFeedbackPatternEffectLED)
#define MLInputStartControllerFeedbackPatternEffectLED ::LUMIN_MLSDK_API::MLInputStartControllerFeedbackPatternEffectLEDShim
CREATE_FUNCTION_SHIM(ml_input, MLResult, MLInputSetKeyboardCallbacks)
#define MLInputSetKeyboardCallbacks ::LUMIN_MLSDK_API::MLInputSetKeyboardCallbacksShim
CREATE_FUNCTION_SHIM(ml_input, MLResult, MLInputGetKeyboardState)
#define MLInputGetKeyboardState ::LUMIN_MLSDK_API::MLInputGetKeyboardStateShim
CREATE_FUNCTION_SHIM(ml_input, MLResult, MLInputDestroy)
#define MLInputDestroy ::LUMIN_MLSDK_API::MLInputDestroyShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
