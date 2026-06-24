// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UnrealType.h"

/*-----------------------------------------------------------------------------
	FWeakObjectProperty.
-----------------------------------------------------------------------------*/
IMPLEMENT_FIELD(FWeakObjectProperty)

FString FWeakObjectProperty::GetCPPType( FString* ExtendedTypeText/*=NULL*/, uint32 CPPExportFlags/*=0*/ ) const
{
	return GetCPPTypeCustom(ExtendedTypeText, CPPExportFlags,
		FString::Printf(TEXT("%s%s"), PropertyClass->GetPrefixCPP(), *PropertyClass->GetName()));
}

FString FWeakObjectProperty::GetCPPTypeCustom(FString* ExtendedTypeText, uint32 CPPExportFlags, const FString& InnerNativeTypeName) const
{
	ensure(!InnerNativeTypeName.IsEmpty());
	if (PropertyFlags & CPF_AutoWeak)
	{
		return FString::Printf(TEXT("TAutoWeakObjectPtr<%s>"), *InnerNativeTypeName);
	}
	return FString::Printf(TEXT("TWeakObjectPtr<%s>"), *InnerNativeTypeName);
}

FString FWeakObjectProperty::GetCPPTypeForwardDeclaration() const
{
	return FString::Printf(TEXT("class %s%s;"), PropertyClass->GetPrefixCPP(), *PropertyClass->GetName());
}

FString FWeakObjectProperty::GetCPPMacroType( FString& ExtendedTypeText ) const
{
	if (PropertyFlags & CPF_AutoWeak)
	{
		ExtendedTypeText = FString::Printf(TEXT("TAutoWeakObjectPtr<%s%s>"), PropertyClass->GetPrefixCPP(), *PropertyClass->GetName());
		return TEXT("AUTOWEAKOBJECT");
	}
	ExtendedTypeText = FString::Printf(TEXT("TWeakObjectPtr<%s%s>"), PropertyClass->GetPrefixCPP(), *PropertyClass->GetName());
	return TEXT("WEAKOBJECT");
}

void FWeakObjectProperty::SerializeItem( FStructuredArchive::FSlot Slot, void* Value, void const* Defaults ) const
{
	FArchive& UnderlyingArchive = Slot.GetUnderlyingArchive();

	UObject* ObjectValue = GetObjectPropertyValue(Value);
	Slot << *(FWeakObjectPtr*)Value;
	if ((UnderlyingArchive.IsLoading() || UnderlyingArchive.IsModifyingWeakAndStrongReferences()) && ObjectValue != GetObjectPropertyValue(Value))
	{
		CheckValidObject(Value);
	}
}

UObject* FWeakObjectProperty::GetObjectPropertyValue(const void* PropertyValueAddress) const
{
	return GetPropertyValue(PropertyValueAddress).Get();
}

void FWeakObjectProperty::SetObjectPropertyValue(void* PropertyValueAddress, UObject* Value) const
{
	SetPropertyValue(PropertyValueAddress, TCppType(Value));
}

uint32 FWeakObjectProperty::GetValueTypeHashInternal(const void* Src) const
{
	return GetTypeHash(*(FWeakObjectPtr*)Src);
}
