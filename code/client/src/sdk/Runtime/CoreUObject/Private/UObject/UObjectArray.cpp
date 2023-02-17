// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	UnObjArray.cpp: Unreal array of all objects
=============================================================================*/

#include "UObject/UObjectArray.h"
#include "Misc/ScopeLock.h"
#include "UObject/UObjectAllocator.h"
#include "UObject/Class.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogUObjectArray, Log, All);

FUObjectClusterContainer GUObjectClusters;

#if STATS || ENABLE_STATNAMEDEVENTS_UOBJECT
void FUObjectItem::CreateStatID() const
{
	// @todo can we put this in a header?
//		SCOPE_CYCLE_COUNTER(STAT_CreateStatID);

	FString LongName;
	LongName.Reserve(255);
	TArray<UObjectBase const*, TInlineAllocator<24>> ClassChain;

	// Build class hierarchy
	UObjectBase const* Target = Object;
	do
	{
		ClassChain.Add(Target);
		Target = Target->GetOuter();
	} while (Target);

	// Start with class name
	if (Object->GetClass())
	{
		Object->GetClass()->GetFName().GetDisplayNameEntry()->AppendNameToString(LongName);
	}

	// Now process from parent -> child so we can append strings more efficiently.
	bool bFirstEntry = true;
	for (int32 i = ClassChain.Num() - 1; i >= 0; i--)
	{
		Target = ClassChain[i];
		const FNameEntry* NameEntry = Target->GetFName().GetDisplayNameEntry();
		if (bFirstEntry)
		{
			NameEntry->AppendNameToPathString(LongName);
		}
		else
		{
			if (!LongName.IsEmpty())
			{
				LongName += TEXT(".");
			}
			NameEntry->AppendNameToString(LongName);
		}
		bFirstEntry = false;
	}

#if STATS
	StatID = FDynamicStats::CreateStatId<FStatGroup_STATGROUP_UObjects>(LongName);
#else // ENABLE_STATNAMEDEVENTS
	const auto& ConversionData = StringCast<PROFILER_CHAR>(*LongName);
	const int32 NumStorageChars = (ConversionData.Length() + 1);	//length doesn't include null terminator

	PROFILER_CHAR* StoragePtr = new PROFILER_CHAR[NumStorageChars];
	FMemory::Memcpy(StoragePtr, ConversionData.Get(), NumStorageChars * sizeof(PROFILER_CHAR));

	if (FPlatformAtomics::InterlockedCompareExchangePointer((void**)&StatIDStringStorage, StoragePtr, nullptr) != nullptr)
	{
		delete[] StoragePtr;
	}

	StatID = TStatId(StatIDStringStorage);
#endif
}
#endif
FUObjectArray::FUObjectArray()
: ObjFirstGCIndex(0)
, ObjLastNonGCIndex(INDEX_NONE)
, MaxObjectsNotConsideredByGC(0)
, OpenForDisregardForGC(!HACK_HEADER_GENERATOR)
, MasterSerialNumber(START_SERIAL_NUMBER)
{
	GCoreObjectArrayForDebugVisualizers = &GUObjectArray.ObjObjects;
}

void FUObjectArray::AllocateObjectPool(int32 InMaxUObjects, int32 InMaxObjectsNotConsideredByGC, bool bPreAllocateObjectArray)
{
	check(IsInGameThread());

	MaxObjectsNotConsideredByGC = InMaxObjectsNotConsideredByGC;

	// GObjFirstGCIndex is the index at which the garbage collector will start for the mark phase.
	// If disregard for GC is enabled this will be set to an invalid value so that later we
	// know if disregard for GC pool has already been closed (at least once)
	ObjFirstGCIndex = DisregardForGCEnabled() ? -1 : 0;

	// Pre-size array.
	check(ObjObjects.Num() == 0);
	UE_CLOG(InMaxUObjects <= 0, LogUObjectArray, Fatal, TEXT("Max UObject count is invalid. It must be a number that is greater than 0."));
	ObjObjects.PreAllocate(InMaxUObjects, bPreAllocateObjectArray);

	if (MaxObjectsNotConsideredByGC > 0)
	{
		ObjObjects.AddRange(MaxObjectsNotConsideredByGC);
	}
}

void FUObjectArray::OpenDisregardForGC()
{
	check(IsInGameThread());
	check(!OpenForDisregardForGC);
	OpenForDisregardForGC = true;
	UE_LOG(LogUObjectArray, Log, TEXT("OpenDisregardForGC: %d/%d objects in disregard for GC pool"), ObjLastNonGCIndex + 1, MaxObjectsNotConsideredByGC);
}

void FUObjectArray::CloseDisregardForGC()
{
#if THREADSAFE_UOBJECTS
	FScopeLock ObjObjectsLock(&ObjObjectsCritical);
#else
	// Disregard from GC pool is only available from the game thread, at least for now
	check(IsInGameThread());
#endif

	check(OpenForDisregardForGC);

	// Make sure all classes that have been loaded/created so far are properly initialized
	if (!IsEngineExitRequested())
	{
		ProcessNewlyLoadedUObjects();

		UClass::AssembleReferenceTokenStreams();

		if (GIsInitialLoad)
		{
			// Iterate over all objects and mark them to be part of root set.
			int32 NumAlwaysLoadedObjects = 0;
			int32 NumRootObjects = 0;
			for (FThreadSafeObjectIterator It; It; ++It)
			{
				UObject* Object = *It;
				if (Object->IsSafeForRootSet())
				{
					NumRootObjects++;
					Object->AddToRoot();
				}
				else if (Object->IsRooted())
				{
					Object->RemoveFromRoot();
				}
				NumAlwaysLoadedObjects++;
			}

			UE_LOG(LogUObjectArray, Log, TEXT("%i objects as part of root set at end of initial load."), NumAlwaysLoadedObjects);
			if (GUObjectArray.DisregardForGCEnabled())
			{
				UE_LOG(LogUObjectArray, Log, TEXT("%i objects are not in the root set, but can never be destroyed because they are in the DisregardForGC set."), NumAlwaysLoadedObjects - NumRootObjects);
			}

			GUObjectAllocator.BootMessage();
		}
	}

	// When disregard for GC pool is closed, make sure the first GC index is set after the last non-GC index.
	// We do allow here for some slack if MaxObjectsNotConsideredByGC > (ObjLastNonGCIndex + 1) so that disregard for GC pool
	// can be re-opened later.
	ObjFirstGCIndex = FMath::Max(ObjFirstGCIndex, ObjLastNonGCIndex + 1);

	UE_LOG(LogUObjectArray, Log, TEXT("CloseDisregardForGC: %d/%d objects in disregard for GC pool"), ObjLastNonGCIndex + 1, MaxObjectsNotConsideredByGC);	

	OpenForDisregardForGC = false;
	GIsInitialLoad = false;
}

void FUObjectArray::DisableDisregardForGC()
{
	MaxObjectsNotConsideredByGC = 0;
	ObjFirstGCIndex = 0;
	if (IsOpenForDisregardForGC())
	{
		CloseDisregardForGC();
	}
}

void FUObjectArray::AllocateUObjectIndex(UObjectBase* Object, bool bMergingThreads /*= false*/)
{
	int32 Index = INDEX_NONE;
	check(Object->InternalIndex == INDEX_NONE || bMergingThreads);

	LockInternalArray();

	// Special non- garbage collectable range.
	if (OpenForDisregardForGC && DisregardForGCEnabled())
	{
		Index = ++ObjLastNonGCIndex;
		// Check if we're not out of bounds, unless there hasn't been any gc objects yet
		UE_CLOG(ObjLastNonGCIndex >= MaxObjectsNotConsideredByGC && ObjFirstGCIndex >= 0, LogUObjectArray, Fatal, TEXT("Unable to add more objects to disregard for GC pool (Max: %d)"), MaxObjectsNotConsideredByGC);
		// If we haven't added any GC objects yet, it's fine to keep growing the disregard pool past its initial size.
		if (ObjLastNonGCIndex >= MaxObjectsNotConsideredByGC)
		{
			Index = ObjObjects.AddSingle();
			check(Index == ObjLastNonGCIndex);
		}
		MaxObjectsNotConsideredByGC = FMath::Max(MaxObjectsNotConsideredByGC, ObjLastNonGCIndex + 1);
	}
	// Regular pool/ range.
	else
	{
		if (ObjAvailableList.Num() > 0)
		{
			Index = ObjAvailableList.Pop();
#if UE_GC_TRACK_OBJ_AVAILABLE
			const int32 AvailableCount = ObjAvailableCount.Decrement();
			checkSlow(AvailableCount >= 0);
#endif
		}
		else
		{
			// Make sure ObjFirstGCIndex is valid, otherwise we didn't close the disregard for GC set
			check(ObjFirstGCIndex >= 0);
			Index = ObjObjects.AddSingle();			
		}
		check(Index >= ObjFirstGCIndex && Index > ObjLastNonGCIndex);
	}
	// Add to global table.
	FUObjectItem* ObjectItem = IndexToObject(Index);
	UE_CLOG(ObjectItem->Object != nullptr, LogUObjectArray, Fatal, TEXT("Attempting to add %s at index %d but another object (0x%016llx) exists at that index!"), *Object->GetFName().ToString(), Index, (int64)(PTRINT)ObjectItem->Object);
	ObjectItem->ResetSerialNumberAndFlags();
	// At this point all not-compiled-in objects are not fully constructed yet and this is the earliest we can mark them as such
	ObjectItem->SetFlags(EInternalObjectFlags::PendingConstruction);
	ObjectItem->Object = Object;		
	Object->InternalIndex = Index;

	UnlockInternalArray();

	//  @todo: threading: lock UObjectCreateListeners
	for (int32 ListenerIndex = 0; ListenerIndex < UObjectCreateListeners.Num(); ListenerIndex++)
	{
		UObjectCreateListeners[ListenerIndex]->NotifyUObjectCreated(Object,Index);
	}
}

/**
 * Reset the serial number from the game thread to invalidate all weak object pointers to it
 *
 * @param Object to reset
 */
void FUObjectArray::ResetSerialNumber(UObjectBase* Object)
{
	int32 Index = Object->InternalIndex;
	FUObjectItem* ObjectItem = IndexToObject(Index);
	checkSlow(ObjectItem);
	ObjectItem->SerialNumber = 0;
}

/**
 * Removes an object from delete listeners
 *
 * @param Object to remove from delete listeners
 */
void FUObjectArray::RemoveObjectFromDeleteListeners(UObjectBase* Object)
{
#if THREADSAFE_UOBJECTS
	FScopeLock UObjectDeleteListenersLock(&UObjectDeleteListenersCritical);
#endif
	int32 Index = Object->InternalIndex;
	check(Index >= 0);
	// Iterate in reverse order so that when one of the listeners removes itself from the array inside of NotifyUObjectDeleted we don't skip the next listener.
	for (int32 ListenerIndex = UObjectDeleteListeners.Num() - 1; ListenerIndex >= 0; --ListenerIndex)
	{
		UObjectDeleteListeners[ListenerIndex]->NotifyUObjectDeleted(Object, Index);
	}
}

/**
 * Returns a UObject index to the global uobject array
 *
 * @param Object object to free
 */
void FUObjectArray::FreeUObjectIndex(UObjectBase* Object)
{
	// This should only be happening on the game thread (GC runs only on game thread when it's freeing objects)
	check(IsInGameThread() || IsInGarbageCollectorThread());

	// No need to call LockInternalArray(); here as it should already be locked by GC

	int32 Index = Object->InternalIndex;
	FUObjectItem* ObjectItem = IndexToObject(Index);
	UE_CLOG(ObjectItem->Object != Object, LogUObjectArray, Fatal, TEXT("Removing object (0x%016llx) at index %d but the index points to a different object (0x%016llx)!"), (int64)(PTRINT)Object, Index, (int64)(PTRINT)ObjectItem->Object);
	ObjectItem->Object = nullptr;
	ObjectItem->ResetSerialNumberAndFlags();

	// You cannot safely recycle indicies in the non-GC range
	// No point in filling this list when doing exit purge. Nothing should be allocated afterwards anyway.
	if (Index > ObjLastNonGCIndex && !GExitPurge)  
	{
		ObjAvailableList.Add(Index);
#if UE_GC_TRACK_OBJ_AVAILABLE
		ObjAvailableCount.Increment();
#endif
	}
}

/**
 * Adds a creation listener
 *
 * @param Listener listener to notify when an object is deleted
 */
void FUObjectArray::AddUObjectCreateListener(FUObjectCreateListener* Listener)
{
	check(!UObjectCreateListeners.Contains(Listener));
	UObjectCreateListeners.Add(Listener);
}

/**
 * Removes a listener for object creation
 *
 * @param Listener listener to remove
 */
void FUObjectArray::RemoveUObjectCreateListener(FUObjectCreateListener* Listener)
{
	int32 NumRemoved = UObjectCreateListeners.RemoveSingleSwap(Listener);
	check(NumRemoved==1);
}

/**
 * Checks whether object is part of permanent object pool.
 *
 * @param Listener listener to notify when an object is deleted
 */
void FUObjectArray::AddUObjectDeleteListener(FUObjectDeleteListener* Listener)
{
#if THREADSAFE_UOBJECTS
	FScopeLock UObjectDeleteListenersLock(&UObjectDeleteListenersCritical);
#endif
	check(!UObjectDeleteListeners.Contains(Listener));
	UObjectDeleteListeners.Add(Listener);
}

/**
 * removes a listener for object deletion
 *
 * @param Listener listener to remove
 */
void FUObjectArray::RemoveUObjectDeleteListener(FUObjectDeleteListener* Listener)
{
#if THREADSAFE_UOBJECTS
	FScopeLock UObjectDeleteListenersLock(&UObjectDeleteListenersCritical);
#endif
	UObjectDeleteListeners.RemoveSingleSwap(Listener);
}



/**
 * Checks if a UObject index is valid
 *
 * @param	Object object to test for validity
 * @return	true if this index is valid
 */
bool FUObjectArray::IsValid(const UObjectBase* Object) const 
{ 
	int32 Index = Object->InternalIndex;
	if( Index == INDEX_NONE )
	{
		UE_LOG(LogUObjectArray, Warning, TEXT("Object is not in global object array") );
		return false;
	}
	if( !ObjObjects.IsValidIndex(Index))
	{
		UE_LOG(LogUObjectArray, Warning, TEXT("Invalid object index %i"), Index );
		return false;
	}
	const FUObjectItem& Slot = ObjObjects[Index];
	if( Slot.Object == NULL )
	{
		UE_LOG(LogUObjectArray, Warning, TEXT("Empty slot") );
		return false;
	}
	if( Slot.Object != Object )
	{
		UE_LOG(LogUObjectArray, Warning, TEXT("Other object in slot") );
		return false;
	}
	return true;
}

int32 FUObjectArray::AllocateSerialNumber(int32 Index)
{
	FUObjectItem* ObjectItem = IndexToObject(Index);
	checkSlow(ObjectItem);

	volatile int32 *SerialNumberPtr = &ObjectItem->SerialNumber;
	int32 SerialNumber = *SerialNumberPtr;
	if (!SerialNumber)
	{
		SerialNumber = MasterSerialNumber.Increment();
		UE_CLOG(SerialNumber <= START_SERIAL_NUMBER, LogUObjectArray, Fatal, TEXT("UObject serial numbers overflowed (trying to allocate serial number %d)."), SerialNumber);
		int32 ValueWas = FPlatformAtomics::InterlockedCompareExchange((int32*)SerialNumberPtr, SerialNumber, 0);
		if (ValueWas != 0)
		{
			// someone else go it first, use their value
			SerialNumber = ValueWas;
		}
	}
	checkSlow(SerialNumber > START_SERIAL_NUMBER);
	return SerialNumber;
}

/**
 * Clears some internal arrays to get rid of false memory leaks
 */
void FUObjectArray::ShutdownUObjectArray()
{
	{
#if THREADSAFE_UOBJECTS
		FScopeLock UObjectDeleteListenersLock(&UObjectDeleteListenersCritical);
#endif
		for (int32 Index = UObjectDeleteListeners.Num() - 1; Index >= 0; --Index)
		{
			FUObjectDeleteListener* Listener = UObjectDeleteListeners[Index];
			Listener->OnUObjectArrayShutdown();
		}
		UE_CLOG(UObjectDeleteListeners.Num(), LogUObjectArray, Fatal, TEXT("All UObject delete listeners should be unregistered when shutting down the UObject array"));
	}
	{
		for (int32 Index = UObjectCreateListeners.Num() - 1; Index >= 0; --Index)
		{
			FUObjectCreateListener* Listener = UObjectCreateListeners[Index];
			Listener->OnUObjectArrayShutdown();
		}
		UE_CLOG(UObjectCreateListeners.Num(), LogUObjectArray, Fatal, TEXT("All UObject delete listeners should be unregistered when shutting down the UObject array"));
	}
}
