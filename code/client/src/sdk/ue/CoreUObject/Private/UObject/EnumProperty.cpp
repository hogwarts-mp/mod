// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/EnumProperty.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/UObjectThreadContext.h"
#include "UObject/PropertyTag.h"
#include "UObject/UnrealTypePrivate.h"
#include "Templates/ChooseClass.h"
#include "Templates/IsSigned.h"
#include "Algo/Find.h"
#include "UObject/LinkerLoad.h"
#include "Misc/NetworkVersion.h"

// WARNING: This should always be the last include in any file that needs it (except .generated.h)
#include "UObject/UndefineUPropertyMacros.h"

namespace UE4EnumProperty_Private
{
	template <typename OldIntType>
	void ConvertIntToEnumProperty(FStructuredArchive::FSlot Slot, FEnumProperty* EnumProp, FNumericProperty* UnderlyingProp, UEnum* Enum, void* Obj)
	{
		OldIntType OldValue;
		Slot << OldValue;

		using LargeIntType = typename TChooseClass<TIsSigned<OldIntType>::Value, int64, uint64>::Result;

		LargeIntType NewValue = OldValue;
		if (!UnderlyingProp->CanHoldValue(NewValue) || !Enum->IsValidEnumValue(NewValue))
		{
			UE_LOG(
				LogClass,
				Warning,
				TEXT("Failed to find valid enum value '%s' for enum type '%s' when converting property '%s' during property loading - setting to '%s'"),
				*LexToString(OldValue),
				*Enum->GetName(),
				*EnumProp->GetName(),
				*Enum->GetNameByValue(Enum->GetMaxEnumValue()).ToString()
			);

			NewValue = Enum->GetMaxEnumValue();
		}

		UnderlyingProp->SetIntPropertyValue(Obj, NewValue);
	}
}

IMPLEMENT_FIELD(FEnumProperty)

FEnumProperty::FEnumProperty(FFieldVariant InOwner, const FName& InName, EObjectFlags InObjectFlags)
	: FProperty(InOwner, InName, InObjectFlags)	
	, UnderlyingProp(nullptr)
	, Enum(nullptr)
{

}

FEnumProperty::FEnumProperty(FFieldVariant InOwner, const FName& InName, EObjectFlags InObjectFlags, UEnum* InEnum)
	: FProperty(InOwner, InName, InObjectFlags, 0, CPF_HasGetValueTypeHash)
	, Enum(InEnum)
{
	// This is expected to be set post-construction by AddCppProperty
	UnderlyingProp = nullptr;
}

FEnumProperty::FEnumProperty(FFieldVariant InOwner, const FName& InName, EObjectFlags InObjectFlags, int32 InOffset, EPropertyFlags InFlags, UEnum* InEnum)
	: FProperty(InOwner, InName, InObjectFlags, InOffset, InFlags | CPF_HasGetValueTypeHash)
	, Enum(InEnum)
{
	// This is expected to be set post-construction by AddCppProperty
	UnderlyingProp = nullptr;
}

#if WITH_EDITORONLY_DATA
FEnumProperty::FEnumProperty(UField* InField)
	: FProperty(InField)
{
	UEnumProperty* SourceProperty = CastChecked<UEnumProperty>(InField);
	Enum = SourceProperty->Enum;

	UnderlyingProp = CastField<FNumericProperty>(SourceProperty->UnderlyingProp->GetAssociatedFField());
	if (!UnderlyingProp)
	{
		UnderlyingProp = CastField<FNumericProperty>(CreateFromUField(SourceProperty->UnderlyingProp));
		SourceProperty->UnderlyingProp->SetAssociatedFField(UnderlyingProp);
	}
}
#endif // WITH_EDITORONLY_DATA

FEnumProperty::~FEnumProperty()
{
	delete UnderlyingProp;
	UnderlyingProp = nullptr;
}

void FEnumProperty::PostDuplicate(const FField& InField)
{
	const FEnumProperty& Source = static_cast<const FEnumProperty&>(InField);
	Enum = Source.Enum;
	UnderlyingProp = CastFieldChecked<FNumericProperty>(FField::Duplicate(Source.UnderlyingProp, this));
	Super::PostDuplicate(InField);
}

void FEnumProperty::AddCppProperty(FProperty* Inner)
{
	check(!UnderlyingProp);
	UnderlyingProp = CastFieldChecked<FNumericProperty>(Inner);
	check(UnderlyingProp->GetOwner<FEnumProperty>() == this);
	if (UnderlyingProp->HasAnyPropertyFlags(CPF_HasGetValueTypeHash))
	{
		PropertyFlags |= CPF_HasGetValueTypeHash;
	}
}

void FEnumProperty::SerializeItem(FStructuredArchive::FSlot Slot, void* Value, void const* Defaults) const
{
	FArchive& UnderlyingArchive = Slot.GetUnderlyingArchive();

	check(UnderlyingProp);

	if (Enum && UnderlyingArchive.UseToResolveEnumerators())
	{
		Slot.EnterStream();
		int64 IntValue = UnderlyingProp->GetSignedIntPropertyValue(Value);
		int64 ResolvedIndex = Enum->ResolveEnumerator(UnderlyingArchive, IntValue);
		UnderlyingProp->SetIntPropertyValue(Value, ResolvedIndex);
		return;
	}

	// Loading
	if (UnderlyingArchive.IsLoading())
	{
		FName EnumValueName;
		Slot << EnumValueName;

		int64 NewEnumValue = 0;

		if (Enum)
		{
			// Make sure enum is properly populated
			if (Enum->HasAnyFlags(RF_NeedLoad))
			{
				UnderlyingArchive.Preload(Enum);
			}

			// There's no guarantee EnumValueName is still present in Enum, in which case Value will be set to the enum's max value.
			// On save, it will then be serialized as NAME_None.
			const int32 EnumIndex = Enum->GetIndexByName(EnumValueName, EGetByNameFlags::ErrorIfNotFound);
			if (EnumIndex == INDEX_NONE)
			{
				NewEnumValue = Enum->GetMaxEnumValue();
			}
			else
			{
				NewEnumValue = Enum->GetValueByIndex(EnumIndex);
			}
		}

		UnderlyingProp->SetIntPropertyValue(Value, NewEnumValue);
	}
	// Saving
	else if (UnderlyingArchive.IsSaving())
	{
		FName EnumValueName;
		if (Enum)
		{
			const int64 IntValue = UnderlyingProp->GetSignedIntPropertyValue(Value);

			if (Enum->IsValidEnumValue(IntValue))
			{
				EnumValueName = Enum->GetNameByValue(IntValue);
			}
		}

		Slot << EnumValueName;
	}
	else
	{
		UnderlyingProp->SerializeItem(Slot, Value, Defaults);
	}
}

bool FEnumProperty::NetSerializeItem(FArchive& Ar, UPackageMap* Map, void* Data, TArray<uint8>* MetaData) const
{
	if (Ar.EngineNetVer() < HISTORY_FIX_ENUM_SERIALIZATION)
	{
		Ar.SerializeBits(Data, FMath::CeilLogTwo64(Enum->GetMaxEnumValue()));
	}
	else
	{
		Ar.SerializeBits(Data, GetMaxNetSerializeBits());
	}

	return true;
}

void FEnumProperty::Serialize( FArchive& Ar )
{
	Super::Serialize(Ar);
	Ar << Enum;
	if (Enum != nullptr)
	{
		Ar.Preload(Enum);
	}
	SerializeSingleField(Ar, UnderlyingProp, this);
}

void FEnumProperty::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Enum);
	Super::AddReferencedObjects(Collector);
}

FString FEnumProperty::GetCPPType(FString* ExtendedTypeText, uint32 CPPExportFlags) const
{
	check(Enum);
	check(UnderlyingProp);

	const bool bNonNativeEnum = Enum->GetClass() != UEnum::StaticClass(); // cannot use RF_Native flag, because in UHT the flag is not set

	if (!Enum->CppType.IsEmpty())
	{
		return Enum->CppType;
	}

	FString EnumName = Enum->GetName();

	// This would give the wrong result if it's a namespaced type and the CppType hasn't
	// been set, but we do this here in case existing code relies on it... somehow.
	if ((CPPExportFlags & CPPF_BlueprintCppBackend) && bNonNativeEnum)
	{
		ensure(Enum->CppType.IsEmpty());
		FString Result = ::UnicodeToCPPIdentifier(EnumName, false, TEXT("E__"));
		return Result;
	}

	return EnumName;
}

void FEnumProperty::ExportTextItem(FString& ValueStr, const void* PropertyValue, const void* DefaultValue, UObject* Parent, int32 PortFlags, UObject* ExportRootScope) const
{
	if (Enum == nullptr)
	{
		UE_LOG(
			LogClass,
			Warning,
			TEXT("Member 'Enum' of %s is nullptr, export operation would fail. This can occur when the enum class has been moved or deleted."),
			*GetFullName()
		);
		return;
	}

	check(UnderlyingProp);

	FNumericProperty* LocalUnderlyingProp = UnderlyingProp;

	if (PortFlags & PPF_ExportCpp)
	{
		const int64 ActualValue = LocalUnderlyingProp->GetSignedIntPropertyValue(PropertyValue);
		const int64 MaxValue = Enum->GetMaxEnumValue();
		const int64 GoodValue = Enum->IsValidEnumValue(ActualValue) ? ActualValue : MaxValue;
		const bool bNonNativeEnum = Enum->GetClass() != UEnum::StaticClass();
		ensure(!bNonNativeEnum || Enum->CppType.IsEmpty());
		const FString FullyQualifiedEnumName = bNonNativeEnum ? ::UnicodeToCPPIdentifier(Enum->GetName(), false, TEXT("E__"))
			: (Enum->CppType.IsEmpty() ? Enum->GetName() : Enum->CppType);
		if (GoodValue == MaxValue)
		{
			// not all native enums have Max value declared
			ValueStr += FString::Printf(TEXT("(%s)(%ull)"), *FullyQualifiedEnumName, ActualValue);
		}
		else
		{
			ValueStr += FString::Printf(TEXT("%s::%s"), *FullyQualifiedEnumName,
				*Enum->GetNameStringByValue(GoodValue));
		}
		return;
	}

	if (PortFlags & PPF_ConsoleVariable)
	{
		UnderlyingProp->ExportTextItem(ValueStr, PropertyValue, DefaultValue, Parent, PortFlags, ExportRootScope);
		return;
	}

	int64 Value = LocalUnderlyingProp->GetSignedIntPropertyValue(PropertyValue);

	// if the value is the max value (the autogenerated *_MAX value), export as "INVALID", unless we're exporting text for copy/paste (for copy/paste,
	// the property text value must actually match an entry in the enum's names array)
	if (!Enum->IsValidEnumValue(Value) || (!(PortFlags & PPF_Copy) && Value == Enum->GetMaxEnumValue()))
	{
		ValueStr += TEXT("(INVALID)");
		return;
	}

	// We do not want to export the enum text for non-display uses, localization text is very dynamic and would cause issues on import
	if (PortFlags & PPF_PropertyWindow)
	{
		ValueStr += Enum->GetDisplayNameTextByValue(Value).ToString();
	}
	else if (PortFlags & PPF_ExternalEditor)
	{
		ValueStr += Enum->GetAuthoredNameStringByValue(Value);
	}
	else
	{
		ValueStr += Enum->GetNameStringByValue(Value);
	}
}

const TCHAR* FEnumProperty::ImportText_Internal(const TCHAR* InBuffer, void* Data, int32 PortFlags, UObject* Parent, FOutputDevice* ErrorText) const
{
	check(Enum);
	check(UnderlyingProp);
	
	if (!(PortFlags & PPF_ConsoleVariable))
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
				UnderlyingProp->SetIntPropertyValue(Data, Enum->GetValueByIndex(EnumIndex));
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

	const TCHAR* Result = UnderlyingProp->ImportText(InBuffer, Data, PortFlags, Parent, ErrorText);
	return Result;
}

FString FEnumProperty::GetCPPMacroType(FString& ExtendedTypeText) const
{
	ExtendedTypeText = Enum->GetName();
	return TEXT("ENUM");
}

FString FEnumProperty::GetCPPTypeForwardDeclaration() const
{
	check(Enum);
	check(Enum->GetCppForm() == UEnum::ECppForm::EnumClass);

	return FString::Printf(TEXT("enum class %s : %s;"), *Enum->GetName(), *UnderlyingProp->GetCPPType());
}

void FEnumProperty::GetPreloadDependencies(TArray<UObject*>& OutDeps)
{
	Super::GetPreloadDependencies(OutDeps);
	//OutDeps.Add(UnderlyingProp);
	OutDeps.Add(Enum);
}

void FEnumProperty::LinkInternal(FArchive& Ar)
{
	check(UnderlyingProp);

	UnderlyingProp->Link(Ar);

	this->ElementSize = UnderlyingProp->ElementSize;
	this->PropertyFlags |= CPF_IsPlainOldData | CPF_NoDestructor | CPF_ZeroConstructor;

	PropertyFlags |= (UnderlyingProp->PropertyFlags & CPF_HasGetValueTypeHash);
}

bool FEnumProperty::Identical(const void* A, const void* B, uint32 PortFlags) const
{
	return UnderlyingProp->Identical(A, B, PortFlags);
}

int32 FEnumProperty::GetMinAlignment() const
{
	return UnderlyingProp->GetMinAlignment();
}

bool FEnumProperty::SameType(const FProperty* Other) const
{
	return Super::SameType(Other) && static_cast<const FEnumProperty*>(Other)->Enum == Enum;
}

EConvertFromTypeResult FEnumProperty::ConvertFromType(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot , uint8* Data, UStruct* DefaultsStruct)
{
	if ((Enum == nullptr) || (UnderlyingProp == nullptr))
	{
		return EConvertFromTypeResult::UseSerializeItem;
	}

	if (Tag.Type == NAME_ByteProperty)
	{
		uint8 PreviousValue = 0;
		if (Tag.EnumName == NAME_None)
		{
			// If we're a nested property the EnumName tag got lost. Handle this case for backward compatibility reasons
			FProperty* const PropertyOwner = GetOwner<FProperty>();

			if (PropertyOwner)
			{
				FPropertyTag InnerPropertyTag;
				InnerPropertyTag.Type = Tag.Type;
				InnerPropertyTag.EnumName = Enum->GetFName();
				InnerPropertyTag.ArrayIndex = 0;

				PreviousValue = (uint8)FNumericProperty::ReadEnumAsInt64(Slot, DefaultsStruct, InnerPropertyTag);
			}
			else
			{
				// a byte property gained an enum
				Slot << PreviousValue;
			}
		}
		else
		{
			PreviousValue = (uint8)FNumericProperty::ReadEnumAsInt64(Slot, DefaultsStruct, Tag);
		}

		// now copy the value into the object's address space
		UnderlyingProp->SetIntPropertyValue(ContainerPtrToValuePtr<void>(Data, Tag.ArrayIndex), (uint64)PreviousValue);
	}
	else if (Tag.Type == NAME_Int8Property)
	{
		UE4EnumProperty_Private::ConvertIntToEnumProperty<int8>(Slot, this, UnderlyingProp, Enum, ContainerPtrToValuePtr<void>(Data, Tag.ArrayIndex));
	}
	else if (Tag.Type == NAME_Int16Property)
	{
		UE4EnumProperty_Private::ConvertIntToEnumProperty<int16>(Slot, this, UnderlyingProp, Enum, ContainerPtrToValuePtr<void>(Data, Tag.ArrayIndex));
	}
	else if (Tag.Type == NAME_IntProperty)
	{
		UE4EnumProperty_Private::ConvertIntToEnumProperty<int32>(Slot, this, UnderlyingProp, Enum, ContainerPtrToValuePtr<void>(Data, Tag.ArrayIndex));
	}
	else if (Tag.Type == NAME_Int64Property)
	{
		UE4EnumProperty_Private::ConvertIntToEnumProperty<int64>(Slot, this, UnderlyingProp, Enum, ContainerPtrToValuePtr<void>(Data, Tag.ArrayIndex));
	}
	else if (Tag.Type == NAME_UInt16Property)
	{
		UE4EnumProperty_Private::ConvertIntToEnumProperty<uint16>(Slot, this, UnderlyingProp, Enum, ContainerPtrToValuePtr<void>(Data, Tag.ArrayIndex));
	}
	else if (Tag.Type == NAME_UInt32Property)
	{
		UE4EnumProperty_Private::ConvertIntToEnumProperty<uint32>(Slot, this, UnderlyingProp, Enum, ContainerPtrToValuePtr<void>(Data, Tag.ArrayIndex));
	}
	else if (Tag.Type == NAME_UInt64Property)
	{
		UE4EnumProperty_Private::ConvertIntToEnumProperty<uint64>(Slot, this, UnderlyingProp, Enum, ContainerPtrToValuePtr<void>(Data, Tag.ArrayIndex));
	}
	else
	{
		return EConvertFromTypeResult::UseSerializeItem;
	}

	return EConvertFromTypeResult::Converted;
}

uint32 FEnumProperty::GetValueTypeHashInternal(const void* Src) const
{
	check(UnderlyingProp);
	return UnderlyingProp->GetValueTypeHash(Src);
}

FField* FEnumProperty::GetInnerFieldByName(const FName& InName)
{
	if (UnderlyingProp && UnderlyingProp->GetFName() == InName)
	{
		return UnderlyingProp;
	}
	return nullptr;
}


void FEnumProperty::GetInnerFields(TArray<FField*>& OutFields)
{
	if (UnderlyingProp)
	{
		OutFields.Add(UnderlyingProp);
		UnderlyingProp->GetInnerFields(OutFields);
	}
}

uint64 FEnumProperty::GetMaxNetSerializeBits() const
{
	const uint64 MaxBits = ElementSize * 8;
	const uint64 DesiredBits = FMath::CeilLogTwo64(Enum->GetMaxEnumValue() + 1);
	
	return FMath::Min(DesiredBits, MaxBits);
}

#include "UObject/DefineUPropertyMacros.h"