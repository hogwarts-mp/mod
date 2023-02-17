// Copyright Epic Games, Inc. All Rights Reserved.

#include "HAL/PlatformMemory.h"

#if ENABLE_MEMORY_SCOPE_STATS

namespace
{
	float CalculateDifference(uint64 Current, uint64 Previous)
	{
		return static_cast<float>(Current) - static_cast<float>(Previous);
	}

	float BytesToMB(float Bytes)
	{
		return Bytes / (1024.0f * 1024.0f);
	}
}

FScopedMemoryStats::FScopedMemoryStats(const TCHAR* Name)
	: Text(Name)
	, StartStats(FPlatformMemory::GetStats())
{
}

FScopedMemoryStats::~FScopedMemoryStats()
{
	const FPlatformMemoryStats EndStats = FPlatformMemory::GetStats();
	UE_LOG(LogMemory, Log, TEXT("ScopedMemoryStat[%s] UsedPhysical %.02fMB (%+.02fMB), PeakPhysical: %.02fMB (%+.02fMB), UsedVirtual: %.02fMB (%+.02fMB) PeakVirtual: %.02fMB (%+.02fMB)"),
		Text,
		BytesToMB(static_cast<float>(EndStats.UsedPhysical)),
		BytesToMB(CalculateDifference(EndStats.UsedPhysical,     StartStats.UsedPhysical)),
		BytesToMB(static_cast<float>(EndStats.PeakUsedPhysical)),
		BytesToMB(CalculateDifference(EndStats.PeakUsedPhysical, StartStats.PeakUsedPhysical)),
		BytesToMB(static_cast<float>(EndStats.UsedVirtual)),
		BytesToMB(CalculateDifference(EndStats.UsedVirtual,      StartStats.UsedVirtual)),
		BytesToMB(static_cast<float>(EndStats.PeakUsedVirtual)),
		BytesToMB(CalculateDifference(EndStats.PeakUsedVirtual,  StartStats.PeakUsedVirtual))
	);
}
#endif
