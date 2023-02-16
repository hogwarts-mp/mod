// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_logging.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_ext_logging, void, MLLoggingEnableLogLevel)
#define MLLoggingEnableLogLevel ::LUMIN_MLSDK_API::MLLoggingEnableLogLevelShim
CREATE_FUNCTION_SHIM(ml_ext_logging, bool, MLLoggingLogLevelIsEnabled)
#define MLLoggingLogLevelIsEnabled ::LUMIN_MLSDK_API::MLLoggingLogLevelIsEnabledShim
CREATE_FUNCTION_SHIM(ml_ext_logging, void, MLLoggingLogVargs)
#define MLLoggingLogVargs ::LUMIN_MLSDK_API::MLLoggingLogVargsShim
CREATE_FUNCTION_SHIM(ml_ext_logging, void, MLLoggingLog)
#define MLLoggingLog ::LUMIN_MLSDK_API::MLLoggingLogShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
