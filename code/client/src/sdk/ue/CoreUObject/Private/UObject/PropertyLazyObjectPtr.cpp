// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/LazyObjectPtr.h"
#include "UObject/UnrealType.h"
#include "UObject/PropertyHelper.h"

/*-----------------------------------------------------------------------------
	FLazyObjectProperty.
-----------------------------------------------------------------------------*/
IMPLEMENT_FIELD(FLazyObjectProperty)

FString FLazyObjectProperty::GetCPPType(FString* ExtendedTypeText/*=NULL*/, uint32 CPPExportFlags/*=0*/) const
{
	return GetCPPTypeCustom(ExtendedTypeText, CPPExportFlags,
		FString::Printf(TEXT("%s%s"), PropertyClass->GetPrefixCPP(), *PropertyClass->GetName()));
}

FString FLazyObjectProperty::GetCPPTypeCustom(FString* ExtendedTypeText, uint32 CPPExportFlags, const FString& InnerNativeTypeName) const
{
	ensure(!InnerNativeTypeName.IsEmpty());
	return FString::Printf(TEXT("TLazyObjectPtr<%s>"), *InnerNativeTypeName);
}

FString FLazyObjectProperty::GetCPPTypeForwardDeclaration() const
{
	return FString::Printf(TEXT("class %s%s;"), PropertyClass->GetPrefixCPP(), *PropertyClass->GetName());
}

FString FLazyObjectProperty::GetCPPMacroType( FString& ExtendedTypeText ) const
{
	ExtendedTypeText = FString::Printf(TEXT("TLazyObjectPtr<%s%s>"), PropertyClass->GetPrefixCPP(), *PropertyClass->GetName());
	return TEXT("LAZYOBJECT");
}

FName FLazyObjectProperty::GetID() const
{
	return NAME_LazyObjectProperty;
}

void FLazyObjectProperty::SerializeItem( FStructuredArchive::FSlot Slot, void* Value, void const* Defaults ) const
{
	FArchive& UnderlyingArchive = Slot.GetUnderlyingArchive();

	// We never serialize our reference while the garbage collector is harvesting references
	// to objects, because we don't want lazy pointers to keep objects from being garbage collected

	if( !UnderlyingArchive.IsObjectReferenceCollector() || UnderlyingArchive.IsModifyingWeakAndStrongReferences() )
	{
		UObject* ObjectValue = GetObjectPropertyValue(Value);

		Slot << *(FLazyObjectPtr*)Value;

		if ((UnderlyingArchive.IsLoading() || UnderlyingArchive.IsModifyingWeakAndStrongReferences()) && ObjectValue != GetObjectPropertyValue(Value))
		{
			CheckValidObject(Value);
		}
	}
	else
	{
		// TODO: This isn't correct, but it keeps binary serialization happy. We should ALWAYS be serializing the pointer
		// to the archive in this function, and allowing the underlying archive to ignore it if necessary
		Slot.EnterStream();
	}
}

bool FLazyObjectProperty::Identical( const void* A, const void* B, uint32 PortFlags ) const
{
	FLazyObjectPtr ObjectA = A ? *((FLazyObjectPtr*)A) : FLazyObjectPtr();
	FLazyObjectPtr ObjectB = B ? *((FLazyObjectPtr*)B) : FLazyObjectPtr();

	// Compare actual pointers. We don't do this during PIE because we want to be sure to serialize everything. An example is the LevelScriptActor being serialized against its CDO,
	// which contains actor references. We want to serialize those references so they are fixed up.
	const bool bDuplicatingForPIE = (PortFlags&PPF_DuplicateForPIE) != 0;
	bool bResult = !bDuplicatingForPIE ? (ObjectA == ObjectB) : false;
	// always serialize the cross level references, because they could be NULL
	// @todo: okay, this is pretty hacky overall - we should have a PortFlag or something
	// that is set during SavePackage. Other times, we don't want to immediately return false
	// (instead of just this ExportDefProps case)
	// instance testing
	if (!bResult && ObjectA.IsValid() && ObjectB.IsValid() && ObjectA->GetClass() == ObjectB->GetClass())
	{
		bool bPerformDeepComparison = (PortFlags&PPF_DeepComparison) != 0;
		if ((PortFlags&PPF_DeepCompareInstances) && !bPerformDeepComparison)
		{
			bPerformDeepComparison = ObjectA->IsTemplate() != ObjectB->IsTemplate();
		}

		if (!bResult && bPerformDeepComparison)
		{
			// In order for deep comparison to be match they both need to have the same name and that name needs to be included in the instancing table for the class
			if (ObjectA->GetFName() == ObjectB->GetFName() && ObjectA->GetClass()->GetDefaultSubobjectByName(ObjectA->GetFName()))
			{
				checkSlow(ObjectA->IsDefaultSubobject() && ObjectB->IsDefaultSubobject() && ObjectA->GetClass()->GetDefaultSubobjectByName(ObjectA->GetFName()) == ObjectB->GetClass()->GetDefaultSubobjectByName(ObjectB->GetFName())); // equivalent
				bResult = AreInstancedObjectsIdentical(ObjectA.Get(), ObjectB.Get(), PortFlags);
			}
		}
	}
	return bResult;
}

UObject* FLazyObjectProperty::GetObjectPropertyValue(const void* PropertyValueAddress) const
{
	return GetPropertyValue(PropertyValueAddress).Get();
}

void FLazyObjectProperty::SetObjectPropertyValue(void* PropertyValueAddress, UObject* Value) const
{
	SetPropertyValue(PropertyValueAddress, TCppType(Value));
}

bool FLazyObjectProperty::AllowCrossLevel() const
{
	return true;
}

uint32 FLazyObjectProperty::GetValueTypeHashInternal(const void* Src) const
{
	return GetTypeHash(GetPropertyValue(Src));
}

