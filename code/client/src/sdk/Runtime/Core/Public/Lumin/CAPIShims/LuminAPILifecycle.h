// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_lifecycle.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_DEPRECATED_MSG_SHIM(ml_lifecycle, MLResult, MLLifecycleInit, "Replaced by MLLifecycleInitEx.")
#define MLLifecycleInit ::LUMIN_MLSDK_API::MLLifecycleInitShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLLifecycleGetSelfInfo)
#define MLLifecycleGetSelfInfo ::LUMIN_MLSDK_API::MLLifecycleGetSelfInfoShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLLifecycleFreeSelfInfo)
#define MLLifecycleFreeSelfInfo ::LUMIN_MLSDK_API::MLLifecycleFreeSelfInfoShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLLifecycleGetInitArgList)
#define MLLifecycleGetInitArgList ::LUMIN_MLSDK_API::MLLifecycleGetInitArgListShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLLifecycleGetInitArgListLength)
#define MLLifecycleGetInitArgListLength ::LUMIN_MLSDK_API::MLLifecycleGetInitArgListLengthShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLLifecycleGetInitArgByIndex)
#define MLLifecycleGetInitArgByIndex ::LUMIN_MLSDK_API::MLLifecycleGetInitArgByIndexShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLLifecycleGetInitArgUri)
#define MLLifecycleGetInitArgUri ::LUMIN_MLSDK_API::MLLifecycleGetInitArgUriShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLLifecycleGetFileInfoListLength)
#define MLLifecycleGetFileInfoListLength ::LUMIN_MLSDK_API::MLLifecycleGetFileInfoListLengthShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLLifecycleGetFileInfoByIndex)
#define MLLifecycleGetFileInfoByIndex ::LUMIN_MLSDK_API::MLLifecycleGetFileInfoByIndexShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLLifecycleFreeInitArgList)
#define MLLifecycleFreeInitArgList ::LUMIN_MLSDK_API::MLLifecycleFreeInitArgListShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLLifecycleSetReadyIndication)
#define MLLifecycleSetReadyIndication ::LUMIN_MLSDK_API::MLLifecycleSetReadyIndicationShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLLifecycleInitEx)
#define MLLifecycleInitEx ::LUMIN_MLSDK_API::MLLifecycleInitExShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
