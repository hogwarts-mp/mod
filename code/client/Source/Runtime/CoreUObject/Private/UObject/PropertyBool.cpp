// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "UObject/UnrealTypePrivate.h"
#include "UObject/PropertyHelper.h"

// WARNING: This should always be the last include in any file that needs it (except .generated.h)
#include "UObject/UndefineUPropertyMacros.h"

/*-----------------------------------------------------------------------------
	FBoolProperty.
-----------------------------------------------------------------------------*/

IMPLEMENT_FIELD(FBoolProperty)

FBoolProperty::FBoolProperty(FFieldVariant InOwner, const FName& InName, EObjectFlags InObjectFlags)
: FProperty(InOwner, InName, InObjectFlags)
	, FieldSize(0)
	, ByteOffset(0)
	, ByteMask(1)
	, FieldMask(1)
{
	SetBoolSize(1, false, 1);
}

FBoolProperty::FBoolProperty(FFieldVariant InOwner, const FName& InName, EObjectFlags InObjectFlags, int32 InOffset, EPropertyFlags InFlags, uint32 InBitMask, uint32 InElementSize, bool bIsNativeBool)
	: FProperty(InOwner, InName, InObjectFlags, InOffset, InFlags | CPF_HasGetValueTypeHash)
	, FieldSize(0)
	, ByteOffset(0)
	, ByteMask(1)
	, FieldMask(1)
{
	SetBoolSize(InElementSize, bIsNativeBool, InBitMask);
}

#if WITH_EDITORONLY_DATA
FBoolProperty::FBoolProperty(UField* InField)
	: FProperty(InField)
{
	UBoolProperty* SourceProperty = CastChecked<UBoolProperty>(InField);
	FieldSize = SourceProperty->FieldSize;
	ByteOffset = SourceProperty->ByteOffset;
	ByteMask = SourceProperty->ByteMask;
	FieldMask = SourceProperty->FieldMask;
}
#endif // WITH_EDITORONLY_DATA

void FBoolProperty::PostDuplicate(const FField& InField)
{
	const FBoolProperty& Source = static_cast<const FBoolProperty&>(InField);
	FieldSize = Source.FieldSize;
	ByteOffset = Source.ByteOffset;
	ByteMask = Source.ByteMask;
	FieldMask = Source.FieldMask;
	Super::PostDuplicate(InField);
}

void FBoolProperty::SetBoolSize( const uint32 InSize, const bool bIsNativeBool, const uint32 InBitMask /*= 0*/ )
{
	if (bIsNativeBool)
	{
		PropertyFlags |= (CPF_IsPlainOldData | CPF_NoDestructor | CPF_ZeroConstructor);
	}
	else
	{
		PropertyFlags &= ~(CPF_IsPlainOldData | CPF_ZeroConstructor);
		PropertyFlags |= CPF_NoDestructor;
	}
	uint32 TestBitmask = InBitMask ? InBitMask : 1;
	ElementSize = InSize;
	FieldSize = (uint8)ElementSize;
	ByteOffset = 0;
	if (bIsNativeBool)
	{		
		ByteMask = true;
		FieldMask = 255;
	}
	else
	{
		// Calculate ByteOffset and get ByteMask.
		for (ByteOffset = 0; ByteOffset < InSize && ((ByteMask = *((uint8*)&TestBitmask + ByteOffset)) == 0); ByteOffset++);
		FieldMask = ByteMask;
	}
	check((int32)FieldSize == ElementSize);
	check(ElementSize != 0);
	check(FieldMask != 0);
	check(ByteMask != 0);
}

int32 FBoolProperty::GetMinAlignment() const
{
	int32 Alignment = 0;
	switch(ElementSize)
	{
	case sizeof(uint8):
		Alignment = alignof(uint8); break;
	case sizeof(uint16):
		Alignment = alignof(uint16); break;
	case sizeof(uint32):
		Alignment = alignof(uint32); break;
	case sizeof(uint64):
		Alignment = alignof(uint64); break;
	default:
		UE_LOG(LogProperty, Fatal, TEXT("Unsupported FBoolProperty %s size %d."), *GetName(), (int32)ElementSize);
	}
	return Alignment;
}
void FBoolProperty::LinkInternal(FArchive& Ar)
{
	check(FieldSize != 0);
	ElementSize = FieldSize;
	if (IsNativeBool())
	{
		PropertyFlags |= (CPF_IsPlainOldData | CPF_NoDestructor | CPF_ZeroConstructor);
	}
	else
	{
		PropertyFlags &= ~(CPF_IsPlainOldData | CPF_ZeroConstructor);
		PropertyFlags |= CPF_NoDestructor;
	}
}
void FBoolProperty::Serialize( FArchive& Ar )
{
	Super::Serialize( Ar );

	Ar << FieldSize;
	Ar << ByteOffset;
	Ar << ByteMask;
	Ar << FieldMask;

	// Serialize additional flags which will help to identify this FBoolProperty type and size.
	uint8 BoolSize = (uint8)ElementSize;
	Ar << BoolSize;
	uint8 NativeBool = false;
	if( Ar.IsLoading())
	{
		Ar << NativeBool;
		//if (!IsPendingKill())
		{
			SetBoolSize( BoolSize, !!NativeBool );
		}
	}
	else
	{
		NativeBool = Ar.IsSaving() ? (IsNativeBool() ? 1 : 0) : 0;
		Ar << NativeBool;
	}
}
FString FBoolProperty::GetCPPType( FString* ExtendedTypeText/*=NULL*/, uint32 CPPExportFlags/*=0*/ ) const
{
	check(FieldSize != 0);

	if (IsNativeBool() 
		|| ((CPPExportFlags & (CPPF_Implementation|CPPF_ArgumentOrReturnValue)) == (CPPF_Implementation|CPPF_ArgumentOrReturnValue))
		|| ((CPPExportFlags & CPPF_BlueprintCppBackend) != 0))
	{
		// Export as bool if this is actually a bool or it's being exported as a return value of C++ function definition.
		return TEXT("bool");
	}
	else
	{
		// Bitfields
		switch(ElementSize)
		{
		case sizeof(uint64):
			return TEXT("uint64");
		case sizeof(uint32):
			return TEXT("uint32");
		case sizeof(uint16):
			return TEXT("uint16");
		case sizeof(uint8):
			return TEXT("uint8");
		default:
			UE_LOG(LogProperty, Fatal, TEXT("Unsupported FBoolProperty %s size %d."), *GetName(), ElementSize);
			break;
		}
	}
	return TEXT("uint32");
}

FString FBoolProperty::GetCPPTypeForwardDeclaration() const
{
	return FString();
}

FString FBoolProperty::GetCPPMacroType( FString& ExtendedTypeText ) const
{
	check(FieldSize != 0);
	if (IsNativeBool())
	{
		return TEXT("UBOOL");
	}
	else
	{
		switch(ElementSize)
		{
		case sizeof(uint64):
			return TEXT("UBOOL64");
		case sizeof(uint32):
			return TEXT("UBOOL32");
		case sizeof(uint16):
			return TEXT("UBOOL16");
		case sizeof(uint8):
			return TEXT("UBOOL8");
		default:
			UE_LOG(LogProperty, Fatal, TEXT("Unsupported FBoolProperty %s size %d."), *GetName(), ElementSize);
			break;
		}
	}
	return TEXT("UBOOL32");
}

template<typename T>
void LoadFromType(FBoolProperty* Property, const FPropertyTag& Tag, FStructuredArchive::FSlot Slot, uint8* Data)
{
	T IntValue;
	Slot << IntValue;

	if (IntValue != 0)
	{
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		if (IntValue != 1)
		{
			UE_LOG(LogClass, Log, TEXT("Loading %s property (%s) that is now a bool - value '%d', expecting 0 or 1. Value set to true."), *Tag.Type.ToString(), *Property->GetPathName(), IntValue);
		}
#endif
		Property->SetPropertyValue_InContainer(Data, true, Tag.ArrayIndex);
	}
	else
	{
		Property->SetPropertyValue_InContainer(Data, false, Tag.ArrayIndex);
	}
}

EConvertFromTypeResult FBoolProperty::ConvertFromType(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot, uint8* Data, UStruct* DefaultsStruct)
{
	if (Tag.Type == NAME_IntProperty)
	{
		LoadFromType<int32>(this, Tag, Slot, Data);
	}
	else if (Tag.Type == NAME_Int8Property)
	{
		LoadFromType<int8>(this, Tag, Slot, Data);
	}
	else if (Tag.Type == NAME_Int16Property)
	{
		LoadFromType<int16>(this, Tag, Slot, Data);
	}
	else if (Tag.Type == NAME_Int64Property)
	{
		LoadFromType<int64>(this, Tag, Slot, Data);
	}
	else if (Tag.Type == NAME_ByteProperty)
	{
		// if the byte property was an enum we won't allow a conversion to bool
		if (Tag.EnumName == NAME_None)
		{
			// If we're a nested property the EnumName tag got lost, don't allow this
			if (GetOwner<FProperty>())
			{
				return EConvertFromTypeResult::UseSerializeItem;
			}

			LoadFromType<uint8>(this, Tag, Slot, Data);
		}
		else
		{
			return EConvertFromTypeResult::UseSerializeItem;
		}
	}
	else if (Tag.Type == NAME_UInt16Property)
	{
		LoadFromType<uint16>(this, Tag, Slot, Data);
	}
	else if (Tag.Type == NAME_UInt32Property)
	{
		LoadFromType<uint32>(this, Tag, Slot, Data);
	}
	else if (Tag.Type == NAME_UInt64Property)
	{
		LoadFromType<uint64>(this, Tag, Slot, Data);
	}
	else
	{
		return EConvertFromTypeResult::UseSerializeItem;
	}

	return EConvertFromTypeResult::Converted;
}

void FBoolProperty::ExportTextItem( FString& ValueStr, const void* PropertyValue, const void* DefaultValue, UObject* Parent, int32 PortFlags, UObject* ExportRootScope ) const
{
	check(FieldSize != 0);
	const uint8* ByteValue = (uint8*)PropertyValue + ByteOffset;
	const bool bValue = 0 != ((*ByteValue) & FieldMask);
	const TCHAR* Temp = nullptr;
	if (0 != (PortFlags & PPF_ExportCpp))
	{
		Temp = (bValue ? TEXT("true") : TEXT("false"));
	}
	else
	{
		Temp = (bValue ? TEXT("True") : TEXT("False"));
	}
	ValueStr += FString::Printf( TEXT("%s"), Temp );
}
const TCHAR* FBoolProperty::ImportText_Internal( const TCHAR* Buffer, void* Data, int32 PortFlags, UObject* Parent, FOutputDevice* ErrorText ) const
{
	FString Temp; 
	Buffer = FPropertyHelpers::ReadToken( Buffer, Temp );
	if( !Buffer )
	{
		return NULL;
	}

	check(FieldSize != 0);
	uint8* ByteValue = (uint8*)Data + ByteOffset;

	const FCoreTexts& CoreTexts = FCoreTexts::Get();
	if( Temp==TEXT("1") || Temp==TEXT("True") || Temp==*CoreTexts.True.ToString() || Temp == TEXT("Yes") || Temp == *CoreTexts.Yes.ToString() )
	{
		*ByteValue |= ByteMask;
	}
	else 
	if( Temp==TEXT("0") || Temp==TEXT("False") || Temp==*CoreTexts.False.ToString() || Temp == TEXT("No") || Temp == *CoreTexts.No.ToString() )
	{
		*ByteValue &= ~FieldMask;
	}
	else
	{
		//UE_LOG(LogProperty, Log,  "Import: Failed to get bool" );
		return NULL;
	}
	return Buffer;
}
bool FBoolProperty::Identical( const void* A, const void* B, uint32 PortFlags ) const
{
	check(FieldSize != 0);
	const uint8* ByteValueA = (const uint8*)A + ByteOffset;
	const uint8* ByteValueB = (const uint8*)B + ByteOffset;
	return ((*ByteValueA ^ (B ? *ByteValueB : 0)) & FieldMask) == 0;
}

void FBoolProperty::SerializeItem( FStructuredArchive::FSlot Slot, void* Value, void const* Defaults ) const
{
	check(FieldSize != 0);
	uint8* ByteValue = (uint8*)Value + ByteOffset;
	uint8 B = (*ByteValue & FieldMask) ? 1 : 0;
	Slot << B;
	*ByteValue = ((*ByteValue) & ~FieldMask) | (B ? ByteMask : 0);
}

bool FBoolProperty::NetSerializeItem( FArchive& Ar, UPackageMap* Map, void* Data, TArray<uint8> * MetaData ) const
{
	check(FieldSize != 0);
	uint8* ByteValue = (uint8*)Data + ByteOffset;
	uint8 Value = ((*ByteValue & FieldMask)!=0);
	Ar.SerializeBits( &Value, 1 );
	*ByteValue = ((*ByteValue) & ~FieldMask) | (Value ? ByteMask : 0);
	return true;
}
void FBoolProperty::CopyValuesInternal( void* Dest, void const* Src, int32 Count  ) const
{
	check(FieldSize != 0 && !IsNativeBool());
	for (int32 Index = 0; Index < Count; Index++)
	{
		uint8* DestByteValue = (uint8*)Dest + Index * ElementSize + ByteOffset;
		uint8* SrcByteValue = (uint8*)Src + Index * ElementSize + ByteOffset;
		*DestByteValue = (*DestByteValue & ~FieldMask) | (*SrcByteValue & FieldMask);
	}
}
void FBoolProperty::ClearValueInternal( void* Data ) const
{
	check(FieldSize != 0);
	uint8* ByteValue = (uint8*)Data + ByteOffset;
	*ByteValue &= ~FieldMask;
}

void FBoolProperty::InitializeValueInternal( void* Data ) const
{
	check(FieldSize != 0);
	uint8* ByteValue = (uint8*)Data + ByteOffset;
	*ByteValue &= ~FieldMask;
}

uint32 FBoolProperty::GetValueTypeHashInternal(const void* Src) const
{
	return GetTypeHash(*(const bool*)Src);
}

#include "UObject/DefineUPropertyMacros.h"