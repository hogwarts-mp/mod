// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include "UObject/UnrealTypePrivate.h"
#include "UObject/PropertyHelper.h"
#include "UObject/LinkerPlaceholderBase.h"
#include "Serialization/ArchiveUObjectFromStructuredArchive.h"

// WARNING: This should always be the last include in any file that needs it (except .generated.h)
#include "UObject/UndefineUPropertyMacros.h"

static inline void PreloadInnerStructMembers(FStructProperty* StructProperty)
{
	if (UseCircularDependencyLoadDeferring())
	{
		uint32 PropagatedLoadFlags = 0;
		if (FLinkerLoad* Linker = StructProperty->GetLinker())
		{
			PropagatedLoadFlags |= (Linker->LoadFlags & LOAD_DeferDependencyLoads);
		}

		if (UScriptStruct* Struct = StructProperty->Struct)
		{
			if (FLinkerLoad* StructLinker = Struct->GetLinker())
			{
				TGuardValue<uint32> LoadFlagGuard(StructLinker->LoadFlags, StructLinker->LoadFlags | PropagatedLoadFlags);
				Struct->RecursivelyPreload();
			}
		}
	}
	else
	{
		StructProperty->Struct->RecursivelyPreload();
	}
}

/*-----------------------------------------------------------------------------
	FStructProperty.
-----------------------------------------------------------------------------*/

IMPLEMENT_FIELD(FStructProperty)

FStructProperty::FStructProperty(FFieldVariant InOwner, const FName& InName, EObjectFlags InObjectFlags)
	: FProperty(InOwner, InName, InObjectFlags)
	, Struct(nullptr)
{
	ElementSize = 0;
}

FStructProperty::FStructProperty(FFieldVariant InOwner, const FName& InName, EObjectFlags InObjectFlags, int32 InOffset, EPropertyFlags InFlags, UScriptStruct* InStruct)
	: FProperty(InOwner, InName, InObjectFlags, InOffset, InStruct->GetCppStructOps() ? InStruct->GetCppStructOps()->GetComputedPropertyFlags() | InFlags : InFlags)
	,	Struct( InStruct )
{
	ElementSize = Struct->PropertiesSize;
}

#if WITH_EDITORONLY_DATA
FStructProperty::FStructProperty(UField* InField)
	: FProperty(InField)
{
	UStructProperty* SourceProperty = CastChecked<UStructProperty>(InField);
	Struct = SourceProperty->Struct;
	check(ElementSize == SourceProperty->ElementSize); // this should've been set by FProperty
}
#endif // WITH_EDITORONLY_DATA

int32 FStructProperty::GetMinAlignment() const
{
	return Struct->GetMinAlignment();
}

void FStructProperty::PostDuplicate(const FField& InField)
{
	const FStructProperty& Source = static_cast<const FStructProperty&>(InField);
	Struct = Source.Struct;
	Super::PostDuplicate(InField);
}

void FStructProperty::LinkInternal(FArchive& Ar)
{
	// We potentially have to preload the property itself here, if we were the inner of an array property
	//if(HasAnyFlags(RF_NeedLoad))
	//{
	//	GetLinker()->Preload(this);
	//}

	if (Struct)
	{
		// Preload is required here in order to load the value of Struct->PropertiesSize
		Ar.Preload(Struct);
	}
	else
	{
		UE_LOG(LogProperty, Error, TEXT("Struct type unknown for property '%s'; perhaps the USTRUCT() was renamed or deleted?"), *GetFullName());
		Struct = GetFallbackStruct();
	}
	PreloadInnerStructMembers(this);
	
	ElementSize = Align(Struct->PropertiesSize, Struct->GetMinAlignment());
	if(UScriptStruct::ICppStructOps* Ops = Struct->GetCppStructOps())
	{
		PropertyFlags |= Ops->GetComputedPropertyFlags();
	}
	else
	{
		// User Defined structs won't have UScriptStruct::ICppStructOps. Setting their flags here.
		PropertyFlags |= CPF_HasGetValueTypeHash;
	}

	if (Struct->StructFlags & STRUCT_ZeroConstructor)
	{
		PropertyFlags |= CPF_ZeroConstructor;
	}
	if (Struct->StructFlags & STRUCT_IsPlainOldData)
	{
		PropertyFlags |= CPF_IsPlainOldData;
	}
	if (Struct->StructFlags & STRUCT_NoDestructor)
	{
		PropertyFlags |= CPF_NoDestructor;
	}
}

bool FStructProperty::Identical( const void* A, const void* B, uint32 PortFlags ) const
{
	return Struct->CompareScriptStruct(A, B, PortFlags);
}

bool FStructProperty::UseBinaryOrNativeSerialization(const FArchive& Ar) const
{
	check(Struct);

	const bool bUseBinarySerialization = Struct->UseBinarySerialization(Ar);
	const bool bUseNativeSerialization = Struct->UseNativeSerialization();
	return bUseBinarySerialization || bUseNativeSerialization;
}

uint32 FStructProperty::GetValueTypeHashInternal(const void* Src) const
{
	check(Struct);
	return Struct->GetStructTypeHash(Src);
}

void FStructProperty::SerializeItem(FStructuredArchive::FSlot Slot, void* Value, void const* Defaults) const
{
	FScopedPlaceholderPropertyTracker ImportPropertyTracker(this);

	Struct->SerializeItem(Slot, Value, Defaults);
}

bool FStructProperty::NetSerializeItem( FArchive& Ar, UPackageMap* Map, void* Data, TArray<uint8> * MetaData ) const
{
	//------------------------------------------------
	//	Custom NetSerialization
	//------------------------------------------------
	if (Struct->StructFlags & STRUCT_NetSerializeNative)
	{
		UScriptStruct::ICppStructOps* CppStructOps = Struct->GetCppStructOps();
		check(CppStructOps); // else should not have STRUCT_NetSerializeNative
		bool bSuccess = true;
		bool bMapped = CppStructOps->NetSerialize(Ar, Map, bSuccess, Data);
		if (!bSuccess)
		{
			UE_LOG(LogProperty, Warning, TEXT("Native NetSerialize %s (%s) failed."), *GetFullName(), *Struct->GetFullName() );
		}
		return bMapped;
	}

	UE_LOG( LogProperty, Fatal, TEXT( "Deprecated code path" ) );

	return 1;
}

bool FStructProperty::SupportsNetSharedSerialization() const
{
	return !(Struct->StructFlags & STRUCT_NetSerializeNative) || (Struct->StructFlags & STRUCT_NetSharedSerialization);
}

void FStructProperty::GetPreloadDependencies(TArray<UObject*>& OutDeps)
{
	Super::GetPreloadDependencies(OutDeps);
	OutDeps.Add(Struct);
}

void FStructProperty::Serialize( FArchive& Ar )
{
	Super::Serialize( Ar );

	static UScriptStruct* FallbackStruct = GetFallbackStruct();
	
	if (Ar.IsPersistent() && Ar.GetLinker() && Ar.IsLoading() && !Struct)
	{
		// It's necessary to solve circular dependency problems, when serializing the Struct causes linking of the Property.
		Struct = FallbackStruct;
	}

	Ar << Struct;
#if WITH_EDITOR
	if (Ar.IsPersistent() && Ar.GetLinker())
	{
		if (!Struct && Ar.IsLoading())
		{
			UE_LOG(LogProperty, Error, TEXT("FStructProperty::Serialize Loading: Property '%s'. Unknown structure."), *GetFullName());
			Struct = FallbackStruct;
		}
		else if ((FallbackStruct == Struct) && Ar.IsSaving())
		{
			UE_LOG(LogProperty, Error, TEXT("FStructProperty::Serialize Saving: Property '%s'. FallbackStruct structure."), *GetFullName());
		}
	}
#endif // WITH_EDITOR
	if (Struct)
	{
		PreloadInnerStructMembers(this);
	}
	else
	{
		ensure(true);
	}
}
void FStructProperty::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(Struct);
	Super::AddReferencedObjects(Collector);
}

#if HACK_HEADER_GENERATOR

bool FStructProperty::HasNoOpConstructor() const
{
	Struct->PrepareCppStructOps();
	UScriptStruct::ICppStructOps* CppStructOps = Struct->GetCppStructOps();
	if (CppStructOps && CppStructOps->HasNoopConstructor())
	{
		return true;
	}
	return false;
}

#endif

FString FStructProperty::GetCPPType( FString* ExtendedTypeText/*=NULL*/, uint32 CPPExportFlags/*=0*/ ) const
{
	return Struct->GetStructCPPName();
}

FString FStructProperty::GetCPPTypeForwardDeclaration() const
{
	return FString::Printf(TEXT("struct F%s;"), *Struct->GetName());
}

FString FStructProperty::GetCPPMacroType( FString& ExtendedTypeText ) const
{
	ExtendedTypeText = GetCPPType(NULL, CPPF_None);
	return TEXT("STRUCT");
}

void FStructProperty::ExportTextItem_Static(UScriptStruct* InStruct, FString& ValueStr, const void* PropertyValue, const void* DefaultValue, UObject* Parent, int32 PortFlags, UObject* ExportRootScope)
{
	// For backward compatibility skip the native export 
	InStruct->ExportText(ValueStr, PropertyValue, DefaultValue, Parent, PortFlags, ExportRootScope, false);
}

void FStructProperty::ExportTextItem( FString& ValueStr, const void* PropertyValue, const void* DefaultValue, UObject* Parent, int32 PortFlags, UObject* ExportRootScope ) const
{
	Struct->ExportText(ValueStr, PropertyValue, DefaultValue, Parent, PortFlags, ExportRootScope, true);
}

const TCHAR* FStructProperty::ImportText_Internal(const TCHAR* InBuffer, void* Data, int32 PortFlags, UObject* Parent, FOutputDevice* ErrorText) const
{
#if USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING
	FScopedPlaceholderPropertyTracker ImportPropertyTracker(this);

	uint32 PropagatedLoadFlags = 0;
	if (FLinkerLoad* Linker = GetLinker())
	{
		PropagatedLoadFlags |= (Linker->LoadFlags & LOAD_DeferDependencyLoads);
	}

	uint32 OldFlags = 0;
	FLinkerLoad* StructLinker = Struct->GetLinker();
	if (StructLinker)
	{
		OldFlags = StructLinker->LoadFlags;
		StructLinker->LoadFlags |= OldFlags | PropagatedLoadFlags;
	}
#endif 
	const TCHAR* Result = Struct->ImportText(InBuffer, Data, Parent, PortFlags, ErrorText, [this]() { return GetName(); }, true);

#if USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING
	if (StructLinker)
	{
		StructLinker->LoadFlags = OldFlags;
}
#endif

	return Result;
}

const TCHAR* FStructProperty::ImportText_Static(UScriptStruct* InStruct, const FString& Name, const TCHAR* InBuffer, void* Data, int32 PortFlags, UObject* Parent, FOutputDevice* ErrorText)
{
	return InStruct->ImportText(InBuffer, Data, Parent, PortFlags, ErrorText, Name, true);
}

void FStructProperty::CopyValuesInternal( void* Dest, void const* Src, int32 Count  ) const
{
	Struct->CopyScriptStruct(Dest, Src, Count);
}

void FStructProperty::InitializeValueInternal( void* InDest ) const
{
	Struct->InitializeStruct(InDest, ArrayDim);
}

void FStructProperty::ClearValueInternal( void* Data ) const
{
	Struct->ClearScriptStruct(Data, 1); // clear only does one value
}

void FStructProperty::DestroyValueInternal( void* Dest ) const
{
	Struct->DestroyStruct(Dest, ArrayDim);
}

/**
 * Creates new copies of components
 * 
 * @param	Data				pointer to the address of the instanced object referenced by this UComponentProperty
 * @param	DefaultData			pointer to the address of the default value of the instanced object referenced by this UComponentProperty
 * @param	Owner				the object that contains this property's data
 * @param	InstanceGraph		contains the mappings of instanced objects and components to their templates
 */
void FStructProperty::InstanceSubobjects( void* Data, void const* DefaultData, UObject* InOwner, FObjectInstancingGraph* InstanceGraph )
{
	for (int32 Index = 0; Index < ArrayDim; Index++)
	{
		Struct->InstanceSubobjectTemplates( (uint8*)Data + ElementSize * Index, DefaultData ? (uint8*)DefaultData + ElementSize * Index : NULL, Struct, InOwner, InstanceGraph );
	}
}

bool FStructProperty::SameType(const FProperty* Other) const
{
	return Super::SameType(Other) && (Struct == ((FStructProperty*)Other)->Struct);
}

EConvertFromTypeResult FStructProperty::ConvertFromType(const FPropertyTag& Tag, FStructuredArchive::FSlot Slot, uint8* Data, UStruct* DefaultsStruct)
{
	FArchive& UnderlyingArchive = Slot.GetUnderlyingArchive();

	auto CanSerializeFromStructWithDifferentName = [](const FArchive& InAr, const FPropertyTag& PropertyTag, const FStructProperty* StructProperty)
	{
		if (InAr.UE4Ver() < VER_UE4_STRUCT_GUID_IN_PROPERTY_TAG)
		{
			// Old Implementation
			return StructProperty && !StructProperty->UseBinaryOrNativeSerialization(InAr);
		}
		return PropertyTag.StructGuid.IsValid() && StructProperty && StructProperty->Struct && (PropertyTag.StructGuid == StructProperty->Struct->GetCustomGuid());
	};

	if (Struct)
	{
		if ((Struct->StructFlags & STRUCT_SerializeFromMismatchedTag) && (Tag.Type != NAME_StructProperty || (Tag.StructName != Struct->GetFName())))
		{
			UScriptStruct::ICppStructOps* CppStructOps = Struct->GetCppStructOps();
			check(CppStructOps && (CppStructOps->HasSerializeFromMismatchedTag() || CppStructOps->HasStructuredSerializeFromMismatchedTag())); // else should not have STRUCT_SerializeFromMismatchedTag
			void* DestAddress = ContainerPtrToValuePtr<void>(Data, Tag.ArrayIndex);
			if (CppStructOps->HasStructuredSerializeFromMismatchedTag() && CppStructOps->StructuredSerializeFromMismatchedTag(Tag, Slot, DestAddress))
			{
				return EConvertFromTypeResult::Converted;
			}
			else 
			{
				FArchiveUObjectFromStructuredArchive Adapter(Slot);
				FArchive& Ar = Adapter.GetArchive();
				if (CppStructOps->HasSerializeFromMismatchedTag() && CppStructOps->SerializeFromMismatchedTag(Tag, Ar, DestAddress))
				{

					return EConvertFromTypeResult::Converted;
				}
				else
				{
					UE_LOG(LogClass, Warning, TEXT("SerializeFromMismatchedTag failed: Type mismatch in %s of %s - Previous (%s) Current(StructProperty) for package:  %s"), *Tag.Name.ToString(), *GetName(), *Tag.Type.ToString(), *UnderlyingArchive.GetArchiveName());
					return EConvertFromTypeResult::CannotConvert;
				}
			}
		}

		if (Tag.Type == NAME_StructProperty && Tag.StructName != Struct->GetFName() && !CanSerializeFromStructWithDifferentName(UnderlyingArchive, Tag, this))
		{
			//handle Vector -> Vector4 upgrades here because using the SerializeFromMismatchedTag system would cause a dependency from Core -> CoreUObject
			if (Tag.StructName == NAME_Vector && Struct->GetFName() == NAME_Vector4)
			{
				void* DestAddress = ContainerPtrToValuePtr<void>(Data, Tag.ArrayIndex);
				FVector OldValue;
				Slot << OldValue;

				//only set X/Y/Z.  The W should already have been set to the property specific default and we don't want to trash it by forcing 0 or 1.
				FVector4* DestValue = (FVector4*)DestAddress;
				DestValue->X = OldValue.X;
				DestValue->Y = OldValue.Y;
				DestValue->Z = OldValue.Z;

				return EConvertFromTypeResult::Converted;
			}

			UE_LOG(LogClass, Warning, TEXT("Property %s of %s has a struct type mismatch (tag %s != prop %s) in package:  %s. If that struct got renamed, add an entry to ActiveStructRedirects."),
				*Tag.Name.ToString(), *GetName(), *Tag.StructName.ToString(), *Struct->GetName(), *UnderlyingArchive.GetArchiveName());
			return EConvertFromTypeResult::CannotConvert;
		}
	}
	return EConvertFromTypeResult::UseSerializeItem;
}

#include "UObject/DefineUPropertyMacros.h"