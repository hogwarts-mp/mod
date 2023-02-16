// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	IOSPlatformMemory.cpp: IOS platform memory functions
=============================================================================*/

#include "IOS/IOSPlatformMemory.h"
#include "Misc/CoreStats.h"
#include "HAL/MallocBinned.h"
#include "HAL/MallocAnsi.h"
#include "GenericPlatform/GenericPlatformMemoryPoolStats.h"
#include "Misc/CoreDelegates.h"

void FIOSPlatformMemory::OnOutOfMemory(uint64 Size, uint32 Alignment)
{
    // Update memory stats before we enter the crash handler.
    OOMAllocationSize = Size;
    OOMAllocationAlignment = Alignment;
    
    // only call this code one time - if already OOM, abort
    if (bIsOOM)
    {
        return;
    }
    bIsOOM = true;
    
    FPlatformMemoryStats PlatformMemoryStats = FPlatformMemory::GetStats();
    if (BackupOOMMemoryPool)
    {
        FPlatformMemory::BinnedFreeToOS(BackupOOMMemoryPool, FPlatformMemory::GetBackMemoryPoolSize());
        UE_LOG(LogMemory, Warning, TEXT("Freeing %d bytes from backup pool to handle out of memory."), FPlatformMemory::GetBackMemoryPoolSize());
        
        LLM(FLowLevelMemTracker::Get().OnLowLevelFree(ELLMTracker::Default, BackupOOMMemoryPool));
    }
    
    UE_LOG(LogMemory, Warning, TEXT("MemoryStats:")\
           TEXT("\n\tAvailablePhysical %llu")\
           TEXT("\n\t AvailableVirtual %llu")\
           TEXT("\n\t     UsedPhysical %llu")\
           TEXT("\n\t PeakUsedPhysical %llu")\
           TEXT("\n\t      UsedVirtual %llu")\
           TEXT("\n\t  PeakUsedVirtual %llu"),
           (uint64)PlatformMemoryStats.AvailablePhysical,
           (uint64)PlatformMemoryStats.AvailableVirtual,
           (uint64)PlatformMemoryStats.UsedPhysical,
           (uint64)PlatformMemoryStats.PeakUsedPhysical,
           (uint64)PlatformMemoryStats.UsedVirtual,
           (uint64)PlatformMemoryStats.PeakUsedVirtual);
    
    // let any registered handlers go
    FCoreDelegates::GetMemoryTrimDelegate().Broadcast();
    
    UE_LOG(LogMemory, Warning, TEXT("Ran out of memory allocating %llu bytes with alignment %u"), Size, Alignment);
    
    // make this a fatal error that ends here not in the log
    // changed to 3 from NULL because clang noticed writing to NULL and warned about it
    *(int32 *)3 = 123;
}
