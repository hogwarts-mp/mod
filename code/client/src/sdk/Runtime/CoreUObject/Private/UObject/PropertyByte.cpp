// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Class.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/UnrealType.h"
#include "UObject/UnrealTypePrivate.h"
#include "UObject/UObjectThreadContext.h"
#include "Serialization/ArchiveUObjectFromStructuredArchive.h"
#include "Algo/Find.h"
#include "UObject/LinkerLoad.h"
#include "Misc/NetworkVersion.h"

// WARNING: This should always be the last include in any file that needs it (except .generated.h)
#include "UObject/UndefineUPropertyMacros.h"

/*-----------------------------------------------------------------------------
	FByteProperty.
-----------------------------------------------------------------------------*/

IMPLEMENT_FIELD(FByteProperty)

#if WITH_EDITORONLY_DATA
FByteProperty::FByteProperty(UField* InField)
	: TProperty_Numeric(InField)
{
	UByteProperty* SourceProperty = CastChecked<UByteProperty>(InField);
	Enum = SourceProperty->Enum;
}
#endif // WITH_EDITORONLY_DATA

void FByteProperty::GetPreloadDependencies(TArray<UObject*>& OutDeps)
{
	Super::GetPreloadDependencies(OutDeps);
	OutDeps.Add(Enum);
}

void FByteProperty::SerializeItem( FStructuredArchive::FSlot Slot, void* Value, void const* Defaults ) const
{
	FArchive& UnderlyingArchive = Slot.GetUnderlyingArchive();

	if(Enum && UnderlyingArchive.UseToResolveEnumerators())
	{
		Slot.EnterStream();
		 const int64 ResolvedIndex = Enum->ResolveEnumerator(UnderlyingArchive, *(uint8*)Value);
		 *(uint8*)Value = static_cast<uint8>(ResolvedIndex);
		 return;
	}

	// Serialize enum values by name unless we're not saving or loading OR for backwards compatibility
	const bool bUseBinarySerialization = (Enum == NULL) || (!UnderlyingArchive.IsLoading() && !UnderlyingArchive.IsSaving());
	if( bUseBinarySerialization )
	{
		Super::SerializeItem(Slot, Value, Defaults);
	}
	// Loading
	else if (UnderlyingArchive.IsLoading())
	{
		FName EnumValueName;
		Slot << EnumValueName;
		// Make sure enum is properly populated
		if( Enum->HasAnyFlags(RF_NeedLoad) )
		{
			UnderlyingArchive.Preload(Enum);
		}

		// There's no guarantee EnumValueName is still present in Enum, in which case Value will be set to the enum's max value.
		// On save, it will then be serialized as NAME_None.
		int32 EnumIndex = Enum->GetIndexByName(EnumValueName, EGetByNameFlags::ErrorIfNotFound);
		if (EnumIndex == INDEX_NONE)
		{
			*(uint8*)Value = Enum->GetMaxEnumValue();
		}
		else
		{
			*(uint8*)Value = Enum->GetValueByIndex(EnumIndex);
		}
	}
	// Saving
	else
	{
		FName EnumValueName;
		uint8 ByteValue = *(uint8*)Value;

		// subtract 1 because the last entry in the enum's Names array
		// is the _MAX entry
		if ( Enum->IsValidEnumValue(ByteValue) )
		{
			EnumValueName = Enum->GetNameByValue(ByteValue);
		}
		else
		{
			EnumValueName = NAME_None;
		}
		Slot << EnumValueName;
	}
}
bool FByteProperty::NetSerializeItem( FArchive& Ar, UPackageMap* Map, void* Data, TArray<uint8> * MetaData ) const
{
	if (Ar.EngineNetVer() < HISTORY_ENUM_SERIALIZATION_COMPAT)
	{
		Ar.SerializeBits(Data, Enum ? FMath::CeilLogTwo(Enum->GetMaxEnumValue()) : 8);
	}
	else
	{
		Ar.SerializeBits(Data, GetMaxNetSerializeBits());
	}

	return true;
}
void FByteProperty::Serialize( FArchive& Ar )
{
	Super::Serialize( Ar );
	Ar << Enum;
	if (Enum != nullptr)
	{
		Ar.Preload(Enum);
	}
}

void FByteProperty::PostDuplicate(const FField& InField)
{
	const FByteProperty& Source = static_cast<const FByteProperty&>(InField);
	Enum = Source.Enum;
	Super::PostDuplicate(InField);
}

void FByteProperty::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Enum, nullptr);
	Super::AddReferencedObjects(Collector);
}
FString FByteProperty::GetCPPType( FString* ExtendedTypeText/*=NULL*/, uint32 CPPExportFlags/*=0*/ ) const
{
	if (Enum)
	{
		const bool bEnumClassForm = Enum->GetCppForm() == UEnum::ECppForm::EnumClass;
		const bool bNonNativeEnum = Enum->GetClass() != UEnum::StaticClass(); // cannot use RF_Native flag, because in UHT the flag is not set
		const bool bRawParam = (CPPExportFlags & CPPF_ArgumentOrReturnValue)
			&& (((PropertyFlags & CPF_ReturnParm) || !(PropertyFlags & CPF_OutParm))
				|| bNonNativeEnum);
		const bool bConvertedCode = (CPPExportFlags & CPPF_BlueprintCppBackend) && bNonNativeEnum;

		FString FullyQualifiedEnumName;
		if (!Enum->CppType.IsEmpty())
		{
			FullyQualifiedEnumName = Enum->CppType;
		}
		else
		{
			// This would give the wrong result if it's a namespaced type and the CppType hasn't
			// been set, but we do this here in case existing code relies on it... somehow.
			if ((CPPExportFlags & CPPF_BlueprintCppBackend) && bNonNativeEnum)
			{
				ensure(Enum->CppType.IsEmpty());
				FullyQualifiedEnumName = ::UnicodeToCPPIdentifier(Enum->GetName(), false, TEXT("E__"));
			}
			else
			{
				FullyQualifiedEnumName = Enum->GetName();
			}
		}
		 
		if (bEnumClassForm || bRawParam || bConvertedCode)
		{
			return FullyQualifiedEnumName;
		}
		else
		{
			return FString::Printf(TEXT("TEnumAsByte<%s>"), *FullyQualifiedEnumName);
		}
	}
	return Super::GetCPPType(ExtendedTypeText, CPPExportFlags);
}

template <typename OldIntType>
struct TConvertIntToEnumProperty
{
	static void Convert(FStructuredArchive::FSlot Slot, FByteProperty* Property, UEnum* Enum, void* Obj, const FPropertyTag& Tag)
	{
		OldIntType OldValue;
		Slot << OldValue;

		uint8 NewValue = OldValue;
		if (OldValue > (OldIntType)TNumericLimits<uint8>::Max() || !Enum->IsValidEnumValue(NewValue))
		{
			UE_LOG(
				LogClass,
				Warning,
				TEXT("Failed to find valid enum value '%d' for enum type '%s' when converting property '%s' during property loading - setting to '%s'"),
				OldValue,
				*Enum->GetName(),
				*Property->GetName(),
				*Enum->GetNameByValue(Enum->GetMaxEnumValue()).ToString()
				);

			NewValue = Enum->GetMaxEnumValue();
		}

		Property->SetPropertyValue_InContainer(Obj, NewValue, Tag.ArrayIndex);
	}
};

EConvertFromTypeResult FByteProperty::ConvertFromType(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot, uint8* Data, UStruct* DefaultsStruct)
{
	if (Tag.Type == NAME_ByteProperty  && ((Tag.EnumName == NAME_None) != (Enum == nullptr)))
	{
		// a byte property gained or lost an enum
		// attempt to convert it
		uint8 PreviousValue = 0;
		if (Tag.EnumName == NAME_None)
		{
			// If we're a nested property the EnumName tag got lost. Fail to read in this case
			FProperty* const PropertyOwner = GetOwner<FProperty>();
			if (PropertyOwner)
			{
				return EConvertFromTypeResult::UseSerializeItem;
			}

			// simply pretend the property still doesn't have an enum and serialize the single byte
			Slot << PreviousValue;
		}
		else
		{
			// attempt to find the old enum and get the byte value from the serialized enum name
			PreviousValue = (uint8)ReadEnumAsInt64(Slot, DefaultsStruct, Tag);
		}

		// now copy the value into the object's address space
		SetPropertyValue_InContainer(Data, PreviousValue, Tag.ArrayIndex);
	}
	else if (Tag.Type == NAME_EnumProperty && (Enum == nullptr || Tag.EnumName == Enum->GetFName()))
	{
		// an enum property became a byte
		// attempt to find the old enum and get the byte value from the serialized enum name
		uint8 PreviousValue = (uint8)ReadEnumAsInt64(Slot, DefaultsStruct, Tag);

		// now copy the value into the object's address space
		SetPropertyValue_InContainer(Data, PreviousValue, Tag.ArrayIndex);
	}
	else if (Tag.Type == NAME_Int8Property)
	{
		if (Enum)
		{
			TConvertIntToEnumProperty<int8>::Convert(Slot, this, Enum, Data, Tag);
		}
		else
		{
			ConvertFromArithmeticValue<int8>(Slot, Data, Tag);
		}
	}
	else if (Tag.Type == NAME_Int16Property)
	{
		if (Enum)
		{
			TConvertIntToEnumProperty<int16>::Convert(Slot, this, Enum, Data, Tag);
		}
		else
		{
			ConvertFromArithmeticValue<int16>(Slot, Data, Tag);
		}
	}
	else if (Tag.Type == NAME_IntProperty)
	{
		if (Enum)
		{
			TConvertIntToEnumProperty<int32>::Convert(Slot, this, Enum, Data, Tag);
		}
		else
		{
			ConvertFromArithmeticValue<int32>(Slot, Data, Tag);
		}
	}
	else if (Tag.Type == NAME_Int64Property)
	{
		if (Enum)
		{
			TConvertIntToEnumProperty<int64>::Convert(Slot, this, Enum, Data, Tag);
		}
		else
		{
			ConvertFromArithmeticValue<int64>(Slot, Data, Tag);
		}
	}
	else if (Tag.Type == NAME_UInt16Property)
	{
		if (Enum)
		{
			TConvertIntToEnumProperty<uint16>::Convert(Slot, this, Enum, Data, Tag);
		}
		else
		{
			ConvertFromArithmeticValue<uint16>(Slot, Data, Tag);
		}
	}
	else if (Tag.Type == NAME_UInt32Property)
	{
		if (Enum)
		{
			TConvertIntToEnumProperty<uint32>::Convert(Slot, this, Enum, Data, Tag);
		}
		else
		{
			ConvertFromArithmeticValue<uint32>(Slot, Data, Tag);
		}
	}
	else if (Tag.Type == NAME_UInt64Property)
	{
		if (Enum)
		{
			TConvertIntToEnumProperty<uint64>::Convert(Slot, this, Enum, Data, Tag);
		}
		else
		{
			ConvertFromArithmeticValue<uint64>(Slot, Data, Tag);
		}
	}
	else
	{
		return EConvertFromTypeResult::UseSerializeItem;
	}

	return EConvertFromTypeResult::Converted;
}

void FByteProperty::ExportTextItem( FString& ValueStr, const void* PropertyValue, const void* DefaultValue, UObject* Parent, int32 PortFlags, UObject* ExportRootScope ) const
{
	if (0 != (PortFlags & PPF_ExportCpp))
	{
		if (Enum)
		{
			const int32 ActualValue = *(const uint8*)PropertyValue;
			const int32 MaxValue = Enum->GetMaxEnumValue();
			const int32 GoodValue = Enum->IsValidEnumValue(ActualValue) ? ActualValue : MaxValue;
			const bool bNonNativeEnum = Enum->GetClass() != UEnum::StaticClass();
			ensure(!bNonNativeEnum || Enum->CppType.IsEmpty());
			const FString FullyQualifiedEnumName = bNonNativeEnum ? ::UnicodeToCPPIdentifier(Enum->GetName(), false, TEXT("E__"))
				: (Enum->CppType.IsEmpty() ? Enum->GetName() : Enum->CppType);
			if (GoodValue == MaxValue)
			{
				// not all native enums have Max value declared
				ValueStr += FString::Printf(TEXT("(%s)(%d)"), *FullyQualifiedEnumName, ActualValue);
			}
			else
			{
				ValueStr += FString::Printf(TEXT("%s::%s"), *FullyQualifiedEnumName,
					*Enum->GetNameStringByValue(GoodValue));
			}
		}
		else
		{
			Super::ExportTextItem(ValueStr, PropertyValue, DefaultValue, Parent, PortFlags, ExportRootScope);
		}
		return;
	}

	if( Enum && (PortFlags & PPF_ConsoleVariable) == 0 )
	{
		// if the value is the max value (the autogenerated *_MAX value), export as "INVALID", unless we're exporting text for copy/paste (for copy/paste,
		// the property text value must actually match an entry in the enum's names array)
		bool bIsValid = Enum->IsValidEnumValue(*(const uint8*)PropertyValue);
		bool bIsMax = *(const uint8*)PropertyValue == Enum->GetMaxEnumValue();
		if (bIsValid && (!bIsMax || (PortFlags & PPF_Copy)))
		{
			// We do not want to export the enum text for non-display uses, localization text is very dynamic and would cause issues on import
			if (PortFlags & PPF_PropertyWindow)
			{
				ValueStr += Enum->GetDisplayNameTextByValue(*(const uint8*)PropertyValue).ToString();
			}
			else if (PortFlags & PPF_ExternalEditor)
			{
				ValueStr += Enum->GetAuthoredNameStringByValue(*(const uint8*)PropertyValue);
			}
			else
			{
				ValueStr += Enum->GetNameStringByValue(*(const uint8*)PropertyValue);
			}
		}
		else
		{
			ValueStr += TEXT("(INVALID)");
		}
	}
	else
	{
		Super::ExportTextItem(ValueStr, PropertyValue, DefaultValue, Parent, PortFlags, ExportRootScope);
	}
}
const TCHAR* FByteProperty::ImportText_Internal( const TCHAR* InBuffer, void* Data, int32 PortFlags, UObject* Parent, FOutputDevice* ErrorText ) const
{
	if( Enum && (PortFlags & PPF_ConsoleVariable) == 0 )
	{
		FString Temp;
		if (const TCHAR* Buffer = FPropertyHelpers::ReadToken(InBuffer, Temp, true))
		{
			int32 EnumIndex = Enum->GetIndexByName(*Temp, EGetByNameFlags::CheckAuthoredName);
			if (EnumIndex == INDEX_NONE && (Temp.IsNumeric() && !Algo::Find(Temp, TEXT('.'))))
			{
				int64 EnumValue = INDEX_NONE;
				LexFromString(EnumValue, *Temp);
				EnumIndex = Enum->GetIndexByValue(EnumValue);
			}
			if (EnumIndex != INDEX_NONE)
			{
				*(uint8*)Data = Enum->GetValueByIndex(EnumIndex);
				return Buffer;
			}

			// Enum could not be created from value. This indicates a bad value so
			// return null so that the caller of ImportText can generate a more meaningful
			// warning/error
			UObject* SerializedObject = nullptr;
			if (FLinkerLoad* Linker = GetLinker())
			{
				if (FUObjectSerializeContext* LoadContext = Linker->GetSerializeContext())
				{
					SerializedObject = LoadContext->SerializedObject;
				}
			}
			UE_LOG(LogClass, Warning, TEXT("In asset '%s', there is an enum property of type '%s' with an invalid value of '%s'"), *GetPathNameSafe(SerializedObject ? SerializedObject : FUObjectThreadContext::Get().ConstructedObject), *Enum->GetName(), *Temp);
			return nullptr;
		}
	}
	
	// Interpret "True" and "False" as 1 and 0. This is mostly for importing a property that was exported as a bool and is imported as a non-enum byte.
	if (!Enum)
	{
		FString Temp;
		if (const TCHAR* Buffer = FPropertyHelpers::ReadToken(InBuffer, Temp))
		{
			const FCoreTexts& CoreTexts = FCoreTexts::Get();

			if (Temp == TEXT("True") || Temp == *(CoreTexts.True.ToString()))
			{
				SetIntPropertyValue(Data, 1ull);
				return Buffer;
			}
			else if (Temp == TEXT("False") || Temp == *(CoreTexts.False.ToString()))
			{
				SetIntPropertyValue(Data, 0ull);
				return Buffer;
			}
		}
	}

	return Super::ImportText_Internal( InBuffer, Data, PortFlags, Parent, ErrorText );
}

UEnum* FByteProperty::GetIntPropertyEnum() const
{
	return Enum;
}

uint64 FByteProperty::GetMaxNetSerializeBits() const
{
	const uint64 MaxBits = 8;
	const uint64 DesiredBits = Enum ? FMath::CeilLogTwo64(Enum->GetMaxEnumValue() + 1) : MaxBits;

	return FMath::Min(DesiredBits, MaxBits);
}

#include "UObject/DefineUPropertyMacros.h"