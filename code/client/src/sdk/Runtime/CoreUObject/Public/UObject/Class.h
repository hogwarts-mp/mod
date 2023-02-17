// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	Class.h: UClass definition.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"

#include "Concepts/GetTypeHashable.h"
#include "Math/RandomStream.h"
#include "Misc/EnumClassFlags.h"
#include "Misc/FallbackStruct.h"
#include "Misc/Guid.h"
#include "Misc/Optional.h"
#include "Misc/ScopeRWLock.h"
#include "Templates/IsAbstract.h"
#include "Templates/IsEnum.h"
#include "Templates/Models.h"
#include "UObject/CoreNative.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ReflectedTypeAccessors.h"
#include "UObject/Script.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/FieldPath.h"

struct FCustomPropertyListNode;
struct FFrame;
struct FNetDeltaSerializeInfo;
struct FObjectInstancingGraph;
struct FPropertyTag;
class FField;
class UPropertyWrapper;

COREUOBJECT_API DECLARE_LOG_CATEGORY_EXTERN(LogClass, Log, All);
COREUOBJECT_API DECLARE_LOG_CATEGORY_EXTERN(LogScriptSerialization, Log, All);

/*-----------------------------------------------------------------------------
	FRepRecord.
-----------------------------------------------------------------------------*/

//
// Information about a property to replicate.
//
struct FRepRecord
{
	FProperty* Property;
	int32 Index;
	FRepRecord(FProperty* InProperty,int32 InIndex)
	: Property(InProperty), Index(InIndex)
	{}
};

/*-----------------------------------------------------------------------------
	UField.
-----------------------------------------------------------------------------*/

//
// Base class of reflection data objects.
//
class COREUOBJECT_API UField : public UObject
{
	DECLARE_CASTED_CLASS_INTRINSIC(UField, UObject, CLASS_Abstract, TEXT("/Script/CoreUObject"), CASTCLASS_UField)

	typedef UField BaseFieldClass;
	typedef UClass FieldTypeClass;

	/** Next Field in the linked list */
	UField*			Next;

	// Constructors.
	UField(EStaticConstructor, EObjectFlags InFlags);

	// UObject interface.
	virtual void Serialize( FArchive& Ar ) override;
	virtual void PostLoad() override;
	virtual bool NeedsLoadForClient() const override;
	virtual bool NeedsLoadForServer() const override;

	// UField interface.
	virtual void AddCppProperty(FProperty* Property);
	virtual void Bind();

	/** Goes up the outer chain to look for a UClass */
	UClass* GetOwnerClass() const;

	/** Goes up the outer chain to look for a UStruct */
	UStruct* GetOwnerStruct() const;

	/** 
	 * Returns a human readable string that was assigned to this field at creation. 
	 * By default this is the same as GetName() but it can be overridden if that is an internal-only name.
	 * This name is consistent in editor/cooked builds, is not localized, and is useful for data import/export.
	 */
	FString GetAuthoredName() const;

#if WITH_EDITORONLY_DATA
	/**
	 * Finds the localized display name or native display name as a fallback.
	 *
	 * @return The display name for this object.
	 */
	FText GetDisplayNameText() const;

	/**
	 * Finds the localized tooltip or native tooltip as a fallback.
	 *
	 * @param bShortTooltip Look for a shorter version of the tooltip (falls back to the long tooltip if none was specified)
	 *
	 * @return The tooltip for this object.
	 */
	FText GetToolTipText(bool bShortTooltip = false) const;

	/** 
	 * Formats a source comment into the form we want to show in the editor, is used by GetToolTipText and anything else that will get a native tooltip 
	 * 
	 * @param ToolTipString			String parsed out of C++ headers that is modified in place
	 * @param bRemoveExtraSections	If true, cut off the comment on first line separator or 2 empty lines in a row
	 */
	static void FormatNativeToolTip(FString& ToolTipString, bool bRemoveExtraSections = true);

	/**
	 * Determines if the property has any metadata associated with the key
	 * 
	 * @param Key The key to lookup in the metadata
	 * @return true if there is a (possibly blank) value associated with this key
	 */
	bool HasMetaData(const TCHAR* Key) const { return FindMetaData(Key) != nullptr; }
	bool HasMetaData(const FName& Key) const { return FindMetaData(Key) != nullptr; }

	/**
	 * Find the metadata value associated with the key
	 *
	 * @param Key The key to lookup in the metadata
	 * @return The value associated with the key if exists, null otherwise
	 */
	const FString* FindMetaData(const TCHAR* Key) const;
	const FString* FindMetaData(const FName& Key) const;

	/**
	 * Find the metadata value associated with the key
	 * 
	 * @param Key The key to lookup in the metadata
	 * @return The value associated with the key
	 */
	const FString& GetMetaData(const TCHAR* Key) const;
	const FString& GetMetaData(const FName& Key) const;

	/**
	 * Find the metadata value associated with the key and localization namespace and key
	 *
	 * @param Key						The key to lookup in the metadata
	 * @param LocalizationNamespace		Namespace to lookup in the localization manager
	 * @param LocalizationKey			Key to lookup in the localization manager
	 * @return							Localized metadata if available, defaults to whatever is provided via GetMetaData
	 */
	FText GetMetaDataText(const TCHAR* MetaDataKey, const FString LocalizationNamespace = FString(), const FString LocalizationKey = FString()) const;
	FText GetMetaDataText(const FName& MetaDataKey, const FString LocalizationNamespace = FString(), const FString LocalizationKey = FString()) const;

	/**
	 * Sets the metadata value associated with the key
	 * 
	 * @param Key The key to lookup in the metadata
	 * @return The value associated with the key
	 */
	void SetMetaData(const TCHAR* Key, const TCHAR* InValue);
	void SetMetaData(const FName& Key, const TCHAR* InValue);

	/**
	 * Find the metadata value associated with the key
	 * and return bool 
	 * @param Key The key to lookup in the metadata
	 * @return return true if the value was true (case insensitive)
	 */
	bool GetBoolMetaData(const TCHAR* Key) const
	{		
		const FString& BoolString = GetMetaData(Key);
		// FString == operator does case insensitive comparison
		return (BoolString == "true");
	}
	bool GetBoolMetaData(const FName& Key) const
	{		
		const FString& BoolString = GetMetaData(Key);
		// FString == operator does case insensitive comparison
		return (BoolString == "true");
	}

	/**
	 * Find the metadata value associated with the key
	 * and return int32 
	 * @param Key The key to lookup in the metadata
	 * @return the int value stored in the metadata. 0 if not a valid integer.
	 */
	int32 GetIntMetaData(const TCHAR* Key) const
	{
		const FString& IntString = GetMetaData(Key);
		int32 Value = FCString::Atoi(*IntString);
		return Value;
	}
	int32 GetIntMetaData(const FName& Key) const
	{
		const FString& IntString = GetMetaData(Key);
		int32 Value = FCString::Atoi(*IntString);
		return Value;
	}

	/**
	 * Find the metadata value associated with the key
	 * and return float
	 * @param Key The key to lookup in the metadata
	 * @return the float value stored in the metadata. 0 if not a valid float.
	 */
	float GetFloatMetaData(const TCHAR* Key) const
	{
		const FString& FloatString = GetMetaData(Key);
		float Value = FCString::Atof(*FloatString);
		return Value;
	}
	float GetFloatMetaData(const FName& Key) const
	{
		const FString& FloatString = GetMetaData(Key);
		float Value = FCString::Atof(*FloatString);
		return Value;
	}
	
	/**
	 * Find the metadata value associated with the key
	 * and return Class
	 * @param Key The key to lookup in the metadata
	 * @return the class value stored in the metadata.
	 */
	UClass* GetClassMetaData(const TCHAR* Key) const;
	UClass* GetClassMetaData(const FName& Key) const;

	/** Clear any metadata associated with the key */
	void RemoveMetaData(const TCHAR* Key);
	void RemoveMetaData(const FName& Key);
#endif // WITH_EDITORONLY_DATA

	bool HasAnyCastFlags(const uint64 InCastFlags) const;
	bool HasAllCastFlags(const uint64 InCastFlags) const;

#if WITH_EDITORONLY_DATA
	/**
	 * Gets the FField object associated with this Field
	 */
	virtual FField* GetAssociatedFField();
	/**
	 * Sets the FField object associated with this Field
	 */
	virtual void SetAssociatedFField(FField* InField);
#endif // WITH_EDITORONLY_DATA
};

#if USTRUCT_FAST_ISCHILDOF_IMPL == USTRUCT_ISCHILDOF_STRUCTARRAY
class FStructBaseChain
{
protected:
	COREUOBJECT_API FStructBaseChain();
	COREUOBJECT_API ~FStructBaseChain();

	// Non-copyable
	FStructBaseChain(const FStructBaseChain&) = delete;
	FStructBaseChain& operator=(const FStructBaseChain&) = delete;

	COREUOBJECT_API void ReinitializeBaseChainArray();

	FORCEINLINE bool IsChildOfUsingStructArray(const FStructBaseChain& Parent) const
	{
		int32 NumParentStructBasesInChainMinusOne = Parent.NumStructBasesInChainMinusOne;
		return NumParentStructBasesInChainMinusOne <= NumStructBasesInChainMinusOne && StructBaseChainArray[NumParentStructBasesInChainMinusOne] == &Parent;
	}

private:
	FStructBaseChain** StructBaseChainArray;
	int32 NumStructBasesInChainMinusOne;

	friend class UStruct;
};
#endif

/*-----------------------------------------------------------------------------
	UStruct.
-----------------------------------------------------------------------------*/

/**
 * Base class for all UObject types that contain fields.
 */
class COREUOBJECT_API UStruct : public UField
#if USTRUCT_FAST_ISCHILDOF_IMPL == USTRUCT_ISCHILDOF_STRUCTARRAY
	, private FStructBaseChain
#endif
{
	DECLARE_CASTED_CLASS_INTRINSIC(UStruct, UField, CLASS_MatchedSerializers, TEXT("/Script/CoreUObject"), CASTCLASS_UStruct)

	// Variables.
protected:
	friend struct Z_Construct_UClass_UStruct_Statics;
public:
	/** Struct this inherits from, may be null */
	UStruct* SuperStruct;
public:
	/** Pointer to start of linked list of child fields */
	UField* Children;
	
	/** Pointer to start of linked list of child fields */
	FField* ChildProperties;

	/** Total size of all UProperties, the allocated structure may be larger due to alignment */
	int32 PropertiesSize;
	/** Alignment of structure in memory, structure will be at least this large */
	int32 MinAlignment;
	
	/** Script bytecode associated with this object */
	TArray<uint8> Script;

	/** In memory only: Linked list of properties from most-derived to base */
	FProperty* PropertyLink;
	/** In memory only: Linked list of object reference properties from most-derived to base */
	FProperty* RefLink;
	/** In memory only: Linked list of properties requiring destruction. Note this does not include things that will be destroyed byt he native destructor */
	FProperty* DestructorLink;
	/** In memory only: Linked list of properties requiring post constructor initialization */
	FProperty* PostConstructLink;

	/** Array of object references embedded in script code and referenced by FProperties. Mirrored for easy access by realtime garbage collection code */
	TArray<UObject*> ScriptAndPropertyObjectReferences;

	typedef TArray<TPair<TFieldPath<FField>, int32>> FUnresolvedScriptPropertiesArray;
	/** Contains a list of script properties that couldn't be resolved at load time */
	FUnresolvedScriptPropertiesArray* UnresolvedScriptProperties;

#if WITH_EDITORONLY_DATA
	/** List of wrapper UObjects for FProperties */
	TArray<UPropertyWrapper*> PropertyWrappers;
	/** Unique id incremented each time this class properties get destroyed */
	int32 FieldPathSerialNumber;
#endif

	/** Cached schema for optimized unversioned property serialization, owned by this. */
	mutable const struct FUnversionedStructSchema* UnversionedSchema = nullptr;

public:
	// Constructors.
	UStruct( EStaticConstructor, int32 InSize, int32 InAlignment, EObjectFlags InFlags );
	explicit UStruct(UStruct* InSuperStruct, SIZE_T ParamsSize = 0, SIZE_T Alignment = 0);
	explicit UStruct(const FObjectInitializer& ObjectInitializer, UStruct* InSuperStruct, SIZE_T ParamsSize = 0, SIZE_T Alignment = 0 );
	virtual ~UStruct();

	// UObject interface.
	virtual void Serialize(FArchive& Ar) override;
	virtual void Serialize(FStructuredArchive::FRecord Record) override;
	virtual void PostLoad() override;
	virtual void FinishDestroy() override;
	virtual void RegisterDependencies() override;
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
	virtual void GetPreloadDependencies(TArray<UObject*>& OutDeps) override;
	virtual void TagSubobjects(EObjectFlags NewFlags) override;

	// UField interface.
	virtual void AddCppProperty(FProperty* Property) override;

	/** Searches property link chain for a property with the specified name */
	FProperty* FindPropertyByName(FName InName) const;

	/**
	 * Creates new copies of components
	 * 
	 * @param	Data						pointer to the address of the subobject referenced by this FProperty
	 * @param	DefaultData					pointer to the address of the default value of the subbject referenced by this FProperty
	 * @param	DefaultStruct				the struct corresponding to the buffer pointed to by DefaultData
	 * @param	Owner						the object that contains the component currently located at Data
	 * @param	InstanceGraph				contains the mappings of instanced objects and components to their templates
	 */
	void InstanceSubobjectTemplates( void* Data, void const* DefaultData, UStruct* DefaultStruct, UObject* Owner, FObjectInstancingGraph* InstanceGraph );

	/** Returns the structure used for inheritance, may be changed by child types */
	virtual UStruct* GetInheritanceSuper() const {return GetSuperStruct();}

	/** Static wrapper for Link, using a dummy archive */
	void StaticLink(bool bRelinkExistingProperties = false);

	/** Creates the field/property links and gets structure ready for use at runtime */
	virtual void Link(FArchive& Ar, bool bRelinkExistingProperties);

	/** Serializes struct properties, does not handle defaults*/
	virtual void SerializeBin(FArchive& Ar, void* Data) const final 
	{
		SerializeBin(FStructuredArchiveFromArchive(Ar).GetSlot(), Data);
	}

	/** Serializes struct properties, does not handle defaults */
	virtual void SerializeBin(FStructuredArchive::FSlot Slot, void* Data) const;

	/**
	 * Serializes the class properties that reside in Data if they differ from the corresponding values in DefaultData
	 *
	 * @param	Ar				the archive to use for serialization
	 * @param	Data			pointer to the location of the beginning of the property data
	 * @param	DefaultData		pointer to the location of the beginning of the data that should be compared against
	 * @param	DefaultStruct	the struct corresponding to the block of memory located at DefaultData 
	 */
	void SerializeBinEx( FStructuredArchive::FSlot Slot, void* Data, void const* DefaultData, UStruct* DefaultStruct ) const;

	/** Serializes list of properties, using property tags to handle mismatches */
	virtual void SerializeTaggedProperties(FArchive& Ar, uint8* Data, UStruct* DefaultsStruct, uint8* Defaults, const UObject* BreakRecursionIfFullyLoad = nullptr) const final 
	{
		SerializeTaggedProperties(FStructuredArchiveFromArchive(Ar).GetSlot(), Data, DefaultsStruct, Defaults, BreakRecursionIfFullyLoad);
	}

	/** Serializes list of properties, using property tags to handle mismatches */
	virtual void SerializeTaggedProperties(FStructuredArchive::FSlot Slot, uint8* Data, UStruct* DefaultsStruct, uint8* Defaults, const UObject* BreakRecursionIfFullyLoad = nullptr) const;

	/**
	 * Initialize a struct over uninitialized memory. This may be done by calling the native constructor or individually initializing properties
	 *
	 * @param	Dest		Pointer to memory to initialize
	 * @param	ArrayDim	Number of elements in the array
	 * @param	Stride		Stride of the array, If this default (0), then we will pull the size from the struct
	 */
	virtual void InitializeStruct(void* Dest, int32 ArrayDim = 1) const;
	
	/**
	 * Destroy a struct in memory. This may be done by calling the native destructor and then the constructor or individually reinitializing properties
	 *
	 * @param	Dest		Pointer to memory to destory
	 * @param	ArrayDim	Number of elements in the array
	 * @param	Stride		Stride of the array. If this default (0), then we will pull the size from the struct
	 */
	virtual void DestroyStruct(void* Dest, int32 ArrayDim = 1) const;

	/** Look up a property by an alternate name if it was not found in the first search, this is overridden for user structs */
	virtual FProperty* CustomFindProperty(const FName InName) const { return nullptr; };

	/** Serialize an expression to an archive. Returns expression token */
	virtual EExprToken SerializeExpr(int32& iCode, FArchive& Ar);

	/**
	 * Returns the struct/ class prefix used for the C++ declaration of this struct/ class.
	 *
	 * @return Prefix character used for C++ declaration of this struct/ class.
	 */
	virtual const TCHAR* GetPrefixCPP() const { return TEXT("F"); }

	/** Total size of all UProperties, the allocated structure may be larger due to alignment */
	FORCEINLINE int32 GetPropertiesSize() const
	{
		return PropertiesSize;
	}

	/** Alignment of structure in memory, structure will be at least this large */
	FORCEINLINE int32 GetMinAlignment() const
	{
		return MinAlignment;
	}

	/** Returns actual allocated size of structure in memory */
	FORCEINLINE int32 GetStructureSize() const
	{
		return Align(PropertiesSize,MinAlignment);
	}

	/** Modifies the property size after it's been recomputed */
	void SetPropertiesSize( int32 NewSize )
	{
		PropertiesSize = NewSize;
	}

	/** Returns true if this struct either is class T, or is a child of class T. This will not crash on null structs */
	template<class T>
	bool IsChildOf() const
	{
		return IsChildOf(T::StaticClass());
	}

	/** Returns true if this struct either is SomeBase, or is a child of SomeBase. This will not crash on null structs */
#if USTRUCT_FAST_ISCHILDOF_COMPARE_WITH_OUTERWALK || USTRUCT_FAST_ISCHILDOF_IMPL == USTRUCT_ISCHILDOF_OUTERWALK
	bool IsChildOf( const UStruct* SomeBase ) const;
#else
	bool IsChildOf(const UStruct* SomeBase) const
	{
		return (SomeBase ? IsChildOfUsingStructArray(*SomeBase) : false);
	}
#endif

	/** Struct this inherits from, may be null */
	UStruct* GetSuperStruct() const
	{
		return SuperStruct;
	}

	/**
	 * Sets the super struct pointer and updates hash information as necessary.
	 * Note that this is not sufficient to actually reparent a struct, it simply sets a pointer.
	 */
	virtual void SetSuperStruct(UStruct* NewSuperStruct);

	UE_DEPRECATED(4.23, "Replace with GetAuthoredNameForField or UField::GetAuthoredName")
	virtual FString PropertyNameToDisplayName(FName InName) const;

	/** Returns a human readable string for a given field, overridden for user defined structs */
	virtual FString GetAuthoredNameForField(const UField* Field) const;

	/** Returns a human readable string for a given field, overridden for user defined structs */
	virtual FString GetAuthoredNameForField(const FField* Field) const;

	/** If true, this class has been cleaned and sanitized (trashed) and should not be used */
	virtual bool IsStructTrashed() const
	{
		return false;
	}

	/** Destroys all properties owned by this struct */
	void DestroyChildPropertiesAndResetPropertyLinks();

#if WITH_EDITORONLY_DATA
	/** Try and find boolean metadata with the given key. If not found on this class, work up hierarchy looking for it. */
	bool GetBoolMetaDataHierarchical(const FName& Key) const;

	/** Try and find string metadata with the given key. If not found on this class, work up hierarchy looking for it. */
	bool GetStringMetaDataHierarchical(const FName& Key, FString* OutValue = nullptr) const;

	/**
	 * Determines if the struct or any of its super structs has any metadata associated with the provided key
	 *
	 * @param Key The key to lookup in the metadata
	 * @return pointer to the UStruct that has associated metadata, nullptr if Key is not associated with any UStruct in the hierarchy
	 */
	const UStruct* HasMetaDataHierarchical(const FName& Key) const;
#endif // WITH_EDITORONLY_DATA

#if HACK_HEADER_GENERATOR
	// Required by UHT makefiles for internal data serialization.
	friend struct FStructArchiveProxy;
#endif // HACK_HEADER_GENERATOR

	/** Sets the UnresolvedScriptProperties array */
	void SetUnresolvedScriptProperties(FUnresolvedScriptPropertiesArray& InUnresolvedProperties)
	{
		if (!UnresolvedScriptProperties)
		{
			UnresolvedScriptProperties = new FUnresolvedScriptPropertiesArray();
		}
		*UnresolvedScriptProperties = MoveTemp(InUnresolvedProperties);
	}

	/** Deletes the UnresolvedScriptProperties array */
	FORCEINLINE void DeleteUnresolvedScriptProperties()
	{
		if (UnresolvedScriptProperties)
		{
			delete UnresolvedScriptProperties;
			UnresolvedScriptProperties = nullptr;
		}
	}

	/**
	 * Collects UObjects referenced by bytecode
	 * @param OutReferencedObjects buffer to store the referenced objects in (not cleared by this function)
	 */
	void CollectBytecodeReferencedObjects(TArray<UObject*>& OutReferencedObjects);
	/**
	 * Collects UObjects referenced by properties
	 * @param OutReferencedObjects buffer to store the referenced objects in (not cleared by this function)
	 */
	void CollectPropertyReferencedObjects(TArray<UObject*>& OutReferencedObjects);
	/**
	 * Collects UObjects referenced by bytecode and properties for faster GC access
	 */
	void CollectBytecodeAndPropertyReferencedObjects();

protected:

	/** Returns the property name from the guid */
	virtual FName FindPropertyNameFromGuid(const FGuid& PropertyGuid) const { return NAME_None; }

	/** Find property guid */
	virtual FGuid FindPropertyGuidFromName(const FName InName) const { return FGuid(); }

	/** Returns if we have access to property guids */
	virtual bool ArePropertyGuidsAvailable() const { return false; }

	/** Serializes properties of this struct */
	void SerializeProperties(FArchive& Ar);

#if WITH_EDITORONLY_DATA
	void ConvertUFieldsToFFields();
#endif // WITH_EDITORONLY_DATA

	/** Serializes list of properties to a te, using property tags to handle mismatches */
	void LoadTaggedPropertiesFromText(FStructuredArchive::FSlot Slot, uint8* Data, UStruct* DefaultsStruct, uint8* Defaults, const UObject* BreakRecursionIfFullyLoad) const;

private:
#if USTRUCT_FAST_ISCHILDOF_IMPL == USTRUCT_ISCHILDOF_STRUCTARRAY
	// For UObjectBaseUtility
	friend class UObjectBaseUtility;
	using FStructBaseChain::IsChildOfUsingStructArray;
	using FStructBaseChain::ReinitializeBaseChainArray;

	friend class FStructBaseChain;
	friend class FBlueprintCompileReinstancer;
#endif

	void SerializeVersionedTaggedProperties(FStructuredArchive::FSlot Slot, uint8* Data, UStruct* DefaultsStruct, uint8* Defaults, const UObject* BreakRecursionIfFullyLoad) const;
};

enum EStructFlags
{
	// State flags.
	STRUCT_NoFlags				= 0x00000000,	
	STRUCT_Native				= 0x00000001,

	/** If set, this struct will be compared using native code */
	STRUCT_IdenticalNative		= 0x00000002,
	
	STRUCT_HasInstancedReference= 0x00000004,

	STRUCT_NoExport				= 0x00000008,

	/** Indicates that this struct should always be serialized as a single unit */
	STRUCT_Atomic				= 0x00000010,

	/** Indicates that this struct uses binary serialization; it is unsafe to add/remove members from this struct without incrementing the package version */
	STRUCT_Immutable			= 0x00000020,

	/** If set, native code needs to be run to find referenced objects */
	STRUCT_AddStructReferencedObjects = 0x00000040,

	/** Indicates that this struct should be exportable/importable at the DLL layer.  Base structs must also be exportable for this to work. */
	STRUCT_RequiredAPI			= 0x00000200,	

	/** If set, this struct will be serialized using the CPP net serializer */
	STRUCT_NetSerializeNative	= 0x00000400,	

	/** If set, this struct will be serialized using the CPP serializer */
	STRUCT_SerializeNative		= 0x00000800,	

	/** If set, this struct will be copied using the CPP operator= */
	STRUCT_CopyNative			= 0x00001000,	

	/** If set, this struct will be copied using memcpy */
	STRUCT_IsPlainOldData		= 0x00002000,	

	/** If set, this struct has no destructor and non will be called. STRUCT_IsPlainOldData implies STRUCT_NoDestructor */
	STRUCT_NoDestructor			= 0x00004000,	

	/** If set, this struct will not be constructed because it is assumed that memory is zero before construction. */
	STRUCT_ZeroConstructor		= 0x00008000,	

	/** If set, native code will be used to export text */
	STRUCT_ExportTextItemNative	= 0x00010000,	

	/** If set, native code will be used to export text */
	STRUCT_ImportTextItemNative	= 0x00020000,	

	/** If set, this struct will have PostSerialize called on it after CPP serializer or tagged property serialization is complete */
	STRUCT_PostSerializeNative  = 0x00040000,

	/** If set, this struct will have SerializeFromMismatchedTag called on it if a mismatched tag is encountered. */
	STRUCT_SerializeFromMismatchedTag = 0x00080000,

	/** If set, this struct will be serialized using the CPP net delta serializer */
	STRUCT_NetDeltaSerializeNative = 0x00100000,

	/** If set, this struct will be have PostScriptConstruct called on it after a temporary object is constructed in a running blueprint */
	STRUCT_PostScriptConstruct     = 0x00200000,

	/** If set, this struct can share net serialization state across connections */
	STRUCT_NetSharedSerialization = 0x00400000,

	/** If set, this struct has been cleaned and sanitized (trashed) and should not be used */
	STRUCT_Trashed = 0x00800000,

	/** Struct flags that are automatically inherited */
	STRUCT_Inherit				= STRUCT_HasInstancedReference|STRUCT_Atomic,

	/** Flags that are always computed, never loaded or done with code generation */
	STRUCT_ComputedFlags		= STRUCT_NetDeltaSerializeNative | STRUCT_NetSerializeNative | STRUCT_SerializeNative | STRUCT_PostSerializeNative | STRUCT_CopyNative | STRUCT_IsPlainOldData | STRUCT_NoDestructor | STRUCT_ZeroConstructor | STRUCT_IdenticalNative | STRUCT_AddStructReferencedObjects | STRUCT_ExportTextItemNative | STRUCT_ImportTextItemNative | STRUCT_SerializeFromMismatchedTag | STRUCT_PostScriptConstruct | STRUCT_NetSharedSerialization
};


/** type traits to cover the custom aspects of a script struct **/
template <class CPPSTRUCT>
struct TStructOpsTypeTraitsBase2
{
	enum
	{
		WithZeroConstructor            = false,                         // struct can be constructed as a valid object by filling its memory footprint with zeroes.
		WithNoInitConstructor          = false,                         // struct has a constructor which takes an EForceInit parameter which will force the constructor to perform initialization, where the default constructor performs 'uninitialization'.
		WithNoDestructor               = false,                         // struct will not have its destructor called when it is destroyed.
		WithCopy                       = !TIsPODType<CPPSTRUCT>::Value, // struct can be copied via its copy assignment operator.
		WithIdenticalViaEquality       = false,                         // struct can be compared via its operator==.  This should be mutually exclusive with WithIdentical.
		WithIdentical                  = false,                         // struct can be compared via an Identical(const T* Other, uint32 PortFlags) function.  This should be mutually exclusive with WithIdenticalViaEquality.
		WithExportTextItem             = false,                         // struct has an ExportTextItem function used to serialize its state into a string.
		WithImportTextItem             = false,                         // struct has an ImportTextItem function used to deserialize a string into an object of that class.
		WithAddStructReferencedObjects = false,                         // struct has an AddStructReferencedObjects function which allows it to add references to the garbage collector.
		WithSerializer                 = false,                         // struct has a Serialize function for serializing its state to an FArchive.
		WithStructuredSerializer       = false,                         // struct has a Serialize function for serializing its state to an FStructuredArchive.
		WithPostSerialize              = false,                         // struct has a PostSerialize function which is called after it is serialized
		WithNetSerializer              = false,                         // struct has a NetSerialize function for serializing its state to an FArchive used for network replication.
		WithNetDeltaSerializer         = false,                         // struct has a NetDeltaSerialize function for serializing differences in state from a previous NetSerialize operation.
		WithSerializeFromMismatchedTag = false,                         // struct has a SerializeFromMismatchedTag function for converting from other property tags.
		WithStructuredSerializeFromMismatchedTag = false,               // struct has an FStructuredArchive-based SerializeFromMismatchedTag function for converting from other property tags.
		WithPostScriptConstruct        = false,                         // struct has a PostScriptConstruct function which is called after it is constructed in blueprints
		WithNetSharedSerialization     = false,                         // struct has a NetSerialize function that does not require the package map to serialize its state.
		WithPureVirtual                = false,                         // struct has PURE_VIRTUAL functions and cannot be constructed when CHECK_PUREVIRTUALS is true
	};
};

template<class CPPSTRUCT>
struct TStructOpsTypeTraits : public TStructOpsTypeTraitsBase2<CPPSTRUCT>
{
};

#if CHECK_PUREVIRTUALS
#define DISABLE_ABSTRACT_CONSTRUCT TStructOpsTypeTraits<CPPSTRUCT>::WithPureVirtual
#else
#define DISABLE_ABSTRACT_CONSTRUCT (false && TStructOpsTypeTraits<CPPSTRUCT>::WithPureVirtual)
#endif


#if !PLATFORM_COMPILER_HAS_IF_CONSTEXPR

	/**
	 * Selection of constructor behavior.
	 */
	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<!DISABLE_ABSTRACT_CONSTRUCT && !TStructOpsTypeTraits<CPPSTRUCT>::WithNoInitConstructor>::Type ConstructWithNoInitOrNot(void* Data)
	{
		new (Data) CPPSTRUCT();
	}

	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<!DISABLE_ABSTRACT_CONSTRUCT && TStructOpsTypeTraits<CPPSTRUCT>::WithNoInitConstructor>::Type ConstructWithNoInitOrNot(void* Data)
	{
		new (Data) CPPSTRUCT(ForceInit);
	}

	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<DISABLE_ABSTRACT_CONSTRUCT>::Type ConstructWithNoInitOrNot(void* Data)
	{
	}

	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<!DISABLE_ABSTRACT_CONSTRUCT && !TStructOpsTypeTraits<CPPSTRUCT>::WithNoInitConstructor>::Type ConstructForTestsWithNoInitOrNot(void* Data)
	{
		new (Data) CPPSTRUCT;
	}

	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<!DISABLE_ABSTRACT_CONSTRUCT && TStructOpsTypeTraits<CPPSTRUCT>::WithNoInitConstructor>::Type ConstructForTestsWithNoInitOrNot(void* Data)
	{
		new (Data) CPPSTRUCT(ForceInit);
	}

	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<DISABLE_ABSTRACT_CONSTRUCT>::Type ConstructForTestsWithNoInitOrNot(void* Data)
	{
	}

	/**
	 * Selection of Serialize call.
	 */
	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<!TStructOpsTypeTraits<CPPSTRUCT>::WithSerializer, bool>::Type SerializeOrNot(FArchive& Ar, CPPSTRUCT *Data)
	{
		return false;
	}

	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<TStructOpsTypeTraits<CPPSTRUCT>::WithSerializer, bool>::Type SerializeOrNot(FArchive& Ar, CPPSTRUCT *Data)
	{
		return Data->Serialize(Ar);
	}

	/**
	* Selection of structured Serialize call.
	*/
	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<!TStructOpsTypeTraits<CPPSTRUCT>::WithStructuredSerializer, bool>::Type SerializeOrNot(FStructuredArchive::FSlot Slot, CPPSTRUCT *Data)
	{
		return false;
	}

	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<TStructOpsTypeTraits<CPPSTRUCT>::WithStructuredSerializer, bool>::Type SerializeOrNot(FStructuredArchive::FSlot Slot, CPPSTRUCT *Data)
	{
		return Data->Serialize(Slot);
	}


	/**
	 * Selection of PostSerialize call.
	 */
	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<!TStructOpsTypeTraits<CPPSTRUCT>::WithPostSerialize>::Type PostSerializeOrNot(const FArchive& Ar, CPPSTRUCT *Data)
	{
	}

	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<TStructOpsTypeTraits<CPPSTRUCT>::WithPostSerialize>::Type PostSerializeOrNot(const FArchive& Ar, CPPSTRUCT *Data)
	{
		Data->PostSerialize(Ar);
	}


	/**
	 * Selection of NetSerialize call.
	 */
	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<!TStructOpsTypeTraits<CPPSTRUCT>::WithNetSerializer, bool>::Type NetSerializeOrNot(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess, CPPSTRUCT *Data)
	{
		return false;
	}

	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<TStructOpsTypeTraits<CPPSTRUCT>::WithNetSerializer, bool>::Type NetSerializeOrNot(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess, CPPSTRUCT *Data)
	{
		return Data->NetSerialize(Ar, Map, bOutSuccess);
	}


	/**
	 * Selection of NetDeltaSerialize call.
	 */
	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<!TStructOpsTypeTraits<CPPSTRUCT>::WithNetDeltaSerializer, bool>::Type NetDeltaSerializeOrNot(FNetDeltaSerializeInfo & DeltaParms, CPPSTRUCT *Data)
	{
		return false;
	}

	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<TStructOpsTypeTraits<CPPSTRUCT>::WithNetDeltaSerializer, bool>::Type NetDeltaSerializeOrNot(FNetDeltaSerializeInfo & DeltaParms, CPPSTRUCT *Data)
	{
		return Data->NetDeltaSerialize(DeltaParms);
	}


	/**
	 * Selection of PostScriptConstruct call.
	 */
	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<!TStructOpsTypeTraits<CPPSTRUCT>::WithPostScriptConstruct>::Type PostScriptConstructOrNot(CPPSTRUCT *Data)
	{
	}

	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<TStructOpsTypeTraits<CPPSTRUCT>::WithPostScriptConstruct>::Type PostScriptConstructOrNot(CPPSTRUCT *Data)
	{
		Data->PostScriptConstruct();
	}


	/**
	 * Selection of Copy behavior.
	 */
	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<!TStructOpsTypeTraits<CPPSTRUCT>::WithCopy, bool>::Type CopyOrNot(CPPSTRUCT* Dest, CPPSTRUCT const* Src, int32 ArrayDim)
	{
		return false;
	}

	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<TStructOpsTypeTraits<CPPSTRUCT>::WithCopy, bool>::Type CopyOrNot(CPPSTRUCT* Dest, CPPSTRUCT const* Src, int32 ArrayDim)
	{
		static_assert((!TIsPODType<CPPSTRUCT>::Value), "You probably don't want custom copy for a POD type.");
		for (; ArrayDim; --ArrayDim)
		{
			*Dest++ = *Src++;
		}
		return true;
	}


	/**
	 * Selection of Identical check.
	 */
	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<TStructOpsTypeTraits<CPPSTRUCT>::WithIdentical && TStructOpsTypeTraits<CPPSTRUCT>::WithIdenticalViaEquality, bool>::Type IdenticalOrNot(const CPPSTRUCT* A, const CPPSTRUCT* B, uint32 PortFlags, bool& bOutResult)
	{
		static_assert(sizeof(CPPSTRUCT) == 0, "Should not have both WithIdenticalViaEquality and WithIdentical.");
	}

	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<!TStructOpsTypeTraits<CPPSTRUCT>::WithIdentical && !TStructOpsTypeTraits<CPPSTRUCT>::WithIdenticalViaEquality, bool>::Type IdenticalOrNot(const CPPSTRUCT* A, const CPPSTRUCT* B, uint32 PortFlags, bool& bOutResult)
	{
		bOutResult = false;
		return false;
	}

	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<TStructOpsTypeTraits<CPPSTRUCT>::WithIdentical && !TStructOpsTypeTraits<CPPSTRUCT>::WithIdenticalViaEquality, bool>::Type IdenticalOrNot(const CPPSTRUCT* A, const CPPSTRUCT* B, uint32 PortFlags, bool& bOutResult)
	{
		bOutResult = A->Identical(B, PortFlags);
		return true;
	}

	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<!TStructOpsTypeTraits<CPPSTRUCT>::WithIdentical && TStructOpsTypeTraits<CPPSTRUCT>::WithIdenticalViaEquality, bool>::Type IdenticalOrNot(const CPPSTRUCT* A, const CPPSTRUCT* B, uint32 PortFlags, bool& bOutResult)
	{
		bOutResult = (*A == *B);
		return true;
	}


	/**
	 * Selection of ExportTextItem call.
	 */
	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<!TStructOpsTypeTraits<CPPSTRUCT>::WithExportTextItem, bool>::Type ExportTextItemOrNot(FString& ValueStr, const CPPSTRUCT* PropertyValue, const CPPSTRUCT* DefaultValue, UObject* Parent, int32 PortFlags, UObject* ExportRootScope)
	{
		return false;
	}

	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<TStructOpsTypeTraits<CPPSTRUCT>::WithExportTextItem, bool>::Type ExportTextItemOrNot(FString& ValueStr, const CPPSTRUCT* PropertyValue, const CPPSTRUCT* DefaultValue, UObject* Parent, int32 PortFlags, UObject* ExportRootScope)
	{
		return PropertyValue->ExportTextItem(ValueStr, *DefaultValue, Parent, PortFlags, ExportRootScope);
	}


	/**
	 * Selection of ImportTextItem call.
	 */
	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<!TStructOpsTypeTraits<CPPSTRUCT>::WithImportTextItem, bool>::Type ImportTextItemOrNot(const TCHAR*& Buffer, CPPSTRUCT* Data, int32 PortFlags, UObject* OwnerObject, FOutputDevice* ErrorText)
	{
		return false;
	}

	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<TStructOpsTypeTraits<CPPSTRUCT>::WithImportTextItem, bool>::Type ImportTextItemOrNot(const TCHAR*& Buffer, CPPSTRUCT* Data, int32 PortFlags, UObject* OwnerObject, FOutputDevice* ErrorText)
	{
		return Data->ImportTextItem(Buffer, PortFlags, OwnerObject, ErrorText);
	}


	/**
	 * Selection of SerializeFromMismatchedTag call.
	 */
	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<!TStructOpsTypeTraits<CPPSTRUCT>::WithSerializeFromMismatchedTag, bool>::Type SerializeFromMismatchedTagOrNot(FPropertyTag const& Tag, FArchive& Ar, CPPSTRUCT *Data)
	{
		return false;
	}

	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<TStructOpsTypeTraits<CPPSTRUCT>::WithSerializeFromMismatchedTag, bool>::Type SerializeFromMismatchedTagOrNot(FPropertyTag const& Tag, FArchive& Ar, CPPSTRUCT *Data)
	{
		return Data->SerializeFromMismatchedTag(Tag, Ar);
	}

	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<!TStructOpsTypeTraits<CPPSTRUCT>::WithStructuredSerializeFromMismatchedTag, bool>::Type StructuredSerializeFromMismatchedTagOrNot(FPropertyTag const& Tag, FStructuredArchive::FSlot Slot, CPPSTRUCT *Data)
	{
		return false;
	}

	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<TStructOpsTypeTraits<CPPSTRUCT>::WithStructuredSerializeFromMismatchedTag, bool>::Type StructuredSerializeFromMismatchedTagOrNot(FPropertyTag const& Tag, FStructuredArchive::FSlot Slot, CPPSTRUCT *Data)
	{
		return Data->SerializeFromMismatchedTag(Tag, Slot);
	}


	/**
	 * Selection of GetTypeHash call.
	 */
	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<!TModels<CGetTypeHashable, CPPSTRUCT>::Value, uint32>::Type GetTypeHashOrNot(const CPPSTRUCT *Data)
	{
		return 0;
	}

	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<TModels<CGetTypeHashable, CPPSTRUCT>::Value, uint32>::Type GetTypeHashOrNot(const CPPSTRUCT *Data)
	{
		return GetTypeHash(*Data);
	}

#endif

#if PLATFORM_COMPILER_HAS_IF_CONSTEXPR

	/**
	 * Selection of AddStructReferencedObjects check.
	 */
	template<class CPPSTRUCT>
	FORCEINLINE void AddStructReferencedObjectsOrNot(void* A, FReferenceCollector& Collector)
	{
		if constexpr (TStructOpsTypeTraits<CPPSTRUCT>::WithAddStructReferencedObjects)
		{
			((CPPSTRUCT*)A)->AddStructReferencedObjects(Collector);
		}
	}

#else

	/**
	 * Selection of AddStructReferencedObjects check.
	 */
	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<!TStructOpsTypeTraits<CPPSTRUCT>::WithAddStructReferencedObjects>::Type AddStructReferencedObjectsOrNot(void* A, FReferenceCollector& Collector)
	{
	}

	template<class CPPSTRUCT>
	FORCEINLINE typename TEnableIf<TStructOpsTypeTraits<CPPSTRUCT>::WithAddStructReferencedObjects>::Type AddStructReferencedObjectsOrNot(void* A, FReferenceCollector& Collector)
	{
		((CPPSTRUCT*)A)->AddStructReferencedObjects(Collector);
	}

#endif

/**
 * Reflection data for a standalone structure declared in a header or as a user defined struct
 */
class UScriptStruct : public UStruct
{
public:
	/** Interface to template to manage dynamic access to C++ struct construction and destruction **/
	struct COREUOBJECT_API ICppStructOps
	{
		/**
		 * Constructor
		 * @param InSize: sizeof() of the structure
		 */
		ICppStructOps(int32 InSize, int32 InAlignment)
			: Size(InSize)
			, Alignment(InAlignment)
		{
		}
		virtual ~ICppStructOps() {}
		/** return true if this class has a no-op constructor and takes EForceInit to init **/
		virtual bool HasNoopConstructor() = 0;
		/** return true if memset can be used instead of the constructor **/
		virtual bool HasZeroConstructor() = 0;
		/** Call the C++ constructor **/
		virtual void Construct(void *Dest) = 0;
		/** Call the C++ constructor without value-init (new T instead of new T()) **/
		virtual void ConstructForTests(void* Dest) = 0;
		/** return false if this destructor can be skipped **/
		virtual bool HasDestructor() = 0;
		/** Call the C++ destructor **/
		virtual void Destruct(void *Dest) = 0;
		/** return the sizeof() of this structure **/
		FORCEINLINE int32 GetSize()
		{
			return Size;
		}
		/** return the alignof() of this structure **/
		FORCEINLINE int32 GetAlignment()
		{
			return Alignment;
		}

		/** return true if this class can serialize **/
		virtual bool HasSerializer() = 0;
		/** return true if this class can serialize to a structured archive**/
		virtual bool HasStructuredSerializer() = 0;
		/** 
		 * Serialize this structure 
		 * @return true if the package is new enough to support this, if false, it will fall back to ordinary script struct serialization
		 */
		virtual bool Serialize(FArchive& Ar, void *Data) = 0;
		virtual bool Serialize(FStructuredArchive::FSlot Slot, void *Data) = 0;

		/** return true if this class implements a post serialize call **/
		virtual bool HasPostSerialize() = 0;
		/** Call PostSerialize on this structure */
		virtual void PostSerialize(const FArchive& Ar, void *Data) = 0;

		/** return true if this struct can net serialize **/
		virtual bool HasNetSerializer() = 0;
		
		/** return true if this can share net serialization across connections */
		virtual bool HasNetSharedSerialization() = 0;
		/** 
		 * Net serialize this structure 
		 * @return true if the struct was serialized, otherwise it will fall back to ordinary script struct net serialization
		 */
		virtual bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess, void *Data) = 0;

		/** return true if this struct can net delta serialize delta (serialize a network delta from a base state) **/
		virtual bool HasNetDeltaSerializer() = 0;
		/** 
		 * Net serialize delta this structure. Serialize a network delta from a base state
		 * @return true if the struct was serialized, otherwise it will fall back to ordinary script struct net delta serialization
		 */
		virtual bool NetDeltaSerialize(FNetDeltaSerializeInfo & DeltaParms, void *Data) = 0;

		/** return true if this class implements a post script construct call **/
		virtual bool HasPostScriptConstruct() = 0;
		/** Call PostScriptConstruct on this structure */
		virtual void PostScriptConstruct(void *Data) = 0;

		/** return true if this struct should be memcopied **/
		virtual bool IsPlainOldData() = 0;

		/** return true if this struct can copy **/
		virtual bool HasCopy() = 0;
		/** 
		 * Copy this structure 
		 * @return true if the copy was handled, otherwise it will fall back to CopySingleValue
		 */
		virtual bool Copy(void* Dest, void const* Src, int32 ArrayDim) = 0;

		/** return true if this struct can compare **/
		virtual bool HasIdentical() = 0;
		/** 
		 * Compare this structure 
		 * @return true if the copy was handled, otherwise it will fall back to FStructProperty::Identical
		 */
		virtual bool Identical(const void* A, const void* B, uint32 PortFlags, bool& bOutResult) = 0;

		/** return true if this struct can export **/
		virtual bool HasExportTextItem() = 0;
		/** 
		 * export this structure 
		 * @return true if the copy was exported, otherwise it will fall back to FStructProperty::ExportTextItem
		 */
		virtual bool ExportTextItem(FString& ValueStr, const void* PropertyValue, const void* DefaultValue, class UObject* Parent, int32 PortFlags, class UObject* ExportRootScope) = 0;

		/** return true if this struct can import **/
		virtual bool HasImportTextItem() = 0;
		/** 
		 * import this structure 
		 * @return true if the copy was imported, otherwise it will fall back to FStructProperty::ImportText
		 */
		virtual bool ImportTextItem(const TCHAR*& Buffer, void* Data, int32 PortFlags, class UObject* OwnerObject, FOutputDevice* ErrorText) = 0;

		/** return true if this struct has custom GC code **/
		virtual bool HasAddStructReferencedObjects() = 0;
		/** 
		 * return a pointer to a function that can add referenced objects
		 * @return true if the copy was imported, otherwise it will fall back to FStructProperty::ImportText
		 */
		typedef void (*TPointerToAddStructReferencedObjects)(void* A, class FReferenceCollector& Collector);
		virtual TPointerToAddStructReferencedObjects AddStructReferencedObjects() = 0;

		/** return true if this class wants to serialize from some other tag (usually for conversion purposes) **/
		virtual bool HasSerializeFromMismatchedTag() = 0;
		virtual bool HasStructuredSerializeFromMismatchedTag() = 0;

		/** 
		 * Serialize this structure, from some other tag
		 * @return true if this succeeded, false will trigger a warning and not serialize at all
		 */
		virtual bool SerializeFromMismatchedTag(struct FPropertyTag const& Tag, FArchive& Ar, void *Data) = 0;
		virtual bool StructuredSerializeFromMismatchedTag(struct FPropertyTag const& Tag, FStructuredArchive::FSlot Slot, void *Data) = 0;

		/** return true if this struct has a GetTypeHash */
		virtual bool HasGetTypeHash() = 0;

		/** Calls GetTypeHash if enabled */
		virtual uint32 GetStructTypeHash(const void* Src) = 0;

		/** Returns property flag values that can be computed at compile time */
		virtual EPropertyFlags GetComputedPropertyFlags() const = 0;

		/** return true if this struct is abstract **/
		virtual bool IsAbstract() const = 0;
	private:
		/** sizeof() of the structure **/
		const int32 Size;
		/** alignof() of the structure **/
		const int32 Alignment;
	};


	/** Template to manage dynamic access to C++ struct construction and destruction **/
	template<class CPPSTRUCT>
	struct TCppStructOps : public ICppStructOps
	{
		typedef TStructOpsTypeTraits<CPPSTRUCT> TTraits;
		TCppStructOps()
			: ICppStructOps(sizeof(CPPSTRUCT), alignof(CPPSTRUCT))
		{
		}
		virtual bool HasNoopConstructor() override
		{
			return TTraits::WithNoInitConstructor;
		}		
		virtual bool HasZeroConstructor() override
		{
			return TTraits::WithZeroConstructor;
		}
		virtual void Construct(void* Dest) override
		{
			check(!TTraits::WithZeroConstructor); // don't call this if we have indicated it is not necessary
			// that could have been an if statement, but we might as well force optimization above the virtual call
			// could also not attempt to call the constructor for types where this is not possible, but I didn't do that here
#if PLATFORM_COMPILER_HAS_IF_CONSTEXPR
#if CHECK_PUREVIRTUALS
			if constexpr (!TStructOpsTypeTraits<CPPSTRUCT>::WithPureVirtual)
#endif
			{
				if constexpr (TStructOpsTypeTraits<CPPSTRUCT>::WithNoInitConstructor)
				{
					new (Dest) CPPSTRUCT(ForceInit);
				}
				else
				{
					new (Dest) CPPSTRUCT();
				}
			}
#else
			ConstructWithNoInitOrNot<CPPSTRUCT>(Dest);
#endif
		}
		virtual void ConstructForTests(void* Dest) override
		{
			check(!TTraits::WithZeroConstructor); // don't call this if we have indicated it is not necessary
			// that could have been an if statement, but we might as well force optimization above the virtual call
			// could also not attempt to call the constructor for types where this is not possible, but I didn't do that here
#if PLATFORM_COMPILER_HAS_IF_CONSTEXPR
#if CHECK_PUREVIRTUALS
			if constexpr (!TStructOpsTypeTraits<CPPSTRUCT>::WithPureVirtual)
#endif
			{
				if constexpr (TStructOpsTypeTraits<CPPSTRUCT>::WithNoInitConstructor)
				{
					new (Dest) CPPSTRUCT(ForceInit);
				}
				else
				{
					new (Dest) CPPSTRUCT;
				}
			}
#else
			ConstructForTestsWithNoInitOrNot<CPPSTRUCT>(Dest);
#endif
		}
		virtual bool HasDestructor() override
		{
			return !(TTraits::WithNoDestructor || TIsPODType<CPPSTRUCT>::Value);
		}
		virtual void Destruct(void *Dest) override
		{
			check(!(TTraits::WithNoDestructor || TIsPODType<CPPSTRUCT>::Value)); // don't call this if we have indicated it is not necessary
			// that could have been an if statement, but we might as well force optimization above the virtual call
			// could also not attempt to call the destructor for types where this is not possible, but I didn't do that here
			((CPPSTRUCT*)Dest)->~CPPSTRUCT();
		}
		virtual bool HasSerializer() override
		{
			return TTraits::WithSerializer;
		}
		virtual bool HasStructuredSerializer() override
		{
			return TTraits::WithStructuredSerializer;
		}
		virtual bool Serialize(FArchive& Ar, void *Data) override
		{
			check(TTraits::WithSerializer); // don't call this if we have indicated it is not necessary
#if PLATFORM_COMPILER_HAS_IF_CONSTEXPR
			if constexpr (TStructOpsTypeTraits<CPPSTRUCT>::WithSerializer)
			{
				return ((CPPSTRUCT*)Data)->Serialize(Ar);
			}
			else
			{
				return false;
			}
#else
			return SerializeOrNot(Ar, (CPPSTRUCT*)Data);
#endif
		}
		virtual bool Serialize(FStructuredArchive::FSlot Slot, void *Data) override
		{
			check(TTraits::WithStructuredSerializer); // don't call this if we have indicated it is not necessary
#if PLATFORM_COMPILER_HAS_IF_CONSTEXPR
			if constexpr (TStructOpsTypeTraits<CPPSTRUCT>::WithStructuredSerializer)
			{
				return ((CPPSTRUCT*)Data)->Serialize(Slot);
			}
			else
			{
				return false;
			}
#else
			return SerializeOrNot(Slot, (CPPSTRUCT*)Data);
#endif
			}
		virtual bool HasPostSerialize() override
		{
			return TTraits::WithPostSerialize;
		}
		virtual void PostSerialize(const FArchive& Ar, void *Data) override
		{
			check(TTraits::WithPostSerialize); // don't call this if we have indicated it is not necessary
#if PLATFORM_COMPILER_HAS_IF_CONSTEXPR
			if constexpr (TStructOpsTypeTraits<CPPSTRUCT>::WithPostSerialize)
			{
				((CPPSTRUCT*)Data)->PostSerialize(Ar);
			}
#else
			PostSerializeOrNot(Ar, (CPPSTRUCT*)Data);
#endif
		}
		virtual bool HasNetSerializer() override
		{
			return TTraits::WithNetSerializer;
		}
		virtual bool HasNetSharedSerialization() override
		{
			return TTraits::WithNetSharedSerialization;
		}
		virtual bool HasNetDeltaSerializer() override
		{
			return TTraits::WithNetDeltaSerializer;
		}
		virtual bool NetSerialize(FArchive& Ar, class UPackageMap* Map, bool& bOutSuccess, void *Data) override
		{
#if PLATFORM_COMPILER_HAS_IF_CONSTEXPR
			if constexpr (TStructOpsTypeTraits<CPPSTRUCT>::WithNetSerializer)
			{
				return ((CPPSTRUCT*)Data)->NetSerialize(Ar, Map, bOutSuccess);
			}
			else
			{
				return false;
			}
#else
			return NetSerializeOrNot(Ar, Map, bOutSuccess, (CPPSTRUCT*)Data);
#endif
		}
		virtual bool NetDeltaSerialize(FNetDeltaSerializeInfo & DeltaParms, void *Data) override
		{
#if PLATFORM_COMPILER_HAS_IF_CONSTEXPR
			if constexpr (TStructOpsTypeTraits<CPPSTRUCT>::WithNetDeltaSerializer)
			{
				return ((CPPSTRUCT*)Data)->NetDeltaSerialize(DeltaParms);
			}
			else
			{
				return false;
			}
#else
			return NetDeltaSerializeOrNot(DeltaParms, (CPPSTRUCT*)Data);
#endif
		}
		virtual bool HasPostScriptConstruct() override
		{
			return TTraits::WithPostScriptConstruct;
		}
		virtual void PostScriptConstruct(void *Data) override
		{
			check(TTraits::WithPostScriptConstruct); // don't call this if we have indicated it is not necessary
#if PLATFORM_COMPILER_HAS_IF_CONSTEXPR
			if constexpr (TStructOpsTypeTraits<CPPSTRUCT>::WithPostScriptConstruct)
			{
				((CPPSTRUCT*)Data)->PostScriptConstruct();
			}
#else
			PostScriptConstructOrNot((CPPSTRUCT*)Data);
#endif
		}
		virtual bool IsPlainOldData() override
		{
			return TIsPODType<CPPSTRUCT>::Value;
		}
		virtual bool HasCopy() override
		{
			return TTraits::WithCopy;
		}
		virtual bool Copy(void* Dest, void const* Src, int32 ArrayDim) override
		{
#if PLATFORM_COMPILER_HAS_IF_CONSTEXPR
			if constexpr (TStructOpsTypeTraits<CPPSTRUCT>::WithCopy)
			{
				static_assert((!TIsPODType<CPPSTRUCT>::Value), "You probably don't want custom copy for a POD type.");

				CPPSTRUCT* TypedDest = (CPPSTRUCT*)Dest;
				const CPPSTRUCT* TypedSrc  = (const CPPSTRUCT*)Src;

				for (; ArrayDim; --ArrayDim)
				{
					*TypedDest++ = *TypedSrc++;
				}
				return true;
			}
			else
			{
				return false;
			}
#else
			return CopyOrNot((CPPSTRUCT*)Dest, (CPPSTRUCT const*)Src, ArrayDim);
#endif
		}
		virtual bool HasIdentical() override
		{
			return TTraits::WithIdentical || TTraits::WithIdenticalViaEquality;
		}
		virtual bool Identical(const void* A, const void* B, uint32 PortFlags, bool& bOutResult) override
		{
			check((TTraits::WithIdentical || TTraits::WithIdenticalViaEquality)); // don't call this if we have indicated it is not necessary
#if PLATFORM_COMPILER_HAS_IF_CONSTEXPR
			if constexpr (TStructOpsTypeTraits<CPPSTRUCT>::WithIdentical)
			{
				static_assert(!TStructOpsTypeTraits<CPPSTRUCT>::WithIdenticalViaEquality, "Should not have both WithIdenticalViaEquality and WithIdentical.");

				bOutResult = ((const CPPSTRUCT*)A)->Identical((const CPPSTRUCT*)B, PortFlags);
				return true;
			}
			else if constexpr (TStructOpsTypeTraits<CPPSTRUCT>::WithIdenticalViaEquality)
			{
				bOutResult = (*(const CPPSTRUCT*)A == *(const CPPSTRUCT*)B);
				return true;
			}
			else
			{
				bOutResult = false;
				return false;
			}
#else
			return IdenticalOrNot((const CPPSTRUCT*)A, (const CPPSTRUCT*)B, PortFlags, bOutResult);
#endif
		}
		virtual bool HasExportTextItem() override
		{
			return TTraits::WithExportTextItem;
		}
		virtual bool ExportTextItem(FString& ValueStr, const void* PropertyValue, const void* DefaultValue, class UObject* Parent, int32 PortFlags, class UObject* ExportRootScope) override
		{
			check(TTraits::WithExportTextItem); // don't call this if we have indicated it is not necessary
#if PLATFORM_COMPILER_HAS_IF_CONSTEXPR
			if constexpr (TStructOpsTypeTraits<CPPSTRUCT>::WithExportTextItem)
			{
				return ((const CPPSTRUCT*)PropertyValue)->ExportTextItem(ValueStr, *(const CPPSTRUCT*)DefaultValue, Parent, PortFlags, ExportRootScope);
			}
			else
			{
				return false;
			}
#else
			return ExportTextItemOrNot(ValueStr, (const CPPSTRUCT*)PropertyValue, (const CPPSTRUCT*)DefaultValue, Parent, PortFlags, ExportRootScope);
#endif
		}
		virtual bool HasImportTextItem() override
		{
			return TTraits::WithImportTextItem;
		}
		virtual bool ImportTextItem(const TCHAR*& Buffer, void* Data, int32 PortFlags, class UObject* OwnerObject, FOutputDevice* ErrorText) override
		{
			check(TTraits::WithImportTextItem); // don't call this if we have indicated it is not necessary
#if PLATFORM_COMPILER_HAS_IF_CONSTEXPR
			if constexpr (TStructOpsTypeTraits<CPPSTRUCT>::WithImportTextItem)
			{
				return ((CPPSTRUCT*)Data)->ImportTextItem(Buffer, PortFlags, OwnerObject, ErrorText);
			}
			else
			{
				return false;
			}
#else
			return ImportTextItemOrNot(Buffer, (CPPSTRUCT*)Data, PortFlags, OwnerObject, ErrorText);
#endif
		}
		virtual bool HasAddStructReferencedObjects() override
		{
			return TTraits::WithAddStructReferencedObjects;
		}
		virtual TPointerToAddStructReferencedObjects AddStructReferencedObjects() override
		{
			check(TTraits::WithAddStructReferencedObjects); // don't call this if we have indicated it is not necessary
			return &AddStructReferencedObjectsOrNot<CPPSTRUCT>;
		}
		virtual bool HasSerializeFromMismatchedTag() override
		{
			return TTraits::WithSerializeFromMismatchedTag;
		}
		virtual bool SerializeFromMismatchedTag(struct FPropertyTag const& Tag, FArchive& Ar, void *Data) override
		{
			check(TTraits::WithSerializeFromMismatchedTag); // don't call this if we have indicated it is not allowed
#if PLATFORM_COMPILER_HAS_IF_CONSTEXPR
			if constexpr (TStructOpsTypeTraits<CPPSTRUCT>::WithSerializeFromMismatchedTag)
			{
				return ((CPPSTRUCT*)Data)->SerializeFromMismatchedTag(Tag, Ar);
			}
			else
			{
				return false;
			}
#else
			return SerializeFromMismatchedTagOrNot(Tag, Ar, (CPPSTRUCT*)Data);
#endif
		}
		virtual bool HasStructuredSerializeFromMismatchedTag() override
		{
			return TTraits::WithStructuredSerializeFromMismatchedTag;
		}
		virtual bool StructuredSerializeFromMismatchedTag(struct FPropertyTag const& Tag, FStructuredArchive::FSlot Slot, void *Data) override
		{
			check(TTraits::WithStructuredSerializeFromMismatchedTag); // don't call this if we have indicated it is not allowed
#if PLATFORM_COMPILER_HAS_IF_CONSTEXPR
			if constexpr (TStructOpsTypeTraits<CPPSTRUCT>::WithStructuredSerializeFromMismatchedTag)
			{
				return ((CPPSTRUCT*)Data)->SerializeFromMismatchedTag(Tag, Slot);
			}
			else
			{
				return false;
			}
#else
			return StructuredSerializeFromMismatchedTagOrNot(Tag, Slot, (CPPSTRUCT*)Data);
#endif
		}

		static_assert(!(TTraits::WithSerializeFromMismatchedTag && TTraits::WithStructuredSerializeFromMismatchedTag), "Structs cannot have both WithSerializeFromMismatchedTag and WithStructuredSerializeFromMismatchedTag set");

		virtual bool HasGetTypeHash() override
		{
			return TModels<CGetTypeHashable, CPPSTRUCT>::Value;
		}
		uint32 GetStructTypeHash(const void* Src) override
		{
			ensure(HasGetTypeHash());

#if PLATFORM_COMPILER_HAS_IF_CONSTEXPR
			if constexpr (TModels<CGetTypeHashable, CPPSTRUCT>::Value)
			{
				return GetTypeHash(*(const CPPSTRUCT*)Src);
			}
			else
			{
				return 0;
			}
#else
			return GetTypeHashOrNot((const CPPSTRUCT*)Src);
#endif
		}
		virtual EPropertyFlags GetComputedPropertyFlags() const override
		{
			return 
				  (TIsPODType<CPPSTRUCT>::Value ? CPF_IsPlainOldData : CPF_None)
				| (TIsTriviallyDestructible<CPPSTRUCT>::Value ? CPF_NoDestructor : CPF_None)
				| (TIsZeroConstructType<CPPSTRUCT>::Value ? CPF_ZeroConstructor : CPF_None)
				| (TModels<CGetTypeHashable, CPPSTRUCT>::Value ? CPF_HasGetValueTypeHash : CPF_None);
		}
		bool IsAbstract() const override
		{
			return TIsAbstract<CPPSTRUCT>::Value;
		}
	};

	/** Template for noexport classes to autoregister before main starts **/
	template<class CPPSTRUCT>
	struct TAutoCppStructOps
	{
		TAutoCppStructOps(FName InName)
		{
			DeferCppStructOps(InName,new TCppStructOps<CPPSTRUCT>);
		}
	};
	#define IMPLEMENT_STRUCT(BaseName) \
		static UScriptStruct::TAutoCppStructOps<F##BaseName> BaseName##_Ops(TEXT(#BaseName)); 

	DECLARE_CASTED_CLASS_INTRINSIC_NO_CTOR(UScriptStruct, UStruct, CLASS_MatchedSerializers, TEXT("/Script/CoreUObject"), CASTCLASS_UScriptStruct, COREUOBJECT_API)

	COREUOBJECT_API UScriptStruct( EStaticConstructor, int32 InSize, int32 InAlignment, EObjectFlags InFlags );
	COREUOBJECT_API explicit UScriptStruct(const FObjectInitializer& ObjectInitializer, UScriptStruct* InSuperStruct, ICppStructOps* InCppStructOps = nullptr, EStructFlags InStructFlags = STRUCT_NoFlags, SIZE_T ExplicitSize = 0, SIZE_T ExplicitAlignment = 0);
	COREUOBJECT_API explicit UScriptStruct(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

public:
	EStructFlags StructFlags;


#if HACK_HEADER_GENERATOR
	int32 StructMacroDeclaredLineNumber;

	// Required by UHT makefiles for internal data serialization.
	friend struct FScriptStructArchiveProxy;
#endif

protected:
	/** true if we have performed PrepareCppStructOps **/
	bool bPrepareCppStructOpsCompleted;
	/** Holds the Cpp ctors and dtors, sizeof, etc. Is not owned by this and is not released. **/
	ICppStructOps* CppStructOps;
public:

	// UObject Interface
	virtual COREUOBJECT_API void Serialize(FArchive& Ar) override;
	virtual COREUOBJECT_API void Serialize(FStructuredArchive::FRecord Record) override;

	// UStruct interface.
	virtual COREUOBJECT_API void Link(FArchive& Ar, bool bRelinkExistingProperties) override;
	virtual COREUOBJECT_API void InitializeStruct(void* Dest, int32 ArrayDim = 1) const override;
	virtual COREUOBJECT_API void DestroyStruct(void* Dest, int32 ArrayDim = 1) const override;
	virtual COREUOBJECT_API bool IsStructTrashed() const override;
	// End of UStruct interface.

	/** Sets or unsets the trashed flag on this struct */
	void COREUOBJECT_API SetStructTrashed(bool bIsTrash);

	/** 
	 * Stash a CppStructOps for future use 
	 * @param Target Name of the struct 
	 * @param InCppStructOps Cpp ops for this struct
	 */
	static COREUOBJECT_API void DeferCppStructOps(FName Target, ICppStructOps* InCppStructOps);

	template<class CPPSTRUCT>
	static typename TEnableIf<!DISABLE_ABSTRACT_CONSTRUCT>::Type DeferCppStructOps(FName Target)
	{
		DeferCppStructOps(Target, new UScriptStruct::TCppStructOps<CPPSTRUCT>);
	}
	template<class CPPSTRUCT>
	static typename TEnableIf<DISABLE_ABSTRACT_CONSTRUCT>::Type DeferCppStructOps(FName Target)
	{
		DeferCppStructOps(Target, nullptr);
	}

	/** Look for the CppStructOps and hook it up **/
	virtual COREUOBJECT_API void PrepareCppStructOps();

	/** Returns the CppStructOps that can be used to do custom operations */
	FORCEINLINE ICppStructOps* GetCppStructOps() const
	{
		checkf(bPrepareCppStructOpsCompleted, TEXT("GetCppStructOps: PrepareCppStructOps() has not been called for class %s"), *GetName());
		return CppStructOps;
	}

	/** Resets currently assigned CppStructOps, called when loading a struct */
	void ClearCppStructOps()
	{
		StructFlags = EStructFlags(StructFlags & ~STRUCT_ComputedFlags);
		bPrepareCppStructOpsCompleted = false;
		CppStructOps = nullptr;
	}

	/** 
	 * If it is native, it is assumed to have defaults because it has a constructor
	 * @return true if this struct has defaults
	 */
	FORCEINLINE bool HasDefaults() const
	{
		return !!GetCppStructOps();
	}

	/**
	 * Returns whether this struct should be serialized atomically.
	 * @param	Ar	Archive the struct is going to be serialized with later on
	 */
	bool ShouldSerializeAtomically(FArchive& Ar) const
	{
		if( (StructFlags&STRUCT_Atomic) != 0)
		{
			return true;
		}
		else
		{
			return false;
		}
	}

	/** Returns true if this struct has a native serialize function */
	bool UseNativeSerialization() const
	{
		if ((StructFlags&(STRUCT_SerializeNative)) != 0)
		{
			return true;
		}
		else
		{
			return false;
		}
	}

	/** Returns true if this struct should be binary serialized for the given archive */
	COREUOBJECT_API bool UseBinarySerialization(const FArchive& Ar) const;

	/** 
	 * Serializes a specific instance of a struct 
	 *
	 * @param	Slot		The structured archive slot we are serializing to
	 * @param	Value		Pointer to memory of struct
	 * @param	Defaults	Default value for this struct, pass nullptr to not use defaults 
	 */
	COREUOBJECT_API void SerializeItem(FArchive& Ar, void* Value, void const* Defaults);
	COREUOBJECT_API void SerializeItem(FStructuredArchive::FSlot Slot, void* Value, void const* Defaults);

	/**
	 * Export script struct to a string that can later be imported
	 *
	 * @param	ValueStr		String to write to
	 * @param	Value			Actual struct being exported
	 * @param	Defaults		Default value for this struct, pass nullptr to not use defaults 
	 * @param	OwnerObject		UObject that contains this struct
	 * @param	PortFlags		EPropertyPortFlags controlling export behavior
	 * @param	ExportRootScope	The scope to create relative paths from, if the PPF_ExportsNotFullyQualified flag is passed in.  If NULL, the package containing the object will be used instead.
	 * @param	bAllowNativeOverride If true, will try to run native version of export text on the struct
	 */
	COREUOBJECT_API void ExportText(FString& ValueStr, const void* Value, const void* Defaults, UObject* OwnerObject, int32 PortFlags, UObject* ExportRootScope, bool bAllowNativeOverride = true) const;

	/**
	 * Sets value of script struct based on imported string
	 *
	 * @param	Buffer			String to read text data out of
	 * @param	Value			Struct that will be modified
	 * @param	OwnerObject		UObject that contains this struct
	 * @param	PortFlags		EPropertyPortFlags controlling import behavior
	 * @param	ErrorText		What to print import errors to
	 * @param	StructName		Name of struct, used in error display
	 * @param	bAllowNativeOverride If true, will try to run native version of export text on the struct
	 * @return Buffer after parsing has succeeded, or NULL on failure
	 */
	COREUOBJECT_API const TCHAR* ImportText(const TCHAR* Buffer, void* Value, UObject* OwnerObject, int32 PortFlags, FOutputDevice* ErrorText, const FString& StructName, bool bAllowNativeOverride = true);

	/**
	 * Sets value of script struct based on imported string
	 *
	 * @param	Buffer			String to read text data out of
	 * @param	Value			Struct that will be modified
	 * @param	OwnerObject		UObject that contains this struct
	 * @param	PortFlags		EPropertyPortFlags controlling import behavior
	 * @param	ErrorText		What to print import errors to
	 * @param	StructNameGetter Function to return the struct name to avoid doing work if no error message is forthcoming
	 * @param	bAllowNativeOverride If true, will try to run native version of export text on the struct
	 * @return Buffer after parsing has succeeded, or NULL on failure
	 */
	COREUOBJECT_API const TCHAR* ImportText(const TCHAR* Buffer, void* Value, UObject* OwnerObject, int32 PortFlags, FOutputDevice* ErrorText, const TFunctionRef<FString()>& StructNameGetter, bool bAllowNativeOverride = true);

	/**
	 * Compare two script structs
	 *
	 * @param	Dest		Pointer to memory to a struct
	 * @param	Src			Pointer to memory to the other struct
	 * @param	PortFlags	Comparison flags
	 * @return true if the structs are identical
	 */
	COREUOBJECT_API bool CompareScriptStruct(const void* A, const void* B, uint32 PortFlags) const;

	/**
	 * Copy a struct over an existing struct
	 *
	 * @param	Dest		Pointer to memory to initialize
	 * @param	Src			Pointer to memory to copy from
	 * @param	ArrayDim	Number of elements in the array
	 * @param	Stride		Stride of the array, If this default (0), then we will pull the size from the struct
	 */
	COREUOBJECT_API void CopyScriptStruct(void* Dest, void const* Src, int32 ArrayDim = 1) const;
	
	/**
	 * Reinitialize a struct in memory. This may be done by calling the native destructor and then the constructor or individually reinitializing properties
	 *
	 * @param	Dest		Pointer to memory to reinitialize
	 * @param	ArrayDim	Number of elements in the array
	 * @param	Stride		Stride of the array, only relevant if there more than one element. If this default (0), then we will pull the size from the struct
	 */
	COREUOBJECT_API void ClearScriptStruct(void* Dest, int32 ArrayDim = 1) const;

	/**
	 * Calls GetTypeHash for native structs, otherwise computes a hash of all struct members
	 * 
	 * @param Src		Pointer to instance to hash
	 * @return hashed value of Src
	 */
	virtual COREUOBJECT_API uint32 GetStructTypeHash(const void* Src) const;

	/** Used by User Defined Structs to preload this struct and any child objects */
	virtual COREUOBJECT_API void RecursivelyPreload();

	/** Returns the custom Guid assigned to this struct for User Defined Structs, or an invalid Guid */
	virtual COREUOBJECT_API FGuid GetCustomGuid() const;
	
	/** Returns the (native, c++) name of the struct */
	virtual COREUOBJECT_API FString GetStructCPPName() const;

	/**
	 * Initializes this structure to its default values
	 * @param InStructData		The memory location to initialize
	 */
	virtual COREUOBJECT_API void InitializeDefaultValue(uint8* InStructData) const;
};

/*-----------------------------------------------------------------------------
	UFunction.
-----------------------------------------------------------------------------*/

//
// Reflection data for a replicated or Kismet callable function.
//
class COREUOBJECT_API UFunction : public UStruct
{
	DECLARE_CASTED_CLASS_INTRINSIC(UFunction, UStruct, 0, TEXT("/Script/CoreUObject"), CASTCLASS_UFunction)
	DECLARE_WITHIN(UClass)
public:
	// Persistent variables.

	/** EFunctionFlags set defined for this function */
	EFunctionFlags FunctionFlags;

	// Variables in memory only.
	
	/** Number of parameters total */
	uint8 NumParms;
	/** Total size of parameters in memory */
	uint16 ParmsSize;
	/** Memory offset of return value property */
	uint16 ReturnValueOffset;
	/** Id of this RPC function call (must be FUNC_Net & (FUNC_NetService|FUNC_NetResponse)) */
	uint16 RPCId;
	/** Id of the corresponding response call (must be FUNC_Net & FUNC_NetService) */
	uint16 RPCResponseId;

	/** pointer to first local struct property in this UFunction that contains defaults */
	FProperty* FirstPropertyToInit;

#if UE_BLUEPRINT_EVENTGRAPH_FASTCALLS
	/** The event graph this function calls in to (persistent) */
	UFunction* EventGraphFunction;

	/** The state offset inside of the event graph (persistent) */
	int32 EventGraphCallOffset;
#endif

private:
	/** C++ function this is bound to */
	FNativeFuncPtr Func;

public:
	/**
	 * Returns the native func pointer.
	 *
	 * @return The native function pointer.
	 */
	FORCEINLINE FNativeFuncPtr GetNativeFunc() const
	{
		return Func;
	}

	/**
	 * Sets the native func pointer.
	 *
	 * @param InFunc - The new function pointer.
	 */
	FORCEINLINE void SetNativeFunc(FNativeFuncPtr InFunc)
	{
		Func = InFunc;
	}

	/**
	 * Invokes the UFunction on a UObject.
	 *
	 * @param Obj    - The object to invoke the function on.
	 * @param Stack  - The parameter stack for the function call.
	 * @param Result - The result of the function.
	 */
	void Invoke(UObject* Obj, FFrame& Stack, RESULT_DECL);

	// Constructors.
	explicit UFunction(const FObjectInitializer& ObjectInitializer, UFunction* InSuperFunction, EFunctionFlags InFunctionFlags = FUNC_None, SIZE_T ParamsSize = 0 );
	explicit UFunction(UFunction* InSuperFunction, EFunctionFlags InFunctionFlags = FUNC_None, SIZE_T ParamsSize = 0);

	/** Initializes transient members like return value offset */
	void InitializeDerivedMembers();

	// UObject interface.
	virtual void Serialize( FArchive& Ar ) override;
	virtual void PostLoad() override;

	// UField interface.
	virtual void Bind() override;

	// UStruct interface.
	virtual UStruct* GetInheritanceSuper() const override { return nullptr;}
	virtual void Link(FArchive& Ar, bool bRelinkExistingProperties) override;

	/** Returns parent function if there is one, or null */
	UFunction* GetSuperFunction() const;

	/** Returns the return value property if there is one, or null */
	FProperty* GetReturnProperty() const;

	/** Returns the owning UClass* without branching */
	FORCEINLINE UClass* GetOuterUClassUnchecked() const
	{
		// declaration order mandates reinterpret_cast:
		return reinterpret_cast<UClass*>(GetOuter());
	}

	/**
	 * Used to safely check whether the passed in flag is set.
	 *
	 * @param	FlagToCheck		Class flag to check for
	 *
	 * @return	true if the passed in flag is set, false otherwise
	 *			(including no flag passed in, unless the FlagsToCheck is CLASS_AllFlags)
	 */
	FORCEINLINE bool HasAnyFunctionFlags( EFunctionFlags FlagsToCheck ) const
	{
		return (FunctionFlags&FlagsToCheck) != 0 || FlagsToCheck == FUNC_AllFlags;
	}

	/**
	 * Used to safely check whether all of the passed in flags are set.
	 *
	 * @param FlagsToCheck	Function flags to check for
	 * @return true if all of the passed in flags are set (including no flags passed in), false otherwise
	 */
	FORCEINLINE bool HasAllFunctionFlags( EFunctionFlags FlagsToCheck ) const
	{
		return ((FunctionFlags & FlagsToCheck) == FlagsToCheck);
	}

	/**
	 * Returns the flags that are ignored by default when comparing function signatures.
	 */
	FORCEINLINE static uint64 GetDefaultIgnoredSignatureCompatibilityFlags()
	{
		//@TODO: UCREMOVAL: CPF_ConstParm added as a hack to get blueprints compiling with a const DamageType parameter.
		const uint64 IgnoreFlags = CPF_PersistentInstance | CPF_ExportObject | CPF_InstancedReference 
			| CPF_ContainsInstancedReference | CPF_ComputedFlags | CPF_ConstParm | CPF_UObjectWrapper
			| CPF_NativeAccessSpecifiers | CPF_AdvancedDisplay | CPF_BlueprintVisible | CPF_BlueprintReadOnly;
		return IgnoreFlags;
	}

	/**
	 * Determines if two functions have an identical signature (note: currently doesn't allow
	 * matches with class parameters that differ only in how derived they are; there is no
	 * directionality to the call)
	 *
	 * @param	OtherFunction	Function to compare this function against.
	 *
	 * @return	true if function signatures are compatible.
	 */
	bool IsSignatureCompatibleWith(const UFunction* OtherFunction) const;

	/**
	 * Determines if two functions have an identical signature (note: currently doesn't allow
	 * matches with class parameters that differ only in how derived they are; there is no
	 * directionality to the call)
	 *
	 * @param	OtherFunction	Function to compare this function against.
	 * @param   IgnoreFlags     Custom flags to ignore when comparing parameters between the functions.
	 *
	 * @return	true if function signatures are compatible.
	 */
	bool IsSignatureCompatibleWith(const UFunction* OtherFunction, uint64 IgnoreFlags) const;
};

//
// Function definition used by dynamic delegate declarations
//
class COREUOBJECT_API UDelegateFunction : public UFunction
{
	DECLARE_CASTED_CLASS_INTRINSIC(UDelegateFunction, UFunction, 0, TEXT("/Script/CoreUObject"), CASTCLASS_UDelegateFunction)
	DECLARE_WITHIN(UObject)
public:
	explicit UDelegateFunction(const FObjectInitializer& ObjectInitializer, UFunction* InSuperFunction, EFunctionFlags InFunctionFlags = FUNC_None, SIZE_T ParamsSize = 0);
	explicit UDelegateFunction(UFunction* InSuperFunction, EFunctionFlags InFunctionFlags = FUNC_None, SIZE_T ParamsSize = 0);
};

//
// Function definition used by sparse dynamic delegate declarations
//
class COREUOBJECT_API USparseDelegateFunction : public UDelegateFunction
{
	DECLARE_CASTED_CLASS_INTRINSIC(USparseDelegateFunction, UDelegateFunction, 0, TEXT("/Script/CoreUObject"), CASTCLASS_USparseDelegateFunction)
	DECLARE_WITHIN(UObject)
public:
	explicit USparseDelegateFunction(const FObjectInitializer& ObjectInitializer, UFunction* InSuperFunction, EFunctionFlags InFunctionFlags = FUNC_None, SIZE_T ParamsSize = 0);
	explicit USparseDelegateFunction(UFunction* InSuperFunction, EFunctionFlags InFunctionFlags = FUNC_None, SIZE_T ParamsSize = 0);

	virtual void Serialize(FArchive& Ar) override;

	FName OwningClassName;
	FName DelegateName;
};

/*-----------------------------------------------------------------------------
	UEnum.
-----------------------------------------------------------------------------*/

typedef FText(*FEnumDisplayNameFn)(int32);

/** Optional flags for the UEnum::Get*ByName() functions. */
enum class EGetByNameFlags
{
	None = 0,

	/** Outputs an warning if the enum lookup fails */
	ErrorIfNotFound		= 0x01,

	/** Does a case sensitive match */
	CaseSensitive		= 0x02,

	/** Checks the GetAuthoredNameStringByIndex value as well as normal names */
	CheckAuthoredName	= 0x04,
};

ENUM_CLASS_FLAGS(EGetByNameFlags)

//
// Reflection data for an enumeration.
//
class COREUOBJECT_API UEnum : public UField
{
	DECLARE_CASTED_CLASS_INTRINSIC_NO_CTOR(UEnum, UField, 0, TEXT("/Script/CoreUObject"), CASTCLASS_UEnum, NO_API)
	UEnum(const FObjectInitializer& ObjectInitialzer);

public:
	/** How this enum is declared in C++, affects the internal naming of enum values */
	enum class ECppForm
	{
		Regular,
		Namespaced,
		EnumClass
	};

	/** This will be the true type of the enum as a string, e.g. "ENamespacedEnum::InnerType" or "ERegularEnum" or "EEnumClass" */
	FString CppType;

	// Index is the internal index into the Enum array, and is not useful outside of the Enum system
	// Value is the value set in the Enum Class in C++ or Blueprint
	// Enums can be sparse, which means that not every valid Index is a proper Value, and they are not necessarily equal
	// It is not safe to cast an Index to a Enum Class, always do that with a Value instead

	/** Gets the internal index for an enum value. Returns INDEX_None if not valid */
	FORCEINLINE int32 GetIndexByValue(int64 InValue) const
	{
		for (int32 i = 0; i < Names.Num(); ++i)
		{
			if (Names[i].Value == InValue)
			{
				return i;
			}
		}
		return INDEX_NONE;
	}

	/** Gets enum value by index in Names array. Asserts on invalid index */
	FORCEINLINE int64 GetValueByIndex(int32 Index) const
	{
		check(Names.IsValidIndex(Index));
		return Names[Index].Value;
	}

	/** Gets enum name by index in Names array. Returns NAME_None if Index is not valid. */
	FName GetNameByIndex(int32 Index) const;

	/** Gets index of name in enum, returns INDEX_NONE and optionally errors when name is not found. This is faster than ByNameString if the FName is exact, but will fall back if needed */
	int32 GetIndexByName(FName InName, EGetByNameFlags Flags = EGetByNameFlags::None) const;

	/** Gets enum name by value. Returns NAME_None if value is not found. */
	FName GetNameByValue(int64 InValue) const;

	/** Gets enum value by name, returns INDEX_NONE and optionally errors when name is not found. This is faster than ByNameString if the FName is exact, but will fall back if needed */
	int64 GetValueByName(FName InName, EGetByNameFlags Flags = EGetByNameFlags::None) const;

	/** Returns the short name at the enum index, returns empty string if invalid */
	FString GetNameStringByIndex(int32 InIndex) const;

	/** Gets index of name in enum, returns INDEX_NONE and optionally errors when name is not found. Handles full or short names. */
	int32 GetIndexByNameString(const FString& SearchString, EGetByNameFlags Flags = EGetByNameFlags::None) const;

	/** Returns the short name matching the enum Value, returns empty string if invalid */
	FString GetNameStringByValue(int64 InValue) const;

	/** Looks for a name with a given value and returns true and writes the name to Out if one was found */
	bool FindNameStringByValue(FString& Out, int64 InValue) const;

	/** Gets enum value by name, returns INDEX_NONE and optionally errors when name is not found. Handles full or short names */
	int64 GetValueByNameString(const FString& SearchString, EGetByNameFlags Flags = EGetByNameFlags::None) const;

	/**
	 * Finds the localized display name or native display name as a fallback.
	 * If called from a cooked build this will normally return the short name as Metadata is not available.
	 *
	 * @param InIndex Index of the enum value to get Display Name for
	 *
	 * @return The display name for this object, or an empty text if Index is invalid
	 */
	virtual FText GetDisplayNameTextByIndex(int32 InIndex) const;

	/** Version of GetDisplayNameTextByIndex that takes a value instead */
	FText GetDisplayNameTextByValue(int64 InValue) const;

	/** Looks for a display name with a given value and returns true and writes the name to Out if one was found */
	bool FindDisplayNameTextByValue(FText& Out, int64 InValue) const;

	/**
	 * Returns the unlocalized logical name originally assigned to the enum at creation.
	 * By default this is the same as the short name but it is overridden in child classes with different internal name storage.
	 * This name is consistent in cooked and editor builds and is useful for things like external data import/export.
	 *
	 * @param InIndex Index of the enum value to get Display Name for
	 *
	 * @return The author-specified name, or an empty string if Index is invalid
	 */
	virtual FString GetAuthoredNameStringByIndex(int32 InIndex) const;

	/** Version of GetAuthoredNameByIndex that takes a value instead */
	FString GetAuthoredNameStringByValue(int64 InValue) const;

	/** Looks for a display name with a given value and returns true and writes the unlocalized logical name to Out if one was found */
	bool FindAuthoredNameStringByValue(FString& Out, int64 InValue) const;

	/** Gets max value of Enum. Defaults to zero if there are no entries. */
	int64 GetMaxEnumValue() const;

	/** Checks if enum has entry with given value. Includes autogenerated _MAX entry. */
	bool IsValidEnumValue(int64 InValue) const;

	/** Checks if enum has entry with given name. Includes autogenerated _MAX entry. */
	bool IsValidEnumName(FName InName) const;

	/** Removes the Names in this enum from the master AllEnumNames list */
	void RemoveNamesFromMasterList();

	/** Try to update an out-of-date enum index after an enum changes at runtime */
	virtual int64 ResolveEnumerator(FArchive& Ar, int64 EnumeratorIndex) const;

	/** Associate a function for looking up Enum display names by index, only intended for use by generated code */
	void SetEnumDisplayNameFn(FEnumDisplayNameFn InEnumDisplayNameFn)
	{
		EnumDisplayNameFn = InEnumDisplayNameFn;
	}

	/**
	 * Returns the type of enum: whether it's a regular enum, namespaced enum or C++11 enum class.
	 *
	 * @return The enum type.
	 */
	ECppForm GetCppForm() const
	{
		return CppForm;
	}

	bool HasAnyEnumFlags(EEnumFlags InFlags) const
	{
		return EnumHasAnyFlags(EnumFlags, InFlags);
	}

	/**
	 * Checks if a enum name is fully qualified name.
	 *
	 * @param InEnumName Name to check.
	 * @return true if the specified name is full enum name, false otherwise.
	 */
	static bool IsFullEnumName(const TCHAR* InEnumName)
	{
		return !!FCString::Strstr(InEnumName, TEXT("::"));
	}

	/**
	 * Generates full name including EnumName:: given enum name.
	 *
	 * @param InEnumName Enum name.
	 * @return Full enum name.
	 */
	virtual FString GenerateFullEnumName(const TCHAR* InEnumName) const;

	/** searches the list of all enum value names for the specified name
	 * @return the value the specified name represents if found, otherwise INDEX_NONE
	 */
	static int64 LookupEnumName(FName TestName, UEnum** FoundEnum = nullptr)
	{
		UEnum* TheEnum = AllEnumNames.FindRef(TestName);
		if (FoundEnum != nullptr)
		{
			*FoundEnum = TheEnum;
		}
		return (TheEnum != nullptr) ? TheEnum->GetValueByName(TestName) : INDEX_NONE;
	}

	/** searches the list of all enum value names for the specified name
	 * @return the value the specified name represents if found, otherwise INDEX_NONE
	 */
	static int64 LookupEnumNameSlow(const TCHAR* InTestShortName, UEnum** FoundEnum = nullptr)
	{
		int64 Result = LookupEnumName(InTestShortName, FoundEnum);
		if (Result == INDEX_NONE)
		{
			FString TestShortName = FString(TEXT("::")) + InTestShortName;
			UEnum* TheEnum = nullptr;
			for (TMap<FName, UEnum*>::TIterator It(AllEnumNames); It; ++It)
			{
				if (It.Key().ToString().Contains(TestShortName) )
				{
					TheEnum = It.Value();
				}
			}
			if (FoundEnum != nullptr)
			{
				*FoundEnum = TheEnum;
			}
			Result = (TheEnum != nullptr) ? TheEnum->GetValueByName(InTestShortName) : INDEX_NONE;
		}
		return Result;
	}

	/** parses the passed in string for a name, then searches for that name in any Enum (in any package)
	 * @param Str	pointer to string to parse; if we successfully find an enum, this pointer is advanced past the name found
	 * @return index of the value the parsed enum name matches, or INDEX_NONE if no matches
	 */
	static int64 ParseEnum(const TCHAR*& Str);

	/**
	 * Tests if the enum contains a MAX value
	 *
	 * @return	true if the enum contains a MAX enum, false otherwise.
	 */
	bool ContainsExistingMax() const;

	/**
	 * Sets the array of enums.
	 *
	 * @param InNames List of enum names.
	 * @param InCppForm The form of enum.
	 * @param bAddMaxKeyIfMissing Should a default Max item be added.
	 * @return	true unless the MAX enum already exists and isn't the last enum.
	 */
	virtual bool SetEnums(TArray<TPair<FName, int64>>& InNames, ECppForm InCppForm, EEnumFlags InFlags = EEnumFlags::None, bool bAddMaxKeyIfMissing = true);

	/**
	 * @return	 The number of enum names.
	 */
	int32 NumEnums() const
	{
		return Names.Num();
	}

	/**
	 * Find the longest common prefix of all items in the enumeration.
	 * 
	 * @return	the longest common prefix between all items in the enum.  If a common prefix
	 *			cannot be found, returns the full name of the enum.
	 */
	FString GenerateEnumPrefix() const;

#if WITH_EDITOR
	/**
	 * Finds the localized tooltip or native tooltip as a fallback.
	 *
	 * @param NameIndex Index of the enum value to get tooltip for
	 *
	 * @return The tooltip for this object.
	 */
	FText GetToolTipTextByIndex(int32 NameIndex) const;

	UE_DEPRECATED(4.16, "GetToolTipText with name index is deprecated, call GetToolTipTextByIndex instead")
	FText GetToolTipText(int32 NameIndex) const { return GetToolTipTextByIndex(NameIndex); }
#endif

#if WITH_EDITORONLY_DATA
	/**
	 * Wrapper method for easily determining whether this enum has metadata associated with it.
	 * 
	 * @param	Key			the metadata tag to check for
	 * @param	NameIndex	if specified, will search for metadata linked to a specified value in this enum; otherwise, searches for metadata for the enum itself
	 *
	 * @return true if the specified key exists in the list of metadata for this enum, even if the value of that key is empty
	 */
	bool HasMetaData( const TCHAR* Key, int32 NameIndex=INDEX_NONE ) const;

	/**
	 * Return the metadata value associated with the specified key.
	 * 
	 * @param	Key			the metadata tag to find the value for
	 * @param	NameIndex	if specified, will search the metadata linked for that enum value; otherwise, searches the metadata for the enum itself
	 * @param	bAllowRemap	if true, the returned value may be remapped from a .ini if the value starts with ini: Pass false when you need the exact string, including any ini:
	 *
	 * @return	the value for the key specified, or an empty string if the key wasn't found or had no value.
	 */
	FString GetMetaData( const TCHAR* Key, int32 NameIndex=INDEX_NONE, bool bAllowRemap=true ) const;

	/**
	 * Set the metadata value associated with the specified key.
	 * 
	 * @param	Key			the metadata tag to find the value for
	 * @param	NameIndex	if specified, will search the metadata linked for that enum value; otherwise, searches the metadata for the enum itself
	 * @param	InValue		Value of the metadata for the key
	 *
	 */
	void SetMetaData( const TCHAR* Key, const TCHAR* InValue, int32 NameIndex=INDEX_NONE) const;
	
	/**
	 * Remove given key meta data
	 * 
	 * @param	Key			the metadata tag to find the value for
	 * @param	NameIndex	if specified, will search the metadata linked for that enum value; otherwise, searches the metadata for the enum itself
	 *
	 */
	void RemoveMetaData( const TCHAR* Key, int32 NameIndex=INDEX_NONE ) const;
#endif // WITH_EDITORONLY_DATA
	
	/**
	 * @param EnumPath         Full enum path.
	 * @param EnumeratorValue  Enumerator VAlue.
	 *
	 * @return the string associated with the enumerator for the specified enum value for the enum specified by a path.
	 */
	template <typename T>
	FORCEINLINE static FString GetValueAsString( const TCHAR* EnumPath, const T EnumeratorValue)
	{
		// For the C++ enum.
		static_assert(TIsEnum<T>::Value, "Should only call this with enum types");
		return GetValueAsString_Internal(EnumPath, (int64)EnumeratorValue);
	}

	template <typename T>
	FORCEINLINE static FString GetValueAsString( const TCHAR* EnumPath, const TEnumAsByte<T> EnumeratorValue)
	{
		return GetValueAsString_Internal(EnumPath, (int64)EnumeratorValue.GetValue());
	}

	template< class T >
	FORCEINLINE static void GetValueAsString( const TCHAR* EnumPath, const T EnumeratorValue, FString& out_StringValue )
	{
		out_StringValue = GetValueAsString( EnumPath, EnumeratorValue );
	}

	/**
	 * @param EnumPath         Full enum path.
	 * @param EnumeratorValue  Enumerator Value.
	 *
	 * @return the localized display string associated with the specified enum value for the enum specified by a path
	 */
	template <typename T>
	FORCEINLINE static FText GetDisplayValueAsText( const TCHAR* EnumPath, const T EnumeratorValue )
	{
		// For the C++ enum.
		static_assert(TIsEnum<T>::Value, "Should only call this with enum types");
		return GetDisplayValueAsText_Internal(EnumPath, (int64)EnumeratorValue);
	}

	template <typename T>
	FORCEINLINE static FText GetDisplayValueAsText( const TCHAR* EnumPath, const TEnumAsByte<T> EnumeratorValue)
	{
		return GetDisplayValueAsText_Internal(EnumPath, (int64)EnumeratorValue.GetValue());
	}

	template< class T >
	FORCEINLINE static void GetDisplayValueAsText( const TCHAR* EnumPath, const T EnumeratorValue, FText& out_TextValue )
	{
		out_TextValue = GetDisplayValueAsText( EnumPath, EnumeratorValue);
	}

	/**
	 * @param EnumeratorValue  Enumerator Value.
	 *
	 * @return the name associated with the enumerator for the specified enum value for the enum specified by the template type.
	 */
	template<typename EnumType>
	FORCEINLINE static FName GetValueAsName(const EnumType EnumeratorValue)
	{
		// For the C++ enum.
		static_assert(TIsEnum<EnumType>::Value, "Should only call this with enum types");
		UEnum* EnumClass = StaticEnum<EnumType>();
		check(EnumClass != nullptr);
		return EnumClass->GetNameByValue((int64)EnumeratorValue);
	}

	template<typename EnumType>
	FORCEINLINE static FName GetValueAsName(const TEnumAsByte<EnumType> EnumeratorValue)
	{
		return GetValueAsName(EnumeratorValue.GetValue());
	}

	template<typename EnumType>
	FORCEINLINE static void GetValueAsName(const EnumType EnumeratorValue, FName& out_NameValue )
	{
		out_NameValue = GetValueAsName(EnumeratorValue);
	}

	/**
	 * @param EnumeratorValue  Enumerator Value.
	 *
	 * @return the string associated with the enumerator for the specified enum value for the enum specified by the template type.
	 */
	template<typename EnumType>
	FORCEINLINE static FString GetValueAsString(const EnumType EnumeratorValue)
	{
		// For the C++ enum.
		static_assert(TIsEnum<EnumType>::Value, "Should only call this with enum types");
		return GetValueAsName(EnumeratorValue).ToString();
	}

	template<typename EnumType>
	FORCEINLINE static FString GetValueAsString(const TEnumAsByte<EnumType> EnumeratorValue)
	{
		return GetValueAsString(EnumeratorValue.GetValue());
	}

	template<typename EnumType>
	FORCEINLINE static void GetValueAsString(const EnumType EnumeratorValue, FString& out_StringValue )
	{
		out_StringValue = GetValueAsString(EnumeratorValue );
	}


	/**
	 * @param EnumeratorValue  Enumerator Value.
	 *
	 * @return the localized display string associated with the specified enum value for the enum specified by the template type.
	 */
	template<typename EnumType>
	FORCEINLINE static FText GetDisplayValueAsText(const EnumType EnumeratorValue )
	{
		// For the C++ enum.
		static_assert(TIsEnum<EnumType>::Value, "Should only call this with enum types");
		UEnum* EnumClass = StaticEnum<EnumType>();
		check(EnumClass != nullptr);
		return EnumClass->GetDisplayNameTextByValue((int64)EnumeratorValue);
	}

	template<typename EnumType>
	FORCEINLINE static FText GetDisplayValueAsText(const TEnumAsByte<EnumType> EnumeratorValue)
	{
		return GetDisplayValueAsText(EnumeratorValue.GetValue());
	}

	template<typename EnumType>
	FORCEINLINE static void GetDisplayValueAsText(const EnumType EnumeratorValue, FText& out_TextValue )
	{
		out_TextValue = GetDisplayValueAsText(EnumeratorValue);
	}

	// Deprecated Functions
	UE_DEPRECATED(4.16, "FindEnumIndex is deprecated, call GetIndexByName or GetValueByName instead")
	int32 FindEnumIndex(FName InName) const { return GetIndexByName(InName, EGetByNameFlags::ErrorIfNotFound); }

	UE_DEPRECATED(4.16, "FindEnumRedirects is deprecated, call GetIndexByNameString instead")
	static int32 FindEnumRedirects(const UEnum* Enum, FName EnumEntryName) { return Enum->GetIndexByNameString(EnumEntryName.ToString()); }

	UE_DEPRECATED(4.16, "GetEnum is deprecated, call GetNameByIndex instead")
	FName GetEnum(int32 InIndex) const { return GetNameByIndex(InIndex); }

	UE_DEPRECATED(4.16, "GetEnumNameStringByValue is deprecated, call GetNameStringByValue instead")
	FString GetEnumNameStringByValue(int64 InValue) const { return GetNameStringByValue(InValue); }

	UE_DEPRECATED(4.16, "GetEnumName is deprecated, call GetNameStringByIndex instead")
	FString GetEnumName(int32 InIndex) const { return GetNameStringByIndex(InIndex); }

	UE_DEPRECATED(4.16, "GetDisplayNameText with name index is deprecated, call GetDisplayNameTextByIndex instead")
	FText GetDisplayNameText(int32 NameIndex) const { return GetDisplayNameTextByIndex(NameIndex); }

	UE_DEPRECATED(4.16, "GetEnumText with name index is deprecated, call GetDisplayNameTextByIndex instead")
	FText GetEnumText(int32 NameIndex) const { return GetDisplayNameTextByIndex(NameIndex); }

	UE_DEPRECATED(4.16, "GetEnumTextByValue with name index is deprecated, call GetDisplayNameTextByValue instead")
	FText GetEnumTextByValue(int64 Value) { return GetDisplayNameTextByValue(Value);  }

	// UObject interface.
	virtual void Serialize(FArchive& Ar) override;
	virtual void BeginDestroy() override;
	// End of UObject interface.

public:
	/** List of pairs of all enum names and values. */
	TArray<TPair<FName, int64>> Names;

	/** How the enum was originally defined. */
	ECppForm CppForm;

	/** Enum flags. */
	EEnumFlags EnumFlags;

	/** pointer to function used to look up the enum's display name. Currently only assigned for UEnums generated for nativized blueprints */
	FEnumDisplayNameFn EnumDisplayNameFn;

	/** global list of all value names used by all enums in memory, used for property text import */
	static TMap<FName, UEnum*> AllEnumNames;

	/** adds the Names in this enum to the master AllEnumNames list */
	void AddNamesToMasterList();

private:
	FORCEINLINE static FString GetValueAsString_Internal( const TCHAR* EnumPath, const int64 EnumeratorValue)
	{
		UEnum* EnumClass = FindObject<UEnum>( nullptr, EnumPath );
		UE_CLOG( !EnumClass, LogClass, Fatal, TEXT("Couldn't find enum '%s'"), EnumPath );
		return EnumClass->GetNameStringByValue(EnumeratorValue);
	}

	FORCEINLINE static FText GetDisplayValueAsText_Internal( const TCHAR* EnumPath, const int64 EnumeratorValue )
	{
		UEnum* EnumClass = FindObject<UEnum>(nullptr, EnumPath);
		UE_CLOG(!EnumClass, LogClass, Fatal, TEXT("Couldn't find enum '%s'"), EnumPath);
		return EnumClass->GetDisplayNameTextByValue(EnumeratorValue);
	}

	/**
	 * Renames enum values to use duplicated enum name instead of base one, e.g.:
	 * 
	 * MyEnum::MyVal
	 * MyEnum::MyEnum_MAX
	 * 
	 * becomes
	 * 
	 * MyDuplicatedEnum::MyVal
	 * MyDuplicatedEnum::MyDuplicatedEnum_MAX
	 */
	void RenameNamesAfterDuplication();

	/** Gets name of enum "this" is duplicate of. If we're not duplicating, just returns "this" name. */
	FString GetBaseEnumNameOnDuplication() const;
};

/*-----------------------------------------------------------------------------
	UClass.
-----------------------------------------------------------------------------*/

/** Base definition for C++ class type traits */
struct FCppClassTypeTraitsBase
{
	enum
	{
		IsAbstract = false
	};
};


/** Defines traits for specific C++ class types */
template<class CPPCLASS>
struct TCppClassTypeTraits : public FCppClassTypeTraitsBase
{
	enum
	{
		IsAbstract = TIsAbstract<CPPCLASS>::Value
	};
};


/** Interface for accessing attributes of the underlying C++ class, for native class types */
struct ICppClassTypeInfo
{
	/** Return true if the underlying C++ class is abstract (i.e. declares at least one pure virtual function) */
	virtual bool IsAbstract() const = 0;
};


struct FCppClassTypeInfoStatic
{
	bool bIsAbstract;
};


/** Implements the type information interface for specific C++ class types */
struct FCppClassTypeInfo : ICppClassTypeInfo
{
	explicit FCppClassTypeInfo(const FCppClassTypeInfoStatic* InInfo)
		: Info(InInfo)
	{
	}

	// Non-copyable
	FCppClassTypeInfo(const FCppClassTypeInfo&) = delete;
	FCppClassTypeInfo& operator=(const FCppClassTypeInfo&) = delete;

	// ICppClassTypeInfo implementation
	virtual bool IsAbstract() const override
	{
		return Info->bIsAbstract;
	}

private:
	const FCppClassTypeInfoStatic* Info;
};


/** information about an interface a class implements */
struct COREUOBJECT_API FImplementedInterface
{
	/** the interface class */
	UClass* Class;
	/** the pointer offset of the interface's vtable */
	int32 PointerOffset;
	/** whether or not this interface has been implemented via K2 */
	bool bImplementedByK2;

	FImplementedInterface()
		: Class(nullptr)
		, PointerOffset(0)
		, bImplementedByK2(false)
	{}
	FImplementedInterface(UClass* InClass, int32 InOffset, bool InImplementedByK2)
		: Class(InClass)
		, PointerOffset(InOffset)
		, bImplementedByK2(InImplementedByK2)
	{}

	friend COREUOBJECT_API FArchive& operator<<(FArchive& Ar, FImplementedInterface& A);
};


/** A struct that maps a string name to a native function */
struct FNativeFunctionLookup
{
	FName Name;
	FNativeFuncPtr Pointer;

	FNativeFunctionLookup(FName InName, FNativeFuncPtr InPointer)
		:	Name(InName)
		,	Pointer(InPointer)
	{}
};


namespace EIncludeSuperFlag
{
	enum Type
	{
		ExcludeSuper,
		IncludeSuper
	};
}

struct FClassFunctionLinkInfo
{
	UFunction* (*CreateFuncPtr)();
	const char* FuncNameUTF8;
};


/**
 * An object class.
 */
class COREUOBJECT_API UClass : public UStruct
{
	DECLARE_CASTED_CLASS_INTRINSIC_NO_CTOR(UClass, UStruct, 0, TEXT("/Script/CoreUObject"), CASTCLASS_UClass, NO_API)
	DECLARE_WITHIN_UPACKAGE()

public:
	friend class FRestoreClassInfo;
	friend class FBlueprintEditorUtils;

	typedef void		(*ClassConstructorType)				(const FObjectInitializer&);
	typedef UObject*	(*ClassVTableHelperCtorCallerType)	(FVTableHelper& Helper);
	typedef void		(*ClassAddReferencedObjectsType)	(UObject*, class FReferenceCollector&);
	typedef UClass* (*StaticClassFunctionType)();

	ClassConstructorType ClassConstructor;
	ClassVTableHelperCtorCallerType ClassVTableHelperCtorCaller;
	/** Pointer to a static AddReferencedObjects method. */
	ClassAddReferencedObjectsType ClassAddReferencedObjects;

	/** Class pseudo-unique counter; used to accelerate unique instance name generation */
	mutable uint32 ClassUnique:31;

	/** Used to check if the class was cooked or not */
	uint32 bCooked:1;

	/** Class flags; See EClassFlags for more information */
	EClassFlags ClassFlags;

	/** Cast flags used to accelerate dynamic_cast<T*> on objects of this type for common T */
	EClassCastFlags ClassCastFlags;

	/** The required type for the outer of instances of this class */
	UClass* ClassWithin;

	/** This is the blueprint that caused the generation of this class, or null if it is a native compiled-in class */
	UObject* ClassGeneratedBy;

#if WITH_EDITORONLY_DATA
	/** Linked list of properties to be destroyed when this class is destroyed that couldn't be destroyed in PurgeClass **/
	FField* PropertiesPendingDestruction;

	/** Destroys properties that couldn't be destroyed in PurgeClass */
	void DestroyPropertiesPendingDestruction();
#endif

#if WITH_EDITOR
	/**
	 * Conditionally recompiles the class after loading, in case any dependencies were also newly loaded
	 * @param ObjLoaded	If set this is the list of objects that are currently loading, usualy GObjLoaded
	 */
	virtual void ConditionalRecompileClass(FUObjectSerializeContext* InLoadContext) {}
	virtual void FlushCompilationQueueForLevel() {}
#endif //WITH_EDITOR

	/** Which Name.ini file to load Config variables out of */
	FName ClassConfigName;

	/** List of replication records */
	TArray<FRepRecord> ClassReps;

	/** List of network relevant fields (functions) */
	TArray<UField*> NetFields;

	/** Index of the first ClassRep that belongs to this class. Anything before that was defined by / belongs to parent classes. */
	int32 FirstOwnedClassRep = 0;

#if WITH_EDITOR || HACK_HEADER_GENERATOR 
	// Editor only properties
	void GetHideFunctions(TArray<FString>& OutHideFunctions) const;
	bool IsFunctionHidden(const TCHAR* InFunction) const;
	void GetAutoExpandCategories(TArray<FString>& OutAutoExpandCategories) const;
	bool IsAutoExpandCategory(const TCHAR* InCategory) const;
	void GetAutoCollapseCategories(TArray<FString>& OutAutoCollapseCategories) const;
	bool IsAutoCollapseCategory(const TCHAR* InCategory) const;
	void GetClassGroupNames(TArray<FString>& OutClassGroupNames) const;
	bool IsClassGroupName(const TCHAR* InGroupName) const;
#endif
	/**
	 * Calls AddReferencedObjects static method on the specified object.
	 *
	 * @param This Object to call ARO on.
	 * @param Collector Reference collector.
	 */
	FORCEINLINE void CallAddReferencedObjects(UObject* This, FReferenceCollector& Collector) const
	{
		// The object must of this class type.
		check(This->IsA(this)); 
		// This is should always be set to something, at the very least to UObject::ARO
		check(ClassAddReferencedObjects != nullptr);
		ClassAddReferencedObjects(This, Collector);
	}

	/** The class default object; used for delta serialization and object initialization */
	UObject* ClassDefaultObject;

protected:
	/** This is where we store the data that is only changed per class instead of per instance */
	UPROPERTY()
	void* SparseClassData;

	/** The struct used to store sparse class data. */
	UPROPERTY()
	UScriptStruct* SparseClassDataStruct;

public:
	/**
	 * Returns a pointer to the sidecar data structure. This function will create an instance of the data structure if one has been specified and it has not yet been created.
	 */
	void* GetOrCreateSparseClassData() { return SparseClassData ? SparseClassData : CreateSparseClassData(); }

	/**
	 * Returns a pointer to the type of the sidecar data structure if one is specified.
	 */
	virtual UScriptStruct* GetSparseClassDataStruct() const;

	void SetSparseClassDataStruct(UScriptStruct* InSparseClassDataStruct);

	/** Assemble reference token streams for all classes if they haven't had it assembled already */
	static void AssembleReferenceTokenStreams();

#if WITH_EDITOR
	void GenerateFunctionList(TArray<FName>& OutArray) const 
	{ 
		FuncMap.GenerateKeyArray(OutArray); 
	}
#endif // WITH_EDITOR

private:
	void* CreateSparseClassData();

	void CleanupSparseClassData();

#if WITH_EDITOR
	/** Provides access to attributes of the underlying C++ class. Should never be unset. */
	TOptional<FCppClassTypeInfo> CppTypeInfo;
#endif

	/** Map of all functions by name contained in this class */
	TMap<FName, UFunction*> FuncMap;

	/** A cache of all functions by name that exist in a parent (superclass or interface) context */
	mutable TMap<FName, UFunction*> SuperFuncMap;

	/** Scope lock to avoid the SuperFuncMap being read and written to simultaneously on multiple threads. */
	mutable FRWLock SuperFuncMapLock;

public:
	/**
	 * The list of interfaces which this class implements, along with the pointer property that is located at the offset of the interface's vtable.
	 * If the interface class isn't native, the property will be null.
	 */
	TArray<FImplementedInterface> Interfaces;

	/** Reference token stream used by realtime garbage collector, finalized in AssembleReferenceTokenStream */
	FGCReferenceTokenStream ReferenceTokenStream;
	/** CS for the token stream. Token stream can assemble code can sometimes be called from two threads throuh a web of async loading calls. */
	FCriticalSection ReferenceTokenStreamCritical;

	/** This class's native functions. */
	TArray<FNativeFunctionLookup> NativeFunctionLookupTable;

public:
	// Constructors
	UClass(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	explicit UClass(const FObjectInitializer& ObjectInitializer, UClass* InSuperClass);
	UClass( EStaticConstructor, FName InName, uint32 InSize, uint32 InAlignment, EClassFlags InClassFlags, EClassCastFlags InClassCastFlags,
		const TCHAR* InClassConfigName, EObjectFlags InFlags, ClassConstructorType InClassConstructor,
		ClassVTableHelperCtorCallerType InClassVTableHelperCtorCaller,
		ClassAddReferencedObjectsType InClassAddReferencedObjects);

#if WITH_HOT_RELOAD
	/**
	 * Called when a class is reloading from a DLL...updates various information in-place.
	 * @param	InSize							sizeof the class
	 * @param	InClassFlags					Class flags for the class
	 * @param	InClassCastFlags				Cast Flags for the class
	 * @param	InConfigName					Config Name
	 * @param	InClassConstructor				Pointer to InternalConstructor<TClass>
	 * @param	TClass_Super_StaticClass		Static class of the super class
	 * @param	TClass_WithinClass_StaticClass	Static class of the WithinClass
	 */
	bool HotReloadPrivateStaticClass(
		uint32			InSize,
		EClassFlags		InClassFlags,
		EClassCastFlags	InClassCastFlags,
		const TCHAR*    InConfigName,
		ClassConstructorType InClassConstructor,
		ClassVTableHelperCtorCallerType InClassVTableHelperCtorCaller,
		ClassAddReferencedObjectsType InClassAddReferencedObjects,
		class UClass* TClass_Super_StaticClass,
		class UClass* TClass_WithinClass_StaticClass
		);


	/**
	* Replace a native function in the  internal native function table
	* @param	InName							name of the function
	* @param	InPointer						pointer to the function
	* @param	bAddToFunctionRemapTable		For C++ hot-reloading, UFunctions are patched in a deferred manner and this should be true
	*											For script hot-reloading, script integrations may have a many to 1 mapping of UFunction to native pointer
	*											because dispatch is shared, so the C++ remap table does not work in this case, and this should be false
	* @return	true if the function was found and replaced, false if it was not
	*/
	bool ReplaceNativeFunction(FName InName, FNativeFuncPtr InPointer, bool bAddToFunctionRemapTable);
#endif

	/**
	 * If there are potentially multiple versions of this class (e.g. blueprint generated classes), this function will return the authoritative version, which should be used for references
	 *
	 * @return The version of this class that references should be stored to
	 */
	virtual UClass* GetAuthoritativeClass();
	const UClass* GetAuthoritativeClass() const { return const_cast<UClass*>(this)->GetAuthoritativeClass(); }

	/**
	 * Add a native function to the internal native function table
	 * @param	InName							name of the function
	 * @param	InPointer						pointer to the function
	 */
	void AddNativeFunction(const ANSICHAR* InName, FNativeFuncPtr InPointer);

	/**
	 * Add a native function to the internal native function table, but with a unicode name. Used when generating code from blueprints, 
	 * which can have unicode identifiers for functions and properties.
	 * @param	InName							name of the function
	 * @param	InPointer						pointer to the function
	 */
	void AddNativeFunction(const WIDECHAR* InName, FNativeFuncPtr InPointer);

	/** Add a function to the function map */
	void AddFunctionToFunctionMap(UFunction* Function, FName FuncName)
	{
		FuncMap.Add(FuncName, Function);
	}

	void CreateLinkAndAddChildFunctionsToMap(const FClassFunctionLinkInfo* Functions, uint32 NumFunctions);

	/** Remove a function from the function map */
	void RemoveFunctionFromFunctionMap(UFunction* Function)
	{
		FuncMap.Remove(Function->GetFName());
	}

	/** Clears the function name caches, in case things have changed */
	void ClearFunctionMapsCaches();

	/** Looks for a given function name */
	UFunction* FindFunctionByName(FName InName, EIncludeSuperFlag::Type IncludeSuper = EIncludeSuperFlag::IncludeSuper) const;

	// UObject interface.
	virtual void Serialize(FArchive& Ar) override;
	virtual void PostLoad() override;
	virtual void FinishDestroy() override;
	virtual void DeferredRegister(UClass *UClassStaticClass,const TCHAR* PackageName,const TCHAR* InName) override;
	virtual bool Rename(const TCHAR* NewName = nullptr, UObject* NewOuter = nullptr, ERenameFlags Flags = REN_None) override;
	virtual void TagSubobjects(EObjectFlags NewFlags) override;
	virtual void PostInitProperties() override;
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);
	virtual FRestoreForUObjectOverwrite* GetRestoreForUObjectOverwrite() override;
	virtual FString GetDesc() override;
	virtual void GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const override;
	virtual bool IsAsset() const override { return false; }	
	virtual bool IsNameStableForNetworking() const override { return true; } // For now, assume all classes have stable net names
	// End of UObject interface.

	// UField interface.
	virtual void Bind() override;
	virtual const TCHAR* GetPrefixCPP() const override;
	// End of UField interface.

	// UStruct interface.
	virtual void Link(FArchive& Ar, bool bRelinkExistingProperties) override;
	virtual void SetSuperStruct(UStruct* NewSuperStruct) override;
	virtual bool IsStructTrashed() const override;
	// End of UStruct interface.

#if WITH_EDITOR
	/** Provides access to C++ type info. */
	const ICppClassTypeInfo* GetCppTypeInfo() const
	{
		return CppTypeInfo ? &CppTypeInfo.GetValue() : nullptr;
	}
#endif

	/** Sets C++ type information. Should not be NULL. */
	void SetCppTypeInfoStatic(const FCppClassTypeInfoStatic* InCppTypeInfoStatic)
	{
#if WITH_EDITOR
		check(InCppTypeInfoStatic);
		CppTypeInfo.Emplace(InCppTypeInfoStatic);
#endif
	}
	
	/**
	 * Translates the hardcoded script config names (engine, editor, input and 
	 * game) to their global pendants and otherwise uses config(myini) name to
	 * look for a game specific implementation and creates one based on the
	 * default if it doesn't exist yet.
	 *
	 * @return	name of the class specific ini file
	 */
	const FString GetConfigName() const;

	/** Returns parent class, the parent of a Class is always another class */
	UClass* GetSuperClass() const
	{
		return (UClass*)GetSuperStruct();
	}

	/** Feedback context for default property import **/
	static class FFeedbackContext& GetDefaultPropertiesFeedbackContext();

	/** Returns amount of memory used by default object */
	int32 GetDefaultsCount()
	{
		return ClassDefaultObject != nullptr ? GetPropertiesSize() : 0;
	}

	/**
	 * Get the default object from the class
	 * @param	bCreateIfNeeded if true (default) then the CDO is created if it is null
	 * @return		the CDO for this class
	 */
	UObject* GetDefaultObject(bool bCreateIfNeeded = true) const
	{
		if (ClassDefaultObject == nullptr && bCreateIfNeeded)
		{
			const_cast<UClass*>(this)->CreateDefaultObject();
		}

		return ClassDefaultObject;
	}

	/**
	 * Called after PostInitProperties during object construction to allow class specific initialization of an object instance.
	 */
	virtual void PostInitInstance(UObject* InObj) {}

	/**
	 * Helper method to assist with initializing object properties from an explicit list.
	 *
	 * @param	InStruct			the current scope for which the given property list applies
	 * @param	DataPtr				destination address (where to start copying values to)
	 * @param	DefaultDataPtr		source address (where to start copying the defaults data from)
	 */
	virtual void InitPropertiesFromCustomList(uint8* DataPtr, const uint8* DefaultDataPtr) {}

	/**
	 * Allows class to provide data to the object initializer that can affect how native class subobjects are created.
	 */
	virtual void SetupObjectInitializer(FObjectInitializer& ObjectInitializer) const {}

	/**
	 * Get the name of the CDO for the this class
	 * @return The name of the CDO
	 */
	FName GetDefaultObjectName() const;

	/** Returns memory used to store temporary data on an instance, used by blueprints */
	virtual uint8* GetPersistentUberGraphFrame(UObject* Obj, UFunction* FuncToCheck) const
	{
		return nullptr;
	}

	/** Creates memory to store temporary data */
	virtual void CreatePersistentUberGraphFrame(UObject* Obj, bool bCreateOnlyIfEmpty = false, bool bSkipSuperClass = false, UClass* OldClass = nullptr) const
	{
	}
	
	/** Clears memory to store temporary data */
	virtual void DestroyPersistentUberGraphFrame(UObject* Obj, bool bSkipSuperClass = false) const
	{
	}

	/**
	 * Get the default object from the class and cast to a particular type
	 * @return		the CDO for this class
	 */
	template<class T>
	T* GetDefaultObject() const
	{
		UObject *Ret = GetDefaultObject();
		check(Ret->IsA(T::StaticClass()));
		return (T*)Ret;
	}

	/** Searches for the default instanced object (often a component) by name **/
	UObject* GetDefaultSubobjectByName(FName ToFind);

	/** Adds a new default instance map item **/
	void AddDefaultSubobject(UObject* NewSubobject, UClass* BaseClass)
	{
		// this compoonent must be a derived class of the base class
		check(NewSubobject->IsA(BaseClass));
		// the outer of the component must be of my class or some superclass of me
		check(IsChildOf(NewSubobject->GetOuter()->GetClass()));
	}

	/**
	 * Gets all default instanced objects (often components).
	 *
	 * @param OutDefaultSubobjects An array to be filled with default subobjects.
	 */
	void GetDefaultObjectSubobjects(TArray<UObject*>& OutDefaultSubobjects);

	/**
	 * Used to safely check whether the passed in flag is set.
	 *
	 * @param	FlagsToCheck		Class flag(s) to check for
	 *
	 * @return	true if the passed in flag is set, false otherwise
	 *			(including no flag passed in, unless the FlagsToCheck is CLASS_AllFlags)
	 */
	FORCEINLINE bool HasAnyClassFlags( EClassFlags FlagsToCheck ) const
	{
		return EnumHasAnyFlags(ClassFlags, FlagsToCheck) != 0;
	}

	/**
	 * Used to safely check whether all of the passed in flags are set.
	 *
	 * @param FlagsToCheck	Class flags to check for
	 * @return true if all of the passed in flags are set (including no flags passed in), false otherwise
	 */
	FORCEINLINE bool HasAllClassFlags( EClassFlags FlagsToCheck ) const
	{
		return EnumHasAllFlags(ClassFlags, FlagsToCheck);
	}

	/**
	 * Gets the class flags.
	 *
	 * @return	The class flags.
	 */
	FORCEINLINE EClassFlags GetClassFlags() const
	{
		return ClassFlags;
	}

	/**
	 * Used to safely check whether the passed in flag is set.
	 *
	 * @param	FlagToCheck		the cast flag to check for (value should be one of the EClassCastFlags enums)
	 *
	 * @return	true if the passed in flag is set, false otherwise
	 *			(including no flag passed in)
	 */
	FORCEINLINE bool HasAnyCastFlag(EClassCastFlags FlagToCheck) const
	{
		return (ClassCastFlags&FlagToCheck) != 0;
	}
	FORCEINLINE bool HasAllCastFlags(EClassCastFlags FlagsToCheck) const
	{
		return (ClassCastFlags&FlagsToCheck) == FlagsToCheck;
	}

	FString GetDescription() const;

	/**
	 * Realtime garbage collection helper function used to emit token containing information about a 
	 * direct UObject reference at the passed in offset.
	 *
	 * @param Offset	Offset into object at which object reference is stored.
	 * @param DebugName	DebugName for this objects token. Only used in non-shipping builds.
	 * @param Kind		Optional parameter the describe the type of the reference.
	 */
	void EmitObjectReference(int32 Offset, const FName& DebugName, EGCReferenceType Kind = GCRT_Object);

	/**
	 * Realtime garbage collection helper function used to emit token containing information about a 
	 * an array of UObject references at the passed in offset. Handles both TArray and TTransArray.
	 *
	 * @param Offset	Offset into object at which array of objects is stored.
	 * @param DebugName	DebugName for this objects token. Only used in non-shipping builds.
	 */
	void EmitObjectArrayReference(int32 Offset, const FName& DebugName);

	/**
	 * Realtime garbage collection helper function used to indicate an array of structs at the passed in 
	 * offset.
	 *
	 * @param Offset	Offset into object at which array of structs is stored
	 * @param DebugName	DebugName for this objects token. Only used in non-shipping builds.
	 * @param Stride	Size/stride of struct
	 * @return	Index into token stream at which later on index to next token after the array is stored
	 *			which is used to skip over empty dynamic arrays
	 */
	uint32 EmitStructArrayBegin(int32 Offset, const FName& DebugName, int32 Stride);

	/**
	 * Realtime garbage collection helper function used to indicate the end of an array of structs. The
	 * index following the current one will be written to the passed in SkipIndexIndex in order to be
	 * able to skip tokens for empty dynamic arrays.
	 *
	 * @param SkipIndexIndex
	 */
	void EmitStructArrayEnd(uint32 SkipIndexIndex);

	/**
	 * Realtime garbage collection helper function used to indicate the beginning of a fixed array.
	 * All tokens issues between Begin and End will be replayed Count times.
	 *
	 * @param Offset	Offset at which fixed array starts.
	 * @param DebugName	DebugName for this objects token. Only used in non-shipping builds.
	 * @param Stride	Stride of array element, e.g. sizeof(struct) or sizeof(UObject*).
	 * @param Count		Fixed array count.
	 */
	void EmitFixedArrayBegin(int32 Offset, const FName& DebugName, int32 Stride, int32 Count);
	
	/**
	 * Realtime garbage collection helper function used to indicated the end of a fixed array.
	 */
	void EmitFixedArrayEnd();

	/**
	 * Realtime garbage collection helper function used to emit token containing information about an
	 * external package reference.
	 */
	void EmitExternalPackageReference();

	/**
	 * Assembles the token stream for realtime garbage collection by combining the per class only
	 * token stream for each class in the class hierarchy. This is only done once and duplicate
	 * work is avoided by using an object flag.
	 * @param bForce Assemble the stream even if it has been already assembled (deletes the old one)
	 */
	void AssembleReferenceTokenStream(bool bForce = false);

	/** 
	 * This will return whether or not this class implements the passed in class / interface 
	 *
	 * @param SomeClass - the interface to check and see if this class implements it
	 */
	bool ImplementsInterface(const class UClass* SomeInterface) const;

	/** serializes the passed in object as this class's default object using the given archive slot
	 * @param Object the object to serialize as default
	 * @param Slot the structured archive slot to serialize from
	 */
	virtual void SerializeDefaultObject(UObject* Object, FStructuredArchive::FSlot Slot);

	/** serializes the passed in object as this class's default object using the given archive
	 * @param Object the object to serialize as default
	 * @param Ar the archive to serialize from
	 */
	virtual void SerializeDefaultObject(UObject* Object, FArchive& Ar) final
	{
		SerializeDefaultObject(Object, FStructuredArchiveFromArchive(Ar).GetSlot());
	}

	/** serializes the associated sparse class data for the passed in object using the given archive slot. This should only be called if the class has an associated sparse data structure.
	 * @param Slot the structured archive slot to serialize from
	 */
	void SerializeSparseClassData(FStructuredArchive::FSlot Slot);

	/** Wraps the PostLoad() call for the class default object.
	 * @param Object the default object to call PostLoad() on
	 */
	virtual void PostLoadDefaultObject(UObject* Object) { Object->PostLoad(); }

	/** 
	 * Purges out the properties of this class in preparation for it to be regenerated
	 * @param bRecompilingOnLoad - true if we are recompiling on load
	 *
	 * In editor, properties are not freed until DestroyPropertiesPendingDestruction is called.
	 */
	virtual void PurgeClass(bool bRecompilingOnLoad);

	/**
	 * Finds the common base class that parents the two classes passed in.
	 *
	 * @param InClassA		the first class to find the common base for
	 * @param InClassB		the second class to find the common base for
	 * @return				the common base class or NULL
	 */
	static UClass* FindCommonBase(UClass* InClassA, UClass* InClassB);

	/**
	 * Finds the common base class that parents the array of classes passed in.
	 *
	 * @param InClasses		the array of classes to find the common base for
	 * @return				the common base class or NULL
	 */
	static UClass* FindCommonBase(const TArray<UClass*>& InClasses);

	/**
	 * Determines if the specified function has been implemented in a Blueprint
	 *
	 * @param InFunctionName	The name of the function to test
	 * @return					True if the specified function exists and is implemented in a blueprint generated class
	 */
	virtual bool IsFunctionImplementedInScript(FName InFunctionName) const;

	UE_DEPRECATED(4.23, "IsFunctionImplementedInBlueprint is deprecated, call IsFunctionImplementedInScript instead")
	bool IsFunctionImplementedInBlueprint(FName InFunctionName) const { return IsFunctionImplementedInScript(InFunctionName); }

	/**
	 * Checks if the property exists on this class or a parent class.
	 * @param InProperty	The property to check if it is contained in this or a parent class.
	 * @return				True if the property exists on this or a parent class.
	 */
	virtual bool HasProperty(FProperty* InProperty) const;

	/** Finds the object that is used as the parent object when serializing properties, overridden for blueprints */
	virtual UObject* FindArchetype(const UClass* ArchetypeClass, const FName ArchetypeName) const { return nullptr; }

	/** Returns archetype object for CDO */
	virtual UObject* GetArchetypeForCDO() const;

	/** Returns archetype for sparse class data */
	virtual void* GetArchetypeForSparseClassData() const;

	/** Returns the struct used by the sparse class data archetype */
	UScriptStruct* GetSparseClassDataArchetypeStruct() const;

	/**
	* Returns all objects that should be preloaded before the class default object is serialized at load time. Only used by the EDL.
	*
	* @param OutDeps		All objects that should be preloaded before the class default object is serialized at load time.
	*/
	virtual void GetDefaultObjectPreloadDependencies(TArray<UObject*>& OutDeps) {}

	/**
	 * Initializes the ClassReps and NetFields arrays used by replication.
	 * This happens lazily based on the CLASS_ReplicationDataIsSetUp flag,
	 * and will generally occur in Link or PostLoad. It's possible that replicated UFunctions
	 * will load after their owning class, so UFunction::PostLoad will clear the flag on its owning class
	 * to force lazy initialization next time the data is needed.
	 * Also happens after blueprint compiliation.
	 */
	void SetUpRuntimeReplicationData();

#if HACK_HEADER_GENERATOR
	void SetUpUhtReplicationData();
#endif  // HACK_HEADER_GENERATOR

	/**
	 * Helper function for determining if the given class is compatible with structured archive serialization
	 */
	static bool IsSafeToSerializeToStructuredArchives(UClass* InClass);

private:
	/** 
	 * This signature intentionally hides the method declared in UObjectBaseUtility to make it private.
	 * Call IsChildOf instead; Hidden because calling IsA on a class almost always indicates an error where the caller should use IsChildOf
	 */
	bool IsA(const UClass* Parent) const
	{
		return UObject::IsA(Parent);
	}

	/** 
	 * This signature intentionally hides the method declared in UObject to make it private.
	 * Call FindFunctionByName instead; This method will search for a function declared in UClass instead of the class it was called on
	 */
	UFunction* FindFunction(FName InName) const
	{
		return UObject::FindFunction(InName);
	}

	/** 
	 * This signature intentionally hides the method declared in UObject to make it private.
	 * Call FindFunctionByName instead; This method will search for a function declared in UClass instead of the class it was called on
	 */
	UFunction* FindFunctionChecked(FName InName) const
	{
		return UObject::FindFunctionChecked(InName);
	}

	/**
	 * Tests if all properties tagged with Replicate were registered in GetLifetimeReplicatedProps
	 */
	void ValidateRuntimeReplicationData();

protected:
	/**
	 * Get the default object from the class, creating it if missing, if requested or under a few other circumstances
	 * @return		the CDO for this class
	 **/
	virtual UObject* CreateDefaultObject();

#if HACK_HEADER_GENERATOR
	// Required by UHT makefiles for internal data serialization.
	friend struct FClassArchiveProxy;
#endif // HACK_HEADER_GENERATOR
};

/**
* Dynamic class (can be constructed after initial startup)
*/
class COREUOBJECT_API UDynamicClass : public UClass
{
	DECLARE_CASTED_CLASS_INTRINSIC_NO_CTOR(UDynamicClass, UClass, 0, TEXT("/Script/CoreUObject"), CASTCLASS_None, NO_API)
	DECLARE_WITHIN_UPACKAGE()

public:

	typedef void (*DynamicClassInitializerType)	(UDynamicClass*);

	UDynamicClass(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	explicit UDynamicClass(const FObjectInitializer& ObjectInitializer, UClass* InSuperClass);
	UDynamicClass(EStaticConstructor, FName InName, uint32 InSize, uint32 InAlignment, EClassFlags InClassFlags, EClassCastFlags InClassCastFlags,
		const TCHAR* InClassConfigName, EObjectFlags InFlags, ClassConstructorType InClassConstructor,
		ClassVTableHelperCtorCallerType InClassVTableHelperCtorCaller,
		ClassAddReferencedObjectsType InClassAddReferencedObjects,
		DynamicClassInitializerType InDynamicClassInitializer);

	// UObject interface.
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	// UClass interface
	virtual UObject* CreateDefaultObject();
	virtual void PurgeClass(bool bRecompilingOnLoad) override;
	virtual UObject* FindArchetype(const UClass* ArchetypeClass, const FName ArchetypeName) const override;
	virtual void SetupObjectInitializer(FObjectInitializer& ObjectInitializer) const override;

	/** Find a struct property, called from generated code */
	FStructProperty* FindStructPropertyChecked(const TCHAR* PropertyName) const;

	/** Misc objects owned by the class. */
	TArray<UObject*> MiscConvertedSubobjects;

	/** Additional converted fields, that are used by the class. */
	TArray<UField*> ReferencedConvertedFields;

	/** Outer assets used by the class */
	TArray<UObject*> UsedAssets;

	/** Specialized sub-object containers */
	TArray<UObject*> DynamicBindingObjects;
	TArray<UObject*> ComponentTemplates;
	TArray<UObject*> Timelines;

	/** Array of blueprint overrides of component classes in parent classes */
	TArray<TPair<FName, UClass*>> ComponentClassOverrides;

	/** IAnimClassInterface (UAnimClassData) or null */
	UObject* AnimClassImplementation;

	DynamicClassInitializerType DynamicClassInitializer;

	/** Prefix for the temporary package where the dynamic classes are stored when being generated */
	static const FString& GetTempPackagePrefix();
};

/**
 * Helper template to call the default constructor for a class
 */
template<class T>
void InternalConstructor( const FObjectInitializer& X )
{ 
	T::__DefaultConstructor(X);
}


/**
 * Helper template to call the vtable ctor caller for a class
 */
template<class T>
UObject* InternalVTableHelperCtorCaller(FVTableHelper& Helper)
{
	return T::__VTableCtorCaller(Helper);
}

COREUOBJECT_API void InitializePrivateStaticClass(
	class UClass* TClass_Super_StaticClass,
	class UClass* TClass_PrivateStaticClass,
	class UClass* TClass_WithinClass_StaticClass,
	const TCHAR* PackageName,
	const TCHAR* Name
	);

/**
 * Helper template allocate and construct a UClass
 *
 * @param PackageName name of the package this class will be inside
 * @param Name of the class
 * @param ReturnClass reference to pointer to result. This must be PrivateStaticClass.
 * @param RegisterNativeFunc Native function registration function pointer.
 * @param InSize Size of the class
 * @param InAlignment Alignment of the class
 * @param InClassFlags Class flags
 * @param InClassCastFlags Class cast flags
 * @param InConfigName Class config name
 * @param InClassConstructor Class constructor function pointer
 * @param InClassVTableHelperCtorCaller Class constructor function for vtable pointer
 * @param InClassAddReferencedObjects Class AddReferencedObjects function pointer
 * @param InSuperClassFn Super class function pointer
 * @param WithinClass Within class
 * @param bIsDynamic true if the class can be constructed dynamically at runtime
 */
COREUOBJECT_API void GetPrivateStaticClassBody(
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
	bool bIsDynamic = false,
	UDynamicClass::DynamicClassInitializerType InDynamicClassInitializer = nullptr);

/*-----------------------------------------------------------------------------
	FObjectInstancingGraph.
-----------------------------------------------------------------------------*/

struct COREUOBJECT_API FObjectInstancingGraph
{
public:

	/** 
	 * Default Constructor 
	 * @param bDisableInstancing - if true, start with component instancing disabled
	**/
	FObjectInstancingGraph(bool bDisableInstancing = false);

	/**
	 * Standard constructor
	 *
	 * @param	DestinationSubobjectRoot	the top-level object that is being created
	 */
	FObjectInstancingGraph( class UObject* DestinationSubobjectRoot );

	/**
	 * Sets the DestinationRoot for this instancing graph.
	 *
	 * @param	DestinationSubobjectRoot	the top-level object that is being created
	 * @param	InSourceRoot	Archetype of DestinationSubobjectRoot
	 */
	void SetDestinationRoot( class UObject* DestinationSubobjectRoot, class UObject* InSourceRoot = nullptr );

	/**
	 * Finds the destination object instance corresponding to the specified source object.
	 *
	 * @param	SourceObject			the object to find the corresponding instance for
	 */
	class UObject* GetDestinationObject(class UObject* SourceObject);

	/**
	 * Returns the component that has SourceComponent as its archetype, instancing the component as necessary.
	 *
	 * @param	SourceComponent		the component to find the corresponding component instance for
	 * @param	CurrentValue		the component currently assigned as the value for the component property
	 *								being instanced.  Used when updating archetypes to ensure that the new instanced component
	 *								replaces the existing component instance in memory.
	 * @param	CurrentObject		the object that owns the component property currently being instanced;  this is NOT necessarily the object
	 *								that should be the Outer for the new component.
	 * @param	bIsTransient		is this for a transient property?
	 * @param	bCausesInstancing	if true, then this property causes an instance to be created...if false, this is just a pointer to a uobject that should be remapped if the object is instanced for some other property
	 * @param	bAllowSelfReference If true, instance the reference to the subobjectroot, so far only delegates remap a self reference
	 *
	 * @return	As with GetInstancedSubobject, above, but also deals with archetype creation and a few other special cases
	 */
	class UObject* InstancePropertyValue( class UObject* SourceComponent, class UObject* CurrentValue, class UObject* CurrentObject, bool bIsTransient, bool bCausesInstancing = false, bool bAllowSelfReference = false );

	/**
	 * Adds a partially built object instance to the map(s) of source objects to their instances.
	 * @param	ObjectInstance  Object that was just allocated, but has not been constructed yet
	 * @param	InArchetype     Archetype of ObjectInstance
	 */
	void AddNewObject(class UObject* ObjectInstance, class UObject* InArchetype = nullptr);

	/**
	 * Adds an object instance to the map of source objects to their instances.  If there is already a mapping for this object, it will be replaced
	 * and the value corresponding to ObjectInstance's archetype will now point to ObjectInstance.
	 *
	 * @param	ObjectInstance  the object that should be added as the corresopnding instance for ObjectSource
	 * @param	InArchetype     Archetype of ObjectInstance
	 */
	void AddNewInstance(class UObject* ObjectInstance, class UObject* InArchetype = nullptr);

	/**
	 * Retrieves a list of objects that have the specified Outer
	 *
	 * @param	SearchOuter		the object to retrieve object instances for
	 * @param	out_Components	receives the list of objects contained by SearchOuter
	 */
	void RetrieveObjectInstances( class UObject* SearchOuter, TArray<class UObject*>& out_Objects );

	/**
	 * Allows looping over instances that were created during this instancing.
	 *
	 * @param	Pred		the object to retrieve object instances for
	 */
	template <typename Predicate>
	void ForEachObjectInstance(Predicate Pred)
	{
		for (TMap<UObject*, UObject*>::TIterator It(SourceToDestinationMap); It; ++It)
		{
			UObject* InstancedObject = It.Value();
			Pred(InstancedObject);
		}
	}

	/**
	 * Enables / disables component instancing.
	 */
	void EnableSubobjectInstancing( bool bEnabled )
	{
		bEnableSubobjectInstancing = bEnabled;
	}

	/**
	 * Returns whether component instancing is enabled
	 */
	bool IsSubobjectInstancingEnabled() const
	{
		return bEnableSubobjectInstancing;
	}

	/**
	 * Sets whether DestinationRoot is currently being loaded from disk.
	 */
	void SetLoadingObject( bool bIsLoading )
	{
		bLoadingObject = bIsLoading;
	}

private:
	/**
	 * Returns whether this instancing graph has a valid destination root.
	 */
	bool HasDestinationRoot() const
	{
		return DestinationRoot != nullptr;
	}

	/**
	 * Returns whether DestinationRoot corresponds to an archetype object.
	 *
	 * @param	bUserGeneratedOnly	true indicates that we only care about cases where the user selected "Create [or Update] Archetype" in the editor
	 *								false causes this function to return true even if we are just loading an archetype from disk
	 */
	bool IsCreatingArchetype( bool bUserGeneratedOnly=true ) const
	{
		// if we only want cases where we are creating an archetype in response to user input, return false if we are in fact just loading the object from disk
		return bCreatingArchetype && (!bUserGeneratedOnly || !bLoadingObject);
	}

	/**
	 * Returns whether DestinationRoot is currently being loaded from disk.
	 */
	bool IsLoadingObject() const
	{
		return bLoadingObject;
	}

	/**
	 * Returns the component that has SourceComponent as its archetype, instancing the component as necessary.
	 *
	 * @param	SourceComponent		the component to find the corresponding component instance for
	 * @param	CurrentValue		the component currently assigned as the value for the component property
	 *								being instanced.  Used when updating archetypes to ensure that the new instanced component
	 *								replaces the existing component instance in memory.
	 * @param	CurrentObject		the object that owns the component property currently being instanced;  this is NOT necessarily the object
	 *								that should be the Outer for the new component.
	 * @param	bDoNotCreateNewInstance If true, then we do not create a new instance, but we will reassign one if there is already a mapping in the table
	 * @param	bAllowSelfReference If true, instance the reference to the subobjectroot, so far only delegates remap a self reference
	 *
	 * @return	if SourceComponent is contained within SourceRoot, returns a pointer to a unique component instance corresponding to
	 *			SourceComponent if SourceComponent is allowed to be instanced in this context, or NULL if the component isn't allowed to be
	 *			instanced at this time (such as when we're a client and the component isn't loaded on clients)
	 *			if SourceComponent is not contained by SourceRoot, return INVALID_OBJECT, indicating that the that has SourceComponent as its ObjectArchetype, or NULL if SourceComponent is not contained within
	 *			SourceRoot.
	 */
	class UObject* GetInstancedSubobject( class UObject* SourceSubobject, class UObject* CurrentValue, class UObject* CurrentObject, bool bDoNotCreateNewInstance, bool bAllowSelfReference );

	/**
	 * The root of the object tree that is the source used for instancing components;
	 * - when placing an instance of an actor class, this would be the actor class default object
	 * - when placing an instance of an archetype, this would be the archetype
	 * - when creating an archetype, this would be the actor instance
	 * - when duplicating an object, this would be the duplication source
	 */
	class		UObject*						SourceRoot;

	/**
	 * The root of the object tree that is the destination used for instancing components
	 * - when placing an instance of an actor class, this would be the placed actor
	 * - when placing an instance of an archetype, this would be the placed actor
	 * - when creating an archetype, this would be the actor archetype
	 * - when updating an archetype, this would be the source archetype
	 * - when duplicating an object, this would be the copied object (destination)
	 */
	class		UObject*						DestinationRoot;

	/**
	 * Indicates whether we are currently instancing components for an archetype.  true if we are creating or updating an archetype.
	 */
	bool										bCreatingArchetype;

	/**
	 * If false, components will not be instanced.
	 */
	bool										bEnableSubobjectInstancing;

	/**
	 * true when loading object data from disk.
	 */
	bool										bLoadingObject;

	/**
	 * Maps the source (think archetype) to the destination (think instance)
	 */
	TMap<class UObject*,class UObject*>			SourceToDestinationMap;
};

// UFunction interface.

inline UFunction* UFunction::GetSuperFunction() const
{
	UStruct* Result = GetSuperStruct();
	checkSlow(!Result || Result->IsA<UFunction>());
	return (UFunction*)Result;
}


// UObject.h

/**
 * Returns true if this object implements the interface T, false otherwise.
 */
template<class T>
FORCEINLINE bool UObject::Implements() const
{
	UClass const* const MyClass = GetClass();
	return MyClass && MyClass->ImplementsInterface(T::StaticClass());
}

// UObjectGlobals.h

/**
 * Gets the default object of a class.
 *
 * In most cases, class default objects should not be modified. This method therefore returns
 * an immutable pointer. If you need to modify the default object, use GetMutableDefault instead.
 *
 * @param Class - The class to get the CDO for.
 *
 * @return Class default object (CDO).
 *
 * @see GetMutableDefault
 */
template< class T > 
inline const T* GetDefault(UClass *Class)
{
	check(Class->GetDefaultObject()->IsA(T::StaticClass()));
	return (const T*)Class->GetDefaultObject();
}

/**
 * Gets the mutable default object of a class.
 *
 * @param Class - The class to get the CDO for.
 *
 * @return Class default object (CDO).
 *
 * @see GetDefault
 */
template< class T > 
inline T* GetMutableDefault(UClass *Class)
{
	check(Class->GetDefaultObject()->IsA(T::StaticClass()));
	return (T*)Class->GetDefaultObject();
}

struct FStructUtils
{
	static bool ArePropertiesTheSame(const FProperty* A, const FProperty* B, bool bCheckPropertiesNames);

	/** Do structures have exactly the same memory layout */
	COREUOBJECT_API static bool TheSameLayout(const UStruct* StructA, const UStruct* StructB, bool bCheckPropertiesNames = false);

	/** Locates a named structure in the package with the given name. Not expected to fail */
	COREUOBJECT_API static UStruct* FindStructureInPackageChecked(const TCHAR* StructName, const TCHAR* PackageName);

#if !(UE_BUILD_TEST || UE_BUILD_SHIPPING)
	/** Looks for uninitialized script struct pointers. Returns the number found */
	COREUOBJECT_API static int32 AttemptToFindUninitializedScriptStructMembers();
#endif
};

// Helper struct to test if member initialization tests work properly
struct FTestUninitializedScriptStructMembersTest
{
	UObject* UninitializedObjectReference;
	UObject* InitializedObjectReference = nullptr;
	float UnusedValue;
};

/*-----------------------------------------------------------------------------
	Mirrors of mirror structures in Object.h. These are used by generated code 
	to facilitate correct offsets and alignments for structures containing these
	odd types.
-----------------------------------------------------------------------------*/

template< class T > struct TBaseStructure
{
	static UScriptStruct* Get()
	{
		return T::StaticStruct();
	}
};

template<> struct TBaseStructure<FRotator>
{
	COREUOBJECT_API static UScriptStruct* Get();
};

template<> struct TBaseStructure<FQuat>
{
	COREUOBJECT_API static UScriptStruct* Get();
};

template<> struct TBaseStructure<FTransform>
{
	COREUOBJECT_API static UScriptStruct* Get();
};

template<> struct TBaseStructure<FLinearColor>
{
	COREUOBJECT_API static UScriptStruct* Get();
};

template<> struct TBaseStructure<FColor>
{
	COREUOBJECT_API static UScriptStruct* Get();
};

template<> struct  TBaseStructure<FPlane>
{
	COREUOBJECT_API static UScriptStruct* Get();
};

template<> struct  TBaseStructure<FVector>
{
	COREUOBJECT_API static UScriptStruct* Get();
};

template<> struct TBaseStructure<FVector2D>
{
	COREUOBJECT_API static UScriptStruct* Get();
};

template<> struct TBaseStructure<FVector4>
{
	COREUOBJECT_API static UScriptStruct* Get();
};

template<> struct TBaseStructure<FRandomStream>
{
	COREUOBJECT_API static UScriptStruct* Get();
};

template<> struct TBaseStructure<FGuid>
{
	COREUOBJECT_API static UScriptStruct* Get();
};

template<> struct TBaseStructure<FBox2D>
{
	COREUOBJECT_API static UScriptStruct* Get();
};	

template<> struct TBaseStructure<FFallbackStruct>
{
	COREUOBJECT_API static UScriptStruct* Get();
};

template<> struct TBaseStructure<FFloatRangeBound>
{
	COREUOBJECT_API static UScriptStruct* Get();
};

template<> struct TBaseStructure<FFloatRange>
{
	COREUOBJECT_API static UScriptStruct* Get();
};

template<> struct TBaseStructure<FInt32RangeBound>
{
	COREUOBJECT_API static UScriptStruct* Get();
};

template<> struct TBaseStructure<FInt32Range>
{
	COREUOBJECT_API static UScriptStruct* Get();
};

template<> struct TBaseStructure<FFloatInterval>
{
	COREUOBJECT_API static UScriptStruct* Get();
};

template<> struct TBaseStructure<FInt32Interval>
{
	COREUOBJECT_API static UScriptStruct* Get();
};

struct FFrameNumber;
template<> struct TBaseStructure<FFrameNumber>
{
	COREUOBJECT_API static UScriptStruct* Get();
};

struct FFrameTime;
template<> struct TBaseStructure<FFrameTime>
{
	COREUOBJECT_API static UScriptStruct* Get();
};

struct FSoftObjectPath;
template<> struct TBaseStructure<FSoftObjectPath>
{
	COREUOBJECT_API static UScriptStruct* Get();
};

struct FSoftClassPath;
template<> struct TBaseStructure<FSoftClassPath>
{
	COREUOBJECT_API static UScriptStruct* Get();
};

struct FPrimaryAssetType;
template<> struct TBaseStructure<FPrimaryAssetType>
{
	COREUOBJECT_API static UScriptStruct* Get();
};

struct FPrimaryAssetId;
template<> struct TBaseStructure<FPrimaryAssetId>
{
	COREUOBJECT_API static UScriptStruct* Get();
};

struct FDateTime;
template<> struct TBaseStructure<FDateTime>
{
	COREUOBJECT_API static UScriptStruct* Get();
};

struct FPolyglotTextData;
template<> struct TBaseStructure<FPolyglotTextData>
{
	COREUOBJECT_API static UScriptStruct* Get();
};

struct FAssetBundleData;
template<> struct TBaseStructure<FAssetBundleData>
{
	COREUOBJECT_API static UScriptStruct* Get();
};

template<> struct TBaseStructure<FTestUninitializedScriptStructMembersTest>
{
	COREUOBJECT_API static UScriptStruct* Get();
};
