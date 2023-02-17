// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/Platform.h"

////////////////////////////////////////////////////////////////////////////////
class CORE_API FTraceAuxiliary
{
public:
	static void Initialize(const TCHAR* CommandLine);
	static void TryAutoConnect();
	static void EnableChannels();
};
