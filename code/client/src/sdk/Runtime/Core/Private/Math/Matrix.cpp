// Copyright Epic Games, Inc. All Rights Reserved.

#include "Math/Matrix.h"

void FMatrix::ErrorEnsure(const TCHAR* Message)
{
	UE_LOG(LogUnrealMath, Error, TEXT("%s"), Message);
	ensureMsgf(false, TEXT("%s"), Message);
}
