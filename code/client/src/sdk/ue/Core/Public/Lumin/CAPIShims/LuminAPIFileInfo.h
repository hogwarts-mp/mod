// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_fileinfo.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLFileInfoAllocateEmpty)
#define MLFileInfoAllocateEmpty ::LUMIN_MLSDK_API::MLFileInfoAllocateEmptyShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLFileInfoGetMimeType)
#define MLFileInfoGetMimeType ::LUMIN_MLSDK_API::MLFileInfoGetMimeTypeShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLFileInfoGetFileName)
#define MLFileInfoGetFileName ::LUMIN_MLSDK_API::MLFileInfoGetFileNameShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLFileInfoGetFD)
#define MLFileInfoGetFD ::LUMIN_MLSDK_API::MLFileInfoGetFDShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLFileInfoSetFD)
#define MLFileInfoSetFD ::LUMIN_MLSDK_API::MLFileInfoSetFDShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLFileInfoSetFileName)
#define MLFileInfoSetFileName ::LUMIN_MLSDK_API::MLFileInfoSetFileNameShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLFileInfoSetMimeType)
#define MLFileInfoSetMimeType ::LUMIN_MLSDK_API::MLFileInfoSetMimeTypeShim
CREATE_FUNCTION_SHIM(ml_lifecycle, MLResult, MLFileInfoRelease)
#define MLFileInfoRelease ::LUMIN_MLSDK_API::MLFileInfoReleaseShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
