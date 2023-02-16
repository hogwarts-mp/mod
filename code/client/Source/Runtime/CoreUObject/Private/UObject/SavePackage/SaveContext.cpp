// Copyright Epic Games, Inc. All Rights Reserved.

#include "SaveContext.h"

void FSaveContext::MarkUnsaveable(UObject* InObject)
{
	if (IsUnsaveable(InObject))
	{
		InObject->SetFlags(RF_Transient);
	}

	// if this is the class default object, make sure it's not
	// marked transient for any reason, as we need it to be saved
	// to disk (unless it's associated with a transient generated class)
	ensureAlways(!InObject->HasAllFlags(RF_ClassDefaultObject | RF_Transient) || (InObject->GetClass()->ClassGeneratedBy != nullptr && InObject->GetClass()->HasAnyFlags(RF_Transient)));
}

bool FSaveContext::IsUnsaveable(UObject* InObject) const
{
	UObject* Obj = InObject;
	while (Obj)
	{
		// if the object class is abstract, has been marked as deprecated, there is a newer version that exist or the class is maked transient, then the object is unsaveable
		if (Obj->GetClass()->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists | CLASS_Transient) && !Obj->HasAnyFlags(RF_ClassDefaultObject))
		{
			if (!InObject->IsPendingKill() 
				&& InObject->GetOutermost() == GetPackage()
				&& Obj->GetClass()->HasAnyClassFlags(CLASS_Deprecated))
			{
				UE_LOG(LogSavePackage, Warning, TEXT("%s has a deprecated, abstract or transient class outer %s, so it will not be saved"), *InObject->GetFullName(), *Obj->GetFullName());
			}

			// there used to be a check for reference if the class had the CLASS_HasInstancedReference,
			// those reference were outer-ed to the object being flagged as unsaveable, making them unsaveable as well without having to look for them
			return true;
		}

		// pending kill object are unsaveable
		if (Obj->IsPendingKill())
		{
			return true;
		}

		// transient object are considered unsaveable if non native
		if (Obj->HasAnyFlags(RF_Transient) && !Obj->IsNative())
		{
			return true;
		}

		Obj = Obj->GetOuter();
	}
	return false;	
}
