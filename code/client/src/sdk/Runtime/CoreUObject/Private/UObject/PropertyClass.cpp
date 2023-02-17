// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Templates/Casts.h"
#include "UObject/UnrealType.h"
#include "UObject/UnrealTypePrivate.h"
#include "UObject/LinkerPlaceholderClass.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/PropertyHelper.h"

// WARNING: This should always be the last include in any file that needs it (except .generated.h)
#include "UObject/UndefineUPropertyMacros.h"

/*-----------------------------------------------------------------------------
	FClassProperty.
-----------------------------------------------------------------------------*/
IMPLEMENT_FIELD(FClassProperty)

#if WITH_EDITORONLY_DATA
FClassProperty::FClassProperty(UField* InField)
	: FObjectProperty(InField)
{
	UClassProperty* SourceProperty = CastChecked<UClassProperty>(InField);
	MetaClass = SourceProperty->MetaClass;
}
#endif // WITH_EDITORONLY_DATA

void FClassProperty::BeginDestroy()
{
#if USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING
	if (ULinkerPlaceholderClass* PlaceholderClass = Cast<ULinkerPlaceholderClass>(MetaClass))
	{
		PlaceholderClass->RemoveReferencingProperty(this);
	}
#endif // USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING

	Super::BeginDestroy();
}

void FClassProperty::PostDuplicate(const FField& InField)
{
	const FClassProperty& Source = static_cast<const FClassProperty&>(InField);
	MetaClass = Source.MetaClass;
	Super::PostDuplicate(InField);
}

void FClassProperty::Serialize( FArchive& Ar )
{
	Super::Serialize( Ar );
	Ar << MetaClass;

#if USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING
	if (Ar.IsLoading() || Ar.IsObjectReferenceCollector())
	{
		if (ULinkerPlaceholderClass* PlaceholderClass = Cast<ULinkerPlaceholderClass>(MetaClass))
		{
			PlaceholderClass->AddReferencingProperty(this);
		}
	}
#endif // USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING

	if( !MetaClass )
	{
		// If we failed to load the MetaClass and we're not a CDO, that means we relied on a class that has been removed or doesn't exist.
		// The most likely cause for this is either an incomplete recompile, or if content was migrated between games that had native class dependencies
		// that do not exist in this game.  We allow blueprint classes to continue, because compile on load will error out, and stub the class that was using it
		UClass* TestClass = dynamic_cast<UClass*>(GetOwnerStruct());
		if( TestClass && TestClass->HasAllClassFlags(CLASS_Native) && !TestClass->HasAllClassFlags(CLASS_NewerVersionExists) && (TestClass->GetOutermost() != GetTransientPackage()) )
		{
			checkf(false, TEXT("Class property tried to serialize a missing class.  Did you remove a native class and not fully recompile?"));
		}
	}
}

#if USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING
void FClassProperty::SetMetaClass(UClass* NewMetaClass)
{
	if (ULinkerPlaceholderClass* NewPlaceholderClass = Cast<ULinkerPlaceholderClass>(NewMetaClass))
	{
		NewPlaceholderClass->AddReferencingProperty(this);
	}

	if (ULinkerPlaceholderClass* OldPlaceholderClass = Cast<ULinkerPlaceholderClass>(MetaClass))
	{
		OldPlaceholderClass->RemoveReferencingProperty(this);
	}
	MetaClass = NewMetaClass;
}
#endif // USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING

void FClassProperty::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject( MetaClass );
	Super::AddReferencedObjects( Collector );
}

const TCHAR* FClassProperty::ImportText_Internal( const TCHAR* Buffer, void* Data, int32 PortFlags, UObject* Parent, FOutputDevice* ErrorText ) const
{
	const TCHAR* Result = FObjectProperty::ImportText_Internal( Buffer, Data, PortFlags, Parent, ErrorText );
	if( Result )
	{
		if (UClass* AssignedPropertyClass = dynamic_cast<UClass*>(GetObjectPropertyValue(Data)))
		{
#if USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING
			FLinkerLoad* ObjectLinker = (Parent != nullptr) ? Parent->GetClass()->GetLinker() : GetLinker();
			auto IsDeferringValueLoad = [](const UClass* Class)->bool
			{
				const ULinkerPlaceholderClass* Placeholder = Cast<ULinkerPlaceholderClass>(Class);
				return Placeholder && !Placeholder->IsMarkedResolved();
			};
			const bool bIsDeferringValueLoad = IsDeferringValueLoad(MetaClass) || ((!ObjectLinker || (ObjectLinker->LoadFlags & LOAD_DeferDependencyLoads) != 0) && IsDeferringValueLoad(AssignedPropertyClass));

#if USE_DEFERRED_DEPENDENCY_CHECK_VERIFICATION_TESTS
			check( bIsDeferringValueLoad || !(Cast<ULinkerPlaceholderClass>(MetaClass) || Cast<ULinkerPlaceholderClass>(AssignedPropertyClass)) );
#endif // USE_DEFERRED_DEPENDENCY_CHECK_VERIFICATION_TESTS

#else  // USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING 
			bool const bIsDeferringValueLoad = false;
#endif // USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING

			// Validate metaclass.
			if ((!AssignedPropertyClass->IsChildOf(MetaClass)) && !bIsDeferringValueLoad)
			{
				// the object we imported doesn't implement our interface class
				ErrorText->Logf(TEXT("Invalid object '%s' specified for property '%s'"), *AssignedPropertyClass->GetFullName(), *GetName());
				SetObjectPropertyValue(Data, NULL);
				Result = NULL;
			}
		}
	}
	return Result;
}

FString FClassProperty::GetCPPType(FString* ExtendedTypeText, uint32 CPPExportFlags) const
{
	check(MetaClass);
	return GetCPPTypeCustom(ExtendedTypeText, CPPExportFlags,
		FString::Printf(TEXT("%s%s"), MetaClass->GetPrefixCPP(), *MetaClass->GetName()));
}

FString FClassProperty::GetCPPTypeCustom(FString* ExtendedTypeText, uint32 CPPExportFlags, const FString& InnerNativeTypeName) const
{
	if (PropertyFlags & CPF_UObjectWrapper)
	{
		ensure(!InnerNativeTypeName.IsEmpty());
		return FString::Printf(TEXT("TSubclassOf<%s> "), *InnerNativeTypeName);
	}
	else
	{
		return TEXT("UClass*");
	}
}

FString FClassProperty::GetCPPTypeForwardDeclaration() const
{
	return FString::Printf(TEXT("class %s%s;"), MetaClass->GetPrefixCPP(), *MetaClass->GetName());
}

FString FClassProperty::GetCPPMacroType( FString& ExtendedTypeText ) const
{
	ExtendedTypeText = TEXT("UClass");
	return TEXT("OBJECT");
}

bool FClassProperty::SameType(const FProperty* Other) const
{
	return Super::SameType(Other) && (MetaClass == ((FClassProperty*)Other)->MetaClass);
}

bool FClassProperty::Identical( const void* A, const void* B, uint32 PortFlags ) const
{
	UObject* ObjectA = A ? GetObjectPropertyValue(A) : nullptr;
	UObject* ObjectB = B ? GetObjectPropertyValue(B) : nullptr;

	check(ObjectA == nullptr || ObjectA->IsA<UClass>());
	check(ObjectB == nullptr || ObjectB->IsA<UClass>());
	return (ObjectA == ObjectB);
}

#include "UObject/DefineUPropertyMacros.h"