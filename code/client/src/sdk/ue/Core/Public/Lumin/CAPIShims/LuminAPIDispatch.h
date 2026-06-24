// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if !defined(WITH_MLSDK) || WITH_MLSDK

#include "LuminAPI.h"

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_dispatch.h>
LUMIN_THIRD_PARTY_INCLUDES_END

namespace LUMIN_MLSDK_API
{

CREATE_FUNCTION_SHIM(ml_dispatch, MLResult, MLDispatchAllocateEmptyPacket)
#define MLDispatchAllocateEmptyPacket ::LUMIN_MLSDK_API::MLDispatchAllocateEmptyPacketShim
CREATE_FUNCTION_SHIM(ml_dispatch, MLResult, MLDispatchReleasePacket)
#define MLDispatchReleasePacket ::LUMIN_MLSDK_API::MLDispatchReleasePacketShim
CREATE_FUNCTION_SHIM(ml_dispatch, MLResult, MLDispatchAllocateFileInfoList)
#define MLDispatchAllocateFileInfoList ::LUMIN_MLSDK_API::MLDispatchAllocateFileInfoListShim
CREATE_FUNCTION_SHIM(ml_dispatch, MLResult, MLDispatchGetFileInfoListLength)
#define MLDispatchGetFileInfoListLength ::LUMIN_MLSDK_API::MLDispatchGetFileInfoListLengthShim
CREATE_FUNCTION_SHIM(ml_dispatch, MLResult, MLDispatchGetFileInfoByIndex)
#define MLDispatchGetFileInfoByIndex ::LUMIN_MLSDK_API::MLDispatchGetFileInfoByIndexShim
CREATE_FUNCTION_SHIM(ml_dispatch, MLResult, MLDispatchAddFileInfo)
#define MLDispatchAddFileInfo ::LUMIN_MLSDK_API::MLDispatchAddFileInfoShim
CREATE_FUNCTION_SHIM(ml_dispatch, MLResult, MLDispatchReleaseFileInfoList)
#define MLDispatchReleaseFileInfoList ::LUMIN_MLSDK_API::MLDispatchReleaseFileInfoListShim
CREATE_FUNCTION_SHIM(ml_dispatch, MLResult, MLDispatchSetUri)
#define MLDispatchSetUri ::LUMIN_MLSDK_API::MLDispatchSetUriShim
CREATE_FUNCTION_SHIM(ml_dispatch, MLResult, MLDispatchReleaseUri)
#define MLDispatchReleaseUri ::LUMIN_MLSDK_API::MLDispatchReleaseUriShim
CREATE_FUNCTION_SHIM(ml_dispatch, MLResult, MLDispatchTryOpenApplication)
#define MLDispatchTryOpenApplication ::LUMIN_MLSDK_API::MLDispatchTryOpenApplicationShim
CREATE_FUNCTION_SHIM(ml_dispatch, MLResult, MLDispatchOAuthRegisterSchemaEx)
#define MLDispatchOAuthRegisterSchemaEx ::LUMIN_MLSDK_API::MLDispatchOAuthRegisterSchemaExShim
CREATE_FUNCTION_SHIM(ml_dispatch, MLResult, MLDispatchOAuthUnregisterSchema)
#define MLDispatchOAuthUnregisterSchema ::LUMIN_MLSDK_API::MLDispatchOAuthUnregisterSchemaShim
CREATE_FUNCTION_SHIM(ml_dispatch, MLResult, MLDispatchOAuthOpenWindow)
#define MLDispatchOAuthOpenWindow ::LUMIN_MLSDK_API::MLDispatchOAuthOpenWindowShim
CREATE_FUNCTION_SHIM(ml_dispatch, const char*, MLDispatchGetResultString)
#define MLDispatchGetResultString ::LUMIN_MLSDK_API::MLDispatchGetResultStringShim

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
