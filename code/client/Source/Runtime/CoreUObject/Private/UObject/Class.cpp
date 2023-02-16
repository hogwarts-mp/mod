// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	UnClass.cpp: Object class implementation.
=============================================================================*/

#include "UObject/Class.h"
#include "HAL/ThreadSafeBool.h"
#include "HAL/LowLevelMemTracker.h"
#include "Misc/ScopeLock.h"
#include "Serialization/MemoryWriter.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/OutputDeviceHelper.h"
#include "Misc/FeedbackContext.h"
#include "Misc/OutputDeviceConsole.h"
#include "Misc/AutomationTest.h"
#include "Misc/EnumClassFlags.h"
#include "Misc/StringBuilder.h"
#include "UObject/ErrorException.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectAllocator.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Package.h"
#include "UObject/MetaData.h"
#include "Templates/Casts.h"
#include "UObject/DebugSerializationFlags.h"
#include "UObject/PropertyTag.h"
#include "UObject/UnrealType.h"
#include "UObject/UnrealTypePrivate.h"
#include "UObject/Stack.h"
#include "Misc/PackageName.h"
#include "UObject/ObjectResource.h"
#include "UObject/LinkerSave.h"
#include "UObject/Interface.h"
#include "Misc/HotReloadInterface.h"
#include "UObject/LinkerPlaceholderClass.h"
#include "UObject/LinkerPlaceholderFunction.h"
#include "UObject/StructScriptLoader.h"
#include "UObject/PropertyHelper.h"
#include "UObject/CoreRedirects.h"
#include "Internationalization/PolyglotTextData.h"
#include "Serialization/ArchiveScriptReferenceCollector.h"
#include "Serialization/ArchiveUObjectFromStructuredArchive.h"
#include "UObject/FrameworkObjectVersion.h"
#include "UObject/GarbageCollection.h"
#include "UObject/UObjectThreadContext.h"
#include "Serialization/LoadTimeTracePrivate.h"
#include "Serialization/UnversionedPropertySerialization.h"
#include "Serialization/UnversionedPropertySerializationTest.h"
#include "UObject/CoreObjectVersion.h"
#include "UObject/FastReferenceCollector.h"
#include "UObject/PropertyProxyArchive.h"
#include "UObject/FieldPath.h"
#include "HAL/ThreadSafeCounter.h"


// WARNING: This should always be the last include in any file that needs it (except .generated.h)
#include "UObject/UndefineUPropertyMacros.h"

// This flag enables some expensive class tree validation that is meant to catch mutations of 
// the class tree outside of SetSuperStruct. It has been disabled because loading blueprints 
// does a lot of mutation of the class tree, and the validation checks impact iteration time.
#define DO_CLASS_TREE_VALIDATION 0

DEFINE_LOG_CATEGORY(LogScriptSerialization);
DEFINE_LOG_CATEGORY(LogClass);

IMPLEMENT_STRUCT(TestUninitializedScriptStructMembersTest);

#if defined(_MSC_VER) && _MSC_VER == 1900
	#ifdef PRAGMA_DISABLE_SHADOW_VARIABLE_WARNINGS
		PRAGMA_DISABLE_SHADOW_VARIABLE_WARNINGS
	#endif
#endif

// If we end up pushing class flags out beyond a uint32, there are various places
// casting it to uint32 that need to be fixed up (mostly printfs but also some serialization code)
static_assert(sizeof(__underlying_type(EClassFlags)) == sizeof(uint32), "expecting ClassFlags enum to fit in a uint32");

//////////////////////////////////////////////////////////////////////////

FThreadSafeBool& InternalSafeGetTokenStreamDirtyFlag()
{
	static FThreadSafeBool TokenStreamDirty(true);
	return TokenStreamDirty;
}

/**
 * Shared function called from the various InitializePrivateStaticClass functions generated my the IMPLEMENT_CLASS macro.
 */
COREUOBJECT_API void InitializePrivateStaticClass(
	class UClass* TClass_Super_StaticClass,
	class UClass* TClass_PrivateStaticClass,
	class UClass* TClass_WithinClass_StaticClass,
	const TCHAR* PackageName,
	const TCHAR* Name
	)
{
	TRACE_LOADTIME_CLASS_INFO(TClass_PrivateStaticClass, Name);
	NotifyRegistrationEvent(PackageName, Name, ENotifyRegistrationType::NRT_Class, ENotifyRegistrationPhase::NRP_Started);

	/* No recursive ::StaticClass calls allowed. Setup extras. */
	if (TClass_Super_StaticClass != TClass_PrivateStaticClass)
	{
		TClass_PrivateStaticClass->SetSuperStruct(TClass_Super_StaticClass);
	}
	else
	{
		TClass_PrivateStaticClass->SetSuperStruct(NULL);
	}
	TClass_PrivateStaticClass->ClassWithin = TClass_WithinClass_StaticClass;

	// Register the class's dependencies, then itself.
	TClass_PrivateStaticClass->RegisterDependencies();
	if (!TClass_PrivateStaticClass->HasAnyFlags(RF_Dynamic))
	{
		// Defer
		TClass_PrivateStaticClass->Register(PackageName, Name);
	}
	else
	{
		// Register immediately (don't let the function name mistake you!)
		TClass_PrivateStaticClass->DeferredRegister(UDynamicClass::StaticClass(), PackageName, Name);
	}
	NotifyRegistrationEvent(PackageName, Name, ENotifyRegistrationType::NRT_Class, ENotifyRegistrationPhase::NRP_Finished);
}

void FNativeFunctionRegistrar::RegisterFunction(class UClass* Class, const ANSICHAR* InName, FNativeFuncPtr InPointer)
{
	Class->AddNativeFunction(InName, InPointer);
}

void FNativeFunctionRegistrar::RegisterFunction(class UClass* Class, const WIDECHAR* InName, FNativeFuncPtr InPointer)
{
	Class->AddNativeFunction(InName, InPointer);
}

void FNativeFunctionRegistrar::RegisterFunctions(class UClass* Class, const FNameNativePtrPair* InArray, int32 NumFunctions)
{
	for (; NumFunctions; ++InArray, --NumFunctions)
	{
		Class->AddNativeFunction(UTF8_TO_TCHAR(InArray->NameUTF8), InArray->Pointer);
	}
}

/*-----------------------------------------------------------------------------
	UField implementation.
-----------------------------------------------------------------------------*/

UField::UField( EStaticConstructor, EObjectFlags InFlags )
: UObject( EC_StaticConstructor, InFlags )
, Next( NULL )
{}

UClass* UField::GetOwnerClass() const
{
	UClass* OwnerClass = NULL;
	UObject* TestObject = const_cast<UField*>(this);

	while ((TestObject != NULL) && (OwnerClass == NULL))
	{
		OwnerClass = dynamic_cast<UClass*>(TestObject);
		TestObject = TestObject->GetOuter();
	}

	return OwnerClass;
}

UStruct* UField::GetOwnerStruct() const
{
	const UObject* Obj = this;
	do
	{
		if (const UStruct* Result = dynamic_cast<const UStruct*>(Obj))
		{
			return const_cast<UStruct*>(Result);
		}

		Obj = Obj->GetOuter();
	}
	while (Obj);

	return nullptr;
}

FString UField::GetAuthoredName() const
{
	UStruct* Struct = GetOwnerStruct();
	if (Struct)
	{
		return Struct->GetAuthoredNameForField(this);
	}
	return FString();
}

void UField::Bind()
{
}

void UField::PostLoad()
{
	Super::PostLoad();
	Bind();
}

bool UField::NeedsLoadForClient() const
{
	// Overridden to avoid calling the expensive generic version, which only ensures that our class is not excluded, which it never can be
	return true;
}

bool UField::NeedsLoadForServer() const
{
	return true;
}

void UField::Serialize( FArchive& Ar )
{
	Super::Serialize( Ar );
	Ar.UsingCustomVersion(FFrameworkObjectVersion::GUID);
	if (Ar.CustomVer(FFrameworkObjectVersion::GUID) < FFrameworkObjectVersion::RemoveUField_Next)
	{
		Ar << Next;
	}
}

void UField::AddCppProperty(FProperty* Property)
{
	UE_LOG(LogClass, Fatal,TEXT("UField::AddCppProperty"));
}

#if WITH_EDITORONLY_DATA

struct FDisplayNameHelper
{
	static FString Get(const UObject& Object)
	{
		const UClass* Class = Cast<const UClass>(&Object);
		if (Class && !Class->HasAnyClassFlags(CLASS_Native))
		{
			FString Name = Object.GetName();
			Name.RemoveFromEnd(TEXT("_C"));
			Name.RemoveFromStart(TEXT("SKEL_"));
			return Name;
		}

		//if (auto Property = dynamic_cast<const FProperty*>(&Object))
		//{
		//	return Property->GetAuthoredName();
		//}

		return Object.GetName();
	}
};

/**
 * Finds the localized display name or native display name as a fallback.
 *
 * @return The display name for this object.
 */
FText UField::GetDisplayNameText() const
{
	FText LocalizedDisplayName;

	static const FString Namespace = TEXT("UObjectDisplayNames");
	static const FName NAME_DisplayName(TEXT("DisplayName"));

	const FString Key = GetFullGroupName(false);

	FString NativeDisplayName = GetMetaData(NAME_DisplayName);
	if (NativeDisplayName.IsEmpty())
	{
		NativeDisplayName = FName::NameToDisplayString(FDisplayNameHelper::Get(*this), false);
	}

	if ( !( FText::FindText( Namespace, Key, /*OUT*/LocalizedDisplayName, &NativeDisplayName ) ) )
	{
		LocalizedDisplayName = FText::FromString(NativeDisplayName );
	}

	return LocalizedDisplayName;
}

/**
 * Finds the localized tooltip or native tooltip as a fallback.
 *
 * @return The tooltip for this object.
 */
FText UField::GetToolTipText(bool bShortTooltip) const
{
	bool bFoundShortTooltip = false;
	static const FName NAME_Tooltip(TEXT("Tooltip"));
	static const FName NAME_ShortTooltip(TEXT("ShortTooltip"));
	FText LocalizedToolTip;
	FString NativeToolTip;
	
	if (bShortTooltip)
	{
		NativeToolTip = GetMetaData(NAME_ShortTooltip);
		if (NativeToolTip.IsEmpty())
		{
			NativeToolTip = GetMetaData(NAME_Tooltip);
		}
		else
		{
			bFoundShortTooltip = true;
		}
	}
	else
	{
		NativeToolTip = GetMetaData(NAME_Tooltip);
	}

	const FString Namespace = bFoundShortTooltip ? TEXT("UObjectShortTooltips") : TEXT("UObjectToolTips");
	const FString Key = GetFullGroupName(false);
	if ( !FText::FindText( Namespace, Key, /*OUT*/LocalizedToolTip, &NativeToolTip ) )
	{
		if (NativeToolTip.IsEmpty())
		{
			NativeToolTip = FName::NameToDisplayString(FDisplayNameHelper::Get(*this), false);
		}
		else if (!bShortTooltip && IsNative())
		{
			FormatNativeToolTip(NativeToolTip, true);
		}
		LocalizedToolTip = FText::FromString(NativeToolTip);
	}

	return LocalizedToolTip;
}

void UField::FormatNativeToolTip(FString& ToolTipString, bool bRemoveExtraSections)
{
	// First do doxygen replace
	static const FString DoxygenSee(TEXT("@see"));
	static const FString TooltipSee(TEXT("See:"));
	ToolTipString.ReplaceInline(*DoxygenSee, *TooltipSee);

	bool bCurrentLineIsEmpty = true;
	int32 EmptyLineCount = 0;
	int32 LastContentIndex = INDEX_NONE;
	const int32 ToolTipLength = ToolTipString.Len();
		
	// Start looking for empty lines and whitespace to strip
	for (int32 StrIndex = 0; StrIndex < ToolTipLength; StrIndex++)
	{
		TCHAR CurrentChar = ToolTipString[StrIndex];

		if (!FChar::IsWhitespace(CurrentChar))
		{
			if (FChar::IsPunct(CurrentChar))
			{
				// Punctuation is considered content if it's on a line with alphanumeric text
				if (!bCurrentLineIsEmpty)
				{
					LastContentIndex = StrIndex;
				}
			}
			else
			{
				// This is something alphanumeric, this is always content and mark line as not empty
				bCurrentLineIsEmpty = false;
				LastContentIndex = StrIndex;
			}
		}
		else if (CurrentChar == TEXT('\n'))
		{
			if (bCurrentLineIsEmpty)
			{
				EmptyLineCount++;
				if (bRemoveExtraSections && EmptyLineCount >= 2)
				{
					// If we get two empty or punctuation/separator lines in a row, cut off the string if requested
					break;
				}
			}
			else
			{
				EmptyLineCount = 0;
			}

			bCurrentLineIsEmpty = true;
		}
	}

	// Trim string to last content character, this strips trailing whitespace as well as extra sections if needed
	if (LastContentIndex >= 0 && LastContentIndex != ToolTipLength - 1)
	{
		ToolTipString.RemoveAt(LastContentIndex + 1, ToolTipLength - (LastContentIndex + 1));
	}
}

/**
 * Determines if the property has any metadata associated with the key
 * 
 * @param Key The key to lookup in the metadata
 * @return true if there is a (possibly blank) value associated with this key
 */
const FString* UField::FindMetaData(const TCHAR* Key) const
{
	UPackage* Package = GetOutermost();
	check(Package);

	UMetaData* MetaData = Package->GetMetaData();
	check(MetaData);

	return MetaData->FindValue(this, Key);
}

const FString* UField::FindMetaData(const FName& Key) const
{
	UPackage* Package = GetOutermost();
	check(Package);

	UMetaData* MetaData = Package->GetMetaData();
	check(MetaData);

	return MetaData->FindValue(this, Key);
}

/**
 * Find the metadata value associated with the key
 * 
 * @param Key The key to lookup in the metadata
 * @return The value associated with the key
*/
const FString& UField::GetMetaData(const TCHAR* Key) const
{
	UPackage* Package = GetOutermost();
	check(Package);

	UMetaData* MetaData = Package->GetMetaData();
	check(MetaData);

	const FString& MetaDataString = MetaData->GetValue(this, Key);
		
	return MetaDataString;
}

const FString& UField::GetMetaData(const FName& Key) const
{
	UPackage* Package = GetOutermost();
	check(Package);

	UMetaData* MetaData = Package->GetMetaData();
	check(MetaData);

	const FString& MetaDataString = MetaData->GetValue(this, Key);

	return MetaDataString;
}

FText UField::GetMetaDataText(const TCHAR* MetaDataKey, const FString LocalizationNamespace, const FString LocalizationKey) const
{
	FString DefaultMetaData;

	if(const FString* FoundMetaData = FindMetaData( MetaDataKey ))
	{
		DefaultMetaData = *FoundMetaData;
	}

	// If attempting to grab the DisplayName metadata, we must correct the source string and output it as a DisplayString for lookup
	if( DefaultMetaData.IsEmpty() && FCString::Stricmp(MetaDataKey, TEXT("DisplayName")) == 0 )
	{
		DefaultMetaData = FName::NameToDisplayString(GetName(), false);
	}

	FText LocalizedMetaData;
	if ( !( FText::FindText( LocalizationNamespace, LocalizationKey, /*OUT*/LocalizedMetaData, &DefaultMetaData ) ) )
	{
		if (!DefaultMetaData.IsEmpty())
		{
			LocalizedMetaData = FText::AsCultureInvariant(DefaultMetaData);
		}
	}

	return LocalizedMetaData;
}

FText UField::GetMetaDataText(const FName& MetaDataKey, const FString LocalizationNamespace, const FString LocalizationKey) const
{
	FString DefaultMetaData;

	if (const FString* FoundMetaData = FindMetaData( MetaDataKey ))
	{
		DefaultMetaData = *FoundMetaData;
	}

	// If attempting to grab the DisplayName metadata, we must correct the source string and output it as a DisplayString for lookup
	if( DefaultMetaData.IsEmpty() && MetaDataKey == TEXT("DisplayName") )
	{
		DefaultMetaData = FName::NameToDisplayString(GetName(), false);
	}
	

	FText LocalizedMetaData;
	if ( !( FText::FindText( LocalizationNamespace, LocalizationKey, /*OUT*/LocalizedMetaData, &DefaultMetaData ) ) )
	{
		if (!DefaultMetaData.IsEmpty())
		{
			LocalizedMetaData = FText::AsCultureInvariant(DefaultMetaData);
		}
	}

	return LocalizedMetaData;
}

/**
 * Sets the metadata value associated with the key
 * 
 * @param Key The key to lookup in the metadata
 * @return The value associated with the key
 */
void UField::SetMetaData(const TCHAR* Key, const TCHAR* InValue)
{
	UPackage* Package = GetOutermost();
	check(Package);

	Package->GetMetaData()->SetValue(this, Key, InValue);
}

void UField::SetMetaData(const FName& Key, const TCHAR* InValue)
{
	UPackage* Package = GetOutermost();
	check(Package);

	Package->GetMetaData()->SetValue(this, Key, InValue);
}

UClass* UField::GetClassMetaData(const TCHAR* Key) const
{
	const FString& ClassName = GetMetaData(Key);
	UClass* const FoundObject = FindObject<UClass>(ANY_PACKAGE, *ClassName);
	return FoundObject;
}

UClass* UField::GetClassMetaData(const FName& Key) const
{
	const FString& ClassName = GetMetaData(Key);
	UClass* const FoundObject = FindObject<UClass>(ANY_PACKAGE, *ClassName);
	return FoundObject;
}

void UField::RemoveMetaData(const TCHAR* Key)
{
	UPackage* Package = GetOutermost();
	check(Package);
	return Package->GetMetaData()->RemoveValue(this, Key);
}

void UField::RemoveMetaData(const FName& Key)
{
	UPackage* Package = GetOutermost();
	check(Package);
	return Package->GetMetaData()->RemoveValue(this, Key);
}

#endif // WITH_EDITORONLY_DATA

bool UField::HasAnyCastFlags(const uint64 InCastFlags) const
{
	return !!(GetClass()->ClassCastFlags & InCastFlags);
}

bool UField::HasAllCastFlags(const uint64 InCastFlags) const
{
	return (GetClass()->ClassCastFlags & InCastFlags) == InCastFlags;
}

#if WITH_EDITORONLY_DATA
FField* UField::GetAssociatedFField()
{
	return nullptr;
}

void UField::SetAssociatedFField(FField* InField)
{
	check(false); // unsupported for this type
}
#endif // WITH_EDITORONLY_DATA

IMPLEMENT_CORE_INTRINSIC_CLASS(UField, UObject,
	{
		Class->EmitObjectReference(STRUCT_OFFSET(UField, Next), TEXT("Next"));
	}
);



/*-----------------------------------------------------------------------------
	UStruct implementation.
-----------------------------------------------------------------------------*/

/** Simple reference processor and collector for collecting all UObjects referenced by FProperties */
class FPropertyReferenceCollector : public FReferenceCollector
{
	/** The owner object for properties we collect references for */
	UObject* Owner;
public:
	FPropertyReferenceCollector(UObject* InOwner)
		: Owner(InOwner)
	{
	}

	TSet<UObject*> UniqueReferences;

	virtual bool IsIgnoringArchetypeRef() const override { return false; }
	virtual bool IsIgnoringTransient() const override { return false;  }
	virtual void HandleObjectReference(UObject*& InObject, const UObject* InReferencingObject, const FProperty* InReferencingProperty) override
	{
		// Skip nulls and the owner object
		if (InObject && InObject != Owner)
		{
			// Don't collect objects that will never be GC'd anyway
			if (!InObject->HasAnyInternalFlags(EInternalObjectFlags::Native) && !GUObjectArray.IsDisregardForGC(InObject))
			{
				UniqueReferences.Add(InObject);
			}
		}
	}
};

#if WITH_EDITORONLY_DATA
static int32 GetNextFieldPathSerialNumber()
{
	static FThreadSafeCounter GlobalSerialNumberCounter;
	return GlobalSerialNumberCounter.Increment();
}
#endif // WITH_EDITORONLY_DATA

//
// Constructors.
//
UStruct::UStruct(EStaticConstructor, int32 InSize, int32 InMinAlignment, EObjectFlags InFlags)
	: UField(EC_StaticConstructor, InFlags)
	, SuperStruct(nullptr)
	, Children(nullptr)
	, ChildProperties(nullptr)
	, PropertiesSize(InSize)
	, MinAlignment(InMinAlignment)
	, PropertyLink(nullptr)
	, RefLink(nullptr)
	, DestructorLink(nullptr)
	, PostConstructLink(nullptr)
	, UnresolvedScriptProperties(nullptr)
{
#if WITH_EDITORONLY_DATA
	FieldPathSerialNumber = GetNextFieldPathSerialNumber();
#endif // WITH_EDITORONLY_DATA
}

UStruct::UStruct(UStruct* InSuperStruct, SIZE_T ParamsSize, SIZE_T Alignment)
	: UField(FObjectInitializer::Get())
	, SuperStruct(InSuperStruct)
	, Children(nullptr)
	, ChildProperties(nullptr)
	, PropertiesSize(ParamsSize ? ParamsSize : (InSuperStruct ? InSuperStruct->GetPropertiesSize() : 0))
	, MinAlignment(Alignment ? Alignment : (FMath::Max(InSuperStruct ? InSuperStruct->GetMinAlignment() : 1, 1)))
	, PropertyLink(nullptr)
	, RefLink(nullptr)
	, DestructorLink(nullptr)
	, PostConstructLink(nullptr)
	, UnresolvedScriptProperties(nullptr)
{
#if USTRUCT_FAST_ISCHILDOF_IMPL == USTRUCT_ISCHILDOF_STRUCTARRAY
	this->ReinitializeBaseChainArray();
#endif
#if WITH_EDITORONLY_DATA
	FieldPathSerialNumber = GetNextFieldPathSerialNumber();
#endif // WITH_EDITORONLY_DATA
}

UStruct::UStruct(const FObjectInitializer& ObjectInitializer, UStruct* InSuperStruct, SIZE_T ParamsSize, SIZE_T Alignment)
	: UField(ObjectInitializer)
	, SuperStruct(InSuperStruct)
	, Children(nullptr)
	, ChildProperties(nullptr)
	, PropertiesSize(ParamsSize ? ParamsSize : (InSuperStruct ? InSuperStruct->GetPropertiesSize() : 0))
	, MinAlignment(Alignment ? Alignment : (FMath::Max(InSuperStruct ? InSuperStruct->GetMinAlignment() : 1, 1)))
	, PropertyLink(nullptr)
	, RefLink(nullptr)
	, DestructorLink(nullptr)
	, PostConstructLink(nullptr)
	, UnresolvedScriptProperties(nullptr)
{
#if USTRUCT_FAST_ISCHILDOF_IMPL == USTRUCT_ISCHILDOF_STRUCTARRAY
	this->ReinitializeBaseChainArray();
#endif
#if WITH_EDITORONLY_DATA
	FieldPathSerialNumber = GetNextFieldPathSerialNumber();
#endif // WITH_EDITORONLY_DATA
}

/**
 * Force any base classes to be registered first, then call BaseRegister
 */
void UStruct::RegisterDependencies()
{
	Super::RegisterDependencies();
	if (SuperStruct != NULL)
	{
		SuperStruct->RegisterDependencies();
	}
}

void UStruct::AddCppProperty(FProperty* Property)
{
	Property->Next = ChildProperties;
	ChildProperties = Property;
}

void UStruct::StaticLink(bool bRelinkExistingProperties)
{
	FArchive ArDummy;
	Link(ArDummy, bRelinkExistingProperties);
}

void UStruct::GetPreloadDependencies(TArray<UObject*>& OutDeps)
{
	Super::GetPreloadDependencies(OutDeps);
	OutDeps.Add(SuperStruct);

	for (UField* Field = Children; Field; Field = Field->Next)
	{
		if (!Cast<UFunction>(Field))
		{
			OutDeps.Add(Field);
		}
	}

	for (FField* Field = ChildProperties; Field; Field = Field->Next)
	{
		Field->GetPreloadDependencies(OutDeps);
	}
}

void UStruct::CollectBytecodeReferencedObjects(TArray<UObject*>& OutReferencedObjects)
{
	FArchiveScriptReferenceCollector ObjRefCollector(OutReferencedObjects);

	int32 BytecodeIndex = 0;
	while (BytecodeIndex < Script.Num())
	{
		SerializeExpr(BytecodeIndex, ObjRefCollector);
	}
}

void UStruct::CollectPropertyReferencedObjects(TArray<UObject*>& OutReferencedObjects)
{
	FPropertyReferenceCollector PropertyReferenceCollector(this);
	for (FField* CurrentField = ChildProperties; CurrentField; CurrentField = CurrentField->Next)
	{
		CurrentField->AddReferencedObjects(PropertyReferenceCollector);
	}
	OutReferencedObjects.Append(PropertyReferenceCollector.UniqueReferences.Array());
}

void UStruct::CollectBytecodeAndPropertyReferencedObjects()
{
	ScriptAndPropertyObjectReferences.Empty();
	CollectBytecodeReferencedObjects(ScriptAndPropertyObjectReferences);
	CollectPropertyReferencedObjects(ScriptAndPropertyObjectReferences);
}

void UStruct::Link(FArchive& Ar, bool bRelinkExistingProperties)
{
	if (bRelinkExistingProperties)
	{
		// Preload everything before we calculate size, as the preload may end up recursively linking things
		UStruct* InheritanceSuper = GetInheritanceSuper();
		if (Ar.IsLoading())
		{
			if (InheritanceSuper)
			{
				Ar.Preload(InheritanceSuper);
			}

			for (UField* Field = Children; Field; Field = Field->Next)
			{
				if (!GEventDrivenLoaderEnabled || !Cast<UFunction>(Field))
				{
					Ar.Preload(Field);
				}
			}

#if WITH_EDITORONLY_DATA
			ConvertUFieldsToFFields();
#endif // WITH_EDITORONLY_DATA
		}

		int32 LoopNum = 1;
		for (int32 LoopIter = 0; LoopIter < LoopNum; LoopIter++)
		{
			PropertiesSize = 0;
			MinAlignment = 1;

			if (InheritanceSuper)
			{
				PropertiesSize = InheritanceSuper->GetPropertiesSize();
				MinAlignment = InheritanceSuper->GetMinAlignment();
			}

			for (FField* Field = ChildProperties; Field; Field = Field->Next)
			{
				if (Field->GetOwner<UObject>() != this)
				{
					break;
				}

				if (FProperty* Property = CastField<FProperty>(Field))
				{
#if !WITH_EDITORONLY_DATA
					// If we don't have the editor, make sure we aren't trying to link properties that are editor only.
					check(!Property->IsEditorOnlyProperty());
#endif // WITH_EDITORONLY_DATA
					ensureMsgf(Property->GetOwner<UObject>() == this, TEXT("Linking '%s'. Property '%s' has outer '%s'"),
						*GetFullName(), *Property->GetName(), *Property->GetOwnerVariant().GetFullName());

					// Linking a property can cause a recompilation of the struct. 
					// When the property was changed, the struct should be relinked again, to be sure, the PropertiesSize is actual.
					const bool bPropertyIsTransient = Property->HasAllFlags(RF_Transient);
					const FName PropertyName = Property->GetFName();

					PropertiesSize = Property->Link(Ar);

					if ((bPropertyIsTransient != Property->HasAllFlags(RF_Transient)) || (PropertyName != Property->GetFName()))
					{
						LoopNum++;
						const int32 MaxLoopLimit = 64;
						ensure(LoopNum < MaxLoopLimit);
						break;
					}

					MinAlignment = FMath::Max(MinAlignment, Property->GetMinAlignment());
				}
			}
		}

		bool bHandledWithCppStructOps = false;
		if (GetClass()->IsChildOf(UScriptStruct::StaticClass()))
		{
			// check for internal struct recursion via arrays
			for (FField* Field = ChildProperties; Field; Field = Field->Next)
			{
				FArrayProperty* ArrayProp = CastField<FArrayProperty>(Field);
				if (ArrayProp != NULL)
				{
					FStructProperty* StructProp = CastField<FStructProperty>(ArrayProp->Inner);
					if (StructProp != NULL && StructProp->Struct == this)
					{
						//we won't support this, too complicated
					#if HACK_HEADER_GENERATOR
						FError::Throwf(TEXT("'Struct recursion via arrays is unsupported for properties."));
					#else
						UE_LOG(LogClass, Fatal, TEXT("'Struct recursion via arrays is unsupported for properties."));
					#endif
					}
				}
			}

			UScriptStruct& ScriptStruct = dynamic_cast<UScriptStruct&>(*this);
			ScriptStruct.PrepareCppStructOps();

			if (UScriptStruct::ICppStructOps* CppStructOps = ScriptStruct.GetCppStructOps())
			{
				MinAlignment = CppStructOps->GetAlignment();
				PropertiesSize = CppStructOps->GetSize();
				bHandledWithCppStructOps = true;
			}
		}
	}
	else
	{
		for (FField* Field = ChildProperties; (Field != NULL) && (Field->GetOwner<UObject>() == this); Field = Field->Next)
		{
			if (FProperty* Property = CastField<FProperty>(Field))
			{
				Property->LinkWithoutChangingOffset(Ar);
			}
		}
	}

	if (GetOutermost()->GetFName() == GLongCoreUObjectPackageName)
	{
		FName ToTest = GetFName();
		if ( ToTest == NAME_Matrix )
		{
			check(MinAlignment == alignof(FMatrix));
			check(PropertiesSize == sizeof(FMatrix));
		}
		else if ( ToTest == NAME_Plane )
		{
			check(MinAlignment == alignof(FPlane));
			check(PropertiesSize == sizeof(FPlane));
		}
		else if ( ToTest == NAME_Vector4 )
		{
			check(MinAlignment == alignof(FVector4));
			check(PropertiesSize == sizeof(FVector4));
		}
		else if ( ToTest == NAME_Quat )
		{
			check(MinAlignment == alignof(FQuat));
			check(PropertiesSize == sizeof(FQuat));
		}
		else if ( ToTest == NAME_Double )
		{
			check(MinAlignment == alignof(double));
			check(PropertiesSize == sizeof(double));
		}
		else if ( ToTest == NAME_Color )
		{
			check(MinAlignment == alignof(FColor));
			check(PropertiesSize == sizeof(FColor));
#if !PLATFORM_LITTLE_ENDIAN
			// Object.h declares FColor as BGRA which doesn't match up with what we'd like to use on
			// Xenon to match up directly with the D3D representation of D3DCOLOR. We manually fiddle 
			// with the property offsets to get everything to line up.
			// In any case, on big-endian systems we want to byte-swap this.
			//@todo cooking: this should be moved into the data cooking step.
			{
				FProperty*	ColorComponentEntries[4];
				uint32		ColorComponentIndex = 0;

				for( UField* Field=Children; Field && Field->GetOuter()==this; Field=Field->Next )
				{
					FProperty* Property = CastFieldChecked<FProperty>( Field );
					ColorComponentEntries[ColorComponentIndex++] = Property;
				}
				check( ColorComponentIndex == 4 );

				Exchange( ColorComponentEntries[0]->Offset, ColorComponentEntries[3]->Offset );
				Exchange( ColorComponentEntries[1]->Offset, ColorComponentEntries[2]->Offset );
			}
#endif

		}
	}


	// Link the references, structs, and arrays for optimized cleanup.
	// Note: Could optimize further by adding FProperty::NeedsDynamicRefCleanup, excluding things like arrays of ints.
	FProperty** PropertyLinkPtr = &PropertyLink;
	FProperty** DestructorLinkPtr = &DestructorLink;
	FProperty** RefLinkPtr = (FProperty**)&RefLink;
	FProperty** PostConstructLinkPtr = &PostConstructLink;

	TArray<const FStructProperty*> EncounteredStructProps;
	for (TFieldIterator<FProperty> It(this); It; ++It)
	{
		FProperty* Property = *It;

		if (Property->ContainsObjectReference(EncounteredStructProps) || Property->ContainsWeakObjectReference())
		{
			*RefLinkPtr = Property;
			RefLinkPtr = &(*RefLinkPtr)->NextRef;
		}

		const UClass* OwnerClass = Property->GetOwnerClass();
		bool bOwnedByNativeClass = OwnerClass && OwnerClass->HasAnyClassFlags(CLASS_Native | CLASS_Intrinsic);

		if (!Property->HasAnyPropertyFlags(CPF_IsPlainOldData | CPF_NoDestructor) &&
			!bOwnedByNativeClass) // these would be covered by the native destructor
		{	
			// things in a struct that need a destructor will still be in here, even though in many cases they will also be destroyed by a native destructor on the whole struct
			*DestructorLinkPtr = Property;
			DestructorLinkPtr = &(*DestructorLinkPtr)->DestructorLinkNext;
		}

		// Link references to properties that require their values to be initialized and/or copied from CDO post-construction. Note that this includes all non-native-class-owned properties.
		if (OwnerClass && (!bOwnedByNativeClass || (Property->HasAnyPropertyFlags(CPF_Config) && !OwnerClass->HasAnyClassFlags(CLASS_PerObjectConfig))))
		{
			*PostConstructLinkPtr = Property;
			PostConstructLinkPtr = &(*PostConstructLinkPtr)->PostConstructLinkNext;
		}

		*PropertyLinkPtr = Property;
		PropertyLinkPtr = &(*PropertyLinkPtr)->PropertyLinkNext;
	}

	*PropertyLinkPtr = nullptr;
	*DestructorLinkPtr = nullptr;
	*RefLinkPtr = nullptr;
	*PostConstructLinkPtr = nullptr;

	{
		// Now collect all references from FProperties to UObjects and store them in GC-exposed array for fast access
		CollectPropertyReferencedObjects(ScriptAndPropertyObjectReferences);

#if USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING
		// The old (non-EDL) FLinkerLoad code paths create placeholder objects
		// for classes and functions. We have to babysit these, just as we do
		// for bytecode references (reusing the AddReferencingScriptExpr fn).
		// Long term we should not use placeholder objects like this:
		for(int32 ReferenceIndex = ScriptAndPropertyObjectReferences.Num() - 1; ReferenceIndex >= 0; --ReferenceIndex)
		{
			if (ScriptAndPropertyObjectReferences[ReferenceIndex])
			{
				if (ULinkerPlaceholderClass* PlaceholderObj = Cast<ULinkerPlaceholderClass>(ScriptAndPropertyObjectReferences[ReferenceIndex]))
				{
					// let the placeholder track the reference to it:
					PlaceholderObj->AddReferencingScriptExpr(reinterpret_cast<UClass**>(&ScriptAndPropertyObjectReferences[ReferenceIndex]));
				}
				// I don't currently see how placeholder functions could be present in this list, but that's
				// a dangerous assumption.
				ensure(!(ScriptAndPropertyObjectReferences[ReferenceIndex]->IsA<ULinkerPlaceholderFunction>()));
			}
			else
			{
				// It's possible that in the process of recompilation one of the refernces got GC'd leaving a null ptr in the array
				ScriptAndPropertyObjectReferences.RemoveAt(ReferenceIndex);
			}
		}
#endif // USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING
	}

#if WITH_EDITORONLY_DATA
	// Discard old wrapper objects used by property grids
	for (UPropertyWrapper* Wrapper : PropertyWrappers)
	{
		Wrapper->Rename(nullptr, GetTransientPackage(), REN_ForceNoResetLoaders | REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
		Wrapper->RemoveFromRoot();
	}
	PropertyWrappers.Empty();
#endif
}

void UStruct::InitializeStruct(void* InDest, int32 ArrayDim/* = 1*/) const
{
	uint8 *Dest = (uint8*)InDest;
	check(Dest);

	int32 Stride = GetStructureSize();

	//@todo UE4 optimize
	FMemory::Memzero(Dest, 1 * Stride);

	for (FProperty* Property = PropertyLink; Property; Property = Property->PropertyLinkNext)
	{
		if (ensure(Property->IsInContainer(Stride)))
		{
			for (int32 ArrayIndex = 0; ArrayIndex < ArrayDim; ArrayIndex++)
			{
				Property->InitializeValue_InContainer(Dest + ArrayIndex * Stride);
			}
		}
		else
		{
			break;
		}
	}
}

void UStruct::DestroyStruct(void* Dest, int32 ArrayDim) const
{
	uint8 *Data = (uint8*)Dest;
	int32 Stride = GetStructureSize();

	bool bHitBase = false;
	for (FProperty* P = DestructorLink; P  && !bHitBase; P = P->DestructorLinkNext)
	{
		if (!P->HasAnyPropertyFlags(CPF_NoDestructor))
		{
			if (P->IsInContainer(Stride))
			{
				for ( int32 ArrayIndex = 0; ArrayIndex < ArrayDim; ArrayIndex++ )
				{
					P->DestroyValue_InContainer(Data + ArrayIndex * Stride);
				}
			}
		}
		else
		{
			bHitBase = true;
		}
	}
}

//
// Serialize all of the class's data that belongs in a particular
// bin and resides in Data.
//
void UStruct::SerializeBin( FStructuredArchive::FSlot Slot, void* Data ) const
{
	FArchive& UnderlyingArchive = Slot.GetUnderlyingArchive();

	FStructuredArchive::FStream PropertyStream = Slot.EnterStream();

	if( UnderlyingArchive.IsObjectReferenceCollector() )
	{
		for( FProperty* RefLinkProperty=RefLink; RefLinkProperty!=NULL; RefLinkProperty=RefLinkProperty->NextRef )
		{
			RefLinkProperty->SerializeBinProperty(PropertyStream.EnterElement(), Data );
		}
	}
	else if( UnderlyingArchive.ArUseCustomPropertyList )
	{
		const FCustomPropertyListNode* CustomPropertyList = UnderlyingArchive.ArCustomPropertyList;
		for (auto PropertyNode = CustomPropertyList; PropertyNode; PropertyNode = PropertyNode->PropertyListNext)
		{
			FProperty* Property = PropertyNode->Property;
			if( Property )
			{
				// Temporarily set to the sub property list, in case we're serializing a UStruct property.
				UnderlyingArchive.ArCustomPropertyList = PropertyNode->SubPropertyList;

				Property->SerializeBinProperty(PropertyStream.EnterElement(), Data, PropertyNode->ArrayIndex);

				// Restore the original property list.
				UnderlyingArchive.ArCustomPropertyList = CustomPropertyList;
			}
		}
	}
	else
	{
		for (FProperty* Property = PropertyLink; Property != NULL; Property = Property->PropertyLinkNext)
		{
			Property->SerializeBinProperty(PropertyStream.EnterElement(), Data);
		}
	}
}

void UStruct::SerializeBinEx( FStructuredArchive::FSlot Slot, void* Data, void const* DefaultData, UStruct* DefaultStruct ) const
{
	if ( !DefaultData || !DefaultStruct )
	{
		SerializeBin(Slot, Data);
		return;
	}

	for( TFieldIterator<FProperty> It(this); It; ++It )
	{
		It->SerializeNonMatchingBinProperty(Slot, Data, DefaultData, DefaultStruct);
	}
}

void UStruct::LoadTaggedPropertiesFromText(FStructuredArchive::FSlot Slot, uint8* Data, UStruct* DefaultsStruct, uint8* Defaults, const UObject* BreakRecursionIfFullyLoad) const
{
	FArchive& UnderlyingArchive = Slot.GetUnderlyingArchive();
	const bool bUseRedirects = !FPlatformProperties::RequiresCookedData() || UnderlyingArchive.IsSaveGame();
	int32 NumProperties = 0;
	FStructuredArchiveMap PropertiesMap = Slot.EnterMap(NumProperties);

	for (int32 PropertyIndex = 0; PropertyIndex < NumProperties; ++PropertyIndex)
	{
		FString PropertyNameString;
		FStructuredArchiveSlot PropertySlot = PropertiesMap.EnterElement(PropertyNameString);
		FName PropertyName = *PropertyNameString;

		// If this property has a guid attached then we need to resolve it to the right name before we start loading
		TOptional<FStructuredArchiveSlot> PropertyGuidSlot = PropertySlot.TryEnterAttribute(SA_FIELD_NAME(TEXT("PropertyGuid")), false);
		if (PropertyGuidSlot.IsSet())
		{
			FGuid PropertyGuid;
			PropertyGuidSlot.GetValue() << PropertyGuid;
			if (PropertyGuid.IsValid())
			{
				FName NewName = FindPropertyNameFromGuid(PropertyGuid);
				if (NewName != NAME_None)
				{
					PropertyName = NewName;
				}
			}
		}

		// Resolve any redirects if necessary
		if (bUseRedirects && !UnderlyingArchive.HasAnyPortFlags(PPF_DuplicateForPIE | PPF_Duplicate))
		{
			for (UStruct* CheckStruct = GetOwnerStruct(); CheckStruct; CheckStruct = CheckStruct->GetSuperStruct())
			{
				FName NewTagName = FProperty::FindRedirectedPropertyName(CheckStruct, PropertyName);
				if (!NewTagName.IsNone())
				{
					PropertyName = NewTagName;
					break;
				}
			}
		}

		// Now we know what the property name is, we can try and load it
		FProperty* Property = FindPropertyByName(PropertyName);

		if (Property == nullptr)
		{
			Property = CustomFindProperty(PropertyName);
		}

		if (Property && Property->ShouldSerializeValue(UnderlyingArchive))
		{
			FName PropID = Property->GetID();

			// Static arrays of tagged properties are special cases where the slot is always an array with no tag data attached. We currently have no TryEnterArray we can't 
			// react based on what is in the file (yet) so we'll just have to assume that nobody converts a property from an array to a single value and go with whatever 
			// the code property tells us.
			TOptional<FStructuredArchiveArray> SlotArray;
			int32 NumItems = Property->ArrayDim;
			if (Property->ArrayDim > 1)
			{
				int32 NumAvailableItems = 0;
				SlotArray.Emplace(PropertySlot.EnterArray(NumAvailableItems));
				NumItems = FMath::Min(Property->ArrayDim, NumAvailableItems);
			}

			for (int32 ItemIndex = 0; ItemIndex < NumItems; ++ItemIndex)
			{
				TOptional<FStructuredArchiveSlot> ItemSlot;
				if (SlotArray.IsSet())
				{
					ItemSlot.Emplace(SlotArray->EnterElement());
				}
				else
				{
					ItemSlot.Emplace(PropertySlot);
				}

				FPropertyTag Tag;
				ItemSlot.GetValue() << Tag;
				Tag.ArrayIndex = ItemIndex;
				Tag.Name = PropertyName;

				if (bUseRedirects)
				{
					if (Tag.Type == NAME_StructProperty && PropID == NAME_StructProperty)
					{
						const FName NewName = FLinkerLoad::FindNewNameForStruct(Tag.StructName);
						const FName StructName = CastFieldChecked<FStructProperty>(Property)->Struct->GetFName();
						if (NewName == StructName)
						{
							Tag.StructName = NewName;
						}
					}
					else if ((PropID == NAME_EnumProperty) && ((Tag.Type == NAME_EnumProperty) || (Tag.Type == NAME_ByteProperty)))
					{
						const FName NewName = FLinkerLoad::FindNewNameForEnum(Tag.EnumName);
						if (!NewName.IsNone())
						{
							Tag.EnumName = NewName;
						}
					}

					if (!(BreakRecursionIfFullyLoad && BreakRecursionIfFullyLoad->HasAllFlags(RF_LoadCompleted)))
					{
						switch (Property->ConvertFromType(Tag, ItemSlot.GetValue(), Data, DefaultsStruct))
						{
						case EConvertFromTypeResult::Converted:
							break;

						case EConvertFromTypeResult::UseSerializeItem:
							if (Tag.Type != PropID)
							{
								UE_LOG(LogClass, Warning, TEXT("Type mismatch in %s of %s - Previous (%s) Current(%s) for package:  %s"), *Tag.Name.ToString(), *GetName(), *Tag.Type.ToString(), *PropID.ToString(), *UnderlyingArchive.GetArchiveName());
							}
							else
							{
								uint8* DestAddress = Property->ContainerPtrToValuePtr<uint8>(Data, Tag.ArrayIndex);
								uint8* DefaultsFromParent = Property->ContainerPtrToValuePtrForDefaults<uint8>(DefaultsStruct, Defaults, Tag.ArrayIndex);

								// This property is ok.
								Tag.SerializeTaggedProperty(ItemSlot.GetValue(), Property, DestAddress, DefaultsFromParent);
							}
							break;

						case EConvertFromTypeResult::CannotConvert:
							break;

						default:
							check(false);
						}
					}
				}
			}
		}
	}
}

void UStruct::SerializeTaggedProperties(FStructuredArchive::FSlot Slot, uint8* Data, UStruct* DefaultsStruct, uint8* Defaults, const UObject* BreakRecursionIfFullyLoad) const
{
	if (Slot.GetArchiveState().UseUnversionedPropertySerialization())
	{
		SerializeUnversionedProperties(this, Slot, Data, DefaultsStruct, Defaults);
	}
	else
	{
		SerializeVersionedTaggedProperties(Slot, Data, DefaultsStruct, Defaults, BreakRecursionIfFullyLoad);
	}
}

void UStruct::SerializeVersionedTaggedProperties(FStructuredArchive::FSlot Slot, uint8* Data, UStruct* DefaultsStruct, uint8* Defaults, const UObject* BreakRecursionIfFullyLoad) const
{
	FArchive& UnderlyingArchive = Slot.GetUnderlyingArchive();
	//SCOPED_LOADTIMER(SerializeTaggedPropertiesTime);

	// Determine if this struct supports optional property guid's (UBlueprintGeneratedClasses Only)
	const bool bArePropertyGuidsAvailable = (UnderlyingArchive.UE4Ver() >= VER_UE4_PROPERTY_GUID_IN_PROPERTY_TAG) && !FPlatformProperties::RequiresCookedData() && ArePropertyGuidsAvailable();
	const bool bUseRedirects = (!FPlatformProperties::RequiresCookedData() || UnderlyingArchive.IsSaveGame()) && !UnderlyingArchive.IsUsingEventDrivenLoader();

	if (UnderlyingArchive.IsLoading())
	{
#if WITH_TEXT_ARCHIVE_SUPPORT
		if (UnderlyingArchive.IsTextFormat())
		{
			LoadTaggedPropertiesFromText(Slot, Data, DefaultsStruct, Defaults, BreakRecursionIfFullyLoad);
		}
		else
#endif // WITH_TEXT_ARCHIVE_SUPPORT
		{
		// Load tagged properties.
		FStructuredArchive::FStream PropertiesStream = Slot.EnterStream();

		// This code assumes that properties are loaded in the same order they are saved in. This removes a n^2 search 
		// and makes it an O(n) when properties are saved in the same order as they are loaded (default case). In the 
		// case that a property was reordered the code falls back to a slower search.
			FProperty*	Property = PropertyLink;
		bool		bAdvanceProperty	= false;
		int32		RemainingArrayDim	= Property ? Property->ArrayDim : 0;

		// Load all stored properties, potentially skipping unknown ones.
		while (true)
		{
				FStructuredArchive::FRecord PropertyRecord = PropertiesStream.EnterElement().EnterRecord();

				FPropertyTag Tag;
				PropertyRecord << SA_VALUE(TEXT("Tag"), Tag);

				if (Tag.Name.IsNone())
				{
					break;
				}

				// Move to the next property to be serialized
				if (bAdvanceProperty && --RemainingArrayDim <= 0)
				{
					Property = Property->PropertyLinkNext;
					// Skip over properties that don't need to be serialized.
					while (Property && !Property->ShouldSerializeValue(UnderlyingArchive))
					{
						Property = Property->PropertyLinkNext;
					}
					RemainingArrayDim = Property ? Property->ArrayDim : 0;
				}
				bAdvanceProperty = false;

				// Optionally resolve properties using Guid Property tags in non cooked builds that support it.
				if (bArePropertyGuidsAvailable && Tag.HasPropertyGuid)
				{
					// Use property guids from blueprint generated classes to redirect serialised data.
					FName Result = FindPropertyNameFromGuid(Tag.PropertyGuid);
					if (Result != NAME_None && Tag.Name != Result)
					{
						Tag.Name = Result;
					}
				}
				// If this property is not the one we expect (e.g. skipped as it matches the default value), do the brute force search.
				if (Property == nullptr || Property->GetFName() != Tag.Name)
				{
					// No need to check redirects on platforms where everything is cooked. Always check for save games
					if (bUseRedirects && !UnderlyingArchive.HasAnyPortFlags(PPF_DuplicateForPIE | PPF_Duplicate))
					{
						for (UStruct* CheckStruct = GetOwnerStruct(); CheckStruct; CheckStruct = CheckStruct->GetSuperStruct())
						{
							FName NewTagName = FProperty::FindRedirectedPropertyName(CheckStruct, Tag.Name);
							if (!NewTagName.IsNone())
							{
								Tag.Name = NewTagName;
								break;
							}
						}
					}

					FProperty* CurrentProperty = Property;
					// Search forward...
					for (; Property; Property = Property->PropertyLinkNext)
					{
						if (Property->GetFName() == Tag.Name)
						{
							break;
						}
					}
					// ... and then search from the beginning till we reach the current property if it's not found.
					if (Property == nullptr)
					{
						for (Property = PropertyLink; Property && Property != CurrentProperty; Property = Property->PropertyLinkNext)
						{
							if (Property->GetFName() == Tag.Name)
							{
								break;
							}
						}

						if (Property == CurrentProperty)
						{
							// Property wasn't found.
							Property = nullptr;
						}
					}

					RemainingArrayDim = Property ? Property->ArrayDim : 0;
				}

				const int64 StartOfProperty = UnderlyingArchive.Tell();

				if (!Property)
				{
					Property = CustomFindProperty(Tag.Name);
				}

				if (Property)
				{
					FName PropID = Property->GetID();

					// Check if this is a struct property and we have a redirector
					// No need to check redirects on platforms where everything is cooked. Always check for save games
					if (bUseRedirects)
					{
						if (Tag.Type == NAME_StructProperty && PropID == NAME_StructProperty)
						{
							const FName NewName = FLinkerLoad::FindNewNameForStruct(Tag.StructName);
							const FName StructName = CastFieldChecked<FStructProperty>(Property)->Struct->GetFName();
							if (NewName == StructName)
							{
								Tag.StructName = NewName;
							}
						}
						else if ((PropID == NAME_EnumProperty) && ((Tag.Type == NAME_EnumProperty) || (Tag.Type == NAME_ByteProperty)))
						{
							const FName NewName = FLinkerLoad::FindNewNameForEnum(Tag.EnumName);
							if (!NewName.IsNone())
							{
								Tag.EnumName = NewName;
							}
						}
					}

	#if WITH_EDITOR
					if (BreakRecursionIfFullyLoad && BreakRecursionIfFullyLoad->HasAllFlags(RF_LoadCompleted))
					{
					}
					else
	#endif // WITH_EDITOR
					// editoronly properties should be skipped if we are NOT the editor, or we are 
					// the editor but are cooking for console (editoronly implies notforconsole)
					if ((Property->PropertyFlags & CPF_EditorOnly) && ((!FPlatformProperties::HasEditorOnlyData() && !GForceLoadEditorOnly) || UnderlyingArchive.IsUsingEventDrivenLoader()))
					{
					}
					// check for valid array index
					else if (Tag.ArrayIndex >= Property->ArrayDim || Tag.ArrayIndex < 0)
					{
						UE_LOG(LogClass, Warning, TEXT("Array bound exceeded (var %s=%d, exceeds %s [0-%d] in package:  %s"),
							*Tag.Name.ToString(), Tag.ArrayIndex, *GetName(), Property->ArrayDim - 1, *UnderlyingArchive.GetArchiveName());
					}
					else if (!Property->ShouldSerializeValue(UnderlyingArchive))
					{
						UE_CLOG((UnderlyingArchive.IsPersistent() && FPlatformProperties::RequiresCookedData()), LogClass, Warning, TEXT("Skipping saved property %s of %s since it is no longer serializable for asset:  %s. (Maybe resave asset?)"), *Tag.Name.ToString(), *GetName(), *UnderlyingArchive.GetArchiveName());
					}
					else
					{
						FStructuredArchive::FSlot ValueSlot = PropertyRecord.EnterField(SA_FIELD_NAME(TEXT("Value")));

						switch (Property->ConvertFromType(Tag, ValueSlot, Data, DefaultsStruct))
						{
							case EConvertFromTypeResult::Converted:
								bAdvanceProperty = true;
								break;

							case EConvertFromTypeResult::UseSerializeItem:
								if (Tag.Type != PropID)
								{
									UE_LOG(LogClass, Warning, TEXT("Type mismatch in %s of %s - Previous (%s) Current(%s) for package:  %s"), *Tag.Name.ToString(), *GetName(), *Tag.Type.ToString(), *PropID.ToString(), *UnderlyingArchive.GetArchiveName());
								}
								else
								{
									uint8* DestAddress = Property->ContainerPtrToValuePtr<uint8>(Data, Tag.ArrayIndex);
									uint8* DefaultsFromParent = Property->ContainerPtrToValuePtrForDefaults<uint8>(DefaultsStruct, Defaults, Tag.ArrayIndex);

									// This property is ok.
									Tag.SerializeTaggedProperty(ValueSlot, Property, DestAddress, DefaultsFromParent);
									bAdvanceProperty = !UnderlyingArchive.IsCriticalError();
								}
								break;

							case EConvertFromTypeResult::CannotConvert:
								break;

							default:
								check(false);
						}
					}
				}

				int64 Loaded = UnderlyingArchive.Tell() - StartOfProperty;

				if (!bAdvanceProperty)
				{
					UnderlyingArchive.Seek(StartOfProperty + Tag.Size);
				}
				else
				{
					check(Tag.Size == Loaded);
				}
			}
		}
	}
	else
	{
		FUnversionedPropertyTestCollector TestCollector;

		FStructuredArchive::FRecord PropertiesRecord = Slot.EnterRecord();

		check(UnderlyingArchive.IsSaving() || UnderlyingArchive.IsCountingMemory());
		checkf(!UnderlyingArchive.ArUseCustomPropertyList, 
				TEXT("Custom property lists only work with binary serialization, not tagged property serialization. "
					 "Attempted for struct '%s' and archive '%s'. "), *GetFName().ToString(), *UnderlyingArchive.GetArchiveName());

		UScriptStruct* DefaultsScriptStruct = dynamic_cast<UScriptStruct*>(DefaultsStruct);

		/** If true, it means that we want to serialize all properties of this struct if any properties differ from defaults */
		bool bUseAtomicSerialization = false;
		if (DefaultsScriptStruct)
		{
			bUseAtomicSerialization = DefaultsScriptStruct->ShouldSerializeAtomically(UnderlyingArchive);
		}

		// Save tagged properties.

		// Iterate over properties in the order they were linked and serialize them.
		const FCustomPropertyListNode* CustomPropertyNode = UnderlyingArchive.ArUseCustomPropertyList ? UnderlyingArchive.ArCustomPropertyList : nullptr;
		for (FProperty* Property = UnderlyingArchive.ArUseCustomPropertyList ? (CustomPropertyNode ? CustomPropertyNode->Property : nullptr) : PropertyLink;
			Property;
			Property = UnderlyingArchive.ArUseCustomPropertyList ? FCustomPropertyListNode::GetNextPropertyAndAdvance(CustomPropertyNode) : Property->PropertyLinkNext)
		{
			if (Property->ShouldSerializeValue(UnderlyingArchive))
			{
				const int32 LoopMin = CustomPropertyNode ? CustomPropertyNode->ArrayIndex : 0;
				const int32 LoopMax = CustomPropertyNode ? LoopMin + 1 : Property->ArrayDim;

				TOptional<FStructuredArchive::FArray> StaticArrayContainer;
				if (((LoopMax - 1) > LoopMin) && UnderlyingArchive.IsTextFormat())
				{
					int32 NumItems = LoopMax - LoopMin;
					StaticArrayContainer.Emplace(PropertiesRecord.EnterArray(SA_FIELD_NAME((*Property->GetName())), NumItems));
				}

				for (int32 Idx = LoopMin; Idx < LoopMax; Idx++)
				{
					uint8* DataPtr      = Property->ContainerPtrToValuePtr           <uint8>(Data, Idx);
					uint8* DefaultValue = Property->ContainerPtrToValuePtrForDefaults<uint8>(DefaultsStruct, Defaults, Idx);
					if (StaticArrayContainer.IsSet() || CustomPropertyNode || !UnderlyingArchive.DoDelta() || UnderlyingArchive.IsTransacting() || (!Defaults && !dynamic_cast<const UClass*>(this)) || !Property->Identical(DataPtr, DefaultValue, UnderlyingArchive.GetPortFlags()))
					{
						if (bUseAtomicSerialization)
						{
							DefaultValue = NULL;
						}
#if WITH_EDITOR
						static const FName NAME_PropertySerialize = FName(TEXT("PropertySerialize"));
						FArchive::FScopeAddDebugData P(UnderlyingArchive, NAME_PropertySerialize);
						FArchive::FScopeAddDebugData S(UnderlyingArchive, Property->GetFName());
#endif
						TestCollector.RecordSavedProperty(Property);

						FPropertyTag Tag( UnderlyingArchive, Property, Idx, DataPtr, DefaultValue );
						// If available use the property guid from BlueprintGeneratedClasses, provided we aren't cooking data.
						if (bArePropertyGuidsAvailable && !UnderlyingArchive.IsCooking())
						{
							const FGuid PropertyGuid = FindPropertyGuidFromName(Tag.Name);
							Tag.SetPropertyGuid(PropertyGuid);
						}

						TStringBuilder<256> TagName;
						Tag.Name.ToString(TagName);
						FStructuredArchive::FSlot PropertySlot = StaticArrayContainer.IsSet() ? StaticArrayContainer->EnterElement() : PropertiesRecord.EnterField(SA_FIELD_NAME(TagName.ToString()));

						PropertySlot << Tag;

						// need to know how much data this call to SerializeTaggedProperty consumes, so mark where we are
						int64 DataOffset = UnderlyingArchive.Tell();

						// if using it, save the current custom property list and switch to its sub property list (in case of UStruct serialization)
						const FCustomPropertyListNode* SavedCustomPropertyList = nullptr;
						if (UnderlyingArchive.ArUseCustomPropertyList && CustomPropertyNode)
						{
							SavedCustomPropertyList = UnderlyingArchive.ArCustomPropertyList;
							UnderlyingArchive.ArCustomPropertyList = CustomPropertyNode->SubPropertyList;
						}

						Tag.SerializeTaggedProperty(PropertySlot, Property, DataPtr, DefaultValue);

						// restore the original custom property list after serializing
						if (SavedCustomPropertyList)
						{
							UnderlyingArchive.ArCustomPropertyList = SavedCustomPropertyList;
						}

						// set the tag's size
						Tag.Size = UnderlyingArchive.Tell() - DataOffset;

						if (Tag.Size > 0 && !UnderlyingArchive.IsTextFormat())
						{
							// mark our current location
							DataOffset = UnderlyingArchive.Tell();

							// go back and re-serialize the size now that we know it
							UnderlyingArchive.Seek(Tag.SizeOffset);
							UnderlyingArchive << Tag.Size;

							// return to the current location
							UnderlyingArchive.Seek(DataOffset);
						}
					}
				}
			}
		}

		if (!UnderlyingArchive.IsTextFormat())
		{
			// Add an empty FName that serves as a null-terminator
			FName NoneTerminator;
			UnderlyingArchive << NoneTerminator;
		}
	}
}
void UStruct::FinishDestroy()
{
	DestroyUnversionedSchema(this);
	Script.Empty();
	Super::FinishDestroy();
}

/** Helper function that destroys properties from the privided linked list and nulls the list head pointer */
inline void DestroyPropertyLinkedList(FField*& PropertiesToDestroy)
{
	for (FField* FieldToDestroy = PropertiesToDestroy; FieldToDestroy; )
	{
		FField* NextField = FieldToDestroy->Next;
		delete FieldToDestroy;
		FieldToDestroy = NextField;
	}
	PropertiesToDestroy = nullptr;
}

void UStruct::DestroyChildPropertiesAndResetPropertyLinks()
{
	DestroyPropertyLinkedList(ChildProperties);
	PropertyLink = nullptr;
	RefLink = nullptr;
	DestructorLink = nullptr;
	PostConstructLink = nullptr;
#if WITH_EDITORONLY_DATA
	FieldPathSerialNumber = GetNextFieldPathSerialNumber();
#endif // WITH_EDITORONLY_DATA
}

UStruct::~UStruct()
{
	// Destroy all properties owned by this struct
	// This needs to happen after FinishDestroy which calls DestroyNonNativeProperties
	// Also, Blueprint generated classes can have DestroyNonNativeProperties called on them after their FinishDestroy has been called
	// so properties can only be deleted in the destructor
	DestroyPropertyLinkedList(ChildProperties);
	DeleteUnresolvedScriptProperties();
}

IMPLEMENT_FSTRUCTUREDARCHIVE_SERIALIZER(UStruct);

#if WITH_EDITORONLY_DATA
void UStruct::ConvertUFieldsToFFields()
{	
	TArray<FField*> NewChildProperties;
	UField* OldField = Children;
	UField* PreviousUnconvertedField = nullptr;

	// First convert all properties and store them in a temp array
	while (OldField)
	{
		if (OldField->IsA<UProperty>())
		{
			FField* NewField = OldField->GetAssociatedFField();
			if (!NewField)
			{
				NewField = FField::CreateFromUField(OldField);
				OldField->SetAssociatedFField(NewField);
				check(NewField);
			}
			NewChildProperties.Add(NewField);
			// Remove this field from the linked list
			if (PreviousUnconvertedField)
			{
				PreviousUnconvertedField->Next = OldField->Next;
			}
			else
			{
				Children = OldField->Next;
			}
			// Move the old UProperty to the transient package and rename it to something unique
			OldField->Rename(*MakeUniqueObjectName(GetTransientPackage(), OldField->GetClass()).ToString(), GetTransientPackage(), REN_ForceNoResetLoaders | REN_DoNotDirty | REN_DontCreateRedirectors | REN_NonTransactional);
			OldField->RemoveFromRoot();
		}
		else 
		{
			// Update the previous unconverted field
			if (PreviousUnconvertedField)
			{
				PreviousUnconvertedField->Next = OldField;				
			}
			else
			{
				Children = OldField;
			}
			PreviousUnconvertedField = OldField;
		}
		OldField = OldField->Next;
	}
	// Now add them to the linked list in the reverse order to preserve their actual order (adding to the list reverses the order)
	for (int32 ChildPropertyIndex = NewChildProperties.Num() - 1; ChildPropertyIndex >= 0; --ChildPropertyIndex)
	{
		FField* NewField = NewChildProperties[ChildPropertyIndex];
		check(NewField->Next == nullptr);
		NewField->Next = ChildProperties;
		ChildProperties = NewField;
	}
}
#endif // WITH_EDITORONLY_DATA

void UStruct::SerializeProperties(FArchive& Ar)
{
	int32 PropertyCount = 0;

	if (Ar.IsSaving())
	{
		// Count properties
		for (FField* Field = ChildProperties; Field; Field = Field->Next)
		{
			bool bSaveProperty = true;
#if WITH_EDITORONLY_DATA
			FProperty* Property = CastField<FProperty>(Field);
			if (Property)
			{
				bSaveProperty = !(Ar.IsFilterEditorOnly() && Property->IsEditorOnlyProperty());
			}
#endif // WITH_EDITORONLY_DATA
			if (bSaveProperty)
			{
				PropertyCount++;
			}
		}
	}

	Ar << PropertyCount;

	if (Ar.IsLoading())
	{
		// Not using SerializeSingleField here to avoid unnecessary checks for each property
		TArray<FField*> LoadedProperties;
		LoadedProperties.Reserve(PropertyCount);
		for (int32 PropertyIndex = 0; PropertyIndex < PropertyCount; ++PropertyIndex)
		{
			FName PropertyTypeName;
			Ar << PropertyTypeName;
			FField* Prop = FField::Construct(PropertyTypeName, this, NAME_None, RF_NoFlags);
			check(Prop);
			Prop->Serialize(Ar);
			LoadedProperties.Add(Prop);
		}
		for (int32 PropertyIndex = LoadedProperties.Num() - 1; PropertyIndex >= 0; --PropertyIndex)
		{
			FField* Prop = LoadedProperties[PropertyIndex];
			Prop->Next = ChildProperties;
			ChildProperties = Prop;
		}
	}
	else
	{
		int32 VerifySerializedFieldsCount = 0;
		for (FField* Field = ChildProperties; Field; Field = Field->Next)
		{
			bool bSaveProperty = true;
#if WITH_EDITORONLY_DATA
			FProperty* Property = CastField<FProperty>(Field);
			if (Property)
			{
				bSaveProperty = !(Ar.IsFilterEditorOnly() && Property->IsEditorOnlyProperty());
			}
#endif // WITH_EDITORONLY_DATA
			if (bSaveProperty)
			{
				FName PropertyTypeName = Field->GetClass()->GetFName();
				Ar << PropertyTypeName;
				Field->Serialize(Ar);
				VerifySerializedFieldsCount++;
			}
		}
		check(!Ar.IsSaving() || VerifySerializedFieldsCount == PropertyCount);
	}
}

void UStruct::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

#if USTRUCT_FAST_ISCHILDOF_IMPL == USTRUCT_ISCHILDOF_STRUCTARRAY
	UStruct* SuperStructBefore = GetSuperStruct();
#endif

	Ar << SuperStruct;

#if USTRUCT_FAST_ISCHILDOF_IMPL == USTRUCT_ISCHILDOF_STRUCTARRAY
	if (Ar.IsLoading())
	{
		this->ReinitializeBaseChainArray();
	}
	// Handle that fact that FArchive takes UObject*s by reference, and archives can just blat
	// over our SuperStruct with impunity.
	else if (SuperStructBefore)
	{
		UStruct* SuperStructAfter = GetSuperStruct();
		if (SuperStructBefore != SuperStructAfter)
		{
			this->ReinitializeBaseChainArray();
		}
	}
#endif

	Ar.UsingCustomVersion(FFrameworkObjectVersion::GUID);
	Ar.UsingCustomVersion(FCoreObjectVersion::GUID);

	if (Ar.CustomVer(FFrameworkObjectVersion::GUID) < FFrameworkObjectVersion::RemoveUField_Next)
	{
		Ar << Children;
	}
	else
	{
		TArray<UField*> ChildArray;
		if (Ar.IsLoading())
		{
			Ar << ChildArray;
			if (ChildArray.Num())
			{
				for (int32 Index = 0; Index + 1 < ChildArray.Num(); Index++)
				{
					ChildArray[Index]->Next = ChildArray[Index + 1];
				}
				Children = ChildArray[0];
				ChildArray[ChildArray.Num() - 1]->Next = nullptr;
			}
			else
			{
				Children = nullptr;
			}
		}
		else
		{
			UField* Child = Children;
			while (Child)
			{
				ChildArray.Add(Child);
				Child = Child->Next;
			}
			Ar << ChildArray;
		}
	}

	if (Ar.CustomVer(FCoreObjectVersion::GUID) >= FCoreObjectVersion::FProperties)
	{
		SerializeProperties(Ar);
	}

	if (Ar.IsLoading())
	{
		FStructScriptLoader ScriptLoadHelper(/*TargetScriptContainer =*/this, Ar);
#if USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING
		bool const bAllowDeferredScriptSerialization = true;
#else  // USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING
		bool const bAllowDeferredScriptSerialization = false;
#endif // USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING

		// NOTE: if bAllowDeferredScriptSerialization is set to true, then this
		//       could temporarily skip script serialization (as it could 
		//       introduce unwanted dependency loads at this time)
		ScriptLoadHelper.LoadStructWithScript(this, Ar, bAllowDeferredScriptSerialization);

		if (!dynamic_cast<UClass*>(this) && !(Ar.GetPortFlags() & PPF_Duplicate)) // classes are linked in the UClass serializer, which just called me
		{
			// Link the properties.
			Link(Ar, true);
		}
	}
	else
	{
		int32 ScriptBytecodeSize = Script.Num();
		int64 ScriptStorageSizeOffset = INDEX_NONE;

		if (Ar.IsSaving())
		{
			FArchive::FScopeSetDebugSerializationFlags S(Ar, DSF_IgnoreDiff);

			Ar << ScriptBytecodeSize;

			int32 ScriptStorageSize = 0;
			// drop a zero here.  will seek back later and re-write it when we know it
			ScriptStorageSizeOffset = Ar.Tell();
			Ar << ScriptStorageSize;
		}

		// Skip serialization if we're duplicating classes for reinstancing, since we only need the memory layout
		if (!GIsDuplicatingClassForReinstancing)
		{

			// no bytecode patch for this struct - serialize normally [i.e. from disk]
			int32 iCode = 0;
			int64 const BytecodeStartOffset = Ar.Tell();

			if (Ar.IsPersistent() && Ar.GetLinker())
			{
				// make sure this is a ULinkerSave
				FLinkerSave* LinkerSave = CastChecked<FLinkerSave>(Ar.GetLinker());

				// remember how we were saving
				FArchive* SavedSaver = LinkerSave->Saver;

				// force writing to a buffer
				TArray<uint8> TempScript;
				FMemoryWriter MemWriter(TempScript, Ar.IsPersistent());
				LinkerSave->Saver = &MemWriter;

				{
					FPropertyProxyArchive PropertyAr(Ar, iCode, this);
					// now, use the linker to save the byte code, but writing to memory
					while (iCode < ScriptBytecodeSize)
					{
						SerializeExpr(iCode, PropertyAr);
					}
				}

				// restore the saver
				LinkerSave->Saver = SavedSaver;

				// now write out the memory bytes
				Ar.Serialize(TempScript.GetData(), TempScript.Num());

				// and update the SHA (does nothing if not currently calculating SHA)
				LinkerSave->UpdateScriptSHAKey(TempScript);
			}
			else
			{
				FPropertyProxyArchive PropertyAr(Ar, iCode, this);
				while (iCode < ScriptBytecodeSize)
				{
					SerializeExpr(iCode, PropertyAr);
				}
			}

			if (iCode != ScriptBytecodeSize)
			{
				UE_LOG(LogClass, Fatal, TEXT("Script serialization mismatch: Got %i, expected %i"), iCode, ScriptBytecodeSize);
			}

			if (Ar.IsSaving())
			{
				FArchive::FScopeSetDebugSerializationFlags S(Ar, DSF_IgnoreDiff);

				int64 const BytecodeEndOffset = Ar.Tell();

				// go back and write on-disk size
				Ar.Seek(ScriptStorageSizeOffset);
				int32 ScriptStorageSize = BytecodeEndOffset - BytecodeStartOffset;
				Ar << ScriptStorageSize;

				// back to where we were
				Ar.Seek(BytecodeEndOffset);
			}
		} // if !GIsDuplicatingClassForReinstancing
	}
}

void UStruct::PostLoad()
{
	Super::PostLoad();

	// Finally try to resolve all script properties that couldn't be resolved at load time
	if (UnresolvedScriptProperties)
	{
		for (TPair<TFieldPath<FField>, int32>& MissingProperty : *UnresolvedScriptProperties)
		{
			FField* ResolvedProperty = MissingProperty.Key.Get(this);
			if (ResolvedProperty)
			{
				check((int32)Script.Num() >= (int32)(MissingProperty.Value + sizeof(FField*)));
				FField** TargetScriptPropertyPtr = (FField**)(Script.GetData() + MissingProperty.Value);
				*TargetScriptPropertyPtr = ResolvedProperty;
			}
			else if (!MissingProperty.Key.IsPathToFieldEmpty())
			{
				UE_LOG(LogClass, Warning, TEXT("Failed to resolve bytecode referenced field from path: %s when loading %s"), *MissingProperty.Key.ToString(), *GetFullName());
			}
		}
		DeleteUnresolvedScriptProperties();
	}
}

void UStruct::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UStruct* This = CastChecked<UStruct>(InThis);
#if WITH_EDITOR
	if( GIsEditor )
	{
		// Required by the unified GC when running in the editor
		Collector.AddReferencedObject( This->SuperStruct, This );
		Collector.AddReferencedObject( This->Children, This );
		Collector.AddReferencedObjects(This->ScriptAndPropertyObjectReferences, This);
	}
#endif
#if WITH_EDITORONLY_DATA
	Collector.AddReferencedObjects(This->PropertyWrappers, This);
#endif
	Super::AddReferencedObjects( This, Collector );
}

void UStruct::SetSuperStruct(UStruct* NewSuperStruct)
{
	SuperStruct = NewSuperStruct;
#if USTRUCT_FAST_ISCHILDOF_IMPL == USTRUCT_ISCHILDOF_STRUCTARRAY
	this->ReinitializeBaseChainArray();
#endif
}

FString UStruct::PropertyNameToDisplayName(FName InName) const
{
	FFieldVariant FoundField = FindUFieldOrFProperty(this, InName);
	if (FoundField.IsUObject())
	{
		return GetAuthoredNameForField(FoundField.Get<UField>());
	}
	else
	{
		return GetAuthoredNameForField(FoundField.Get<FField>());
	}
}

FString UStruct::GetAuthoredNameForField(const UField* Field) const
{
	if (Field)
	{
		return Field->GetName();
	}
	return FString();
}

FString UStruct::GetAuthoredNameForField(const FField* Field) const
{
	if (Field)
	{
		return Field->GetName();
	}
	return FString();
}

#if WITH_EDITORONLY_DATA
bool UStruct::GetBoolMetaDataHierarchical(const FName& Key) const
{
	bool bResult = false;
	const UStruct* TestStruct = this;
	while( TestStruct )
	{
		if( TestStruct->HasMetaData(Key) )
		{
			bResult = TestStruct->GetBoolMetaData(Key);
			break;
		}

		TestStruct = TestStruct->SuperStruct;
	}
	return bResult;
}

bool UStruct::GetStringMetaDataHierarchical(const FName& Key, FString* OutValue) const
{
	for (const UStruct* TestStruct = this; TestStruct != nullptr; TestStruct = TestStruct->GetSuperStruct())
	{
		if (const FString* FoundMetaData = TestStruct->FindMetaData(Key))
		{
			if (OutValue != nullptr)
			{
				*OutValue = *FoundMetaData;
			}

			return true;
		}
	}

	return false;
}

const UStruct* UStruct::HasMetaDataHierarchical(const FName& Key) const
{
	for (const UStruct* TestStruct = this; TestStruct != nullptr; TestStruct = TestStruct->GetSuperStruct())
	{
		if (TestStruct->HasMetaData(Key))
		{
			return TestStruct;
		}
	}

	return nullptr;
}

#endif // WITH_EDITORONLY_DATA

#if USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING
	/**
	 * If we're loading, then the value of the script's UObject* expression 
	 * could be pointing at a ULinkerPlaceholderClass (used by the linker to 
	 * fight cyclic dependency issues on load). So here, if that's the case, we
	 * have the placeholder track this ref (so it'll replace it once the real 
	 * class is loaded).
	 * 
	 * @param  ScriptPtr    Reference to the point in the bytecode buffer, where a UObject* has been stored (for us to check).
	 */
	static void HandlePlaceholderScriptRef(void* ScriptPtr)
	{
		ScriptPointerType  Temp = FPlatformMemory::ReadUnaligned<ScriptPointerType>(ScriptPtr);
		UObject*& ExprPtrRef = (UObject*&)Temp;
		if (ULinkerPlaceholderClass* PlaceholderObj = Cast<ULinkerPlaceholderClass>(ExprPtrRef))
		{
			PlaceholderObj->AddReferencingScriptExpr((UClass**)(&ExprPtrRef));
		}
		else if (ULinkerPlaceholderFunction* PlaceholderFunc = Cast<ULinkerPlaceholderFunction>(ExprPtrRef))
		{
			PlaceholderFunc->AddReferencingScriptExpr((UFunction**)(&ExprPtrRef));
		}
	}

	#define FIXUP_EXPR_OBJECT_POINTER(Type) \
	{ \
		if (!Ar.IsSaving()) \
		{ \
			int32 const ExprIndex = iCode - sizeof(ScriptPointerType); \
			HandlePlaceholderScriptRef(&Script[ExprIndex]); \
		} \
	}
#endif // #if USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING

EExprToken UStruct::SerializeExpr( int32& iCode, FArchive& Ar )
{
#define SERIALIZEEXPR_INC
#define SERIALIZEEXPR_AUTO_UNDEF_XFER_MACROS
#include "UObject/ScriptSerialization.h"
	return Expr;
#undef SERIALIZEEXPR_INC
#undef SERIALIZEEXPR_AUTO_UNDEF_XFER_MACROS
}

void UStruct::InstanceSubobjectTemplates( void* Data, void const* DefaultData, UStruct* DefaultStruct, UObject* Owner, FObjectInstancingGraph* InstanceGraph )
{
	checkSlow(Data);
	checkSlow(Owner);

	for ( FProperty* Property = RefLink; Property != NULL; Property = Property->NextRef )
	{
		if (Property->ContainsInstancedObjectProperty())
		{
			Property->InstanceSubobjects( Property->ContainerPtrToValuePtr<uint8>(Data), (uint8*)Property->ContainerPtrToValuePtrForDefaults<uint8>(DefaultStruct, DefaultData), Owner, InstanceGraph );
		}
	}
}

IMPLEMENT_CORE_INTRINSIC_CLASS(UStruct, UField,
	{
		Class->ClassAddReferencedObjects = &UStruct::AddReferencedObjects;
		Class->EmitObjectReference(STRUCT_OFFSET(UStruct, SuperStruct), TEXT("SuperStruct"));
		Class->EmitObjectReference(STRUCT_OFFSET(UStruct, Children), TEXT("Children"));

		// Note: None of the *Link members need to be emitted, as they only contain properties
		// that are in the Children chain or SuperStruct->Children chains.

		Class->EmitObjectArrayReference(STRUCT_OFFSET(UStruct, ScriptAndPropertyObjectReferences), TEXT("ScriptAndPropertyObjectReferences"));
	}
);

void UStruct::TagSubobjects(EObjectFlags NewFlags)
{
	Super::TagSubobjects(NewFlags);

	// Tag our properties
	for (TFieldIterator<FProperty> It(this, EFieldIteratorFlags::ExcludeSuper); It; ++It)
	{
		FProperty* Property = *It;
		if (Property && !Property->HasAnyFlags(GARBAGE_COLLECTION_KEEPFLAGS) && !Property->IsRooted())
		{
			Property->SetFlags(NewFlags);
		}
	}
}

/**
* @return	true if this object is of the specified type.
*/
#if USTRUCT_FAST_ISCHILDOF_COMPARE_WITH_OUTERWALK || USTRUCT_FAST_ISCHILDOF_IMPL == USTRUCT_ISCHILDOF_OUTERWALK
bool UStruct::IsChildOf( const UStruct* SomeBase ) const
{
	if (SomeBase == nullptr)
	{
		return false;
	}

	bool bOldResult = false;
	for ( const UStruct* TempStruct=this; TempStruct; TempStruct=TempStruct->GetSuperStruct() )
	{
		if ( TempStruct == SomeBase )
		{
			bOldResult = true;
			break;
		}
	}

#if USTRUCT_FAST_ISCHILDOF_IMPL == USTRUCT_ISCHILDOF_STRUCTARRAY
	const bool bNewResult = IsChildOfUsingStructArray(*SomeBase);
#endif

#if USTRUCT_FAST_ISCHILDOF_COMPARE_WITH_OUTERWALK
	ensureMsgf(bOldResult == bNewResult, TEXT("New cast code failed"));
#endif

	return bOldResult;
}
#endif

/*-----------------------------------------------------------------------------
	UScriptStruct.
-----------------------------------------------------------------------------*/

// sample of how to customize structs
#if 0
USTRUCT()
struct ENGINE_API FTestStruct
{
	GENERATED_USTRUCT_BODY()

	UObject* RawObjectPtr = nullptr;
	TMap<int32, double> Doubles;
	FTestStruct()
	{
		Doubles.Add(1, 1.5);
		Doubles.Add(2, 2.5);
	}
	void AddStructReferencedObjects(class FReferenceCollector& Collector)
	{
		Collector.AddReferencedObject(RawObjectPtr);
	}
	bool Serialize(FArchive& Ar)
	{
		Ar << Doubles;
		return true;
	}
	bool operator==(FTestStruct const& Other) const
	{
		if (Doubles.Num() != Other.Doubles.Num())
		{
			return false;
		}
		for (TMap<int32, double>::TConstIterator It(Doubles); It; ++It)
		{
			double const* OtherVal = Other.Doubles.Find(It.Key());
			if (!OtherVal || *OtherVal != It.Value() )
			{
				return false;
			}
		}
		return true;
	}
	bool Identical(FTestStruct const& Other, uint32 PortFlags) const
	{
		return (*this) == Other;
	}
	void operator=(FTestStruct const& Other)
	{
		Doubles.Empty(Other.Doubles.Num());
		for (TMap<int32, double>::TConstIterator It(Other.Doubles); It; ++It)
		{
			Doubles.Add(It.Key(), It.Value());
		}
	}
	bool ExportTextItem(FString& ValueStr, FTestStruct const& DefaultValue, UObject* Parent, int32 PortFlags, UObject* ExportRootScope) const
	{
		ValueStr += TEXT("(");
		for (TMap<int32, double>::TConstIterator It(Doubles); It; ++It)
		{
			ValueStr += FString::Printf( TEXT("(%d,%f)"),It.Key(), It.Value());
		}
		ValueStr += TEXT(")");
		return true;
	}
	bool ImportTextItem( const TCHAR*& Buffer, int32 PortFlags, UObject* Parent, FOutputDevice* ErrorText )
	{
		check(*Buffer == TEXT('('));
		Buffer++;
		Doubles.Empty();
		while (1)
		{
			const TCHAR* Start = Buffer;
			while (*Buffer && *Buffer != TEXT(','))
			{
				if (*Buffer == TEXT(')'))
				{
					break;
				}
				Buffer++;
			}
			if (*Buffer == TEXT(')'))
			{
				break;
			}
			int32 Key = FCString::Atoi(Start);
			if (*Buffer)
			{
				Buffer++;
			}
			Start = Buffer;
			while (*Buffer && *Buffer != TEXT(')'))
			{
				Buffer++;
			}
			double Value = FCString::Atod(Start);

			if (*Buffer)
			{
				Buffer++;
			}
			Doubles.Add(Key, Value);
		}
		if (*Buffer)
		{
			Buffer++;
		}
		return true;
	}
	bool SerializeFromMismatchedTag(struct FPropertyTag const& Tag, FArchive& Ar)
	{
		// no example of this provided, doesn't make sense
		return false;
	}
};

template<>
struct TStructOpsTypeTraits<FTestStruct> : public TStructOpsTypeTraitsBase2<FTestStruct>
{
	enum 
	{
		WithZeroConstructor = true,
		WithSerializer = true,
		WithPostSerialize = true,
		WithCopy = true,
		WithIdenticalViaEquality = true,
		//WithIdentical = true,
		WithExportTextItem = true,
		WithImportTextItem = true,
		WithAddStructReferencedObjects = true,
		WithSerializeFromMismatchedTag = true,
	};
};

#endif


/** Used to hold virtual methods to construct, destruct, etc native structs in a generic and dynamic fashion 
 * singleton-style to avoid issues with static constructor order
**/
static TMap<FName,UScriptStruct::ICppStructOps*>& GetDeferredCppStructOps()
{
	static struct TMapWithAutoCleanup : public TMap<FName, UScriptStruct::ICppStructOps*>
	{
		~TMapWithAutoCleanup()
		{
			for (ElementSetType::TConstIterator It(Pairs); It; ++It)
			{
				delete It->Value;
			}
		}
	}
	DeferredCppStructOps;
	return DeferredCppStructOps;
}

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
bool FindConstructorUninitialized(UStruct* BaseClass,uint8* Data,uint8* Defaults)
{
	bool bAnyProblem = false;
	static TSet<FString> PrintedWarnings;
	for(FProperty* P=BaseClass->PropertyLink; P; P=P->PropertyLinkNext )
	{		
		int32 Size = P->GetSize();
		bool bProblem = false;
		check(Size);
		FBoolProperty*   PB     = CastField<FBoolProperty>(P);
		FStructProperty* PS     = CastField<FStructProperty>(P);
		FStrProperty*    PStr   = CastField<FStrProperty>(P);
		FArrayProperty*  PArray = CastField<FArrayProperty>(P);
		if(PStr)
		{
			// string that actually have data would be false positives, since they would point to the same string, but actually be different pointers
			// string is known to have a good default constructor
		}
		else if(PB)
		{
			check(Size == PB->ElementSize);
			if( PB->GetPropertyValue_InContainer(Data) && !PB->GetPropertyValue_InContainer(Defaults) )
			{
				bProblem = true;
			}
		}
		else if (PS)
		{
			// these are legitimate exceptions
			if (PS->Struct->GetName() != TEXT("BitArray")
				&& PS->Struct->GetName() != TEXT("SparseArray")
				&& PS->Struct->GetName() != TEXT("Set")
				&& PS->Struct->GetName() != TEXT("Map")
				&& PS->Struct->GetName() != TEXT("MultiMap")
				&& PS->Struct->GetName() != TEXT("ShowFlags_Mirror")
				&& PS->Struct->GetName() != TEXT("Pointer")
				)
			{
				bProblem = FindConstructorUninitialized(PS->Struct, P->ContainerPtrToValuePtr<uint8>(Data), P->ContainerPtrToValuePtr<uint8>(Defaults));
			}
		}
		else if (PArray)
		{
			bProblem = !PArray->Identical_InContainer(Data, Defaults);
		}
		else
		{
			if (FMemory::Memcmp(P->ContainerPtrToValuePtr<uint8>(Data), P->ContainerPtrToValuePtr<uint8>(Defaults), Size) != 0)
			{
//				UE_LOG(LogClass, Warning,TEXT("Mismatch %d %d"),(int32)*(Data + P->Offset), (int32)*(Defaults + P->Offset));
				bProblem = true;
			}	
		}
		if (bProblem)
		{
			FString Issue;
			if (PS)
			{
				Issue = TEXT("     From ");
				Issue += P->GetFullName();
			}
			else
			{
				Issue = BaseClass->GetPathName() + TEXT(",") + P->GetFullName();
			}
			if (!PrintedWarnings.Contains(Issue))
			{
				bAnyProblem = true;
				PrintedWarnings.Add(Issue);
				if (PS)
				{
					UE_LOG(LogClass, Warning,TEXT("%s"),*Issue);
//					OutputDebugStringW(*FString::Printf(TEXT("%s\n"),*Issue));
				}
				else
				{
					UE_LOG(LogClass, Warning,TEXT("Native constructor does not initialize all properties %s (may need to recompile excutable with new headers)"),*Issue);
//					OutputDebugStringW(*FString::Printf(TEXT("Native contructor does not initialize all properties %s\n"),*Issue));
				}
			}
		}
	}
	return bAnyProblem;
}
#endif


UScriptStruct::UScriptStruct( EStaticConstructor, int32 InSize, int32 InAlignment, EObjectFlags InFlags )
	: UStruct( EC_StaticConstructor, InSize, InAlignment, InFlags )
	, StructFlags(STRUCT_NoFlags)
#if HACK_HEADER_GENERATOR
	, StructMacroDeclaredLineNumber(INDEX_NONE)
#endif
	, bPrepareCppStructOpsCompleted(false)
	, CppStructOps(NULL)
{
}

UScriptStruct::UScriptStruct(const FObjectInitializer& ObjectInitializer, UScriptStruct* InSuperStruct, ICppStructOps* InCppStructOps, EStructFlags InStructFlags, SIZE_T ExplicitSize, SIZE_T ExplicitAlignment )
	: UStruct(ObjectInitializer, InSuperStruct, InCppStructOps ? InCppStructOps->GetSize() : ExplicitSize, InCppStructOps ? InCppStructOps->GetAlignment() : ExplicitAlignment )
	, StructFlags(EStructFlags(InStructFlags | (InCppStructOps ? STRUCT_Native : STRUCT_NoFlags)))
#if HACK_HEADER_GENERATOR
	, StructMacroDeclaredLineNumber(INDEX_NONE)
#endif
	, bPrepareCppStructOpsCompleted(false)
	, CppStructOps(InCppStructOps)
{
	PrepareCppStructOps(); // propgate flags, etc
}

UScriptStruct::UScriptStruct(const FObjectInitializer& ObjectInitializer)
	: UStruct(ObjectInitializer)
	, StructFlags(STRUCT_NoFlags)
#if HACK_HEADER_GENERATOR
	, StructMacroDeclaredLineNumber(INDEX_NONE)
#endif
	, bPrepareCppStructOpsCompleted(false)
	, CppStructOps(NULL)
{
}

/** Stash a CppStructOps for future use 
	* @param Target Name of the struct 
	* @param InCppStructOps Cpp ops for this struct
**/
void UScriptStruct::DeferCppStructOps(FName Target, ICppStructOps* InCppStructOps)
{
	TMap<FName,UScriptStruct::ICppStructOps*>& DeferredStructOps = GetDeferredCppStructOps();

	if (UScriptStruct::ICppStructOps* ExistingOps = DeferredStructOps.FindRef(Target))
	{
#if WITH_HOT_RELOAD
		if (!GIsHotReload) // in hot reload, we will just leak these...they may be in use.
#endif
		{
			check(ExistingOps != InCppStructOps); // if it was equal, then we would be re-adding a now stale pointer to the map
			delete ExistingOps;
		}
	}
	DeferredStructOps.Add(Target,InCppStructOps);
}

/** Look for the CppStructOps if we don't already have it and set the property size **/
void UScriptStruct::PrepareCppStructOps()
{
	if (bPrepareCppStructOpsCompleted)
	{
		return;
	}
	if (!CppStructOps)
	{
		CppStructOps = GetDeferredCppStructOps().FindRef(GetFName());
		if (!CppStructOps)
		{
			if (!GIsUCCMakeStandaloneHeaderGenerator && (StructFlags&STRUCT_Native))
			{
				UE_LOG(LogClass, Fatal,TEXT("Couldn't bind to native struct %s. Headers need to be rebuilt, or a noexport class is missing a IMPLEMENT_STRUCT."),*GetName());
			}
			check(!bPrepareCppStructOpsCompleted); // recursion is unacceptable
			bPrepareCppStructOpsCompleted = true;
			return;
		}
#if !HACK_HEADER_GENERATOR
		StructFlags = EStructFlags(StructFlags | STRUCT_Native);
#endif
		
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
		// test that the constructor is initializing everything
		if (!CppStructOps->HasZeroConstructor()
#if WITH_HOT_RELOAD
			&& !GIsHotReload // in hot reload, these produce bogus warnings
#endif
			)
		{
			int32 Size = CppStructOps->GetSize();
			uint8* TestData00 = (uint8*)FMemory::Malloc(Size);
			FMemory::Memzero(TestData00,Size);
			CppStructOps->Construct(TestData00);
			uint8* TestDataFF = (uint8*)FMemory::Malloc(Size);
			FMemory::Memset(TestDataFF,0xff,Size);
			CppStructOps->Construct(TestDataFF);

			if (FMemory::Memcmp(TestData00,TestDataFF, Size) != 0)
			{
				FindConstructorUninitialized(this,TestData00,TestDataFF);
			}
			if (CppStructOps->HasDestructor())
			{
				CppStructOps->Destruct(TestData00);
				CppStructOps->Destruct(TestDataFF);
			}
			FMemory::Free(TestData00);
			FMemory::Free(TestDataFF);
		}
#endif
	}

	check(!(StructFlags & STRUCT_ComputedFlags));
	if (CppStructOps->HasSerializer() || CppStructOps->HasStructuredSerializer())
	{
		UE_LOG(LogClass, Verbose, TEXT("Native struct %s has a custom serializer."),*GetName());
		StructFlags = EStructFlags(StructFlags | STRUCT_SerializeNative );
	}
	if (CppStructOps->HasPostSerialize())
	{
		UE_LOG(LogClass, Verbose, TEXT("Native struct %s wants post serialize."),*GetName());
		StructFlags = EStructFlags(StructFlags | STRUCT_PostSerializeNative );
	}
	if (CppStructOps->HasPostScriptConstruct())
	{
		UE_LOG(LogClass, Verbose, TEXT("Native struct %s wants post script construct."),*GetName());
		StructFlags = EStructFlags(StructFlags | STRUCT_PostScriptConstruct);
	}
	if (CppStructOps->HasNetSerializer())
	{
		UE_LOG(LogClass, Verbose, TEXT("Native struct %s has a custom net serializer."),*GetName());
		StructFlags = EStructFlags(StructFlags | STRUCT_NetSerializeNative);

		if (CppStructOps->HasNetSharedSerialization())
		{
			UE_LOG(LogClass, Verbose, TEXT("Native struct %s can share net serialization."),*GetName());
			StructFlags = EStructFlags(StructFlags | STRUCT_NetSharedSerialization);
		}
	}
	if (CppStructOps->HasNetDeltaSerializer())
	{
		UE_LOG(LogClass, Verbose, TEXT("Native struct %s has a custom net delta serializer."),*GetName());
		StructFlags = EStructFlags(StructFlags | STRUCT_NetDeltaSerializeNative);
	}

	if (CppStructOps->IsPlainOldData())
	{
		UE_LOG(LogClass, Verbose, TEXT("Native struct %s is plain old data."),*GetName());
		StructFlags = EStructFlags(StructFlags | STRUCT_IsPlainOldData | STRUCT_NoDestructor);
	}
	else
	{
		if (CppStructOps->HasCopy())
		{
			UE_LOG(LogClass, Verbose, TEXT("Native struct %s has a native copy."),*GetName());
			StructFlags = EStructFlags(StructFlags | STRUCT_CopyNative);
		}
		if (!CppStructOps->HasDestructor())
		{
			UE_LOG(LogClass, Verbose, TEXT("Native struct %s has no destructor."),*GetName());
			StructFlags = EStructFlags(StructFlags | STRUCT_NoDestructor);
		}
	}
	if (CppStructOps->HasZeroConstructor())
	{
		UE_LOG(LogClass, Verbose, TEXT("Native struct %s has zero construction."),*GetName());
		StructFlags = EStructFlags(StructFlags | STRUCT_ZeroConstructor);
	}
	if (CppStructOps->IsPlainOldData() && !CppStructOps->HasZeroConstructor())
	{
		// hmm, it is safe to see if this can be zero constructed, lets try
		int32 Size = CppStructOps->GetSize();
		uint8* TestData00 = (uint8*)FMemory::Malloc(Size);
		FMemory::Memzero(TestData00,Size);
		CppStructOps->Construct(TestData00);
		CppStructOps->Construct(TestData00); // slightly more like to catch "internal counters" if we do this twice
		bool IsZeroConstruct = true;
		for (int32 Index = 0; Index < Size && IsZeroConstruct; Index++)
		{
			if (TestData00[Index])
			{
				IsZeroConstruct = false;
			}
		}
		FMemory::Free(TestData00);
		if (IsZeroConstruct)
		{
			UE_LOG(LogClass, Verbose, TEXT("Native struct %s has DISCOVERED zero construction. Size = %d"),*GetName(), Size);
			StructFlags = EStructFlags(StructFlags | STRUCT_ZeroConstructor);
		}
	}
	if (CppStructOps->HasIdentical())
	{
		UE_LOG(LogClass, Verbose, TEXT("Native struct %s has native identical."),*GetName());
		StructFlags = EStructFlags(StructFlags | STRUCT_IdenticalNative);
	}
	if (CppStructOps->HasAddStructReferencedObjects())
	{
		UE_LOG(LogClass, Verbose, TEXT("Native struct %s has native AddStructReferencedObjects."),*GetName());
		StructFlags = EStructFlags(StructFlags | STRUCT_AddStructReferencedObjects);
	}
	if (CppStructOps->HasExportTextItem())
	{
		UE_LOG(LogClass, Verbose, TEXT("Native struct %s has native ExportTextItem."),*GetName());
		StructFlags = EStructFlags(StructFlags | STRUCT_ExportTextItemNative);
	}
	if (CppStructOps->HasImportTextItem())
	{
		UE_LOG(LogClass, Verbose, TEXT("Native struct %s has native ImportTextItem."),*GetName());
		StructFlags = EStructFlags(StructFlags | STRUCT_ImportTextItemNative);
	}
	if (CppStructOps->HasSerializeFromMismatchedTag() || CppStructOps->HasStructuredSerializeFromMismatchedTag())
	{
		UE_LOG(LogClass, Verbose, TEXT("Native struct %s has native SerializeFromMismatchedTag."),*GetName());
		StructFlags = EStructFlags(StructFlags | STRUCT_SerializeFromMismatchedTag);
	}

	check(!bPrepareCppStructOpsCompleted); // recursion is unacceptable
	bPrepareCppStructOpsCompleted = true;
}

IMPLEMENT_FSTRUCTUREDARCHIVE_SERIALIZER(UScriptStruct);

void UScriptStruct::Serialize( FArchive& Ar )
{
	Super::Serialize(Ar);

	// serialize the struct's flags
	Ar << (uint32&)StructFlags;

	if (Ar.IsLoading())
	{
		ClearCppStructOps(); // we want to be sure to do this from scratch
		PrepareCppStructOps();
	}
}

bool UScriptStruct::UseBinarySerialization(const FArchive& Ar) const
{
	return !(Ar.IsLoading() || Ar.IsSaving())
		|| Ar.WantBinaryPropertySerialization()
		|| (0 != (StructFlags & STRUCT_Immutable));
}

void UScriptStruct::SerializeItem(FArchive& Ar, void* Value, void const* Defaults)
{
	SerializeItem(FStructuredArchiveFromArchive(Ar).GetSlot(), Value, Defaults);
}

void UScriptStruct::SerializeItem(FStructuredArchive::FSlot Slot, void* Value, void const* Defaults)
{
	FArchive& UnderlyingArchive = Slot.GetUnderlyingArchive();

	const bool bUseBinarySerialization = UseBinarySerialization(UnderlyingArchive);
	const bool bUseNativeSerialization = UseNativeSerialization();

	// Preload struct before serialization tracking to not double count time.
	if (bUseBinarySerialization || bUseNativeSerialization)
	{
		UnderlyingArchive.Preload(this);
	}

	bool bItemSerialized = false;
	if (bUseNativeSerialization)
	{
		UScriptStruct::ICppStructOps* TheCppStructOps = GetCppStructOps();
		check(TheCppStructOps); // else should not have STRUCT_SerializeNative

		if (TheCppStructOps->HasStructuredSerializer())
		{
			bItemSerialized = TheCppStructOps->Serialize(Slot, Value);
		}
		else
		{
#if WITH_TEXT_ARCHIVE_SUPPORT
			FArchiveUObjectFromStructuredArchive Adapter(Slot);
			FArchive& Ar = Adapter.GetArchive();
			bItemSerialized = TheCppStructOps->Serialize(Ar, Value);
			if (bItemSerialized && !Slot.IsFilled())
			{
				// The struct said that serialization succeeded but it didn't actually write anything.
				Slot.EnterRecord();
			}
			Adapter.Close();
#else
			bItemSerialized = TheCppStructOps->Serialize(Slot.GetUnderlyingArchive(), Value);
#endif
		}		
	}

	if (!bItemSerialized)
	{
		if (bUseBinarySerialization)
		{
			// Struct is already preloaded above.
			if (!UnderlyingArchive.IsPersistent() && UnderlyingArchive.GetPortFlags() != 0 && !ShouldSerializeAtomically(UnderlyingArchive) && !UnderlyingArchive.ArUseCustomPropertyList)
			{
				SerializeBinEx(Slot, Value, Defaults, this);
			}
			else
			{
				SerializeBin(Slot, Value);
			}
		}
		else
		{
			SerializeTaggedProperties(Slot, (uint8*)Value, this, (uint8*)Defaults);
		}
	}

	if (StructFlags & STRUCT_PostSerializeNative)
	{
		UScriptStruct::ICppStructOps* TheCppStructOps = GetCppStructOps();
		check(TheCppStructOps); // else should not have STRUCT_PostSerializeNative
		TheCppStructOps->PostSerialize(UnderlyingArchive, Value);
	}
}

const TCHAR* UScriptStruct::ImportText(const TCHAR* InBuffer, void* Value, UObject* OwnerObject, int32 PortFlags, FOutputDevice* ErrorText, const FString& StructName, bool bAllowNativeOverride)
{
	return ImportText(InBuffer, Value, OwnerObject, PortFlags, ErrorText, [&StructName](){return StructName;}, bAllowNativeOverride);
}

const TCHAR* UScriptStruct::ImportText(const TCHAR* InBuffer, void* Value, UObject* OwnerObject, int32 PortFlags, FOutputDevice* ErrorText, const TFunctionRef<FString()>& StructNameGetter, bool bAllowNativeOverride)
{
	if (bAllowNativeOverride && StructFlags & STRUCT_ImportTextItemNative)
	{
		UScriptStruct::ICppStructOps* TheCppStructOps = GetCppStructOps();
		check(TheCppStructOps); // else should not have STRUCT_ImportTextItemNative
		if (TheCppStructOps->ImportTextItem(InBuffer, Value, PortFlags, OwnerObject, ErrorText))
		{
			return InBuffer;
		}
	}

	TArray<FDefinedProperty> DefinedProperties;
	// this keeps track of the number of errors we've logged, so that we can add new lines when logging more than one error
	int32 ErrorCount = 0;
	const TCHAR* Buffer = InBuffer;
	if (*Buffer++ == TCHAR('('))
	{
		// Parse all properties.
		while (*Buffer != TCHAR(')'))
		{
			// parse and import the value
			Buffer = FProperty::ImportSingleProperty(Buffer, Value, this, OwnerObject, PortFlags | PPF_Delimited, ErrorText, DefinedProperties);

			// skip any remaining text before the next property value
			SkipWhitespace(Buffer);
			int32 SubCount = 0;
			while (*Buffer && *Buffer != TCHAR('\r') && *Buffer != TCHAR('\n') &&
				(SubCount > 0 || *Buffer != TCHAR(')')) && (SubCount > 0 || *Buffer != TCHAR(',')))
			{
				SkipWhitespace(Buffer);
				if (*Buffer == TCHAR('\"'))
				{
					do
					{
						Buffer++;
					} while (*Buffer && *Buffer != TCHAR('\"') && *Buffer != TCHAR('\n') && *Buffer != TCHAR('\r'));

					if (*Buffer != TCHAR('\"'))
					{
						ErrorText->Logf(TEXT("%sImportText (%s): Bad quoted string at: %s"), ErrorCount++ > 0 ? LINE_TERMINATOR : TEXT(""), *StructNameGetter(), Buffer);
						return nullptr;
					}
				}
				else if (*Buffer == TCHAR('('))
				{
					SubCount++;
				}
				else if (*Buffer == TCHAR(')'))
				{
					SubCount--;
					if (SubCount < 0)
					{
						ErrorText->Logf(TEXT("%sImportText (%s): Too many closing parenthesis in: %s"), ErrorCount++ > 0 ? LINE_TERMINATOR : TEXT(""), *StructNameGetter(), InBuffer);
						return nullptr;
					}
				}
				Buffer++;
			}
			if (SubCount > 0)
			{
				ErrorText->Logf(TEXT("%sImportText(%s): Not enough closing parenthesis in: %s"), ErrorCount++ > 0 ? LINE_TERMINATOR : TEXT(""), *StructNameGetter(), InBuffer);
				return nullptr;
			}

			// Skip comma.
			if (*Buffer == TCHAR(','))
			{
				// Skip comma.
				Buffer++;
			}
			else if (*Buffer != TCHAR(')'))
			{
				ErrorText->Logf(TEXT("%sImportText (%s): Missing closing parenthesis: %s"), ErrorCount++ > 0 ? LINE_TERMINATOR : TEXT(""), *StructNameGetter(), InBuffer);
				return nullptr;
			}

			SkipWhitespace(Buffer);
		}

		// Skip trailing ')'.
		Buffer++;
	}
	else
	{
		ErrorText->Logf(TEXT("%sImportText (%s): Missing opening parenthesis: %s"), ErrorCount++ > 0 ? LINE_TERMINATOR : TEXT(""), *StructNameGetter(), InBuffer); //-V547
		return nullptr;
	}
	return Buffer;
}

void UScriptStruct::ExportText(FString& ValueStr, const void* Value, const void* Defaults, UObject* OwnerObject, int32 PortFlags, UObject* ExportRootScope, bool bAllowNativeOverride) const
{
	if (bAllowNativeOverride && StructFlags & STRUCT_ExportTextItemNative)
	{
		UScriptStruct::ICppStructOps* TheCppStructOps = GetCppStructOps();
		check(TheCppStructOps); // else should not have STRUCT_ExportTextItemNative
		if (TheCppStructOps->ExportTextItem(ValueStr, Value, Defaults, OwnerObject, PortFlags, ExportRootScope))
		{
			return;
		}
	}

	if (0 != (PortFlags & PPF_ExportCpp))
	{
		return;
	}

	int32 Count = 0;

	// if this struct is configured to be serialized as a unit, it must be exported as a unit as well.
	if ((StructFlags & STRUCT_Atomic) != 0)
	{
		// change Defaults to match Value so that ExportText always exports this item
		Defaults = Value;
	}

	for (TFieldIterator<FProperty> It(this); It; ++It)
	{
		if (It->ShouldPort(PortFlags))
		{
			for (int32 Index = 0; Index < It->ArrayDim; Index++)
			{
				FString InnerValue;
				if (It->ExportText_InContainer(Index, InnerValue, Value, Defaults, OwnerObject, PPF_Delimited | PortFlags, ExportRootScope))
				{
					Count++;
					if (Count == 1)
					{
						ValueStr += TCHAR('(');
					}
					else if ((PortFlags & PPF_BlueprintDebugView) == 0)
					{
						ValueStr += TCHAR(',');
					}
					else
					{
						ValueStr += TEXT(",\n");
					}

					const FString PropertyName = (PortFlags & (PPF_ExternalEditor | PPF_BlueprintDebugView)) != 0 ? It->GetAuthoredName() : It->GetName();

					if (It->ArrayDim == 1)
					{
						ValueStr += FString::Printf(TEXT("%s="), *PropertyName);
					}
					else
					{
						ValueStr += FString::Printf(TEXT("%s[%i]="), *PropertyName, Index);
					}
					ValueStr += InnerValue;
				}
			}
		}
	}

	if (Count > 0)
	{
		ValueStr += TEXT(")");
	}
	else
	{
		ValueStr += TEXT("()");
	}
}

void UScriptStruct::Link(FArchive& Ar, bool bRelinkExistingProperties)
{
	Super::Link(Ar, bRelinkExistingProperties);
	SetStructTrashed(false);
	if (!HasDefaults()) // if you have CppStructOps, then that is authoritative, otherwise we look at the properties
	{
		StructFlags = EStructFlags(StructFlags | STRUCT_ZeroConstructor | STRUCT_NoDestructor | STRUCT_IsPlainOldData);
		for( FProperty* Property = PropertyLink; Property; Property = Property->PropertyLinkNext )
		{
			if (!Property->HasAnyPropertyFlags(CPF_ZeroConstructor))
			{
				StructFlags = EStructFlags(StructFlags & ~STRUCT_ZeroConstructor);
			}
			if (!Property->HasAnyPropertyFlags(CPF_NoDestructor))
			{
				StructFlags = EStructFlags(StructFlags & ~STRUCT_NoDestructor);
			}
			if (!Property->HasAnyPropertyFlags(CPF_IsPlainOldData))
			{
				StructFlags = EStructFlags(StructFlags & ~STRUCT_IsPlainOldData);
			}
		}
		if (StructFlags & STRUCT_IsPlainOldData)
		{
			UE_LOG(LogClass, Verbose, TEXT("Non-Native struct %s is plain old data."),*GetName());
		}
		if (StructFlags & STRUCT_NoDestructor)
		{
			UE_LOG(LogClass, Verbose, TEXT("Non-Native struct %s has no destructor."),*GetName());
		}
		if (StructFlags & STRUCT_ZeroConstructor)
		{
			UE_LOG(LogClass, Verbose, TEXT("Non-Native struct %s has zero construction."),*GetName());
		}
	}
}

bool UScriptStruct::CompareScriptStruct(const void* A, const void* B, uint32 PortFlags) const
{
	check(A);

	if (nullptr == B) // if the comparand is NULL, we just call this no-match
	{
		return false;
	}

	if (StructFlags & STRUCT_IdenticalNative)
	{
		UScriptStruct::ICppStructOps* TheCppStructOps = GetCppStructOps();
		check(TheCppStructOps);
		bool bResult = false;
		if (TheCppStructOps->Identical(A, B, PortFlags, bResult))
		{
			return bResult;
		}
	}

	for( TFieldIterator<FProperty> It(this); It; ++It )
	{
		for( int32 i=0; i<It->ArrayDim; i++ )
		{
			if( !It->Identical_InContainer(A,B,i,PortFlags) )
			{
				return false;
			}
		}
	}
	return true;
}


void UScriptStruct::CopyScriptStruct(void* InDest, void const* InSrc, int32 ArrayDim) const
{
	uint8 *Dest = (uint8*)InDest;
	check(Dest);
	uint8 const* Src = (uint8 const*)InSrc;
	check(Src);

	int32 Stride = GetStructureSize();

	if (StructFlags & STRUCT_CopyNative)
	{
		check(!(StructFlags & STRUCT_IsPlainOldData)); // should not have both
		UScriptStruct::ICppStructOps* TheCppStructOps = GetCppStructOps();
		check(TheCppStructOps);
		check(Stride == TheCppStructOps->GetSize() && PropertiesSize == Stride);
		if (TheCppStructOps->Copy(Dest, Src, ArrayDim))
		{
			return;
		}
	}
	if (StructFlags & STRUCT_IsPlainOldData)
	{
		FMemory::Memcpy(Dest, Src, ArrayDim * Stride);
	}
	else
	{
		for( TFieldIterator<FProperty> It(this); It; ++It )
		{
			for (int32 Index = 0; Index < ArrayDim; Index++)
			{
				It->CopyCompleteValue_InContainer((uint8*)Dest + Index * Stride,(uint8*)Src + Index * Stride);
			}
		}
	}
}

uint32 UScriptStruct::GetStructTypeHash(const void* Src) const
{
	// Calling GetStructTypeHash on struct types that doesn't provide a native 
	// GetTypeHash implementation is an error that neither the C++ compiler nor the BP
	// compiler permit. Still, old reflection data could be loaded that invalidly uses 
	// unhashable types. 

	// If any the ensure or check in this function fires the fix is to implement GetTypeHash 
	// or erase the data. USetProperties and UMapProperties that are loaded from disk
	// will clear themselves when they detect this error (see FSetProperty and 
	// FMapProperty::ConvertFromType).

	UScriptStruct::ICppStructOps* TheCppStructOps = GetCppStructOps();
	return TheCppStructOps->GetStructTypeHash(Src);
}

void UScriptStruct::InitializeStruct(void* InDest, int32 ArrayDim) const
{
	uint8 *Dest = (uint8*)InDest;
	check(Dest);

	int32 Stride = GetStructureSize();

	//@todo UE4 optimize
	FMemory::Memzero(Dest, ArrayDim * Stride);

	int32 InitializedSize = 0;
	UScriptStruct::ICppStructOps* TheCppStructOps = GetCppStructOps();
	if (TheCppStructOps != NULL)
	{
		if (!TheCppStructOps->HasZeroConstructor())
		{
			for (int32 ArrayIndex = 0; ArrayIndex < ArrayDim; ArrayIndex++)
			{
				void* PropertyDest = Dest + ArrayIndex * Stride;
				checkf(IsAligned(PropertyDest, TheCppStructOps->GetAlignment()),
					TEXT("Destination address for property does not match requirement of %d byte alignment for %s"), 
					TheCppStructOps->GetAlignment(),
					*GetPathNameSafe(this));
				TheCppStructOps->Construct(PropertyDest);
			}
		}

		InitializedSize = TheCppStructOps->GetSize();
		// here we want to make sure C++ and the property system agree on the size
		check(Stride == InitializedSize && PropertiesSize == InitializedSize);
	}

	if (PropertiesSize > InitializedSize)
	{
		bool bHitBase = false;
		for (FProperty* Property = PropertyLink; Property && !bHitBase; Property = Property->PropertyLinkNext)
		{
			if (!Property->IsInContainer(InitializedSize))
			{
				for (int32 ArrayIndex = 0; ArrayIndex < ArrayDim; ArrayIndex++)
				{
					Property->InitializeValue_InContainer(Dest + ArrayIndex * Stride);
				}
			}
			else
			{
				bHitBase = true;
			}
		}
	}
}

void UScriptStruct::InitializeDefaultValue(uint8* InStructData) const
{
	InitializeStruct(InStructData);
}

void UScriptStruct::ClearScriptStruct(void* Dest, int32 ArrayDim) const
{
	uint8 *Data = (uint8*)Dest;
	int32 Stride = GetStructureSize();

	int32 ClearedSize = 0;
	UScriptStruct::ICppStructOps* TheCppStructOps = GetCppStructOps();
	if (TheCppStructOps)
	{
		for ( int32 ArrayIndex = 0; ArrayIndex < ArrayDim; ArrayIndex++ )
		{
			uint8* PropertyData = Data + ArrayIndex * Stride;
			if (TheCppStructOps->HasDestructor())
			{
				TheCppStructOps->Destruct(PropertyData);
			}
			if (TheCppStructOps->HasZeroConstructor())
			{
				FMemory::Memzero(PropertyData, Stride);
			}
			else
			{
				TheCppStructOps->Construct(PropertyData);
			}
		}
		ClearedSize = TheCppStructOps->GetSize();
		// here we want to make sure C++ and the property system agree on the size
		check(Stride == ClearedSize && PropertiesSize == ClearedSize);
	}
	if ( PropertiesSize > ClearedSize )
	{
		bool bHitBase = false;
		for ( FProperty* Property = PropertyLink; Property && !bHitBase; Property = Property->PropertyLinkNext )
		{
			if (!Property->IsInContainer(ClearedSize))
			{
				for ( int32 ArrayIndex = 0; ArrayIndex < ArrayDim; ArrayIndex++ )
				{
					for ( int32 PropArrayIndex = 0; PropArrayIndex < Property->ArrayDim; PropArrayIndex++ )
					{
						Property->ClearValue_InContainer(Data + ArrayIndex * Stride, PropArrayIndex);
					}
				}
			}
			else
			{
				bHitBase = true;
			}
		}
	}

}

void UScriptStruct::DestroyStruct(void* Dest, int32 ArrayDim) const
{
	if (StructFlags & (STRUCT_IsPlainOldData | STRUCT_NoDestructor))
	{
		return; // POD types don't need destructors
	}
	uint8 *Data = (uint8*)Dest;
	int32 Stride = GetStructureSize();
	int32 ClearedSize = 0;

	UScriptStruct::ICppStructOps* TheCppStructOps = GetCppStructOps();
	if (TheCppStructOps)
	{
		if (TheCppStructOps->HasDestructor())
		{
			for ( int32 ArrayIndex = 0; ArrayIndex < ArrayDim; ArrayIndex++ )
			{
				uint8* PropertyData = (uint8*)Dest + ArrayIndex * Stride;
				TheCppStructOps->Destruct(PropertyData);
			}
		}
		ClearedSize = TheCppStructOps->GetSize();
		// here we want to make sure C++ and the property system agree on the size
		check(Stride == ClearedSize && PropertiesSize == ClearedSize);
	}

	if (PropertiesSize > ClearedSize)
	{
		bool bHitBase = false;
		for (FProperty* P = DestructorLink; P  && !bHitBase; P = P->DestructorLinkNext)
		{
			if (!P->IsInContainer(ClearedSize))
			{
				if (!P->HasAnyPropertyFlags(CPF_NoDestructor))
				{
					for ( int32 ArrayIndex = 0; ArrayIndex < ArrayDim; ArrayIndex++ )
					{
						P->DestroyValue_InContainer(Data + ArrayIndex * Stride);
					}
				}
			}
			else
			{
				bHitBase = true;
			}
		}
	}
}

bool UScriptStruct::IsStructTrashed() const
{
	return !!(StructFlags & STRUCT_Trashed);
}

void UScriptStruct::SetStructTrashed(bool bIsTrash)
{
	if (bIsTrash)
	{
		StructFlags = EStructFlags(StructFlags | STRUCT_Trashed);
	}
	else
	{
		StructFlags = EStructFlags(StructFlags & ~STRUCT_Trashed);
	}
}

void UScriptStruct::RecursivelyPreload() {}

FGuid UScriptStruct::GetCustomGuid() const
{
	return FGuid();
}

FString UScriptStruct::GetStructCPPName() const
{
	return FString::Printf(TEXT("F%s"), *GetName());
}

#if !(UE_BUILD_TEST || UE_BUILD_SHIPPING)

enum class EScriptStructTestCtorSyntax
{
	NoInit = 0,
	CompilerZeroed = 1
};

struct FScriptStructTestWrapper
{
public:

	FScriptStructTestWrapper(UScriptStruct* InStruct, uint8 InitValue = 0xFD, EScriptStructTestCtorSyntax ConstrutorSyntax = EScriptStructTestCtorSyntax::NoInit)
		: ScriptStruct(InStruct)
		, TempBuffer(nullptr)
	{
		if (ScriptStruct->IsNative())
		{
			UScriptStruct::ICppStructOps* StructOps = ScriptStruct->GetCppStructOps();

			// Make one
			if ((StructOps != nullptr) && StructOps->HasZeroConstructor())
			{
				// These structs have basically promised to be used safely, not going to audit them
			}
			else
			{
				// Allocate space for the struct
				const int32 RequiredAllocSize = ScriptStruct->GetStructureSize();
				TempBuffer = (uint8*)FMemory::Malloc(RequiredAllocSize, ScriptStruct->GetMinAlignment());

				// The following section is a partial duplication of ScriptStruct->InitializeStruct, except we initialize with 0xFD instead of 0x00
				FMemory::Memset(TempBuffer, InitValue, RequiredAllocSize);

				int32 InitializedSize = 0;
				if (StructOps != nullptr)
				{
					if (ConstrutorSyntax == EScriptStructTestCtorSyntax::NoInit)
					{
						StructOps->ConstructForTests(TempBuffer);
					}
					else
					{
						StructOps->Construct(TempBuffer);
					}
					InitializedSize = StructOps->GetSize();
				}

				if (ScriptStruct->PropertiesSize > InitializedSize)
				{
					bool bHitBase = false;
					for (FProperty* Property = ScriptStruct->PropertyLink; Property && !bHitBase; Property = Property->PropertyLinkNext)
					{
						if (!Property->IsInContainer(InitializedSize))
						{
							Property->InitializeValue_InContainer(TempBuffer);
						}
						else
						{
							bHitBase = true;
						}
					}
				}

				if (ScriptStruct->StructFlags & STRUCT_PostScriptConstruct)
				{
					check(StructOps);
					StructOps->PostScriptConstruct(TempBuffer);
				}
			}
		}
	}

	~FScriptStructTestWrapper()
	{
		if (TempBuffer != nullptr)
		{
			// Destroy it
			ScriptStruct->DestroyStruct(TempBuffer);
			FMemory::Free(TempBuffer);
		}
	}

	static bool CanRunTests(UScriptStruct* Struct)
	{
		return (Struct != nullptr) && Struct->IsNative() && (!Struct->GetCppStructOps() || !Struct->GetCppStructOps()->HasZeroConstructor());
	}

	uint8* GetData() { return TempBuffer; }
private:
	UScriptStruct* ScriptStruct;
	uint8* TempBuffer;
};

static void FindUninitializedScriptStructMembers(UScriptStruct* ScriptStruct, EScriptStructTestCtorSyntax ConstructorSyntax, TSet<const FProperty*>& OutUninitializedProperties)
{
	FScriptStructTestWrapper WrapperFF(ScriptStruct, 0xFF, ConstructorSyntax);
	FScriptStructTestWrapper Wrapper00(ScriptStruct, 0x00, ConstructorSyntax);
	FScriptStructTestWrapper WrapperAA(ScriptStruct, 0xAA, ConstructorSyntax);
	FScriptStructTestWrapper Wrapper55(ScriptStruct, 0x55, ConstructorSyntax);

	const void* BadPointer = (void*)0xFFFFFFFFFFFFFFFFull;

	for (const FProperty* Property : TFieldRange<FProperty>(ScriptStruct, EFieldIteratorFlags::ExcludeSuper))
	{
#if	WITH_EDITORONLY_DATA
		static const FName NAME_IgnoreForMemberInitializationTest(TEXT("IgnoreForMemberInitializationTest"));
		if (Property->HasMetaData(NAME_IgnoreForMemberInitializationTest))
		{
			continue;
		}
#endif // WITH_EDITORONLY_DATA

		if (const FObjectPropertyBase* ObjectProperty = CastField<const FObjectPropertyBase>(Property))
		{
			// Check any reflected pointer properties to make sure they got initialized
			const UObject* PropValue = ObjectProperty->GetObjectPropertyValue_InContainer(WrapperFF.GetData());
			if (PropValue == BadPointer)
			{
				OutUninitializedProperties.Add(Property);
			}
		}
		else if (const FBoolProperty* BoolProperty = CastField<const FBoolProperty>(Property))
		{
			// Check for uninitialized boolean properties (done separately to deal with byte-wide booleans that would evaluate to true with either 0x55 or 0xAA)
			const bool bValue0 = BoolProperty->GetPropertyValue_InContainer(Wrapper00.GetData());
			const bool bValue1 = BoolProperty->GetPropertyValue_InContainer(WrapperFF.GetData());

			if (bValue0 != bValue1)
			{
				OutUninitializedProperties.Add(Property);
			}
		}
		else if (Property->IsA(FNameProperty::StaticClass()))
		{
			// Skip some other types that will crash in equality with garbage data
			//@TODO: Shouldn't need to skip FName, it's got a default ctor that initializes correctly...
		}
		else
		{
			bool bShouldInspect = true;
			if (Property->IsA(FStructProperty::StaticClass()))
			{
				// Skip user defined structs since we will consider those structs directly.
				// Calling again here will just result in false positives
				const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
				bShouldInspect = (StructProperty->Struct->StructFlags & STRUCT_NoExport) != 0;
			}

			if (bShouldInspect)
			{
				// Catch all remaining properties

				// Uncomment the following line to aid finding crash sources encountered while running this test. A crash usually indicates an uninitialized pointer
				// UE_LOG(LogClass, Log, TEXT("Testing %s%s::%s for proper initialization"), ScriptStruct->GetPrefixCPP(), *ScriptStruct->GetName(), *Property->GetNameCPP());
				if (!Property->Identical_InContainer(WrapperAA.GetData(), Wrapper55.GetData()))
				{
					OutUninitializedProperties.Add(Property);
				}
			}
		}
	}
}

int32 FStructUtils::AttemptToFindUninitializedScriptStructMembers()
{
	auto GetStructLocation = [](const UScriptStruct* ScriptStruct) -> FString {
		check(ScriptStruct);
		UPackage* ScriptPackage = ScriptStruct->GetOutermost();
		FString StructLocation = FString::Printf(TEXT(" Module:%s"), *FPackageName::GetShortName(ScriptPackage->GetName()));
#if WITH_EDITORONLY_DATA
		static const FName NAME_ModuleRelativePath(TEXT("ModuleRelativePath"));
		const FString& ModuleRelativeIncludePath = ScriptStruct->GetMetaData(NAME_ModuleRelativePath);
		if (!ModuleRelativeIncludePath.IsEmpty())
		{
			StructLocation += FString::Printf(TEXT(" File:%s"), *ModuleRelativeIncludePath);
		}
#endif
		return StructLocation;
	};

	int32 UninitializedScriptStructMemberCount = 0;
	int32 UninitializedObjectPropertyCount = 0;
	UScriptStruct* TestUninitializedScriptStructMembersTestStruct = TBaseStructure<FTestUninitializedScriptStructMembersTest>::Get();
	check(TestUninitializedScriptStructMembersTestStruct != nullptr);
	
	{
		const void* BadPointer = (void*)0xFFFFFFFFFFFFFFFFull;

		// First test if the tests aren't broken
		FScriptStructTestWrapper WrapperFF(TestUninitializedScriptStructMembersTestStruct, 0xFF);
		const FObjectPropertyBase* UninitializedProperty = CastFieldChecked<const FObjectPropertyBase>(TestUninitializedScriptStructMembersTestStruct->FindPropertyByName(TEXT("UninitializedObjectReference")));
		const FObjectPropertyBase* InitializedProperty = CastFieldChecked<const FObjectPropertyBase>(TestUninitializedScriptStructMembersTestStruct->FindPropertyByName(TEXT("InitializedObjectReference")));
		
		const UObject* UninitializedPropValue = UninitializedProperty->GetObjectPropertyValue_InContainer(WrapperFF.GetData());
		if (UninitializedPropValue != BadPointer)
		{
			UE_LOG(LogClass, Warning, TEXT("ObjectProperty %s%s::%s seems to be initialized properly but it shouldn't be. Verify that AttemptToFindUninitializedScriptStructMembers() is working properly"), 
				TestUninitializedScriptStructMembersTestStruct->GetPrefixCPP(), *TestUninitializedScriptStructMembersTestStruct->GetName(), *UninitializedProperty->GetNameCPP());
		}
		const UObject* InitializedPropValue = InitializedProperty->GetObjectPropertyValue_InContainer(WrapperFF.GetData());
		if (InitializedPropValue != nullptr)
		{
			UE_LOG(LogClass, Warning, TEXT("ObjectProperty %s%s::%s seems to be not initialized properly but it should be. Verify that AttemptToFindUninitializedScriptStructMembers() is working properly"),
				TestUninitializedScriptStructMembersTestStruct->GetPrefixCPP(), *TestUninitializedScriptStructMembersTestStruct->GetName(), *InitializedProperty->GetNameCPP());
		}
	}

	TSet<const FProperty*> UninitializedPropertiesNoInit;
	TSet<const FProperty*> UninitializedPropertiesZeroed;
	for (TObjectIterator<UScriptStruct> ScriptIt; ScriptIt; ++ScriptIt)
	{
		UScriptStruct* ScriptStruct = *ScriptIt;

		if (FScriptStructTestWrapper::CanRunTests(ScriptStruct) && ScriptStruct != TestUninitializedScriptStructMembersTestStruct)
		{
			UninitializedPropertiesNoInit.Reset();
			UninitializedPropertiesZeroed.Reset();

			// Test the struct by constructing it with 'new FMyStruct();' syntax first. The compiler should zero all members in this case if the 
			// struct doesn't have a custom default constructor defined
			FindUninitializedScriptStructMembers(ScriptStruct, EScriptStructTestCtorSyntax::CompilerZeroed, UninitializedPropertiesZeroed);
			// Test the struct by constructing it with 'new FStruct;' syntax in which case the compiler doesn't zero the properties automatically
			FindUninitializedScriptStructMembers(ScriptStruct, EScriptStructTestCtorSyntax::NoInit, UninitializedPropertiesNoInit);			

			for (const FProperty* Property : UninitializedPropertiesZeroed)
			{
				++UninitializedScriptStructMemberCount;
				if (Property->IsA<FObjectPropertyBase>())
				{
					++UninitializedObjectPropertyCount;
				}
				UE_LOG(LogClass, Warning, TEXT("%s %s%s::%s is not initialized properly even though its struct probably has a custom default constructor.%s"), *Property->GetClass()->GetName(), ScriptStruct->GetPrefixCPP(), *ScriptStruct->GetName(), *Property->GetNameCPP(), *GetStructLocation(ScriptStruct));
			}
			for (const FProperty* Property : UninitializedPropertiesNoInit)
			{
				if (!UninitializedPropertiesZeroed.Contains(Property))
				{
					++UninitializedScriptStructMemberCount;
					if (Property->IsA<FObjectPropertyBase>())
					{
						++UninitializedObjectPropertyCount;
						UE_LOG(LogClass, Warning, TEXT("%s %s%s::%s is not initialized properly.%s"), *Property->GetClass()->GetName(), ScriptStruct->GetPrefixCPP(), *ScriptStruct->GetName(), *Property->GetNameCPP(), *GetStructLocation(ScriptStruct));
					}
					else
					{
						UE_LOG(LogClass, Display, TEXT("%s %s%s::%s is not initialized properly.%s"), *Property->GetClass()->GetName(), ScriptStruct->GetPrefixCPP(), *ScriptStruct->GetName(), *Property->GetNameCPP(), *GetStructLocation(ScriptStruct));
					}
				}
			}
		}
	}

	if (UninitializedScriptStructMemberCount > 0)
	{
		UE_LOG(LogClass, Display, TEXT("%i Uninitialized script struct members found including %d object properties"), UninitializedScriptStructMemberCount, UninitializedObjectPropertyCount);
	}

	return UninitializedScriptStructMemberCount;
}

#include "HAL/IConsoleManager.h"

FAutoConsoleCommandWithWorldAndArgs GCmdListBadScriptStructs(
	TEXT("CoreUObject.AttemptToFindUninitializedScriptStructMembers"),
	TEXT("Finds USTRUCT() structs that fail to initialize reflected member variables"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Params, UWorld* World)
{
	FStructUtils::AttemptToFindUninitializedScriptStructMembers();
}));

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAutomationTestAttemptToFindUninitializedScriptStructMembers, "UObject.Class AttemptToFindUninitializedScriptStructMembers", EAutomationTestFlags::EditorContext | EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ServerContext | EAutomationTestFlags::SmokeFilter)
bool FAutomationTestAttemptToFindUninitializedScriptStructMembers::RunTest(const FString& Parameters)
{
	return FStructUtils::AttemptToFindUninitializedScriptStructMembers() == 0;
}

#endif

IMPLEMENT_CORE_INTRINSIC_CLASS(UScriptStruct, UStruct,
	{
	}
);

/*-----------------------------------------------------------------------------
	UClass implementation.
-----------------------------------------------------------------------------*/

/** Default C++ class type information, used for all new UClass objects. */
static const FCppClassTypeInfoStatic DefaultCppClassTypeInfoStatic = { false };

void UClass::PostInitProperties()
{
	Super::PostInitProperties();
	if (HasAnyFlags(RF_ClassDefaultObject))
	{
		if (ClassAddReferencedObjects == NULL)
		{
			// Default__Class uses its own AddReferencedObjects function.
			ClassAddReferencedObjects = &UClass::AddReferencedObjects;
		}
	}
}

UObject* UClass::GetDefaultSubobjectByName(FName ToFind)
{
	UObject* DefaultObj = GetDefaultObject();
	UObject* DefaultSubobject = nullptr;
	if (DefaultObj)
	{
		DefaultSubobject = DefaultObj->GetDefaultSubobjectByName(ToFind);
	}
	return DefaultSubobject;
}

void UClass::GetDefaultObjectSubobjects(TArray<UObject*>& OutDefaultSubobjects)
{
	UObject* DefaultObj = GetDefaultObject();
	if (DefaultObj)
	{
		DefaultObj->GetDefaultSubobjects(OutDefaultSubobjects);
	}
	else
	{
		OutDefaultSubobjects.Empty();
	}
}

/**
 * Callback used to allow an object to register its direct object references that are not already covered by
 * the token stream.
 *
 * @param ObjectArray	array to add referenced objects to via AddReferencedObject
 */
void UClass::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UClass* This = CastChecked<UClass>(InThis);
	for( auto& Inter : This->Interfaces )
	{
		Collector.AddReferencedObject( Inter.Class, This );
	}

	for( auto& Func : This->FuncMap )
	{
		Collector.AddReferencedObject( Func.Value, This );
	}

	Collector.AddReferencedObject( This->ClassWithin, This );
	Collector.AddReferencedObject( This->ClassGeneratedBy, This );

	if ( !Collector.IsIgnoringArchetypeRef() )
	{
		Collector.AddReferencedObject( This->ClassDefaultObject, This );
	}
	else if( This->ClassDefaultObject != NULL)
	{
		// Get the ARO function pointer from the CDO class (virtual functions using static function pointers).
		This->CallAddReferencedObjects(This->ClassDefaultObject, Collector);
	}

	Super::AddReferencedObjects( This, Collector );
}

/**
 * Helper class used to save and restore information across a StaticAllocateObject over the top of an existing UClass.
 */
class FRestoreClassInfo: public FRestoreForUObjectOverwrite
{
	/** Keep a copy of the pointer, which isn't supposed to change **/
	UClass*			Target;
	/** Saved ClassWithin **/
	UClass*			Within;
	/** Saved ClassGeneratedBy */
	UObject*		GeneratedBy;
	/** Saved ClassDefaultObject **/
	UObject*		DefaultObject;
	/** Saved ClassFlags **/
	EClassFlags		Flags;
	/** Saved ClassCastFlags **/
	EClassCastFlags	CastFlags;
	/** Saved ClassConstructor **/
	UClass::ClassConstructorType Constructor;
	/** Saved ClassVTableHelperCtorCaller **/
	UClass::ClassVTableHelperCtorCallerType ClassVTableHelperCtorCaller;
	/** Saved ClassConstructor **/
	UClass::ClassAddReferencedObjectsType AddReferencedObjects;
	/** Saved NativeFunctionLookupTable. */
	TArray<FNativeFunctionLookup> NativeFunctionLookupTable;
public:

	/**
	 * Constructor: remember the info for the class so that we can restore it after we've called
	 * FMemory::Memzero() on the object's memory address, which results in the non-intrinsic classes losing
	 * this data
	 */
	FRestoreClassInfo(UClass *Save) :
		Target(Save),
		Within(Save->ClassWithin),
		GeneratedBy(Save->ClassGeneratedBy),
		DefaultObject(Save->GetDefaultsCount() ? Save->GetDefaultObject() : NULL),
		Flags(Save->ClassFlags & CLASS_Abstract),
		CastFlags(Save->ClassCastFlags),
		Constructor(Save->ClassConstructor),
		ClassVTableHelperCtorCaller(Save->ClassVTableHelperCtorCaller),
		AddReferencedObjects(Save->ClassAddReferencedObjects),
		NativeFunctionLookupTable(Save->NativeFunctionLookupTable)
	{
	}
	/** Called once the new object has been reinitialized 
	**/
	virtual void Restore() const
	{
		Target->ClassWithin = Within;
		Target->ClassGeneratedBy = GeneratedBy;
		Target->ClassDefaultObject = DefaultObject;
		Target->ClassFlags |= Flags;
		Target->ClassCastFlags |= CastFlags;
		Target->ClassConstructor = Constructor;
		Target->ClassVTableHelperCtorCaller = ClassVTableHelperCtorCaller;
		Target->ClassAddReferencedObjects = AddReferencedObjects;
		Target->NativeFunctionLookupTable = NativeFunctionLookupTable;
	}
};

/**
 * Save information for StaticAllocateObject in the case of overwriting an existing object.
 * StaticAllocateObject will call delete on the result after calling Restore()
 *
 * @return An FRestoreForUObjectOverwrite that can restore the object or NULL if this is not necessary.
 */
FRestoreForUObjectOverwrite* UClass::GetRestoreForUObjectOverwrite()
{
	return new FRestoreClassInfo(this);
}

/**
	* Get the default object from the class, creating it if missing, if requested or under a few other circumstances
	* @return		the CDO for this class
**/
UObject* UClass::CreateDefaultObject()
{
	if ( ClassDefaultObject == NULL )
	{
		ensureMsgf(!HasAnyClassFlags(CLASS_LayoutChanging), TEXT("Class named %s creating its CDO while changing its layout"), *GetName());

		UClass* ParentClass = GetSuperClass();
		UObject* ParentDefaultObject = NULL;
		if ( ParentClass != NULL )
		{
			UObjectForceRegistration(ParentClass);
			ParentDefaultObject = ParentClass->GetDefaultObject(); // Force the default object to be constructed if it isn't already
			check(GConfig);
			if (GEventDrivenLoaderEnabled && EVENT_DRIVEN_ASYNC_LOAD_ACTIVE_AT_RUNTIME)
			{ 
				check(ParentDefaultObject && !ParentDefaultObject->HasAnyFlags(RF_NeedLoad));
			}
		}

		if ( (ParentDefaultObject != NULL) || (this == UObject::StaticClass()) )
		{
			// If this is a class that can be regenerated, it is potentially not completely loaded.  Preload and Link here to ensure we properly zero memory and read in properties for the CDO
			if( HasAnyClassFlags(CLASS_CompiledFromBlueprint) && (PropertyLink == NULL) && !GIsDuplicatingClassForReinstancing)
			{
				auto ClassLinker = GetLinker();
				if (ClassLinker && !ClassLinker->bDynamicClassLinker)
				{
					if (!GEventDrivenLoaderEnabled)
					{
						UField* FieldIt = Children;
						while (FieldIt && (FieldIt->GetOuter() == this))
						{
							// If we've had cyclic dependencies between classes here, we might need to preload to ensure that we load the rest of the property chain
							if (FieldIt->HasAnyFlags(RF_NeedLoad))
							{
								ClassLinker->Preload(FieldIt);
							}
							FieldIt = FieldIt->Next;
						}
					}
					
					StaticLink(true);
				}
			}

			// in the case of cyclic dependencies, the above Preload() calls could end up 
			// invoking this method themselves... that means that once we're done with  
			// all the Preload() calls we have to make sure ClassDefaultObject is still 
			// NULL (so we don't invalidate one that has already been setup)
			if (ClassDefaultObject == NULL)
			{
				FString PackageName;
				FString CDOName;
				bool bDoNotify = false;
				if (GIsInitialLoad && GetOutermost()->HasAnyPackageFlags(PKG_CompiledIn) && !GetOutermost()->HasAnyPackageFlags(PKG_RuntimeGenerated))
				{
					PackageName = GetOutermost()->GetFName().ToString();
					CDOName = GetDefaultObjectName().ToString();
					NotifyRegistrationEvent(*PackageName, *CDOName, ENotifyRegistrationType::NRT_ClassCDO, ENotifyRegistrationPhase::NRP_Started);
					bDoNotify = true;
				}

				// RF_ArchetypeObject flag is often redundant to RF_ClassDefaultObject, but we need to tag
				// the CDO as RF_ArchetypeObject in order to propagate that flag to any default sub objects.
				ClassDefaultObject = StaticAllocateObject(this, GetOuter(), NAME_None, EObjectFlags(RF_Public|RF_ClassDefaultObject|RF_ArchetypeObject));
				check(ClassDefaultObject);
				// Blueprint CDOs have their properties always initialized.
				const bool bShouldInitializeProperties = !HasAnyClassFlags(CLASS_Native | CLASS_Intrinsic);
				// Register the offsets of any sparse delegates this class introduces with the sparse delegate storage
				for (TFieldIterator<FMulticastSparseDelegateProperty> SparseDelegateIt(this, EFieldIteratorFlags::ExcludeSuper, EFieldIteratorFlags::ExcludeDeprecated); SparseDelegateIt; ++SparseDelegateIt)
				{
					const FSparseDelegate& SparseDelegate = SparseDelegateIt->GetPropertyValue_InContainer(ClassDefaultObject);
					USparseDelegateFunction* SparseDelegateFunction = CastChecked<USparseDelegateFunction>(SparseDelegateIt->SignatureFunction);
					FSparseDelegateStorage::RegisterDelegateOffset(ClassDefaultObject, SparseDelegateFunction->DelegateName, (size_t)&SparseDelegate - (size_t)ClassDefaultObject);
				}
				if (HasAnyClassFlags(CLASS_CompiledFromBlueprint))
				{
					if (UDynamicClass* DynamicClass = Cast<UDynamicClass>(this))
					{
						(*(DynamicClass->DynamicClassInitializer))(DynamicClass);
					}
				}
				(*ClassConstructor)(FObjectInitializer(ClassDefaultObject, ParentDefaultObject, false, bShouldInitializeProperties));
				if (bDoNotify)
				{
					NotifyRegistrationEvent(*PackageName, *CDOName, ENotifyRegistrationType::NRT_ClassCDO, ENotifyRegistrationPhase::NRP_Finished);
				}
				ClassDefaultObject->PostCDOContruct();
			}
		}
	}
	return ClassDefaultObject;
}

/**
 * Feedback context implementation for windows.
 */
class FFeedbackContextImportDefaults : public FFeedbackContext
{
	/** Context information for warning and error messages */
	FContextSupplier*	Context;

public:

	// Constructor.
	FFeedbackContextImportDefaults()
		: Context( NULL )
	{
		TreatWarningsAsErrors = true;
	}
	void Serialize( const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category )
	{
		if( Verbosity==ELogVerbosity::Error || Verbosity==ELogVerbosity::Warning )
		{
			if( TreatWarningsAsErrors && Verbosity==ELogVerbosity::Warning )
			{
				Verbosity = ELogVerbosity::Error;
			}

			FString Prefix;
			if( Context )
			{
				Prefix = Context->GetContext() + TEXT(" : ");
			}
			FString Format = Prefix + FOutputDeviceHelper::FormatLogLine(Verbosity, Category, V);

			if(Verbosity == ELogVerbosity::Error)
			{
				AddError(Format);
			}
			else
			{
				AddWarning(Format);
			}
		}

		if (GLogConsole)
		{
			GLogConsole->Serialize(V, Verbosity, Category);
		}
		if (!GLog->IsRedirectingTo(this))
		{
			GLog->Serialize(V, Verbosity, Category);
		}
	}

	FContextSupplier* GetContext() const
	{
		return Context;
	}
	void SetContext( FContextSupplier* InSupplier )
	{
		Context = InSupplier;
	}
};

FFeedbackContext& UClass::GetDefaultPropertiesFeedbackContext()
{
	static FFeedbackContextImportDefaults FeedbackContextImportDefaults;
	return FeedbackContextImportDefaults;
}

/**
* Get the name of the CDO for the this class
* @return The name of the CDO
*/
FName UClass::GetDefaultObjectName() const
{
	FString DefaultName;
	DefaultName.Reserve(NAME_SIZE);
	DefaultName += DEFAULT_OBJECT_PREFIX;
	AppendName(DefaultName);
	return FName(*DefaultName);
}

//
// Register the native class.
//
void UClass::DeferredRegister(UClass *UClassStaticClass,const TCHAR* PackageName,const TCHAR* Name)
{
	Super::DeferredRegister(UClassStaticClass,PackageName,Name);

	// Get stashed registration info.

	// PVS-Studio justifiably complains about this cast, but we expect this to work because we 'know' that 
	// we're coming from the UClass constructor that is used when 'statically linked'. V580 disables 
	// a warning that indicates this is an 'odd explicit type casting'.
	const TCHAR* InClassConfigName = *(TCHAR**)&ClassConfigName; //-V580 //-V641
	ClassConfigName = InClassConfigName;

	// Propagate inherited flags.
	UClass* SuperClass = GetSuperClass();
	if (SuperClass != NULL)
	{
		ClassFlags |= (SuperClass->ClassFlags & CLASS_Inherit);
		ClassCastFlags |= SuperClass->ClassCastFlags;
	}
}

bool UClass::Rename( const TCHAR* InName, UObject* NewOuter, ERenameFlags Flags )
{
	bool bSuccess = Super::Rename( InName, NewOuter, Flags );

	// If we have a default object, rename that to the same package as the class, and rename so it still matches the class name (Default__ClassName)
	if(bSuccess && (ClassDefaultObject != NULL))
	{
		ClassDefaultObject->Rename(*GetDefaultObjectName().ToString(), NewOuter, Flags);
	}

	// Now actually rename the class
	return bSuccess;
}

void UClass::TagSubobjects(EObjectFlags NewFlags)
{
	Super::TagSubobjects(NewFlags);

	if (ClassDefaultObject && !ClassDefaultObject->HasAnyFlags(GARBAGE_COLLECTION_KEEPFLAGS) && !ClassDefaultObject->IsRooted())
	{
		ClassDefaultObject->SetFlags(NewFlags);
		ClassDefaultObject->TagSubobjects(NewFlags);
	}
}

/**
 * Find the class's native constructor.
 */
void UClass::Bind()
{
	UStruct::Bind();

	if( !GIsUCCMakeStandaloneHeaderGenerator && !ClassConstructor && IsNative() )
	{
		UE_LOG(LogClass, Fatal, TEXT("Can't bind to native class %s"), *GetPathName() );
	}

	UClass* SuperClass = GetSuperClass();
	if (SuperClass && (ClassConstructor == nullptr || ClassAddReferencedObjects == nullptr
		|| ClassVTableHelperCtorCaller == nullptr
		))
	{
		// Chase down constructor in parent class.
		SuperClass->Bind();
		if (!ClassConstructor)
		{
			ClassConstructor = SuperClass->ClassConstructor;
		}
		if (!ClassVTableHelperCtorCaller)
		{
			ClassVTableHelperCtorCaller = SuperClass->ClassVTableHelperCtorCaller;
		}
		if (!ClassAddReferencedObjects)
		{
			ClassAddReferencedObjects = SuperClass->ClassAddReferencedObjects;
		}

		// propagate flags.
		// we don't propagate the inherit flags, that is more of a header generator thing
		ClassCastFlags |= SuperClass->ClassCastFlags;
	}
	//if( !Class && SuperClass )
	//{
	//}
	if( !ClassConstructor )
	{
		UE_LOG(LogClass, Fatal, TEXT("Can't find ClassConstructor for class %s"), *GetPathName() );
	}
}


/**
 * Returns the struct/ class prefix used for the C++ declaration of this struct/ class.
 * Classes deriving from AActor have an 'A' prefix and other UObject classes an 'U' prefix.
 *
 * @return Prefix character used for C++ declaration of this struct/ class.
 */
const TCHAR* UClass::GetPrefixCPP() const
{
	const UClass* TheClass	= this;
	bool	bIsActorClass	= false;
	bool	bIsDeprecated	= TheClass->HasAnyClassFlags(CLASS_Deprecated);
	while( TheClass && !bIsActorClass )
	{
		bIsActorClass	= TheClass->GetFName() == NAME_Actor;
		TheClass		= TheClass->GetSuperClass();
	}

	if( bIsActorClass )
	{
		if( bIsDeprecated )
		{
			return TEXT("ADEPRECATED_");
		}
		else
		{
			return TEXT("A");
		}
	}
	else
	{
		if( bIsDeprecated )
		{
			return TEXT("UDEPRECATED_");
		}
		else
		{
			return TEXT("U");
		}		
	}
}

FString UClass::GetDescription() const
{
	FString Description;

#if WITH_EDITOR
	// See if display name meta data has been specified
	Description = GetDisplayNameText().ToString();
	if (Description.Len())
	{
		return Description;
	}
#endif

	// Look up the the classes name in the legacy int file and return the class name if there is no match.
	//Description = Localize( TEXT("Objects"), *GetName(), *(FInternationalization::Get().GetCurrentCulture()->GetName()), true );
	//if (Description.Len())
	//{
	//	return Description;
	//}

	// Otherwise just return the class name
	return FString( GetName() );
}

//	UClass UObject implementation.

void UClass::FinishDestroy()
{
	// Empty arrays.
	//warning: Must be emptied explicitly in order for intrinsic classes
	// to not show memory leakage on exit.
	NetFields.Empty();
	ClassReps.Empty();

	ClassDefaultObject = nullptr;

#if WITH_EDITORONLY_DATA
	// If for whatever reason there's still properties that have not been destroyed in PurgeClass, destroy them now
	DestroyPropertiesPendingDestruction();
#endif // WITH_EDITORONLY_DATA

	Super::FinishDestroy();
}

void UClass::PostLoad()
{
	check(ClassWithin);
	Super::PostLoad();

	// Postload super.
	if( GetSuperClass() )
	{
		GetSuperClass()->ConditionalPostLoad();
	}

	if (!HasAnyClassFlags(CLASS_Native))
	{
		ClassFlags &= ~CLASS_ReplicationDataIsSetUp;
	}
}

FString UClass::GetDesc()
{
	return GetName();
}

void UClass::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	Super::GetAssetRegistryTags(OutTags);

#if WITH_EDITOR
	static const FName ParentClassFName = "ParentClass";
	const UClass* const ParentClass = GetSuperClass();
	OutTags.Add( FAssetRegistryTag(ParentClassFName, ((ParentClass) ? ParentClass->GetFName() : NAME_None).ToString(), FAssetRegistryTag::TT_Alphabetical) );

	static const FName ModuleNameFName = "ModuleName";
	const UPackage* const ClassPackage = GetOuterUPackage();
	OutTags.Add( FAssetRegistryTag(ModuleNameFName, ((ClassPackage) ? FPackageName::GetShortFName(ClassPackage->GetFName()) : NAME_None).ToString(), FAssetRegistryTag::TT_Alphabetical) );

	static const FName ModuleRelativePathFName = "ModuleRelativePath";
	const FString& ClassModuleRelativeIncludePath = GetMetaData(ModuleRelativePathFName);
	OutTags.Add( FAssetRegistryTag(ModuleRelativePathFName, ClassModuleRelativeIncludePath, FAssetRegistryTag::TT_Alphabetical) );
#endif
}

void UClass::Link(FArchive& Ar, bool bRelinkExistingProperties)
{
	check(!bRelinkExistingProperties || !(ClassFlags & CLASS_Intrinsic));
	Super::Link(Ar, bRelinkExistingProperties);
}

#if (UE_BUILD_SHIPPING)
static int32 GValidateReplicatedProperties = 0;
#else 
static int32 GValidateReplicatedProperties = 1;
#endif

static FAutoConsoleVariable CVarValidateReplicatedPropertyRegistration(TEXT("net.ValidateReplicatedPropertyRegistration"), GValidateReplicatedProperties, TEXT("Warns if replicated properties were not registered in GetLifetimeReplicatedProps."));

#if HACK_HEADER_GENERATOR
void UClass::SetUpUhtReplicationData()
{
	if (!HasAnyClassFlags(CLASS_ReplicationDataIsSetUp) && PropertyLink != NULL)
	{
        ClassReps.Empty();
		if (UClass* SuperClass = GetSuperClass())
		{
			SuperClass->SetUpUhtReplicationData();
			ClassReps = SuperClass->ClassReps;
			FirstOwnedClassRep = ClassReps.Num();
		}
		else
		{
			FirstOwnedClassRep = 0;
		}

		for (TFieldIterator<FProperty> It(this, EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			if (It->PropertyFlags & CPF_Net)
			{
				It->RepIndex = ClassReps.Num();
				new (ClassReps) FRepRecord(*It, 0);
			}
		}

		ClassFlags |= CLASS_ReplicationDataIsSetUp;
		ClassReps.Shrink();
	}
}
#endif

void UClass::SetUpRuntimeReplicationData()
{
	if (!HasAnyClassFlags(CLASS_ReplicationDataIsSetUp) && PropertyLink != NULL)
	{
		NetFields.Empty();

		if (UClass* SuperClass = GetSuperClass())
		{
			SuperClass->SetUpRuntimeReplicationData();
			ClassReps = SuperClass->ClassReps;
			FirstOwnedClassRep = ClassReps.Num();
		}
		else
		{
			ClassReps.Empty();
			FirstOwnedClassRep = 0;
		}

		// Track properties so me can ensure they are sorted by offsets at the end
		TArray<FProperty*> NetProperties;
		for (TFieldIterator<FField> It(this, EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			if (FProperty* Prop = CastField<FProperty>(*It))
			{
				if ((Prop->PropertyFlags & CPF_Net) && Prop->GetOwner<UObject>() == this)
				{
					NetProperties.Add(Prop);
				}
			}
			}

		for(TFieldIterator<UField> It(this,EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			if (UFunction * Func = Cast<UFunction>(*It))
			{
				// When loading reflection data (e.g. from blueprints), we may have references to placeholder functions, or reflection data 
				// in children may be out of date. In that case we cannot enforce this check, but that is ok because reflection data will
				// be regenerated by compile on load anyway:
				const bool bCanCheck = (!GIsEditor && !IsRunningCommandlet()) || !Func->HasAnyFlags(RF_WasLoaded);
				check(!bCanCheck || (!Func->GetSuperFunction() || (Func->GetSuperFunction()->FunctionFlags&FUNC_NetFuncFlags) == (Func->FunctionFlags&FUNC_NetFuncFlags)));
				if ((Func->FunctionFlags&FUNC_Net) && !Func->GetSuperFunction())
				{
					NetFields.Add(Func);
				}
			}
		}

		const bool bIsNativeClass = HasAnyClassFlags(CLASS_Native);
		if (!bIsNativeClass)
		{
		// Sort NetProperties so that their ClassReps are sorted by memory offset
			struct FComparePropertyOffsets
		{
				FORCEINLINE bool operator()(FProperty& A, FProperty& B) const
			{
				// Ensure stable sort
					if (A.GetOffset_ForGC() == B.GetOffset_ForGC())
				{
					return A.GetName() < B.GetName();
				}

				return A.GetOffset_ForGC() < B.GetOffset_ForGC();
			}
		};

			Sort(NetProperties.GetData(), NetProperties.Num(), FComparePropertyOffsets());
		}

		ClassReps.Reserve(ClassReps.Num() + NetProperties.Num());
		for (int32 i = 0; i < NetProperties.Num(); i++)
		{
			NetProperties[i]->RepIndex = ClassReps.Num();
			for (int32 j = 0; j < NetProperties[i]->ArrayDim; j++)
			{
				new(ClassReps)FRepRecord(NetProperties[i], j);
			}
		}

		if (bIsNativeClass && GValidateReplicatedProperties)
		{
			GetDefaultObject()->ValidateGeneratedRepEnums(ClassReps);
		}

		NetFields.Shrink();

		struct FCompareUFieldNames
		{
			FORCEINLINE bool operator()(UField& A, UField& B) const
			{
				return A.GetName() < B.GetName();
			}
		};
		Sort(NetFields.GetData(), NetFields.Num(), FCompareUFieldNames());

		ClassFlags |= CLASS_ReplicationDataIsSetUp;

		if (GValidateReplicatedProperties != 0)
		{
			ValidateRuntimeReplicationData();
		}
	}
}

void UClass::ValidateRuntimeReplicationData()
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("Class ValidateRuntimeReplicationData"), STAT_Class_ValidateRuntimeReplicationData, STATGROUP_Game);

	if (HasAnyClassFlags(CLASS_CompiledFromBlueprint|CLASS_LayoutChanging))
	{
		// Blueprint classes don't always generate a GetLifetimeReplicatedProps function. 
		// Assume the Blueprint compiler was ok to do this.
		return;
	}

	if (HasAnyClassFlags(CLASS_ReplicationDataIsSetUp) == false)
	{
		UE_LOG(LogClass, Warning, TEXT("ValidateRuntimeReplicationData for class %s called before ReplicationData was setup."), *GetName());
		return;
	}

	// Our replication data was set up, but there are no class reps, so there's nothing to do.
	if (ClassReps.Num() == 0)
	{
		return;
	}

	// Let's compare the CDO's registered lifetime properties with the Class's net properties
	TArray<FLifetimeProperty> LifetimeProps;
	LifetimeProps.Reserve(ClassReps.Num());

	const UObject* Object = GetDefaultObject();
	Object->GetLifetimeReplicatedProps(LifetimeProps);

	if (LifetimeProps.Num() == ClassReps.Num())
	{
		// All replicated properties were registered for this class
		return;
	}

	// Find which properties where not registered by the user code
	for (int32 RepIndex = 0; RepIndex < ClassReps.Num(); ++RepIndex)
	{
		const FProperty* RepProp = ClassReps[RepIndex].Property;

		const FLifetimeProperty* LifetimeProp = LifetimeProps.FindByPredicate([&RepIndex](const FLifetimeProperty& Var) { return Var.RepIndex == RepIndex; });

		if (LifetimeProp == nullptr)
		{
			// Check if this unregistered property type uses a custom delta serializer
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(RepProp))
			{
				const UScriptStruct* Struct = StructProperty->Struct;

				if (EnumHasAnyFlags(Struct->StructFlags, STRUCT_NetDeltaSerializeNative))
				{
					UE_LOG(LogClass, Warning, TEXT("Property %s::%s (SourceClass: %s) with custom net delta serializer was not registered in GetLifetimeReplicatedProps. This property will replicate but you should still register it."),
						*GetName(), *RepProp->GetName(), *RepProp->GetOwnerClass()->GetName());
					continue;
				}
			}

			UE_LOG(LogClass, Warning, TEXT("Property %s::%s (SourceClass: %s) was not registered in GetLifetimeReplicatedProps. This property will not be replicated. Use DISABLE_REPLICATED_PROPERTY if not replicating was intentional."),
				*GetName(), *RepProp->GetName(), *RepProp->GetOwnerClass()->GetName());
		}
	}
}

/**
* Helper function for determining if the given class is compatible with structured archive serialization
*/
bool UClass::IsSafeToSerializeToStructuredArchives(UClass* InClass)
{
	while (InClass)
	{
		if (!InClass->HasAnyClassFlags(CLASS_MatchedSerializers))
		{
			return false;
		}
		InClass = InClass->GetSuperClass();
	}
	return true;
}

#if USTRUCT_FAST_ISCHILDOF_IMPL == USTRUCT_ISCHILDOF_STRUCTARRAY

	FStructBaseChain::FStructBaseChain()
		: StructBaseChainArray(nullptr)
		, NumStructBasesInChainMinusOne(-1)
	{
	}

	FStructBaseChain::~FStructBaseChain()
	{
		delete [] StructBaseChainArray;
	}

	void FStructBaseChain::ReinitializeBaseChainArray()
	{
		delete [] StructBaseChainArray;

		int32 Depth = 0;
		for (UStruct* Ptr = static_cast<UStruct*>(this); Ptr; Ptr = Ptr->GetSuperStruct())
		{
			++Depth;
		}

		FStructBaseChain** Bases = new FStructBaseChain*[Depth];
		{
			FStructBaseChain** Base = Bases + Depth;
			for (UStruct* Ptr = static_cast<UStruct*>(this); Ptr; Ptr = Ptr->GetSuperStruct())
			{
				*--Base = Ptr;
			}
		}

		StructBaseChainArray = Bases;
		NumStructBasesInChainMinusOne = Depth - 1;
	}

#endif

void UClass::SetSuperStruct(UStruct* NewSuperStruct)
{
	UnhashObject(this);
	ClearFunctionMapsCaches();
	Super::SetSuperStruct(NewSuperStruct);

	if (!GetSparseClassDataStruct())
	{
		if (UScriptStruct* SparseClassDataStructArchetype = GetSparseClassDataArchetypeStruct())
		{
			SetSparseClassDataStruct(SparseClassDataStructArchetype);
		}
	}

	HashObject(this);
}

bool UClass::IsStructTrashed() const
{
	return Children == nullptr && ChildProperties == nullptr && ClassDefaultObject == nullptr;
}

void UClass::Serialize( FArchive& Ar )
{
	if ( Ar.IsLoading() || Ar.IsModifyingWeakAndStrongReferences() )
	{
		// Rehash since SuperStruct will be serialized in UStruct::Serialize
		UnhashObject(this);
	}

	Super::Serialize( Ar );

	if ( Ar.IsLoading() || Ar.IsModifyingWeakAndStrongReferences() )
	{
		HashObject(this);
	}

	Ar.ThisContainsCode();

	// serialize the function map
	//@TODO: UCREMOVAL: Should we just regenerate the FuncMap post load, instead of serializing it?
	Ar << FuncMap;

	// Class flags first.
	if (Ar.IsSaving())
	{
		uint32 SavedClassFlags = ClassFlags;
		SavedClassFlags &= ~(CLASS_ShouldNeverBeLoaded | CLASS_TokenStreamAssembled);
		Ar << SavedClassFlags;
	}
	else if (Ar.IsLoading())
	{
		Ar << (uint32&)ClassFlags;
		ClassFlags &= ~(CLASS_ShouldNeverBeLoaded | CLASS_TokenStreamAssembled);
	}
	else 
	{
		Ar << (uint32&)ClassFlags;
	}
	if (Ar.UE4Ver() < VER_UE4_CLASS_NOTPLACEABLE_ADDED)
	{
		// We need to invert the CLASS_NotPlaceable flag here because it used to mean CLASS_Placeable
		ClassFlags ^= CLASS_NotPlaceable;

		// We can't import a class which is placeable and has a not-placeable base, so we need to check for that here.
		if (ensure(HasAnyClassFlags(CLASS_NotPlaceable) || !GetSuperClass()->HasAnyClassFlags(CLASS_NotPlaceable)))
		{
			// It's good!
		}
		else
		{
			// We'll just make it non-placeable to ensure loading works, even if there's an off-chance that it's already been placed
			ClassFlags |= CLASS_NotPlaceable;
		}
	}

	// Variables.
	Ar << ClassWithin;
	Ar << ClassConfigName;

	int32 NumInterfaces = 0;
	int64 InterfacesStart = 0L;
	if(Ar.IsLoading())
	{
		// Always start with no interfaces
		Interfaces.Empty();

		// In older versions, interface classes were serialized before linking. In case of cyclic dependencies, we need to skip over the serialized array and defer the load until after Link() is called below.
		if(Ar.UE4Ver() < VER_UE4_UCLASS_SERIALIZE_INTERFACES_AFTER_LINKING && !GIsDuplicatingClassForReinstancing)
		{
			// Get our current position
			InterfacesStart = Ar.Tell();

			// Load the length of the Interfaces array
			Ar << NumInterfaces;

			// Seek past the Interfaces array
			struct FSerializedInterfaceReference
			{
				FPackageIndex Class;
				int32 PointerOffset;
				bool bImplementedByK2;
			};
			Ar.Seek(InterfacesStart + sizeof(NumInterfaces) + NumInterfaces * sizeof(FSerializedInterfaceReference));
		}
	}

	if (!Ar.IsIgnoringClassGeneratedByRef())
	{
		Ar << ClassGeneratedBy;
	}

	if(Ar.IsLoading())
	{
		checkf(!HasAnyClassFlags(CLASS_Native), TEXT("Class %s loaded with CLASS_Native....we should not be loading any native classes."), *GetFullName());
		checkf(!HasAnyClassFlags(CLASS_Intrinsic), TEXT("Class %s loaded with CLASS_Intrinsic....we should not be loading any intrinsic classes."), *GetFullName());
		ClassFlags &= ~(CLASS_ShouldNeverBeLoaded | CLASS_TokenStreamAssembled);
		if (!(Ar.GetPortFlags() & PPF_Duplicate))
		{
			Link(Ar, true);
		}
	}

	if(Ar.IsLoading())
	{
		// Save current position
		int64 CurrentOffset = Ar.Tell();

		// In older versions, we need to seek backwards to the start of the interfaces array
		if(Ar.UE4Ver() < VER_UE4_UCLASS_SERIALIZE_INTERFACES_AFTER_LINKING && !GIsDuplicatingClassForReinstancing)
		{
			Ar.Seek(InterfacesStart);
		}
		
		// Load serialized interface classes
		TArray<FImplementedInterface> SerializedInterfaces;
		Ar << SerializedInterfaces;

		// Apply loaded interfaces only if we have not already set them (i.e. during compile-on-load)
		if(Interfaces.Num() == 0 && SerializedInterfaces.Num() > 0)
		{
			Interfaces = SerializedInterfaces;
		}

		// In older versions, seek back to our current position after linking
		if(Ar.UE4Ver() < VER_UE4_UCLASS_SERIALIZE_INTERFACES_AFTER_LINKING && !GIsDuplicatingClassForReinstancing)
		{
			Ar.Seek(CurrentOffset);
		}
	}
	else
	{
		Ar << Interfaces;
	}

	bool bDeprecatedForceScriptOrder = false;
	Ar << bDeprecatedForceScriptOrder;

	FName Dummy = NAME_None;
	Ar << Dummy;

	if (Ar.UE4Ver() >= VER_UE4_ADD_COOKED_TO_UCLASS)
	{
		if (Ar.IsSaving())
		{
			bCooked = Ar.IsCooking();
		}
		bool bCookedAsBool = bCooked;
		Ar << bCookedAsBool;
		if (Ar.IsLoading())
		{
			bCooked = bCookedAsBool;
		}
	}

	// Defaults.

	// mark the archive as serializing defaults
	Ar.StartSerializingDefaults();

	if( Ar.IsLoading() )
	{
		check((Ar.GetPortFlags() & PPF_Duplicate) || (GetStructureSize() >= sizeof(UObject)));
		check(!GetSuperClass() || !GetSuperClass()->HasAnyFlags(RF_NeedLoad));
		
		// record the current CDO, as it stands, so we can compare against it 
		// after we've serialized in the new CDO (to detect if, as a side-effect
		// of the serialization, a different CDO was generated)
		UObject* const OldCDO = ClassDefaultObject;

		// serialize in the CDO, but first store it here (in a temporary var) so
		// we can check to see if it should be the authoritative CDO (a newer 
		// CDO could be generated as a side-effect of this serialization)
		//
		// @TODO: for USE_CIRCULAR_DEPENDENCY_LOAD_DEFERRING, do we need to 
		//        defer this serialization (should we just save off the tagged
		//        serialization data for later use)?
		UObject* PerspectiveNewCDO = NULL;
		Ar << PerspectiveNewCDO;

		// Blueprint class regeneration could cause the class's CDO to be set.
		// The CDO (<<) serialization call (above) probably will invoke class 
		// regeneration, and as a side-effect the CDO could already be set by 
		// the time it returns. So we only want to set the CDO here (to what was 
		// serialized in) if it hasn't already changed (else, the serialized
		// version could be stale). See: TTP #343166
		if (ClassDefaultObject == OldCDO)
		{
			ClassDefaultObject = PerspectiveNewCDO;
		}
		// if we reach this point, then the CDO was regenerated as a side-effect
		// of the serialization... let's log if the regenerated CDO (what's 
		// already been set) is not the same as what was returned from the 
		// serialization (could mean the CDO was regenerated multiple times?)
		else if (PerspectiveNewCDO != ClassDefaultObject)
		{
			UE_LOG(LogClass, Log, TEXT("CDO was changed while class serialization.\n\tOld: '%s'\n\tSerialized: '%s'\n\tActual: '%s'")
				, OldCDO ? *OldCDO->GetFullName() : TEXT("NULL")
				, PerspectiveNewCDO ? *PerspectiveNewCDO->GetFullName() : TEXT("NULL")
				, ClassDefaultObject ? *ClassDefaultObject->GetFullName() : TEXT("NULL"));
		}
		ClassUnique = 0;
	}
	else
	{
		check(!ClassDefaultObject || GetDefaultsCount()==GetPropertiesSize());

		// only serialize the class default object if the archive allows serialization of ObjectArchetype
		// otherwise, serialize the properties that the ClassDefaultObject references
		// The logic behind this is the assumption that the reason for not serializing the ObjectArchetype
		// is because we are performing some actions on objects of this class and we don't want to perform
		// that action on the ClassDefaultObject.  However, we do want to perform that action on objects that
		// the ClassDefaultObject is referencing, so we'll serialize it's properties instead of serializing
		// the object itself
		if ( !Ar.IsIgnoringArchetypeRef() )
		{
			Ar << ClassDefaultObject;
		}
		else if( (ClassDefaultObject != nullptr && !Ar.HasAnyPortFlags(PPF_DuplicateForPIE|PPF_Duplicate)) || ClassDefaultObject != nullptr )
		{
			ClassDefaultObject->Serialize(Ar);
		}
	}

	if (!Ar.IsLoading() && !Ar.IsSaving())
	{
		if (GetSparseClassDataStruct() != nullptr)
		{
			SerializeSparseClassData(FStructuredArchiveFromArchive(Ar).GetSlot());
		}
	}

	// mark the archive we that we are no longer serializing defaults
	Ar.StopSerializingDefaults();

	if( Ar.IsLoading() )
	{
		if (ClassDefaultObject == NULL)
		{
			check(GConfig);
			if (GEventDrivenLoaderEnabled || Ar.IsUsingEventDrivenLoader())
			{
				ClassDefaultObject = GetDefaultObject();
				// we do this later anyway, once we find it and set it in the export table. 
				// ClassDefaultObject->SetFlags(RF_NeedLoad | RF_NeedPostLoad | RF_NeedPostLoadSubobjects);
			}
			else if( !Ar.HasAnyPortFlags(PPF_DuplicateForPIE|PPF_Duplicate) )
			{
				UE_LOG(LogClass, Error, TEXT("CDO for class %s did not load!"), *GetPathName());
				ensure(ClassDefaultObject != NULL);
				ClassDefaultObject = GetDefaultObject();
				Ar.ForceBlueprintFinalization();
			}
		}
	}
}

bool UClass::ImplementsInterface( const class UClass* SomeInterface ) const
{
	if (SomeInterface != NULL && SomeInterface->HasAnyClassFlags(CLASS_Interface) && SomeInterface != UInterface::StaticClass())
	{
		for (const UClass* CurrentClass = this; CurrentClass; CurrentClass = CurrentClass->GetSuperClass())
		{
			// SomeInterface might be a base interface of our implemented interface
			for (TArray<FImplementedInterface>::TConstIterator It(CurrentClass->Interfaces); It; ++It)
			{
				const UClass* InterfaceClass = It->Class;
				if (InterfaceClass && InterfaceClass->IsChildOf(SomeInterface))
				{
					return true;
				}
			}
		}
	}

	return false;
}

/** serializes the passed in object as this class's default object using the given archive
 * @param Object the object to serialize as default
 * @param Ar the archive to serialize from
 */
void UClass::SerializeDefaultObject(UObject* Object, FStructuredArchive::FSlot Slot)
{
	// tell the archive that it's allowed to load data for transient properties
	FArchive& UnderlyingArchive = Slot.GetUnderlyingArchive();

	UnderlyingArchive.StartSerializingDefaults();

	if( ((UnderlyingArchive.IsLoading() || UnderlyingArchive.IsSaving()) && !UnderlyingArchive.WantBinaryPropertySerialization()) )
	{
	    // class default objects do not always have a vtable when saved
		// so use script serialization as opposed to native serialization to
	    // guarantee that all property data is loaded into the correct location
	    SerializeTaggedProperties(Slot, (uint8*)Object, GetSuperClass(), (uint8*)Object->GetArchetype());
	}
	else if (UnderlyingArchive.GetPortFlags() != 0 )
	{
		SerializeBinEx(Slot, Object, Object->GetArchetype(), GetSuperClass() );
	}
	else
	{
		SerializeBin(Slot, Object);
	}
	UnderlyingArchive.StopSerializingDefaults();
}

void UClass::SerializeSparseClassData(FStructuredArchive::FSlot Slot)
{
	if (!SparseClassDataStruct)
	{
		return;
	}

	// tell the archive that it's allowed to load data for transient properties
	FArchive& UnderlyingArchive = Slot.GetUnderlyingArchive();

	// make sure we always have sparse class a sparse class data struct to read from/write to
	GetOrCreateSparseClassData();

	if (((UnderlyingArchive.IsLoading() || UnderlyingArchive.IsSaving()) && !UnderlyingArchive.WantBinaryPropertySerialization()))
	{
		// class default objects do not always have a vtable when saved
		// so use script serialization as opposed to native serialization to
		// guarantee that all property data is loaded into the correct location
		SparseClassDataStruct->SerializeItem(Slot, SparseClassData, GetArchetypeForSparseClassData());
	}
	else if (UnderlyingArchive.GetPortFlags() != 0)
	{
		SparseClassDataStruct->SerializeBinEx(Slot, (uint8*)SparseClassData, SparseClassDataStruct, GetSparseClassDataArchetypeStruct());
	}
	else
	{
		SparseClassDataStruct->SerializeBin(Slot, (uint8*)SparseClassData);
	}
}


FArchive& operator<<(FArchive& Ar, FImplementedInterface& A)
{
	Ar << A.Class;
	Ar << A.PointerOffset;
	Ar << A.bImplementedByK2;

	return Ar;
}

void* UClass::GetArchetypeForSparseClassData() const
{
	UClass* SuperClass = GetSuperClass();
	return SuperClass ? SuperClass->GetOrCreateSparseClassData() : nullptr;
}

UScriptStruct* UClass::GetSparseClassDataArchetypeStruct() const
{
	UClass* SuperClass = GetSuperClass();
	return SuperClass ? SuperClass->GetSparseClassDataStruct() : nullptr;
}

UObject* UClass::GetArchetypeForCDO() const
{
	UClass* SuperClass = GetSuperClass();
	return SuperClass ? SuperClass->GetDefaultObject() : nullptr;
}

void UClass::PurgeClass(bool bRecompilingOnLoad)
{
	ClassConstructor = nullptr;
	ClassVTableHelperCtorCaller = nullptr;
	ClassFlags = CLASS_None;
	ClassCastFlags = CASTCLASS_None;
	ClassUnique = 0;
	ClassReps.Empty();
	NetFields.Empty();

#if WITH_EDITOR
	if (!bRecompilingOnLoad)
	{
		// this is not safe to do at COL time. The meta data is not loaded yet, so if we attempt to load it, we recursively load the package and that will fail
		RemoveMetaData("HideCategories");
		RemoveMetaData("ShowCategories");
		RemoveMetaData("HideFunctions");
		RemoveMetaData("AutoExpandCategories");
		RemoveMetaData("AutoCollapseCategories");
		RemoveMetaData("ClassGroupNames");
	}
#endif

	ClassDefaultObject = nullptr;

	Interfaces.Empty();
	NativeFunctionLookupTable.Empty();
	SetSuperStruct(nullptr);
	Children = nullptr;
	Script.Empty();
	MinAlignment = 0;
	RefLink = nullptr;
	PropertyLink = nullptr;
	DestructorLink = nullptr;
	ClassAddReferencedObjects = nullptr;

	ScriptAndPropertyObjectReferences.Empty();
	DeleteUnresolvedScriptProperties();

	FuncMap.Empty();
	ClearFunctionMapsCaches();
	PropertyLink = nullptr;

#if WITH_EDITORONLY_DATA
	{
		for (UPropertyWrapper* Wrapper : PropertyWrappers)
		{
			Wrapper->SetProperty(nullptr);
		}
		PropertyWrappers.Empty();
	}

	// When compiling properties can't be immediately destroyed because we need 
	// to fix up references to these properties. The caller of PurgeClass is 
	// expected to call DestroyPropertiesPendingDestruction
	FField* LastField = ChildProperties;
	if (LastField)
	{
		while (LastField->Next)
		{
			LastField = LastField->Next;
		}
		check(LastField->Next == nullptr);
		LastField->Next = PropertiesPendingDestruction;
		PropertiesPendingDestruction = ChildProperties;
		ChildProperties = nullptr;
	}
	// Update the serial number so that FFieldPaths that point to properties of this struct know they need to resolve themselves again
	FieldPathSerialNumber = GetNextFieldPathSerialNumber();
#else
	{
		// Destroy all properties owned by this struct
		DestroyPropertyLinkedList(ChildProperties);
	}
#endif // WITH_EDITORONLY_DATA

	DestroyUnversionedSchema(this);
}

#if WITH_EDITORONLY_DATA
void UClass::DestroyPropertiesPendingDestruction()
{
	DestroyPropertyLinkedList(PropertiesPendingDestruction);
}
#endif // WITH_EDITORONLY_DATA

UClass* UClass::FindCommonBase(UClass* InClassA, UClass* InClassB)
{
	check(InClassA);
	UClass* CommonClass = InClassA;
	while (InClassB && !InClassB->IsChildOf(CommonClass))
	{
		CommonClass = CommonClass->GetSuperClass();

		if( !CommonClass )
			break;
	}
	return CommonClass;
}

UClass* UClass::FindCommonBase(const TArray<UClass*>& InClasses)
{
	check(InClasses.Num() > 0);
	auto ClassIter = InClasses.CreateConstIterator();
	UClass* CommonClass = *ClassIter;
	ClassIter++;

	for (; ClassIter; ++ClassIter)
	{
		CommonClass = UClass::FindCommonBase(CommonClass, *ClassIter);
	}
	return CommonClass;
}

bool UClass::IsFunctionImplementedInScript(FName InFunctionName) const
{
	// Implemented in classes such as UBlueprintGeneratedClass
	return false;
}

bool UClass::HasProperty(FProperty* InProperty) const
{
	if (InProperty->GetOwner<UObject>())
	{
		UClass* PropertiesClass = InProperty->GetOwner<UClass>();
		if (PropertiesClass)
		{
			return IsChildOf(PropertiesClass);
		}
	}

	return false;
}


/*-----------------------------------------------------------------------------
	UClass constructors.
-----------------------------------------------------------------------------*/

/**
 * Internal constructor.
 */
UClass::UClass(const FObjectInitializer& ObjectInitializer)
:	UStruct( ObjectInitializer )
,	ClassUnique(0)
,	bCooked(false)
,	ClassFlags(CLASS_None)
,	ClassCastFlags(CASTCLASS_None)
,	ClassWithin( UObject::StaticClass() )
,	ClassGeneratedBy(nullptr)
#if WITH_EDITORONLY_DATA
,	PropertiesPendingDestruction(nullptr)
#endif
,	ClassDefaultObject(nullptr)
,	SparseClassData(nullptr)
,	SparseClassDataStruct(nullptr)
{
	// If you add properties here, please update the other constructors and PurgeClass()

	SetCppTypeInfoStatic(&DefaultCppClassTypeInfoStatic);
	TRACE_LOADTIME_CLASS_INFO(this, GetFName());
}

/**
 * Create a new UClass given its superclass.
 */
UClass::UClass(const FObjectInitializer& ObjectInitializer, UClass* InBaseClass)
:	UStruct(ObjectInitializer, InBaseClass)
,	ClassUnique(0)
,	bCooked(false)
,	ClassFlags(CLASS_None)
,	ClassCastFlags(CASTCLASS_None)
,	ClassWithin(UObject::StaticClass())
,	ClassGeneratedBy(nullptr)
#if WITH_EDITORONLY_DATA
,	PropertiesPendingDestruction(nullptr)
#endif
,	ClassDefaultObject(nullptr)
,	SparseClassData(nullptr)
,	SparseClassDataStruct(nullptr)
{
	// If you add properties here, please update the other constructors and PurgeClass()

	SetCppTypeInfoStatic(&DefaultCppClassTypeInfoStatic);

	UClass* ParentClass = GetSuperClass();
	if (ParentClass)
	{
		ClassWithin = ParentClass->ClassWithin;
		Bind();

		// if this is a native class, we may have defined a StaticConfigName() which overrides
		// the one from the parent class, so get our config name from there
		if (IsNative())
		{
			ClassConfigName = StaticConfigName();
		}
		else
		{
			// otherwise, inherit our parent class's config name
			ClassConfigName = ParentClass->ClassConfigName;
		}
	}
}

/**
 * Called when statically linked.
 */
UClass::UClass
(
	EStaticConstructor,
	FName			InName,
	uint32			InSize,
	uint32			InAlignment,
	EClassFlags		InClassFlags,
	EClassCastFlags	InClassCastFlags,
	const TCHAR*    InConfigName,
	EObjectFlags	InFlags,
	ClassConstructorType InClassConstructor,
	ClassVTableHelperCtorCallerType InClassVTableHelperCtorCaller,
	ClassAddReferencedObjectsType InClassAddReferencedObjects
)
:	UStruct					( EC_StaticConstructor, InSize, InAlignment, InFlags )
,	ClassConstructor		( InClassConstructor )
,	ClassVTableHelperCtorCaller(InClassVTableHelperCtorCaller)
,	ClassAddReferencedObjects( InClassAddReferencedObjects )
,	ClassUnique				( 0 )
,	bCooked					( false )
,	ClassFlags				( InClassFlags | CLASS_Native )
,	ClassCastFlags			( InClassCastFlags )
,	ClassWithin				( nullptr )
,	ClassGeneratedBy		( nullptr )
#if WITH_EDITORONLY_DATA
,	PropertiesPendingDestruction( nullptr )
#endif
,	ClassConfigName			()
,	NetFields				()
,	ClassDefaultObject		( nullptr )
,	SparseClassData			( nullptr )
,	SparseClassDataStruct	( nullptr )
{
	// If you add properties here, please update the other constructors and PurgeClass()

	SetCppTypeInfoStatic(&DefaultCppClassTypeInfoStatic);

	// We store the pointer to the ConfigName in an FName temporarily, this cast is intentional
	// as we expect the mis-typed data to get picked up in UClass::DeferredRegister. PVS-Studio
	// complains about this operation, but AFAIK it is safe (and we've been doing it a long time)
	// so the warning has been disabled for now:
	*(const TCHAR**)&ClassConfigName = InConfigName; //-V580
}

void* UClass::CreateSparseClassData()
{
	check(SparseClassData == nullptr);

	if (SparseClassDataStruct)
	{
		SparseClassData = FMemory::Malloc(SparseClassDataStruct->GetStructureSize(), SparseClassDataStruct->GetMinAlignment());
		SparseClassDataStruct->GetCppStructOps()->Construct(SparseClassData);
	}
	if (SparseClassData)
	{
		// initialize per class data from the archetype if we have one
		void* SparseArchetypeData = GetArchetypeForSparseClassData();
		UStruct* SparseClassDataArchetypeStruct = GetSparseClassDataArchetypeStruct();

		if (SparseArchetypeData)
		{
			for (FProperty* P = SparseClassDataArchetypeStruct->PropertyLink; P; P = P->PropertyLinkNext)
			{
				P->CopyCompleteValue_InContainer(SparseClassData, SparseArchetypeData);
			}
		}
	}

	return SparseClassData;
}

void UClass::CleanupSparseClassData()
{
	if (SparseClassData)
	{
		SparseClassDataStruct->GetCppStructOps()->Destruct(SparseClassData);
		FMemory::Free(SparseClassData);
		SparseClassData = nullptr;
	}
}

UScriptStruct* UClass::GetSparseClassDataStruct() const
{
	// this info is specified on the object via code generation so we use it instead of looking at the UClass
	return SparseClassDataStruct;
}

void UClass::SetSparseClassDataStruct(UScriptStruct* InSparseClassDataStruct)
{ 
	if (SparseClassDataStruct != InSparseClassDataStruct)
	{
		SparseClassDataStruct = InSparseClassDataStruct;

		// the old type and new type may not match when we do a hot reload so get rid of the old data
		CleanupSparseClassData();
	}
}

#if WITH_HOT_RELOAD

bool UClass::HotReloadPrivateStaticClass(
	uint32			InSize,
	EClassFlags		InClassFlags,
	EClassCastFlags	InClassCastFlags,
	const TCHAR*    InConfigName,
	ClassConstructorType InClassConstructor,
	ClassVTableHelperCtorCallerType InClassVTableHelperCtorCaller,
	ClassAddReferencedObjectsType InClassAddReferencedObjects,
	class UClass* TClass_Super_StaticClass,
	class UClass* TClass_WithinClass_StaticClass
	)
{
	if (InSize != PropertiesSize)
	{
		UClass::GetDefaultPropertiesFeedbackContext().Logf(ELogVerbosity::Warning, TEXT("Property size mismatch. Will not update class %s (was %d, new %d)."), *GetName(), PropertiesSize, InSize);
		return false;
	}
	//We could do this later, but might as well get it before we start corrupting the object
	UObject* CDO = GetDefaultObject();
	void* OldVTable = *(void**)CDO;


	//@todo safe? ClassFlags = InClassFlags | CLASS_Native;
	//@todo safe? ClassCastFlags = InClassCastFlags;
	//@todo safe? ClassConfigName = InConfigName;
	ClassConstructorType OldClassConstructor = ClassConstructor;
	ClassConstructor = InClassConstructor;
	ClassVTableHelperCtorCaller = InClassVTableHelperCtorCaller;
	ClassAddReferencedObjects = InClassAddReferencedObjects;
	/* No recursive ::StaticClass calls allowed. Setup extras. */
	/* @todo safe? 
	if (TClass_Super_StaticClass != this)
	{
		SetSuperStruct(TClass_Super_StaticClass);
	}
	else
	{
		SetSuperStruct(NULL);
	}
	ClassWithin = TClass_WithinClass_StaticClass;
	*/

	UE_LOG(LogClass, Verbose, TEXT("Attempting to change VTable for class %s."),*GetName());
	ClassWithin = UPackage::StaticClass();  // We are just avoiding error checks with this...we don't care about this temp object other than to get the vtable.

	static struct FUseVTableConstructorsCache
	{
		FUseVTableConstructorsCache()
		{
			bUseVTableConstructors = false;
			GConfig->GetBool(TEXT("Core.System"), TEXT("UseVTableConstructors"), bUseVTableConstructors, GEngineIni);
		}

		bool bUseVTableConstructors;
	} UseVTableConstructorsCache;

	UObject* TempObjectForVTable = nullptr;
	{
		TGuardValue<bool> Guard(GIsRetrievingVTablePtr, true);
		FVTableHelper Helper;
		TempObjectForVTable = ClassVTableHelperCtorCaller(Helper);
		TempObjectForVTable->AtomicallyClearInternalFlags(EInternalObjectFlags::PendingConstruction);
	}

	if( !TempObjectForVTable->IsRooted() )
	{
		TempObjectForVTable->MarkPendingKill();
	}
	else
	{
		UE_LOG(LogClass, Warning, TEXT("Hot Reload:  Was not expecting temporary object '%s' for class '%s' to become rooted during construction.  This object cannot be marked pending kill." ), *TempObjectForVTable->GetFName().ToString(), *this->GetName() );
	}

	ClassWithin = TClass_WithinClass_StaticClass;

	void* NewVTable = *(void**)TempObjectForVTable;
	if (NewVTable != OldVTable)
	{
		int32 Count = 0;
		int32 CountClass = 0;
		for ( FRawObjectIterator It; It; ++It )
		{
			UObject* Target = static_cast<UObject*>(It->Object);
			if (OldVTable == *(void**)Target)
			{
				*(void**)Target = NewVTable;
				Count++;
			}
			else if (dynamic_cast<UClass*>(Target))
			{
				UClass *Class = CastChecked<UClass>(Target);
				if (Class->ClassConstructor == OldClassConstructor)
				{
					Class->ClassConstructor = ClassConstructor;
					Class->ClassVTableHelperCtorCaller = ClassVTableHelperCtorCaller;
					Class->ClassAddReferencedObjects = ClassAddReferencedObjects;
					CountClass++;
				}
			}
		}
		UE_LOG(LogClass, Verbose, TEXT("Updated the vtable for %d live objects and %d blueprint classes.  %016llx -> %016llx"), Count, CountClass, PTRINT(OldVTable), PTRINT(NewVTable));
	}
	else
	{
		UE_LOG(LogClass, Error, TEXT("VTable for class %s did not change?"),*GetName());
	}

	return true;
}

bool UClass::ReplaceNativeFunction(FName InFName, FNativeFuncPtr InPointer, bool bAddToFunctionRemapTable)
{
	IHotReloadInterface* HotReloadSupport = nullptr;

	if(bAddToFunctionRemapTable)
	{
		HotReloadSupport = &FModuleManager::LoadModuleChecked<IHotReloadInterface>("HotReload");
	}

	// Find the function in the class's native function lookup table.
	for (int32 FunctionIndex = 0; FunctionIndex < NativeFunctionLookupTable.Num(); ++FunctionIndex)
	{
		FNativeFunctionLookup& NativeFunctionLookup = NativeFunctionLookupTable[FunctionIndex];
		if (NativeFunctionLookup.Name == InFName)
		{
			if (bAddToFunctionRemapTable)
			{
				HotReloadSupport->AddHotReloadFunctionRemap(InPointer, NativeFunctionLookup.Pointer);
			}
			NativeFunctionLookup.Pointer = InPointer;
			return true;
		}
	}
	return false;
}

#endif

UClass* UClass::GetAuthoritativeClass()
{
#if WITH_HOT_RELOAD && WITH_ENGINE
	if (GIsHotReload)
	{
		const TMap<UClass*, UClass*>& ReinstancedClasses = GetClassesToReinstanceForHotReload();
		if (UClass* const* FoundMapping = ReinstancedClasses.Find(this))
		{
			return *FoundMapping ? *FoundMapping : this;
		}
	}
#endif

	return this;
}

void UClass::AddNativeFunction(const ANSICHAR* InName, FNativeFuncPtr InPointer)
{
	FName InFName(InName);
#if WITH_HOT_RELOAD
	if (GIsHotReload)
	{
		// Find the function in the class's native function lookup table.
		if (ReplaceNativeFunction(InFName, InPointer, true))
		{
			return;
		}
		else
		{
			// function was not found, so it's new
			UE_LOG(LogClass, Log, TEXT("Function %s is new."), *InFName.ToString());
		}
	}
#endif
	new(NativeFunctionLookupTable) FNativeFunctionLookup(InFName,InPointer);
}

void UClass::AddNativeFunction(const WIDECHAR* InName, FNativeFuncPtr InPointer)
{
	FName InFName(InName);
#if WITH_HOT_RELOAD
	if (GIsHotReload)
	{
		// Find the function in the class's native function lookup table.
		if (ReplaceNativeFunction(InFName, InPointer, true))
		{
			return;
		}
		else
		{
			// function was not found, so it's new
			UE_LOG(LogClass, Log, TEXT("Function %s is new."), *InFName.ToString());
		}
	}
#endif
	new(NativeFunctionLookupTable)FNativeFunctionLookup(InFName, InPointer);
}

void UClass::CreateLinkAndAddChildFunctionsToMap(const FClassFunctionLinkInfo* Functions, uint32 NumFunctions)
{
	for (; NumFunctions; --NumFunctions, ++Functions)
	{
		const char* FuncNameUTF8 = Functions->FuncNameUTF8;
		UFunction*  Func         = Functions->CreateFuncPtr();

		Func->Next = Children;
		Children = Func;

		AddFunctionToFunctionMap(Func, FName(UTF8_TO_TCHAR(FuncNameUTF8)));
	}
}

void UClass::ClearFunctionMapsCaches()
{
	FRWScopeLock ScopeLock(SuperFuncMapLock, FRWScopeLockType::SLT_Write);
	SuperFuncMap.Empty();
}

UFunction* UClass::FindFunctionByName(FName InName, EIncludeSuperFlag::Type IncludeSuper) const
{
	LLM_SCOPE(ELLMTag::UObject);
	UFunction* Result = FuncMap.FindRef(InName);
	if (Result == nullptr && IncludeSuper == EIncludeSuperFlag::IncludeSuper)
	{
		UClass* SuperClass = GetSuperClass();
		if (SuperClass || Interfaces.Num() > 0)
		{
			bool bFoundInSuperFuncMap = false;
			{
				FRWScopeLock ScopeLock(SuperFuncMapLock, FRWScopeLockType::SLT_ReadOnly);
				if (UFunction** SuperResult = SuperFuncMap.Find(InName))
				{
					Result = *SuperResult;
					bFoundInSuperFuncMap = true;
				}
			}

			if (!bFoundInSuperFuncMap)
			{
				for (const FImplementedInterface& Inter : Interfaces)
				{
					Result = Inter.Class ? Inter.Class->FindFunctionByName(InName) : nullptr;
					if (Result)
					{
						break;
					}
				}

				if (SuperClass && Result == nullptr)
				{
					Result = SuperClass->FindFunctionByName(InName);
				}

				FRWScopeLock ScopeLock(SuperFuncMapLock, FRWScopeLockType::SLT_Write);
				SuperFuncMap.Add(InName, Result);
			}
		}
	}

	return Result;
}

void UClass::AssembleReferenceTokenStreams()
{
	SCOPED_BOOT_TIMING("AssembleReferenceTokenStreams (can be optimized)");
	// Iterate over all class objects and force the default objects to be created. Additionally also
	// assembles the token reference stream at this point. This is required for class objects that are
	// not taken into account for garbage collection but have instances that are.
	for (FRawObjectIterator It(false); It; ++It) // GetDefaultObject can create a new class, that need to be handled as well, so we cannot use TObjectIterator
	{
		if (UClass* Class = Cast<UClass>((UObject*)(It->Object)))
		{
			// Force the default object to be created (except when we're in the middle of exit purge -
			// this may happen if we exited PreInit early because of error).
			// 
			// Keep from handling script generated classes here, as those systems handle CDO 
			// instantiation themselves.
			if (!GExitPurge && !Class->HasAnyFlags(RF_BeingRegenerated))
			{
				Class->GetDefaultObject(); // Force the default object to be constructed if it isn't already
			}
			// Assemble reference token stream for garbage collection/ RTGC.
			if (!Class->HasAnyFlags(RF_ClassDefaultObject) && !Class->HasAnyClassFlags(CLASS_TokenStreamAssembled))
			{
				Class->AssembleReferenceTokenStream();
			}
		}
	}
}

const FString UClass::GetConfigName() const
{
	static FName NAME_GameplayTags("GameplayTags");

	if (ClassConfigName == NAME_Engine)
	{
		return GEngineIni;
	}
	else if( ClassConfigName == NAME_Editor )
	{
		return GEditorIni;
	}
	else if( ClassConfigName == NAME_Input )
	{
		return GInputIni;
	}
	else if( ClassConfigName == NAME_Game )
	{
		return GGameIni;
	}
	else if ( ClassConfigName == NAME_EditorSettings )
	{
		return GEditorSettingsIni;
	}
	else if ( ClassConfigName == NAME_EditorLayout )
	{
		return GEditorLayoutIni;
	}
	else if ( ClassConfigName == NAME_EditorKeyBindings )
	{
		return GEditorKeyBindingsIni;
	}
	else if( ClassConfigName == NAME_None )
	{
		UE_LOG(LogClass, Fatal,TEXT("UObject::GetConfigName() called on class with config name 'None'. Class flags = 0x%08X"), (uint32)ClassFlags );
		return TEXT("");
	}
	else if (ClassConfigName == NAME_GameUserSettings)
	{
		return GGameUserSettingsIni;
	}
	else if (ClassConfigName == NAME_GameplayTags)
	{
		return GGameplayTagsIni;
	}
	else
	{
		// generate the class ini name, and make sure it's up to date
		FString ConfigGameName;
		FConfigCacheIni::LoadGlobalIniFile(ConfigGameName, *ClassConfigName.ToString());
		return ConfigGameName;
	}
}

#if WITH_EDITOR || HACK_HEADER_GENERATOR
void UClass::GetHideFunctions(TArray<FString>& OutHideFunctions) const
{
	static const FName NAME_HideFunctions(TEXT("HideFunctions"));
	if (const FString* HideFunctions = FindMetaData(NAME_HideFunctions))
	{
		HideFunctions->ParseIntoArray(OutHideFunctions, TEXT(" "), true);
	}
}

bool UClass::IsFunctionHidden(const TCHAR* InFunction) const
{
	static const FName NAME_HideFunctions(TEXT("HideFunctions"));
	if (const FString* HideFunctions = FindMetaData(NAME_HideFunctions))
	{
		return !!FCString::StrfindDelim(**HideFunctions, InFunction, TEXT(" "));
	}
	return false;
}

void UClass::GetAutoExpandCategories(TArray<FString>& OutAutoExpandCategories) const
{
	static const FName NAME_AutoExpandCategories(TEXT("AutoExpandCategories"));
	if (const FString* AutoExpandCategories = FindMetaData(NAME_AutoExpandCategories))
	{
		AutoExpandCategories->ParseIntoArray(OutAutoExpandCategories, TEXT(" "), true);
	}
}

bool UClass::IsAutoExpandCategory(const TCHAR* InCategory) const
{
	static const FName NAME_AutoExpandCategories(TEXT("AutoExpandCategories"));
	if (const FString* AutoExpandCategories = FindMetaData(NAME_AutoExpandCategories))
	{
		return !!FCString::StrfindDelim(**AutoExpandCategories, InCategory, TEXT(" "));
	}
	return false;
}

void UClass::GetAutoCollapseCategories(TArray<FString>& OutAutoCollapseCategories) const
{
	static const FName NAME_AutoCollapseCategories(TEXT("AutoCollapseCategories"));
	if (const FString* AutoCollapseCategories = FindMetaData(NAME_AutoCollapseCategories))
	{
		AutoCollapseCategories->ParseIntoArray(OutAutoCollapseCategories, TEXT(" "), true);
	}
}

bool UClass::IsAutoCollapseCategory(const TCHAR* InCategory) const
{
	static const FName NAME_AutoCollapseCategories(TEXT("AutoCollapseCategories"));
	if (const FString* AutoCollapseCategories = FindMetaData(NAME_AutoCollapseCategories))
	{
		return !!FCString::StrfindDelim(**AutoCollapseCategories, InCategory, TEXT(" "));
	}
	return false;
}

void UClass::GetClassGroupNames(TArray<FString>& OutClassGroupNames) const
{
	static const FName NAME_ClassGroupNames(TEXT("ClassGroupNames"));
	if (const FString* ClassGroupNames = FindMetaData(NAME_ClassGroupNames))
	{
		ClassGroupNames->ParseIntoArray(OutClassGroupNames, TEXT(" "), true);
	}
}

bool UClass::IsClassGroupName(const TCHAR* InGroupName) const
{
	static const FName NAME_ClassGroupNames(TEXT("ClassGroupNames"));
	if (const FString* ClassGroupNames = FindMetaData(NAME_ClassGroupNames))
	{
		return !!FCString::StrfindDelim(**ClassGroupNames, InGroupName, TEXT(" "));
	}
	return false;
}

#endif // WITH_EDITOR || HACK_HEADER_GENERATOR


IMPLEMENT_CORE_INTRINSIC_CLASS(UClass, UStruct,
	{
		Class->ClassAddReferencedObjects = &UClass::AddReferencedObjects;

		Class->EmitObjectReference(STRUCT_OFFSET(UClass, ClassDefaultObject), TEXT("ClassDefaultObject"));
		Class->EmitObjectReference(STRUCT_OFFSET(UClass, ClassWithin), TEXT("ClassWithin"));
		Class->EmitObjectReference(STRUCT_OFFSET(UClass, ClassGeneratedBy), TEXT("ClassGeneratedBy"));
		Class->EmitObjectArrayReference(STRUCT_OFFSET(UClass, NetFields), TEXT("NetFields"));
	}
);

void GetPrivateStaticClassBody(
	const TCHAR* PackageName,
	const TCHAR* Name,
	UClass*& ReturnClass,
	void(*RegisterNativeFunc)(),
	uint32 InSize,
	uint32 InAlignment,
	EClassFlags InClassFlags,
	EClassCastFlags InClassCastFlags,
	const TCHAR* InConfigName,
	UClass::ClassConstructorType InClassConstructor,
	UClass::ClassVTableHelperCtorCallerType InClassVTableHelperCtorCaller,
	UClass::ClassAddReferencedObjectsType InClassAddReferencedObjects,
	UClass::StaticClassFunctionType InSuperClassFn,
	UClass::StaticClassFunctionType InWithinClassFn,
	bool bIsDynamic /*= false*/,
	UDynamicClass::DynamicClassInitializerType InDynamicClassInitializerFn /*= nullptr*/
	)
{
#if WITH_HOT_RELOAD
	if (GIsHotReload)
	{
		check(!bIsDynamic);
		UPackage* Package = FindPackage(NULL, PackageName);
		if (Package)
		{
			ReturnClass = FindObject<UClass>((UObject *)Package, Name);
			if (ReturnClass)
			{
				if (ReturnClass->HotReloadPrivateStaticClass(
					InSize,
					InClassFlags,
					InClassCastFlags,
					InConfigName,
					InClassConstructor,
					InClassVTableHelperCtorCaller,
					InClassAddReferencedObjects,
					InSuperClassFn(),
					InWithinClassFn()
					))
				{
					// Register the class's native functions.
					RegisterNativeFunc();
				}
				return;
			}
			else
			{
				UE_LOG(LogClass, Log, TEXT("Could not find existing class %s in package %s for HotReload, assuming new class"), Name, PackageName);
			}
		}
		else
		{
			UE_LOG(LogClass, Log, TEXT("Could not find existing package %s for HotReload of class %s, assuming a new package."), PackageName, Name);
		}
	}
#endif

	if (!bIsDynamic)
	{
		ReturnClass = (UClass*)GUObjectAllocator.AllocateUObject(sizeof(UClass), alignof(UClass), true);
		ReturnClass = ::new (ReturnClass)
			UClass
			(
			EC_StaticConstructor,
			Name,
			InSize,
			InAlignment,
			InClassFlags,
			InClassCastFlags,
			InConfigName,
			EObjectFlags(RF_Public | RF_Standalone | RF_Transient | RF_MarkAsNative | RF_MarkAsRootSet),
			InClassConstructor,
			InClassVTableHelperCtorCaller,
			InClassAddReferencedObjects
			);
		check(ReturnClass);
	}
	else
	{
		ReturnClass = (UClass*)GUObjectAllocator.AllocateUObject(sizeof(UDynamicClass), alignof(UDynamicClass), GIsInitialLoad);
		ReturnClass = ::new (ReturnClass)
			UDynamicClass
			(
			EC_StaticConstructor,
			Name,
			InSize,
			InAlignment,
			InClassFlags|CLASS_CompiledFromBlueprint,
			InClassCastFlags,
			InConfigName,
			EObjectFlags(RF_Public | RF_Standalone | RF_Transient | RF_Dynamic | (GIsInitialLoad ? RF_MarkAsRootSet : RF_NoFlags)),
			InClassConstructor,
			InClassVTableHelperCtorCaller,
			InClassAddReferencedObjects,
			InDynamicClassInitializerFn
			);
		check(ReturnClass);
	}
	InitializePrivateStaticClass(
		InSuperClassFn(),
		ReturnClass,
		InWithinClassFn(),
		PackageName,
		Name
		);

	// Register the class's native functions.
	RegisterNativeFunc();
}

/*-----------------------------------------------------------------------------
	UFunction.
-----------------------------------------------------------------------------*/

UFunction::UFunction(const FObjectInitializer& ObjectInitializer, UFunction* InSuperFunction, EFunctionFlags InFunctionFlags, SIZE_T ParamsSize )
: UStruct( ObjectInitializer, InSuperFunction, ParamsSize )
, FunctionFlags(InFunctionFlags)
, RPCId(0)
, RPCResponseId(0)
, FirstPropertyToInit(nullptr)
#if UE_BLUEPRINT_EVENTGRAPH_FASTCALLS
, EventGraphFunction(nullptr)
, EventGraphCallOffset(0)
#endif
{
}

UFunction::UFunction(UFunction* InSuperFunction, EFunctionFlags InFunctionFlags, SIZE_T ParamsSize)
	: UStruct(InSuperFunction, ParamsSize)
	, FunctionFlags(InFunctionFlags)
	, RPCId(0)
	, RPCResponseId(0)
	, FirstPropertyToInit(NULL)
{
}


void UFunction::InitializeDerivedMembers()
{
	NumParms = 0;
	ParmsSize = 0;
	ReturnValueOffset = MAX_uint16;

	for (FProperty* Property = CastField<FProperty>(ChildProperties); Property; Property = CastField<FProperty>(Property->Next))
	{
		if (Property->PropertyFlags & CPF_Parm)
		{
			NumParms++;
			ParmsSize = Property->GetOffset_ForUFunction() + Property->GetSize();
			if (Property->PropertyFlags & CPF_ReturnParm)
			{
				ReturnValueOffset = Property->GetOffset_ForUFunction();
			}
		}
		else if ((FunctionFlags & FUNC_HasDefaults) != 0)
		{
			if (!Property->HasAnyPropertyFlags(CPF_ZeroConstructor))
			{
				FirstPropertyToInit = Property;
				break;
			}
		}
		else
		{
			break;
		}
	}
}

void UFunction::Invoke(UObject* Obj, FFrame& Stack, RESULT_DECL)
{
	checkSlow(Func);

	UClass* OuterClass = (UClass*)GetOuter();
	if (OuterClass->IsChildOf(UInterface::StaticClass()))
	{
		Obj = (UObject*)Obj->GetInterfaceAddress(OuterClass);
	}

	TGuardValue<UFunction*> NativeFuncGuard(Stack.CurrentNativeFunction, this);
	return (*Func)(Obj, Stack, RESULT_PARAM);
}

void UFunction::Serialize( FArchive& Ar )
{
#if WITH_EDITOR
	const static FName NAME_UFunction(TEXT("UFunction"));
	FArchive::FScopeAddDebugData S(Ar, NAME_UFunction);
	FArchive::FScopeAddDebugData Q(Ar, GetFName());
#endif

	Super::Serialize( Ar );

	Ar.ThisContainsCode();

	Ar << FunctionFlags;

	// Replication info.
	if (FunctionFlags & FUNC_Net)
	{
		// Unused
		int16 RepOffset = 0;
		Ar << RepOffset;
	}

#if !UE_BLUEPRINT_EVENTGRAPH_FASTCALLS
	// We need to serialize these values even if the feature is disabled, in order to keep the serialization stream in sync
	UFunction* EventGraphFunction = nullptr;
	int32 EventGraphCallOffset = 0;
#endif

	if (Ar.UE4Ver() >= VER_UE4_SERIALIZE_BLUEPRINT_EVENTGRAPH_FASTCALLS_IN_UFUNCTION)
	{
		Ar << EventGraphFunction;
		Ar << EventGraphCallOffset;
	}

	// Precomputation.
	if ((Ar.GetPortFlags() & PPF_Duplicate) != 0)
	{
		Ar << NumParms;
		Ar << ParmsSize;
		Ar << ReturnValueOffset;
		Ar << FirstPropertyToInit;
	}
	else
	{
		if (Ar.IsLoading())
		{
			InitializeDerivedMembers();
		}
	}
}

void UFunction::PostLoad()
{
	Super::PostLoad();

	UClass* const OwningClass = GetOuterUClass();
	if (OwningClass && HasAnyFunctionFlags(FUNC_Net))
	{
		OwningClass->ClassFlags &= ~CLASS_ReplicationDataIsSetUp;
	}
}

FProperty* UFunction::GetReturnProperty() const
{
	for( TFieldIterator<FProperty> It(this); It && (It->PropertyFlags & CPF_Parm); ++It )
	{
		if( It->PropertyFlags & CPF_ReturnParm )
		{
			return *It;
		}
	}
	return NULL;
}

void UFunction::Bind()
{
	UClass* OwnerClass = GetOwnerClass();

	// if this isn't a native function, or this function belongs to a native interface class (which has no C++ version), 
	// use ProcessInternal (call into script VM only) as the function pointer for this function
	if (!HasAnyFunctionFlags(FUNC_Native))
	{
		// Use processing function.
		Func = &UObject::ProcessInternal;
	}
	else
	{
		// Find the function in the class's native function lookup table.
		FName Name = GetFName();
		FNativeFunctionLookup* Found = OwnerClass->NativeFunctionLookupTable.FindByPredicate([=](const FNativeFunctionLookup& NativeFunctionLookup){ return Name == NativeFunctionLookup.Name; });
		if (Found)
		{
			Func = Found->Pointer;
		}
#if USE_COMPILED_IN_NATIVES
		else if (!HasAnyFunctionFlags(FUNC_NetRequest))
		{
			UE_LOG(LogClass, Warning,TEXT("Failed to bind native function %s.%s"),*OwnerClass->GetName(),*GetName());
		}
#endif
	}
}

void UFunction::Link(FArchive& Ar, bool bRelinkExistingProperties)
{
	Super::Link(Ar, bRelinkExistingProperties);

	InitializeDerivedMembers();
}

bool UFunction::IsSignatureCompatibleWith(const UFunction* OtherFunction) const
{
	const uint64 IgnoreFlags = UFunction::GetDefaultIgnoredSignatureCompatibilityFlags();

	return IsSignatureCompatibleWith(OtherFunction, IgnoreFlags);
}

bool FStructUtils::ArePropertiesTheSame(const FProperty* A, const FProperty* B, bool bCheckPropertiesNames)
{
	if (A == B)
	{
		return true;
	}

	if (!A || !B) //one of properties is null
	{
		return false;
	}

	if (bCheckPropertiesNames && (A->GetFName() != B->GetFName()))
	{
		return false;
	}

	if (A->GetSize() != B->GetSize())
	{
		return false;
	}

	if (A->GetOffset_ForGC() != B->GetOffset_ForGC())
	{
		return false;
	}

	if (!A->SameType(B))
	{
		return false;
	}

	return true;
}

bool FStructUtils::TheSameLayout(const UStruct* StructA, const UStruct* StructB, bool bCheckPropertiesNames)
{
	bool bResult = false;
	if (StructA 
		&& StructB 
		&& (StructA->GetPropertiesSize() == StructB->GetPropertiesSize())
		&& (StructA->GetMinAlignment() == StructB->GetMinAlignment()))
	{
		const FProperty* PropertyA = StructA->PropertyLink;
		const FProperty* PropertyB = StructB->PropertyLink;

		bResult = true;
		while (bResult && (PropertyA != PropertyB))
		{
			bResult = ArePropertiesTheSame(PropertyA, PropertyB, bCheckPropertiesNames);
			PropertyA = PropertyA ? PropertyA->PropertyLinkNext : NULL;
			PropertyB = PropertyB ? PropertyB->PropertyLinkNext : NULL;
		}
	}
	return bResult;
}

UStruct* FStructUtils::FindStructureInPackageChecked(const TCHAR* StructName, const TCHAR* PackageName)
{
	const FName StructPackageFName(PackageName);
	if (StructPackageFName != NAME_None)
	{
		static TMap<FName, UPackage*> StaticStructPackageMap;

		UPackage* StructPackage;
		UPackage** StructPackagePtr = StaticStructPackageMap.Find(StructPackageFName);
		if (StructPackagePtr != nullptr)
		{
			StructPackage = *StructPackagePtr;
		}
		else
		{
			StructPackage = StaticStructPackageMap.Add(StructPackageFName, FindObjectChecked<UPackage>(nullptr, PackageName));
		}

		return FindObjectChecked<UStruct>(StructPackage, StructName);
	}
	else
	{
		return FindObjectChecked<UStruct>(ANY_PACKAGE, StructName);
	}
}

bool UFunction::IsSignatureCompatibleWith(const UFunction* OtherFunction, uint64 IgnoreFlags) const
{
	// Early out if they're exactly the same function
	if (this == OtherFunction)
	{
		return true;
	}

	// Run thru the parameter property chains to compare each property
	TFieldIterator<FProperty> IteratorA(this);
	TFieldIterator<FProperty> IteratorB(OtherFunction);

	while (IteratorA && (IteratorA->PropertyFlags & CPF_Parm))
	{
		if (IteratorB && (IteratorB->PropertyFlags & CPF_Parm))
		{
			// Compare the two properties to make sure their types are identical
			// Note: currently this requires both to be strictly identical and wouldn't allow functions that differ only by how derived a class is,
			// which might be desirable when binding delegates, assuming there is directionality in the SignatureIsCompatibleWith call
			FProperty* PropA = *IteratorA;
			FProperty* PropB = *IteratorB;

			// Check the flags as well
			const uint64 PropertyMash = PropA->PropertyFlags ^ PropB->PropertyFlags;
			if (!FStructUtils::ArePropertiesTheSame(PropA, PropB, false) || ((PropertyMash & ~IgnoreFlags) != 0))
			{
				// Type mismatch between an argument of A and B
				return false;
			}
		}
		else
		{
			// B ran out of arguments before A did
			return false;
		}
		++IteratorA;
		++IteratorB;
	}

	// They matched all the way thru A's properties, but it could still be a mismatch if B has remaining parameters
	return !(IteratorB && (IteratorB->PropertyFlags & CPF_Parm));
}

static UScriptStruct* StaticGetBaseStructureInternal(FName Name)
{
	static UPackage* CoreUObjectPkg = FindObjectChecked<UPackage>(nullptr, TEXT("/Script/CoreUObject"));

	UScriptStruct* Result = (UScriptStruct*)StaticFindObjectFastInternal(UScriptStruct::StaticClass(), CoreUObjectPkg, Name, false, false, RF_NoFlags, EInternalObjectFlags::None);

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	if (!Result)
	{
		UE_LOG(LogClass, Fatal, TEXT("Failed to find native struct '%s.%s'"), *CoreUObjectPkg->GetName(), *Name.ToString());
	}
#endif
	return Result;
}

UScriptStruct* TBaseStructure<FRotator>::Get()
{
	static UScriptStruct* ScriptStruct = StaticGetBaseStructureInternal(TEXT("Rotator"));
	return ScriptStruct;
}

UScriptStruct* TBaseStructure<FQuat>::Get()
{
	static UScriptStruct* ScriptStruct = StaticGetBaseStructureInternal(TEXT("Quat"));
	return ScriptStruct;
}

UScriptStruct* TBaseStructure<FTransform>::Get()
{
	static UScriptStruct* ScriptStruct = StaticGetBaseStructureInternal(TEXT("Transform"));
	return ScriptStruct;
}

UScriptStruct* TBaseStructure<FLinearColor>::Get()
{
	static UScriptStruct* ScriptStruct = StaticGetBaseStructureInternal(TEXT("LinearColor"));
	return ScriptStruct;
}

UScriptStruct* TBaseStructure<FColor>::Get()
{
	static UScriptStruct* ScriptStruct = StaticGetBaseStructureInternal(TEXT("Color"));
	return ScriptStruct;
}

UScriptStruct* TBaseStructure<FPlane>::Get()
{
	static UScriptStruct* ScriptStruct = StaticGetBaseStructureInternal(TEXT("Plane"));
	return ScriptStruct;
}

UScriptStruct* TBaseStructure<FVector>::Get()
{
	static UScriptStruct* ScriptStruct = StaticGetBaseStructureInternal(TEXT("Vector"));
	return ScriptStruct;
}

UScriptStruct* TBaseStructure<FVector2D>::Get()
{
	static UScriptStruct* ScriptStruct = StaticGetBaseStructureInternal(TEXT("Vector2D"));
	return ScriptStruct;
}

UScriptStruct* TBaseStructure<FVector4>::Get()
{
	static UScriptStruct* ScriptStruct = StaticGetBaseStructureInternal(TEXT("Vector4"));
	return ScriptStruct;
}

UScriptStruct* TBaseStructure<FRandomStream>::Get()
{
	static UScriptStruct* ScriptStruct = StaticGetBaseStructureInternal(TEXT("RandomStream"));
	return ScriptStruct;
}

UScriptStruct* TBaseStructure<FGuid>::Get()
{
	static UScriptStruct* ScriptStruct = StaticGetBaseStructureInternal(TEXT("Guid"));
	return ScriptStruct;
}

UScriptStruct* TBaseStructure<FBox2D>::Get()
{
	static UScriptStruct* ScriptStruct = StaticGetBaseStructureInternal(TEXT("Box2D"));
	return ScriptStruct;
}

UScriptStruct* TBaseStructure<FFallbackStruct>::Get()
{
	static UScriptStruct* ScriptStruct = StaticGetBaseStructureInternal(TEXT("FallbackStruct"));
	return ScriptStruct;
}

UScriptStruct* TBaseStructure<FFloatRangeBound>::Get()
{
	static UScriptStruct* ScriptStruct = StaticGetBaseStructureInternal(TEXT("FloatRangeBound"));
	return ScriptStruct;
}

UScriptStruct* TBaseStructure<FFloatRange>::Get()
{
	static UScriptStruct* ScriptStruct = StaticGetBaseStructureInternal(TEXT("FloatRange"));
	return ScriptStruct;
}

UScriptStruct* TBaseStructure<FInt32RangeBound>::Get()
{
	static UScriptStruct* ScriptStruct = StaticGetBaseStructureInternal(TEXT("Int32RangeBound"));
	return ScriptStruct;
}

UScriptStruct* TBaseStructure<FInt32Range>::Get()
{
	static UScriptStruct* ScriptStruct = StaticGetBaseStructureInternal(TEXT("Int32Range"));
	return ScriptStruct;
}

UScriptStruct* TBaseStructure<FFloatInterval>::Get()
{
	static UScriptStruct* ScriptStruct = StaticGetBaseStructureInternal(TEXT("FloatInterval"));
	return ScriptStruct;
}

UScriptStruct* TBaseStructure<FInt32Interval>::Get()
{
	static UScriptStruct* ScriptStruct = StaticGetBaseStructureInternal(TEXT("Int32Interval"));
	return ScriptStruct;
}

UScriptStruct* TBaseStructure<FSoftObjectPath>::Get()
{
	static UScriptStruct* ScriptStruct = StaticGetBaseStructureInternal(TEXT("SoftObjectPath"));
	return ScriptStruct;
}

UScriptStruct* TBaseStructure<FSoftClassPath>::Get()
{
	static UScriptStruct* ScriptStruct = StaticGetBaseStructureInternal(TEXT("SoftClassPath"));
	return ScriptStruct;
}

UScriptStruct* TBaseStructure<FPrimaryAssetType>::Get()
{
	static UScriptStruct* ScriptStruct = StaticGetBaseStructureInternal(TEXT("PrimaryAssetType"));
	return ScriptStruct;
}

UScriptStruct* TBaseStructure<FPrimaryAssetId>::Get()
{
	static UScriptStruct* ScriptStruct = StaticGetBaseStructureInternal(TEXT("PrimaryAssetId"));
	return ScriptStruct;
}

UScriptStruct* TBaseStructure<FPolyglotTextData>::Get()
{
	static UScriptStruct* ScriptStruct = StaticGetBaseStructureInternal(TEXT("PolyglotTextData"));
	return ScriptStruct;
}

UScriptStruct* TBaseStructure<FDateTime>::Get()
{
	static UScriptStruct* ScriptStruct = StaticGetBaseStructureInternal(TEXT("DateTime"));
	return ScriptStruct;
}

UScriptStruct* TBaseStructure<FFrameNumber>::Get()
{
	static UScriptStruct* ScriptStruct = StaticGetBaseStructureInternal(TEXT("FrameNumber"));
	return ScriptStruct;
}

UScriptStruct* TBaseStructure<FFrameTime>::Get()
{
	static UScriptStruct* ScriptStruct = StaticGetBaseStructureInternal(TEXT("FrameTime"));
	return ScriptStruct;
}

UScriptStruct* TBaseStructure<FAssetBundleData>::Get()
{
	static UScriptStruct* ScriptStruct = StaticGetBaseStructureInternal(TEXT("AssetBundleData"));
	return ScriptStruct;
}

UScriptStruct* TBaseStructure<FTestUninitializedScriptStructMembersTest>::Get()
{
	static UScriptStruct* ScriptStruct = StaticGetBaseStructureInternal(TEXT("TestUninitializedScriptStructMembersTest"));
	return ScriptStruct;
}

IMPLEMENT_CORE_INTRINSIC_CLASS(UFunction, UStruct,
	{
	}
);

UDelegateFunction::UDelegateFunction(const FObjectInitializer& ObjectInitializer, UFunction* InSuperFunction, EFunctionFlags InFunctionFlags, SIZE_T ParamsSize)
	: UFunction(ObjectInitializer, InSuperFunction, InFunctionFlags, ParamsSize)
{

}

UDelegateFunction::UDelegateFunction(UFunction* InSuperFunction, EFunctionFlags InFunctionFlags, SIZE_T ParamsSize)
	: UFunction(InSuperFunction, InFunctionFlags, ParamsSize)
{

}

IMPLEMENT_CORE_INTRINSIC_CLASS(UDelegateFunction, UFunction,
	{
	}
);

USparseDelegateFunction::USparseDelegateFunction(const FObjectInitializer& ObjectInitializer, UFunction* InSuperFunction, EFunctionFlags InFunctionFlags, SIZE_T ParamsSize)
	: UDelegateFunction(ObjectInitializer, InSuperFunction, InFunctionFlags, ParamsSize)
{

}

USparseDelegateFunction::USparseDelegateFunction(UFunction* InSuperFunction, EFunctionFlags InFunctionFlags, SIZE_T ParamsSize)
	: UDelegateFunction(InSuperFunction, InFunctionFlags, ParamsSize)
{

}

void USparseDelegateFunction::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar << OwningClassName;
	Ar << DelegateName;
}

IMPLEMENT_CORE_INTRINSIC_CLASS(USparseDelegateFunction, UDelegateFunction,
	{
	}
);

/*-----------------------------------------------------------------------------
UDynamicClass constructors.
-----------------------------------------------------------------------------*/

/**
* Internal constructor.
*/
UDynamicClass::UDynamicClass(const FObjectInitializer& ObjectInitializer)
: UClass(ObjectInitializer)
, AnimClassImplementation(nullptr)
{
	// If you add properties here, please update the other constructors and PurgeClass()
}

/**
* Create a new UDynamicClass given its superclass.
*/
UDynamicClass::UDynamicClass(const FObjectInitializer& ObjectInitializer, UClass* InBaseClass)
: UClass(ObjectInitializer, InBaseClass)
, AnimClassImplementation(nullptr)
{
}

/**
* Called when dynamically linked.
*/
UDynamicClass::UDynamicClass(
	EStaticConstructor,
	FName			InName,
	uint32			InSize,
	uint32			InAlignment,
	EClassFlags		InClassFlags,
	EClassCastFlags	InClassCastFlags,
	const TCHAR*    InConfigName,
	EObjectFlags	InFlags,
	ClassConstructorType InClassConstructor,
	ClassVTableHelperCtorCallerType InClassVTableHelperCtorCaller,
	ClassAddReferencedObjectsType InClassAddReferencedObjects,
	DynamicClassInitializerType InDynamicClassInitializer)
: UClass(
  EC_StaticConstructor
, InName
, InSize
, InAlignment
, InClassFlags
, InClassCastFlags
, InConfigName
, InFlags
, InClassConstructor
, InClassVTableHelperCtorCaller
, InClassAddReferencedObjects)
, AnimClassImplementation(nullptr)
, DynamicClassInitializer(InDynamicClassInitializer)
{
}

void UDynamicClass::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UDynamicClass* This = CastChecked<UDynamicClass>(InThis);

	Collector.AddReferencedObjects(This->MiscConvertedSubobjects, This);
	Collector.AddReferencedObjects(This->ReferencedConvertedFields, This);
	Collector.AddReferencedObjects(This->UsedAssets, This);
	Collector.AddReferencedObjects(This->DynamicBindingObjects, This);
	Collector.AddReferencedObjects(This->ComponentTemplates, This);
	Collector.AddReferencedObjects(This->Timelines, This);

	for (TPair<FName, UClass*>& Override : This->ComponentClassOverrides)
	{
		Collector.AddReferencedObject(Override.Value);
	}

	Collector.AddReferencedObject(This->AnimClassImplementation, This);

	Super::AddReferencedObjects(This, Collector);
}

UObject* UDynamicClass::CreateDefaultObject()
{
#if DO_CHECK
	if (!HasAnyFlags(RF_ClassDefaultObject) && (0 == (ClassFlags & CLASS_Constructed)))
	{
		UE_LOG(LogClass, Error, TEXT("CDO is created for a dynamic class, before the class was constructed. %s"), *GetPathName());
	}
#endif
	return Super::CreateDefaultObject();
}

void UDynamicClass::PurgeClass(bool bRecompilingOnLoad)
{
	Super::PurgeClass(bRecompilingOnLoad);

	MiscConvertedSubobjects.Empty();
	ReferencedConvertedFields.Empty();
	UsedAssets.Empty();

	DynamicBindingObjects.Empty();
	ComponentTemplates.Empty();
	Timelines.Empty();
	ComponentClassOverrides.Empty();

	AnimClassImplementation = nullptr;
}

UObject* UDynamicClass::FindArchetype(const UClass* ArchetypeClass, const FName ArchetypeName) const
{
	UObject* Archetype = static_cast<UObject*>(FindObjectWithOuter(this, ArchetypeClass, ArchetypeName));
	if (!Archetype)
	{
		// See UBlueprintGeneratedClass::FindArchetype, UE-35259, UE-37480
		const FName ArchetypeBaseName = FName(ArchetypeName, 0);
		if (ArchetypeBaseName != ArchetypeName)
		{
			UObject* const* FountComponentTemplate = ComponentTemplates.FindByPredicate([&](UObject* InObj) -> bool
			{ 
				return InObj && (InObj->GetFName() == ArchetypeBaseName) && InObj->IsA(ArchetypeClass);
			});
			Archetype = FountComponentTemplate ? *FountComponentTemplate : nullptr;
		}
	}
	const UClass* SuperClass = GetSuperClass();
	return Archetype ? Archetype :
		(SuperClass ? SuperClass->FindArchetype(ArchetypeClass, ArchetypeName) : nullptr);
}

void UDynamicClass::SetupObjectInitializer(FObjectInitializer& ObjectInitializer) const
{
	for (const TPair<FName, UClass*>& Override : ComponentClassOverrides)
	{
		ObjectInitializer.SetDefaultSubobjectClass(Override.Key, Override.Value);
	}

	GetSuperClass()->SetupObjectInitializer(ObjectInitializer);
}


FStructProperty* UDynamicClass::FindStructPropertyChecked(const TCHAR* PropertyName) const
{
	return FindFieldChecked<FStructProperty>(this, PropertyName);
}

const FString& UDynamicClass::GetTempPackagePrefix()
{
	static const FString PackagePrefix(TEXT("/Temp/__TEMP_BP__"));
	return PackagePrefix;
}

IMPLEMENT_CORE_INTRINSIC_CLASS(UDynamicClass, UClass,
{
	Class->ClassAddReferencedObjects = &UDynamicClass::AddReferencedObjects;
}
);

#if defined(_MSC_VER) && _MSC_VER == 1900
	#ifdef PRAGMA_ENABLE_SHADOW_VARIABLE_WARNINGS
		PRAGMA_ENABLE_SHADOW_VARIABLE_WARNINGS
	#endif
#endif

#include "UObject/DefineUPropertyMacros.h"
