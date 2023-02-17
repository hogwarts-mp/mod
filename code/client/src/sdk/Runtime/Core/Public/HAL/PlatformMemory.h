// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreTypes.h"
#include "GenericPlatform/GenericPlatformMemory.h"

#include COMPILED_PLATFORM_HEADER(PlatformMemory.h)

#ifndef ENABLE_MEMORY_SCOPE_STATS
#define ENABLE_MEMORY_SCOPE_STATS 0
#endif

// This will grab the memory stats of VM and Physcial before and at the end of scope
// reporting +/- difference in memory.
// WARNING This will also capture differences in Threads which have nothing to do with the scope
#if ENABLE_MEMORY_SCOPE_STATS
class CORE_API FScopedMemoryStats
{
public:
	explicit FScopedMemoryStats(const TCHAR* Name);

	~FScopedMemoryStats();

private:
	const TCHAR* Text;
	const FPlatformMemoryStats StartStats;
};
#else
class FScopedMemoryStats
{
public:
	explicit FScopedMemoryStats(const TCHAR* Name) {}
};
#endif
