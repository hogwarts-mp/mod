// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GCObjectReferencer.cpp: Implementation of UGCObjectReferencer
=============================================================================*/

#include "CoreMinimal.h"
#include "Misc/ScopeLock.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "Templates/Casts.h"
#include "UObject/GCObject.h"

// Global GC state flags
extern bool GObjIncrementalPurgeIsInProgress;
extern bool GObjUnhashUnreachableIsInProgress;

void UGCObjectReferencer::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UGCObjectReferencer* This = CastChecked<UGCObjectReferencer>(InThis);

	checkSlow(!This->bIsAddingReferencedObjects);
	This->bIsAddingReferencedObjects = true;
	// Note we're not locking ReferencedObjectsCritical here because we guard
	// against adding new references during GC in AddObject and RemoveObject.
	// Let each registered object handle its AddReferencedObjects call
	for (FGCObject* Object : This->ReferencedObjects)
	{
		check(Object);
		This->CurrentlySerializingObject = Object;
		Object->AddReferencedObjects(Collector);
	}
	This->CurrentlySerializingObject = nullptr;
	Super::AddReferencedObjects(This, Collector);
	This->bIsAddingReferencedObjects = false;
}

void UGCObjectReferencer::AddObject(FGCObject* Object)
{
	check(Object);
	check(GObjUnhashUnreachableIsInProgress || GObjIncrementalPurgeIsInProgress || !IsGarbageCollecting());	FScopeLock ReferencedObjectsLock(&ReferencedObjectsCritical);
	// Make sure there are no duplicates. Should be impossible...
	checkSlow(!ReferencedObjects.Contains(Object));
	ReferencedObjects.Add(Object);
}

void UGCObjectReferencer::RemoveObject(FGCObject* Object)
{
	check(Object);
	check(GObjUnhashUnreachableIsInProgress || GObjIncrementalPurgeIsInProgress || !IsGarbageCollecting());	FScopeLock ReferencedObjectsLock(&ReferencedObjectsCritical);
	int32 NumRemoved = ReferencedObjects.RemoveSingleSwap(Object);
	check(NumRemoved == 1);
}

bool UGCObjectReferencer::GetReferencerName(UObject* Object, FString& OutName, bool bOnlyIfAddingReferenced) const
{
	if (bOnlyIfAddingReferenced)
	{
		if (!bIsAddingReferencedObjects || !CurrentlySerializingObject)
		{
			return false;
		}
		OutName = CurrentlySerializingObject->GetReferencerName();
		FString ReferencerProperty;
		if (CurrentlySerializingObject->GetReferencerPropertyName(Object, ReferencerProperty))
		{
			OutName += TEXT(":") + ReferencerProperty;
		}
		return true;
	}

	// Let each registered object handle its AddReferencedObjects call
	for (int32 i = 0; i < ReferencedObjects.Num(); i++)
	{
		TArray<UObject*> ObjectArray;
		FReferenceFinder Collector(ObjectArray);

		FGCObject* GCReporter = ReferencedObjects[i];
		check(GCReporter);
		GCReporter->AddReferencedObjects(Collector);

		if (ObjectArray.Contains(Object))
		{
			OutName = GCReporter->GetReferencerName();
			FString ReferencerProperty;
			if (GCReporter->GetReferencerPropertyName(Object, ReferencerProperty))
			{
				OutName += TEXT(":") + ReferencerProperty;
			}
			return true;
		}
	}

	return false;
}

void UGCObjectReferencer::FinishDestroy()
{
	if ( !HasAnyFlags(RF_ClassDefaultObject) )
	{
		// Make sure FGCObjects that are around after exit purge don't
		// reference this object.
		check( FGCObject::GGCObjectReferencer == this );
		FGCObject::GGCObjectReferencer = NULL;
		ReferencedObjects.Empty();
	}

	Super::FinishDestroy();
}

IMPLEMENT_CORE_INTRINSIC_CLASS(UGCObjectReferencer, UObject, 
	{
		Class->ClassAddReferencedObjects = &UGCObjectReferencer::AddReferencedObjects;
	}
);

/** Static used for calling AddReferencedObjects on non-UObject objects */
UGCObjectReferencer* FGCObject::GGCObjectReferencer = NULL;


