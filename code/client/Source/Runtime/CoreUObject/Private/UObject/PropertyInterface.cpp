// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "UObject/Package.h"
#include "Templates/Casts.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/UnrealType.h"
#include "UObject/UnrealTypePrivate.h"
#include "UObject/LinkerPlaceholderClass.h"

// WARNING: This should always be the last include in any file that needs it (except .generated.h)
#include "UObject/UndefineUPropertyMacros.h"

/*-----------------------------------------------------------------------------
	FInterfaceProperty.
-----------------------------------------------------------------------------*/
IMPLEMENT_FIELD(FInterfaceProperty)

#if WITH_EDITORONLY_DATA
FInterfaceProperty::FInterfaceProperty(UField* InField)
	: FInterfaceProperty_Super(InField)
{
	UInterfaceProperty* SourceProperty = CastChecked<UInterfaceProperty>(InField);
	InterfaceClass = SourceProperty->InterfaceClass;
}
#endif // WITH_EDITORONLY_DATA

void FInterfaceProperty::BeginDestroy()
{
#if USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING
	if (ULinkerPlaceholderClass* PlaceholderClass = Cast<ULinkerPlaceholderClass>(InterfaceClass))
	{
		PlaceholderClass->RemoveReferencingProperty(this);
	}
#endif // USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING

	Super::BeginDestroy();
}

void FInterfaceProperty::PostDuplicate(const FField& InField)
{
	const FInterfaceProperty& Source = static_cast<const FInterfaceProperty&>(InField);
	InterfaceClass = Source.InterfaceClass;
	Super::PostDuplicate(InField);
}

/**
 * Returns the text to use for exporting this property to header file.
 *
 * @param	ExtendedTypeText	for property types which use templates, will be filled in with the type
 * @param	CPPExportFlags		flags for modifying the behavior of the export
 */
FString FInterfaceProperty::GetCPPMacroType( FString& ExtendedTypeText ) const
{
	checkSlow(InterfaceClass);

	UClass* ExportClass = InterfaceClass;
	while ( ExportClass && !ExportClass->HasAnyClassFlags(CLASS_Native) )
	{
		ExportClass = ExportClass->GetSuperClass();
	}
	check(ExportClass);
	check(ExportClass->HasAnyClassFlags(CLASS_Interface));

	ExtendedTypeText = FString::Printf(TEXT("I%s"), *ExportClass->GetName());
	return TEXT("TINTERFACE");
}

/**
 * Returns the text to use for exporting this property to header file.
 *
 * @param	ExtendedTypeText	for property types which use templates, will be filled in with the type
 * @param	CPPExportFlags		flags for modifying the behavior of the export
 */
FString FInterfaceProperty::GetCPPType( FString* ExtendedTypeText/*=NULL*/, uint32 CPPExportFlags/*=0*/ ) const
{
	checkSlow(InterfaceClass);

	if ( ExtendedTypeText != NULL )
	{
		UClass* ExportClass = InterfaceClass;
		if (0 == (CPPF_BlueprintCppBackend & CPPExportFlags))
		{
			while (ExportClass && !ExportClass->HasAnyClassFlags(CLASS_Native))
			{
				ExportClass = ExportClass->GetSuperClass();
			}
		}
		check(ExportClass);
		check(ExportClass->HasAnyClassFlags(CLASS_Interface) || 0 != (CPPF_BlueprintCppBackend & CPPExportFlags));

		*ExtendedTypeText = FString::Printf(TEXT("<I%s>"), *ExportClass->GetName());
	}

	return TEXT("TScriptInterface");
}

FString FInterfaceProperty::GetCPPTypeForwardDeclaration() const
{
	checkSlow(InterfaceClass);
	UClass* ExportClass = InterfaceClass;
	while (ExportClass && !ExportClass->HasAnyClassFlags(CLASS_Native))
	{
		ExportClass = ExportClass->GetSuperClass();
	}
	check(ExportClass);
	check(ExportClass->HasAnyClassFlags(CLASS_Interface));

	return FString::Printf(TEXT("class I%s;"), *ExportClass->GetName());
}

void FInterfaceProperty::LinkInternal(FArchive& Ar)
{
	// for now, we won't support instancing of interface properties...it might be possible, but for the first pass we'll keep it simple
	PropertyFlags &= ~CPF_InterfaceClearMask;
	Super::LinkInternal(Ar);
}

bool FInterfaceProperty::Identical( const void* A, const void* B, uint32 PortFlags/*=0*/ ) const
{
	FScriptInterface* InterfaceA = (FScriptInterface*)A;
	FScriptInterface* InterfaceB = (FScriptInterface*)B;

	if ( InterfaceB == NULL )
	{
		return InterfaceA->GetObject() == NULL;
	}

	return (InterfaceA->GetObject() == InterfaceB->GetObject() && InterfaceA->GetInterface() == InterfaceB->GetInterface());
}

void FInterfaceProperty::SerializeItem( FStructuredArchive::FSlot Slot, void* Value, void const* Defaults ) const
{
	FArchive& UnderlyingArchive = Slot.GetUnderlyingArchive();
	FScriptInterface* InterfaceValue = (FScriptInterface*)Value;

	Slot << InterfaceValue->GetObjectRef();
	if (UnderlyingArchive.IsLoading() || UnderlyingArchive.IsTransacting() || UnderlyingArchive.IsObjectReferenceCollector())
	{
		if ( InterfaceValue->GetObject() != NULL )
		{
			InterfaceValue->SetInterface(InterfaceValue->GetObject()->GetInterfaceAddress(InterfaceClass));
		}
		else
		{
			InterfaceValue->SetInterface(NULL);
		}
	}
}

bool FInterfaceProperty::NetSerializeItem( FArchive& Ar, UPackageMap* Map, void* Data, TArray<uint8> * MetaData ) const
{
	//@todo
	return false;
}

void FInterfaceProperty::ExportTextItem( FString& ValueStr, const void* PropertyValue, const void* DefaultValue, UObject* Parent, int32 PortFlags, UObject* ExportRootScope ) const
{
	FScriptInterface* InterfaceValue = (FScriptInterface*)PropertyValue;

	UObject* Temp = InterfaceValue->GetObject();

	if (0 != (PortFlags & PPF_ExportCpp))
	{
		const FString GetObjectStr = Temp
			? FString::Printf(TEXT("LoadObject<UObject>(nullptr, TEXT(\"%s\"))"), *Temp->GetPathName().ReplaceCharWithEscapedChar())
			: TEXT("");
		ValueStr += FString::Printf(TEXT("TScriptInterface<I%s>(%s)")
			, (InterfaceClass ? *InterfaceClass->GetName() : TEXT("Interface"))
			, *GetObjectStr);
		return;
	}

	if( Temp != NULL )
	{
		bool bExportFullyQualified = true;

		// When exporting from one package or graph to another package or graph, we don't want to fully qualify the name, as it may refer
		// to a level or graph that doesn't exist or cause a linkage to a node in a different graph
		UObject* StopOuter = NULL;
		if (PortFlags & PPF_ExportsNotFullyQualified)
		{
			StopOuter = (ExportRootScope || (Parent == NULL)) ? ExportRootScope : Parent->GetOutermost();
			bExportFullyQualified = !Temp->IsIn(StopOuter);
		}

		// if we want a full qualified object reference, use the pathname, otherwise, use just the object name
		if (bExportFullyQualified)
		{
			StopOuter = NULL;
			if ( (PortFlags&PPF_SimpleObjectText) != 0 && Parent != NULL )
			{
				StopOuter = Parent->GetOutermost();
			}
		}
		
		ValueStr += FString::Printf( TEXT("%s'%s'"), *Temp->GetClass()->GetName(), *Temp->GetPathName(StopOuter) );
	}
	else
	{
		ValueStr += TEXT("None");
	}
}

const TCHAR* FInterfaceProperty::ImportText_Internal( const TCHAR* InBuffer, void* Data, int32 PortFlags, UObject* Parent, FOutputDevice* ErrorText/*=NULL*/ ) const
{
	FScriptInterface* InterfaceValue = (FScriptInterface*)Data;
	UObject* ResolvedObject = InterfaceValue->GetObject();
	void* InterfaceAddress = InterfaceValue->GetInterface();

	const TCHAR* Buffer = InBuffer;
	if ( !FObjectPropertyBase::ParseObjectPropertyValue(this, Parent, UObject::StaticClass(), PortFlags, Buffer, ResolvedObject) )
	{
		// we only need to call SetObject here - if ObjectAddress was not modified, then InterfaceValue should not be modified either
		// if it was set to NULL, SetObject will take care of clearing the interface address too
		InterfaceValue->SetObject(ResolvedObject);
		return NULL;
	}

	// so we should now have a valid object
	if ( ResolvedObject == NULL )
	{
		// if ParseObjectPropertyValue returned true but ResolvedObject is NULL, the imported text was "None".  Make sure the interface pointer
		// is cleared, then stop
		InterfaceValue->SetObject(NULL);
		return Buffer;
	}

	void* NewInterfaceAddress = ResolvedObject->GetInterfaceAddress(InterfaceClass);
	if ( NewInterfaceAddress == NULL )
	{
		// the object we imported doesn't implement our interface class
		ErrorText->Logf( TEXT("%s: specified object doesn't implement the required interface class '%s': %s"),
						*GetFullName(), *InterfaceClass->GetName(), InBuffer );

		return NULL;
	}

	InterfaceValue->SetObject(ResolvedObject);
	InterfaceValue->SetInterface(NewInterfaceAddress);
	return Buffer;
}

bool FInterfaceProperty::ContainsObjectReference(TArray<const FStructProperty*>& EncounteredStructProps, EPropertyObjectReferenceType InReferenceType /*= EPropertyObjectReferenceType::Strong*/) const
{
	return !!(InReferenceType & EPropertyObjectReferenceType::Strong);
}

/** Manipulates the data referenced by this FProperty */
void FInterfaceProperty::Serialize( FArchive& Ar )
{
	Super::Serialize( Ar );

	Ar << InterfaceClass;

#if USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING
	if (Ar.IsLoading() || Ar.IsObjectReferenceCollector())
	{
		if (ULinkerPlaceholderClass* PlaceholderClass = Cast<ULinkerPlaceholderClass>(InterfaceClass))
		{
			PlaceholderClass->AddReferencingProperty(this);
		}
	}
#endif // USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING

	if ( !InterfaceClass )
 	{
		// If we failed to load the InterfaceClass and we're not a CDO, that means we relied on a class that has been removed or doesn't exist.
		// The most likely cause for this is either an incomplete recompile, or if content was migrated between games that had native class dependencies
		// that do not exist in this game.  We allow blueprint classes to continue, because compile-on-load will error out, and stub the class that was using it
		UClass* TestClass = dynamic_cast<UClass*>(GetOwnerStruct());
		if( TestClass && TestClass->HasAllClassFlags(CLASS_Native) && !TestClass->HasAllClassFlags(CLASS_NewerVersionExists) && (TestClass->GetOutermost() != GetTransientPackage()) )
		{
			checkf(false, TEXT("Interface property tried to serialize a missing interface.  Did you remove a native class and not fully recompile?"));
		}
 	}
}


#if USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING
void FInterfaceProperty::SetInterfaceClass(UClass* NewInterfaceClass)
{
	if (ULinkerPlaceholderClass* NewPlaceholderClass = Cast<ULinkerPlaceholderClass>(NewInterfaceClass))
	{
		NewPlaceholderClass->AddReferencingProperty(this);
	}

	if (ULinkerPlaceholderClass* OldPlaceholderClass = Cast<ULinkerPlaceholderClass>(InterfaceClass))
	{
		OldPlaceholderClass->RemoveReferencingProperty(this);
	}
	InterfaceClass = NewInterfaceClass;
}
#endif // USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING

bool FInterfaceProperty::SameType(const FProperty* Other) const
{
	return Super::SameType(Other) && (InterfaceClass == ((FInterfaceProperty*)Other)->InterfaceClass);
}

void FInterfaceProperty::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(InterfaceClass);
	Super::AddReferencedObjects(Collector);
}

#include "UObject/DefineUPropertyMacros.h"