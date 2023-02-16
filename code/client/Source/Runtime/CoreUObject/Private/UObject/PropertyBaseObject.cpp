// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Templates/Casts.h"
#include "UObject/UnrealType.h"
#include "UObject/UnrealTypePrivate.h"
#include "UObject/UObjectHash.h"
#include "Blueprint/BlueprintSupport.h"
#include "UObject/PropertyHelper.h"
#include "UObject/LinkerPlaceholderClass.h"
#include "UObject/LinkerPlaceholderExportObject.h"
#include "Misc/StringBuilder.h"

// WARNING: This should always be the last include in any file that needs it (except .generated.h)
#include "UObject/UndefineUPropertyMacros.h"

/*-----------------------------------------------------------------------------
	FObjectPropertyBase.
-----------------------------------------------------------------------------*/
IMPLEMENT_FIELD(FObjectPropertyBase)

#if WITH_EDITORONLY_DATA
FObjectPropertyBase::FObjectPropertyBase(UField* InField)
	: FProperty(InField)
{
	UObjectPropertyBase* SourceProperty = CastChecked<UObjectPropertyBase>(InField);
	PropertyClass = SourceProperty->PropertyClass;
}
#endif // WITH_EDITORONLY_DATA

void FObjectPropertyBase::BeginDestroy()
{
#if USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING
	if (ULinkerPlaceholderClass* PlaceholderClass = Cast<ULinkerPlaceholderClass>(PropertyClass))
	{
		PlaceholderClass->RemoveReferencingProperty(this);
	}
#endif // USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING

	Super::BeginDestroy();
}

void FObjectPropertyBase::InstanceSubobjects(void* Data, void const* DefaultData, UObject* InOwner, FObjectInstancingGraph* InstanceGraph )
{
	for ( int32 ArrayIndex = 0; ArrayIndex < ArrayDim; ArrayIndex++ )
	{
		UObject* CurrentValue = GetObjectPropertyValue((uint8*)Data + ArrayIndex * ElementSize);
		if ( CurrentValue )
		{
			UObject *SubobjectTemplate = DefaultData ? GetObjectPropertyValue((uint8*)DefaultData + ArrayIndex * ElementSize): nullptr;
			UObject* NewValue = InstanceGraph->InstancePropertyValue(SubobjectTemplate, CurrentValue, InOwner, HasAnyPropertyFlags(CPF_Transient), HasAnyPropertyFlags(CPF_InstancedReference));
			SetObjectPropertyValue((uint8*)Data + ArrayIndex * ElementSize, NewValue);
		}
	}
}

bool FObjectPropertyBase::Identical( const void* A, const void* B, uint32 PortFlags ) const
{
	UObject* ObjectA = A ? GetObjectPropertyValue(A) : nullptr;
	UObject* ObjectB = B ? GetObjectPropertyValue(B) : nullptr;
	if (!ObjectA && !ObjectB)
	{
		return true;
	}
	if (!ObjectA || !ObjectB)
	{
		return false;
	}
	// Compare actual pointers. We don't do this during PIE because we want to be sure to serialize everything. An example is the LevelScriptActor being serialized against its CDO,
	// which contains actor references. We want to serialize those references so they are fixed up.
	const bool bDuplicatingForPIE = (PortFlags&PPF_DuplicateForPIE) != 0;
	bool bResult = !bDuplicatingForPIE ? (ObjectA == ObjectB) : false;
	// always serialize the cross level references, because they could be nullptr
	// @todo: okay, this is pretty hacky overall - we should have a PortFlag or something
	// that is set during SavePackage. Other times, we don't want to immediately return false
	// (instead of just this ExportDefProps case)

	// In order for a deep comparison of instanced objects to match both objects must have the same class and name
	if (!bResult && ObjectA->GetClass() == ObjectB->GetClass() && ObjectA->GetFName() == ObjectB->GetFName())
	{
		bool bPerformDeepComparison = (PortFlags&PPF_DeepComparison) != 0;
		if (((PortFlags&PPF_DeepCompareInstances) != 0) && !bPerformDeepComparison)
		{
			bPerformDeepComparison = !(ObjectA->IsTemplate() && ObjectB->IsTemplate());
		}

		if (bPerformDeepComparison)
		{
			if ((PortFlags&PPF_DeepCompareDSOsOnly) != 0)
			{
				if (UObject* DSO = ObjectA->GetClass()->GetDefaultSubobjectByName(ObjectA->GetFName()))
				{
					checkSlow(ObjectA->IsDefaultSubobject() && ObjectB->IsDefaultSubobject() && DSO == ObjectB->GetClass()->GetDefaultSubobjectByName(ObjectB->GetFName()));
				}
				else
				{
					bPerformDeepComparison = false;
				}
			}

			if (bPerformDeepComparison)
			{
				bResult = AreInstancedObjectsIdentical(ObjectA, ObjectB, PortFlags);
			}
		}
	}
	return bResult;
}

bool FObjectPropertyBase::NetSerializeItem( FArchive& Ar, UPackageMap* Map, void* Data, TArray<uint8> * MetaData ) const
{
	UObject* Object = GetObjectPropertyValue(Data);
	bool Result = Map->SerializeObject( Ar, PropertyClass, Object );
	SetObjectPropertyValue(Data, Object);
	return Result;
}
void FObjectPropertyBase::Serialize( FArchive& Ar )
{
	Super::Serialize( Ar );
	Ar << PropertyClass;

#if USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING
	if (Ar.IsLoading() || Ar.IsObjectReferenceCollector())
	{
		if (ULinkerPlaceholderClass* PlaceholderClass = Cast<ULinkerPlaceholderClass>(PropertyClass))
		{
			PlaceholderClass->AddReferencingProperty(this);
		}
	}
#endif // USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING
}

void FObjectPropertyBase::PostDuplicate(const FField& InField)
{
	const FObjectPropertyBase& Source = static_cast<const FObjectPropertyBase&>(InField);
	PropertyClass = Source.PropertyClass;
	Super::PostDuplicate(InField);
}

#if USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING
void FObjectPropertyBase::SetPropertyClass(UClass* NewPropertyClass)
{
	if (ULinkerPlaceholderClass* NewPlaceholderClass = Cast<ULinkerPlaceholderClass>(NewPropertyClass))
	{
		NewPlaceholderClass->AddReferencingProperty(this);
	}
	
	if (ULinkerPlaceholderClass* OldPlaceholderClass = Cast<ULinkerPlaceholderClass>(PropertyClass))
	{
		OldPlaceholderClass->RemoveReferencingProperty(this);
	}
	PropertyClass = NewPropertyClass;
}
#endif // USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING

void FObjectPropertyBase::AddReferencedObjects(FReferenceCollector& Collector)
{	
	Collector.AddReferencedObject( PropertyClass );
	Super::AddReferencedObjects( Collector );
}

FString FObjectPropertyBase::GetExportPath(const UObject* Object, const UObject* Parent, const UObject* ExportRootScope, const uint32 PortFlags)
{
	bool bExportFullyQualified = true;

	// When exporting from one package or graph to another package or graph, we don't want to fully qualify the name, as it may refer
	// to a level or graph that doesn't exist or cause a linkage to a node in a different graph
	const UObject* StopOuter = nullptr;
	if (PortFlags & PPF_ExportsNotFullyQualified)
	{
		StopOuter = (ExportRootScope || (Parent == nullptr)) ? ExportRootScope : Parent->GetOutermost();
		bExportFullyQualified = StopOuter && !Object->IsIn(StopOuter);

		// Also don't fully qualify the name if it's a sibling of the root scope, since it may be included in the exported set of objects
		if (bExportFullyQualified)
		{
			StopOuter = StopOuter->GetOuter();
			bExportFullyQualified = (StopOuter == nullptr) || (!Object->IsIn(StopOuter));
		}
	}

	// if we want a full qualified object reference, use the pathname, otherwise, use just the object name
	if (bExportFullyQualified)
	{
		StopOuter = nullptr;
		if ( (PortFlags&PPF_SimpleObjectText) != 0 && Parent != nullptr )
		{
			StopOuter = Parent->GetOutermost();
		}
	}
	else if (Parent != nullptr && Object->IsIn(Parent))
	{
		StopOuter = Parent;
	}

	// Take the path name relative to the stopping point outermost ptr.
	// This is so that cases like a component referencing a component in another actor work correctly when pasted
	FString PathName = Object->GetPathName(StopOuter);
	int32 ResultIdx = 0;
	// Object names that contain invalid characters and paths that contain spaces must be put into quotes to be handled correctly
	if (PortFlags & PPF_Delimited)
	{
		PathName = FString::Printf(TEXT("\"%s\""), *PathName.ReplaceQuotesWithEscapedQuotes());
	}
	return FString::Printf( TEXT("%s'%s'"), *Object->GetClass()->GetName(), *PathName );
}

void FObjectPropertyBase::ExportTextItem( FString& ValueStr, const void* PropertyValue, const void* DefaultValue, UObject* Parent, int32 PortFlags, UObject* ExportRootScope ) const
{
	UObject* Temp = GetObjectPropertyValue(PropertyValue);

	if (0 != (PortFlags & PPF_ExportCpp))
	{
		ValueStr += Temp
			? FString::Printf(TEXT("LoadObject<%s%s>(nullptr, TEXT(\"%s\"))")
				, PropertyClass->GetPrefixCPP()
				, *PropertyClass->GetName()
				, *(Temp->GetPathName().ReplaceCharWithEscapedChar()))
			: TEXT("nullptr");
		return;
	}

	if( Temp != nullptr )
	{
		if (PortFlags & PPF_DebugDump)
		{
			ValueStr += Temp->GetFullName();
		}
		else if (Parent && !Parent->HasAnyFlags(RF_ClassDefaultObject) && Temp->IsDefaultSubobject())
		{
			if (PortFlags & PPF_Delimited)
			{
				ValueStr += FString::Printf(TEXT("\"%s\""), *Temp->GetName().ReplaceQuotesWithEscapedQuotes());
			}
			else
			{
				ValueStr += Temp->GetName();
			}
		}
		else
		{
			ValueStr += GetExportPath(Temp, Parent, ExportRootScope, PortFlags);
		}
	}
	else
	{
		ValueStr += TEXT("None");
	}
}

/**
 * Parses a text buffer into an object reference.
 *
 * @param	Property			the property that the value is being importing to
 * @param	OwnerObject			the object that is importing the value; used for determining search scope.
 * @param	RequiredMetaClass	the meta-class for the object to find; if the object that is resolved is not of this class type, the result is nullptr.
 * @param	PortFlags			bitmask of EPropertyPortFlags that can modify the behavior of the search
 * @param	Buffer				the text to parse; should point to a textual representation of an object reference.  Can be just the object name (either fully 
 *								fully qualified or not), or can be formatted as a const object reference (i.e. SomeClass'SomePackage.TheObject')
 *								When the function returns, Buffer will be pointing to the first character after the object value text in the input stream.
 * @param	ResolvedValue		receives the object that is resolved from the input text.
 *
 * @return	true if the text is successfully resolved into a valid object reference of the correct type, false otherwise.
 */
bool FObjectPropertyBase::ParseObjectPropertyValue(const FProperty* Property, UObject* OwnerObject, UClass* RequiredMetaClass, uint32 PortFlags, const TCHAR*& Buffer, UObject*& out_ResolvedValue, FUObjectSerializeContext* InSerializeContext /*= nullptr*/, bool bAllowAnyPackage /*= true*/)
{
	check(Property);
	if (!RequiredMetaClass)
	{
		UE_LOG(LogProperty, Error, TEXT("ParseObjectPropertyValue Error: RequiredMetaClass is null, for property: %s "), *Property->GetFullName());
		out_ResolvedValue = nullptr;
		return false;
	}

 	const TCHAR* InBuffer = Buffer;

	TStringBuilder<256> Temp;
	Buffer = FPropertyHelpers::ReadToken(Buffer, /* out */ Temp, true);
	if ( Buffer == nullptr )
	{
		return false;
	}

	if ( Temp == TEXT("None"_SV) )
	{
		out_ResolvedValue = nullptr;
	}
	else
	{
		UClass*	ObjectClass = RequiredMetaClass;

		SkipWhitespace(Buffer);

		bool bWarnOnnullptr = (PortFlags&PPF_CheckReferences)!=0;

		if( *Buffer == TCHAR('\'') )
		{
			Temp.Reset();
			Buffer = FPropertyHelpers::ReadToken( ++Buffer, /* out */ Temp, true);
			if( Buffer == nullptr )
			{
				return false;
			}

			if( *Buffer++ != TCHAR('\'') )
			{
				return false;
			}

			// ignore the object class, it isn't fully qualified, and searching ANY_PACKAGE might get the wrong one!
			// Try the find the object.
			out_ResolvedValue = FObjectPropertyBase::FindImportedObject(Property, OwnerObject, ObjectClass, RequiredMetaClass, Temp.ToString(), PortFlags, InSerializeContext, bAllowAnyPackage);
		}
		else
		{
			// Try the find the object.
			out_ResolvedValue = FObjectPropertyBase::FindImportedObject(Property, OwnerObject, ObjectClass, RequiredMetaClass, Temp.ToString(), PortFlags, InSerializeContext, bAllowAnyPackage);
		}

		if ( out_ResolvedValue != nullptr && !out_ResolvedValue->GetClass()->IsChildOf(RequiredMetaClass) )
		{
			if (bWarnOnnullptr )
			{
				UE_LOG(LogProperty, Error, TEXT("%s: bad cast in '%s'"), *Property->GetFullName(), InBuffer );
			}

			out_ResolvedValue = nullptr;
			return false;
		}

		// If we couldn't find it or load it, we'll have to do without it.
		if ( out_ResolvedValue == nullptr )
		{
			if( bWarnOnnullptr )
			{
				UE_LOG(LogProperty, Warning, TEXT("%s: unresolved reference to '%s'"), *Property->GetFullName(), InBuffer );
			}
			return false;
		}
	}

	return true;
}

const TCHAR* FObjectPropertyBase::ImportText_Internal( const TCHAR* InBuffer, void* Data, int32 PortFlags, UObject* Parent, FOutputDevice* ErrorText ) const
{
	const TCHAR* Buffer = InBuffer;
	UObject* Result = nullptr;
	FLinkerLoad* Linker = GetLinker();

	bool bOk = ParseObjectPropertyValue(this, Parent, PropertyClass, PortFlags, Buffer, Result, Linker ? Linker->GetSerializeContext() : nullptr);

	if (Result && (PortFlags & PPF_InstanceSubobjects) != 0 && HasAnyPropertyFlags(CPF_InstancedReference))
	{
		FName DesiredName = Result->GetFName();

		// If an object currently exists with the same name as the imported object that is to be instanced
		// 
		if (UObject* ExistingObject = static_cast<UObject*>(FindObjectWithOuter(Parent, nullptr, DesiredName)))
		{
			ExistingObject->Rename(nullptr, nullptr, REN_DontCreateRedirectors | REN_ForceNoResetLoaders);
		}

		Result = DuplicateObject<UObject>(Result, Parent, DesiredName);
		if (Parent->IsTemplate())
		{
			Result->SetFlags(RF_ArchetypeObject);
		}
		else
		{
			Result->ClearFlags(RF_ArchetypeObject);
		}
	}

	SetObjectPropertyValue(Data, Result);
	return Buffer;
}

UObject* FObjectPropertyBase::FindImportedObject( const FProperty* Property, UObject* OwnerObject, UClass* ObjectClass, UClass* RequiredMetaClass, const TCHAR* Text, uint32 PortFlags/*=0*/, FUObjectSerializeContext* InSerializeContext /*= nullptr*/, bool bAllowAnyPackage /*= true*/)
{
	UObject*	Result = nullptr;
	check( ObjectClass->IsChildOf(RequiredMetaClass) );

	bool AttemptNonQualifiedSearch = (PortFlags & PPF_AttemptNonQualifiedSearch) != 0; 

	// if we are importing default properties, first look for a matching subobject by
	// looking through the archetype chain at each outer and stop once the outer chain reaches the owning class's default object
	if (PortFlags & PPF_ParsingDefaultProperties)
	{
		for (UObject* SearchStart = OwnerObject; Result == nullptr && SearchStart != nullptr; SearchStart = SearchStart->GetOuter())
		{
			UObject* ScopedSearchRoot = SearchStart;
			while (Result == nullptr && ScopedSearchRoot != nullptr)
			{
				Result = StaticFindObjectSafe(ObjectClass, ScopedSearchRoot, Text);
				// don't think it's possible to get a non-subobject here, but it doesn't hurt to check
				if (Result != nullptr && !Result->IsTemplate(RF_ClassDefaultObject))
				{
					Result = nullptr;
				}

				ScopedSearchRoot = ScopedSearchRoot->GetArchetype();
			}
			if (SearchStart->HasAnyFlags(RF_ClassDefaultObject))
			{
				break;
			}
		}
	}
	
	// if we have a parent, look in the parent, then it's outer, then it's outer, ... 
	// this is because exported object properties that point to objects in the level aren't
	// fully qualified, and this will step up the nested object chain to solve any name
	// collisions within a nested object tree
	UObject* ScopedSearchRoot = OwnerObject;
	while (Result == nullptr && ScopedSearchRoot != nullptr)
	{
		Result = StaticFindObjectSafe(ObjectClass, ScopedSearchRoot, Text);
		// disallow class default subobjects here while importing defaults
		// this prevents the use of a subobject name that doesn't exist in the scope of the default object being imported
		// from grabbing some other subobject with the same name and class in some other arbitrary default object
		if (Result != nullptr && (PortFlags & PPF_ParsingDefaultProperties) && Result->IsTemplate(RF_ClassDefaultObject))
		{
			Result = nullptr;
		}

		ScopedSearchRoot = ScopedSearchRoot->GetOuter();
	}

	if (Result == nullptr)
	{
		// attempt to find a fully qualified object
		Result = StaticFindObjectSafe(ObjectClass, nullptr, Text);

		if (Result == nullptr && (PortFlags & PPF_SerializedAsImportText))
		{
			// Check string asset redirectors
			FSoftObjectPath Path(Text);
			if (Path.PreSavePath())
			{
				Result = StaticFindObjectSafe(ObjectClass, nullptr, *Path.ToString());
			}
		}

		if (Result == nullptr && bAllowAnyPackage)
		{
			// match any object of the correct class who shares the same name regardless of package path
			Result = StaticFindObjectSafe(ObjectClass, ANY_PACKAGE, Text);
			// disallow class default subobjects here while importing defaults
			if (Result != nullptr && (PortFlags & PPF_ParsingDefaultProperties) && Result->IsTemplate(RF_ClassDefaultObject))
			{
				Result = nullptr;
			}
		}
	}

	// if we haven;t found it yet, then try to find it without a qualified name
	if (!Result)
	{
		const TCHAR* Dot = FCString::Strrchr(Text, '.');
		if (Dot && AttemptNonQualifiedSearch)
		{
			// search with just the object name
			Result = FindImportedObject(Property, OwnerObject, ObjectClass, RequiredMetaClass, Dot + 1, 0);
		}
		FString NewText(Text);
		// if it didn't have a dot, then maybe they just gave a uasset package name
		if (!Dot && !Result)
		{
			int32 LastSlash = NewText.Find(TEXT("/"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			if (LastSlash >= 0)
			{
				NewText += TEXT(".");
				NewText += (Text + LastSlash + 1);
				Dot = FCString::Strrchr(*NewText, '.');
			}
		}
		// If we still can't find it, try to load it. (Only try to load fully qualified names)
		if(!Result && Dot && !GIsSavingPackage)
		{
#if USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING
			FLinkerLoad* Linker = (OwnerObject != nullptr) ? OwnerObject->GetClass()->GetLinker() : nullptr;
			if (Linker == nullptr)
			{
				// Fall back on the Properties owner. That is probably the thing that has triggered this load:
				Linker = Property->GetLinker();
			}
			const bool bDeferAssetImports = (Linker != nullptr) && (Linker->LoadFlags & LOAD_DeferDependencyLoads);

			if (bDeferAssetImports)
			{
				Result = Linker->RequestPlaceholderValue(ObjectClass, Text);
			}
			
			if (Result == nullptr)
#endif // USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING
			{
				const uint32 LoadFlags = LOAD_NoWarn | LOAD_FindIfFail;		

				UE_LOG(LogProperty, Verbose, TEXT("FindImportedObject is attempting to import [%s] (class = %s) with StaticLoadObject"), Text, *GetFullNameSafe(ObjectClass));
				Result = StaticLoadObject(ObjectClass, nullptr, Text, nullptr, LoadFlags, nullptr, true);

#if USE_DEFERRED_DEPENDENCY_CHECK_VERIFICATION_TESTS
				check(!bDeferAssetImports || !Result || !FBlueprintSupport::IsInBlueprintPackage(Result));
#endif // USE_DEFERRED_DEPENDENCY_CHECK_VERIFICATION_TESTS
			}
		}
	}

	// if we found an object, and we have a parent, make sure we are in the same package or share an outer if the found object is private, unless it's a cross level property
	if (Result && !Result->HasAnyFlags(RF_Public) && OwnerObject 
		&& Result->GetOutermostObject() != OwnerObject->GetOutermostObject()
		&& Result->GetPackage() != OwnerObject->GetPackage())
	{
		const FObjectPropertyBase* ObjectProperty = CastField<const FObjectPropertyBase>(Property);
		if ( !ObjectProperty || !ObjectProperty->AllowCrossLevel())
		{
			UE_LOG(LogProperty, Warning, TEXT("Illegal TEXT reference to a private object in external package (%s) from referencer (%s).  Import failed..."), *Result->GetFullName(), *OwnerObject->GetFullName());
			Result = nullptr;
		}
	}

	check(!Result || Result->IsA(RequiredMetaClass));
	return Result;
}

FName FObjectPropertyBase::GetID() const
{
	return NAME_ObjectProperty;
}

UObject* FObjectPropertyBase::GetObjectPropertyValue(const void* PropertyValueAddress) const
{
	check(0);
	return nullptr;
}

void FObjectPropertyBase::SetObjectPropertyValue(void* PropertyValueAddress, UObject* Value) const
{
	check(0);
}

bool FObjectPropertyBase::AllowCrossLevel() const
{
	return false;
}

void FObjectPropertyBase::CheckValidObject(void* Value) const
{
	UObject *Object = GetObjectPropertyValue(Value);
	if (Object)
	{
		//
		// here we want to make sure the the object value still matches the 
		// object type expected by the property...

		UClass* ObjectClass = Object->GetClass();
		UE_CLOG(!ObjectClass, LogProperty, Fatal, TEXT("Object without class referenced by %s, object: 0x%016llx %s"), *GetPathName(), (int64)(PTRINT)Object, *Object->GetPathName());

		// we could be in the middle of replacing references to the 
		// PropertyClass itself (in the middle of an FArchiveReplaceObjectRef 
		// pass)... if this is the case, then we might have already replaced 
		// the object's class, but not the PropertyClass yet (or vise-versa)... 
		// so we use this to ensure, in that situation, that we don't clear the 
		// object value (if CLASS_NewerVersionExists is set, then we are likely 
		// in the middle of an FArchiveReplaceObjectRef pass)
		bool bIsReplacingClassRefs = PropertyClass && PropertyClass->HasAnyClassFlags(CLASS_NewerVersionExists) != ObjectClass->HasAnyClassFlags(CLASS_NewerVersionExists);
		
#if USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING
		FLinkerLoad* PropertyLinker = GetLinker();
		bool const bIsDeferringValueLoad = ((PropertyLinker == nullptr) || (PropertyLinker->LoadFlags & LOAD_DeferDependencyLoads)) &&
			(Object->IsA<ULinkerPlaceholderExportObject>() || Object->IsA<ULinkerPlaceholderClass>());

#if USE_DEFERRED_DEPENDENCY_CHECK_VERIFICATION_TESTS
		check( bIsDeferringValueLoad || (!Object->IsA<ULinkerPlaceholderExportObject>() && !Object->IsA<ULinkerPlaceholderClass>()) );
#endif // USE_DEFERRED_DEPENDENCY_CHECK_VERIFICATION_TESTS

#else  // USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING 
		bool const bIsDeferringValueLoad = false;
#endif // USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING

		if ((PropertyClass != nullptr) && !ObjectClass->IsChildOf(PropertyClass) && !ObjectClass->GetAuthoritativeClass()->IsChildOf(PropertyClass) && !bIsReplacingClassRefs && !bIsDeferringValueLoad)
		{
			UE_LOG(LogProperty, Warning,
				TEXT("Serialized %s for a property of %s. Reference will be nullptred.\n    Property = %s\n    Item = %s"),
				*Object->GetClass()->GetFullName(),
				*PropertyClass->GetFullName(),
				*GetFullName(),
				*Object->GetFullName()
			);
			SetObjectPropertyValue(Value, nullptr);
		}
	}
}

bool FObjectPropertyBase::SameType(const FProperty* Other) const
{
	return Super::SameType(Other) && (PropertyClass == ((FObjectPropertyBase*)Other)->PropertyClass);
}

void FObjectPropertyBase::CopySingleValueToScriptVM( void* Dest, void const* Src ) const
{
	*(UObject**)Dest = GetObjectPropertyValue(Src);
}

void FObjectPropertyBase::CopyCompleteValueToScriptVM( void* Dest, void const* Src ) const
{
	for (int32 Index = 0; Index < ArrayDim; Index++)
	{
		((UObject**)Dest)[Index] = GetObjectPropertyValue(((uint8*)Src) + Index * ElementSize);
	}
}

void FObjectPropertyBase::CopySingleValueFromScriptVM( void* Dest, void const* Src ) const
{
	SetObjectPropertyValue(Dest, *(UObject**)Src);
}

void FObjectPropertyBase::CopyCompleteValueFromScriptVM( void* Dest, void const* Src ) const
{
	checkSlow(ElementSize == sizeof(UObject*)); // the idea that script pointers are the same size as weak pointers is maybe required, maybe not
	for (int32 Index = 0; Index < ArrayDim; Index++)
	{
		SetObjectPropertyValue(((uint8*)Dest) + Index * ElementSize, ((UObject**)Src)[Index]);
	}
}

#include "UObject/DefineUPropertyMacros.h"