// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Misc/Guid.h"

class FPackageIndex;

/**
 * Wrapper for index into a ULnker's ImportMap or ExportMap.
 * Values greater than zero indicate that this is an index into the ExportMap.  The
 * actual array index will be (FPackageIndex - 1).
 *
 * Values less than zero indicate that this is an index into the ImportMap. The actual
 * array index will be (-FPackageIndex - 1)
 */
class FPackageIndex
{
	/**
	 * Values greater than zero indicate that this is an index into the ExportMap.  The
	 * actual array index will be (FPackageIndex - 1).
	 *
	 * Values less than zero indicate that this is an index into the ImportMap. The actual
	 * array index will be (-FPackageIndex - 1)
	 */
	int32 Index;

	/** Internal constructor, sets the index directly **/
	FORCEINLINE explicit FPackageIndex(int32 InIndex)
		: Index(InIndex)
	{

	}
public:
	/** Constructor, sets the value to null **/
	FORCEINLINE FPackageIndex()
		: Index(0)
	{

	}
	/** return true if this is an index into the import map **/
	FORCEINLINE bool IsImport() const
	{
		return Index < 0;
	}
	/** return true if this is an index into the export map **/
	FORCEINLINE bool IsExport() const
	{
		return Index > 0;
	}
	/** return true if this null (i.e. neither an import nor an export) **/
	FORCEINLINE bool IsNull() const
	{
		return Index == 0;
	}
	/** Check that this is an import and return the index into the import map **/
	FORCEINLINE int32 ToImport() const
	{
		check(IsImport());
		return -Index - 1;
	}
	/** Check that this is an export and return the index into the export map **/
	FORCEINLINE int32 ToExport() const
	{
		check(IsExport());
		return Index - 1;
	}
	/** Return the raw value, for debugging purposes**/
	FORCEINLINE int32 ForDebugging() const
	{
		return Index;
	}

	/** Create a FPackageIndex from an import index **/
	FORCEINLINE static FPackageIndex FromImport(int32 ImportIndex)
	{
		check(ImportIndex >= 0);
		return FPackageIndex(-ImportIndex - 1);
	}
	/** Create a FPackageIndex from an export index **/
	FORCEINLINE static FPackageIndex FromExport(int32 ExportIndex)
	{
		check(ExportIndex >= 0);
		return FPackageIndex(ExportIndex + 1);
	}

	/** Compare package indecies for equality **/
	FORCEINLINE bool operator==(const FPackageIndex& Other) const
	{
		return Index == Other.Index;
	}
	/** Compare package indecies for inequality **/
	FORCEINLINE bool operator!=(const FPackageIndex& Other) const
	{
		return Index != Other.Index;
	}

	/** Compare package indecies **/
	FORCEINLINE bool operator<(const FPackageIndex& Other) const
	{
		return Index < Other.Index;
	}
	FORCEINLINE bool operator>(const FPackageIndex& Other) const
	{
		return Index > Other.Index;
	}
	FORCEINLINE bool operator<=(const FPackageIndex& Other) const
	{
		return Index <= Other.Index;
	}
	FORCEINLINE bool operator>=(const FPackageIndex& Other) const
	{
		return Index >= Other.Index;
	}
	/**
	 * Serializes a package index value from or into an archive.
	 *
	 * @param Ar - The archive to serialize from or to.
	 * @param Value - The value to serialize.
	 */
	FORCEINLINE friend FArchive& operator<<(FArchive& Ar, FPackageIndex& Value)
	{
		FStructuredArchiveFromArchive(Ar).GetSlot() << Value;
		return Ar;
	}

	/**
	 * Serializes a package index value from or into a structured archive slot.
	 *
	 * @param Slot - The structured archive slot to serialize from or to.
	 * @param Value - The value to serialize.
	 */
	FORCEINLINE friend void operator<<(FStructuredArchive::FSlot Slot, FPackageIndex& Value)
	{
		Slot << Value.Index;
	}

	FORCEINLINE friend uint32 GetTypeHash(const FPackageIndex& In)
	{
		return uint32(In.Index);
	}

	/**
		Lex functions
	*/
	/**
	Lex functions
	*/
	friend FString LexToString(const FPackageIndex& Value)
	{
		return FString::FromInt(Value.Index);
	}

	friend void LexFromString(FPackageIndex& Value, const TCHAR* String)
	{
		Value.Index = FCString::Atoi(String);
	}
};

/**
 * Base class for UObject resource types.  FObjectResources are used to store UObjects on disk
 * via FLinker's ImportMap (for resources contained in other packages) and ExportMap (for resources
 * contained within the same package)
 */
struct FObjectResource
{
	/**
	 * The name of the UObject represented by this resource.
	 * Serialized
	 */
	FName			ObjectName;

	/**
	 * Location of the resource for this resource's Outer.  Values of 0 indicate that this resource
	 * represents a top-level UPackage object (the linker's LinkerRoot).
	 * Serialized
	 */
	FPackageIndex	OuterIndex;

#if WITH_EDITORONLY_DATA
	/**
	 * Name of the class this object was serialized with (in case active class redirects have changed it)
	 * If this is a class and was directly redirected, this is what it was redirected from
	 */
	FName			OldClassName;
#endif

	FObjectResource();
	FObjectResource( UObject* InObject );
};

/*-----------------------------------------------------------------------------
	FObjectExport.
-----------------------------------------------------------------------------*/

/**
 * UObject resource type for objects that are contained within this package and can
 * be referenced by other packages.
 */
struct FObjectExport : public FObjectResource
{
	/**
	 * Location of the resource for this export's class (if non-zero).  A value of zero
	 * indicates that this export represents a UClass object; there is no resource for
	 * this export's class object
	 * Serialized
	 */
	FPackageIndex  	ClassIndex;

	/**
	* Location of this resource in export map. Used for export fixups while loading packages.
	* Value of zero indicates resource is invalid and shouldn't be loaded.
	* Not serialized.
	*/
	FPackageIndex ThisIndex;

	/**
	 * Location of the resource for this export's SuperField (parent).  Only valid if
	 * this export represents a UStruct object. A value of zero indicates that the object
	 * represented by this export isn't a UStruct-derived object.
	 * Serialized
	 */
	FPackageIndex 	SuperIndex;

	/**
	* Location of the resource for this export's template/archetypes.  Only used
	* in the new cooked loader. A value of zero indicates that the value of GetArchetype
	* was zero at cook time, which is more or less impossible and checked.
	* Serialized
	*/
	FPackageIndex 	TemplateIndex;

	/**
	 * The object flags for the UObject represented by this resource.  Only flags that
	 * match the RF_Load combination mask will be loaded from disk and applied to the UObject.
	 * Serialized
	 */
	EObjectFlags	ObjectFlags;

	/**
	 * The number of bytes to serialize when saving/loading this export's UObject.
	 * Serialized
	 */
	int64         	SerialSize;

	/**
	 * The location (into the FLinker's underlying file reader archive) of the beginning of the
	 * data for this export's UObject.  Used for verification only.
	 * Serialized
	 */
	int64         	SerialOffset;

	/**
	 * The location (into the FLinker's underlying file reader archive) of the beginning of the
	 * portion of this export's data that is serialized using script serialization.
	 * Transient
	 */
	int64				ScriptSerializationStartOffset;

	/**
	 * The location (into the FLinker's underlying file reader archive) of the end of the
	 * portion of this export's data that is serialized using script serialization.
	 * Transient
	 */
	int64				ScriptSerializationEndOffset;

	/**
	 * The UObject represented by this export.  Assigned the first time CreateExport is called for this export.
	 * Transient
	 */
	UObject*		Object;

	/**
	 * The index into the FLinker's ExportMap for the next export in the linker's export hash table.
	 * Transient
	 */
	int32				HashNext;

	/**
	 * Whether the export was forced into the export table via OBJECTMARK_ForceTagExp.
	 * Serialized
	 */
	bool			bForcedExport;   

	/**
	 * Whether the export should be loaded on clients
	 * Serialized
	 */
	bool			bNotForClient;   

	/**
	 * Whether the export should be loaded on servers
	 * Serialized
	 */
	bool			bNotForServer;

	/**
	 * Whether the export should be always loaded in editor game
	 * False means that the object is 
	 * True doesn't means, that the object won't be loaded.
	 * Serialized
	 */
	bool			bNotAlwaysLoadedForEditorGame;

	/**
	 * True if this export is an asset object.
	 */
	bool			bIsAsset;

	/**
	 * Force this export to not load, it failed because the outer didn't exist.
	 */
	bool			bExportLoadFailed;

	/**
	 * Export is a dynamic type.
	 */
	enum class EDynamicType : uint8
	{
		NotDynamicExport,
		DynamicType,
		ClassDefaultObject,
	};

	EDynamicType	DynamicType;

	/**
	 * Export was filtered out on load
	 */
	bool			bWasFiltered;

	/** If this object is a top level package (which must have been forced into the export table via OBJECTMARK_ForceTagExp)
	 * this is the GUID for the original package file
	 * Serialized
	 */
	UE_DEPRECATED(4.27, "UPackage::Guid has not been used by the engine for a long time and FObjectExport::PackageGuid will be removed.")
	FGuid			PackageGuid;

	/** If this object is a top level package (which must have been forced into the export table via OBJECTMARK_ForceTagExp)
	 * this is the package flags for the original package file
	 * Serialized
	 */
	uint32			PackageFlags;

	/**
	 * The export table must serialize as a fixed size, this is use to index into a long list, which is later loaded into the array. -1 means dependencies are not present
	 * These are contiguous blocks, so CreateBeforeSerializationDependencies starts at FirstExportDependency + SerializationBeforeSerializationDependencies
	 */
	int32 FirstExportDependency;
	int32 SerializationBeforeSerializationDependencies;
	int32 CreateBeforeSerializationDependencies;
	int32 SerializationBeforeCreateDependencies;
	int32 CreateBeforeCreateDependencies;

	/**
	 * Constructors
	 */
	COREUOBJECT_API FObjectExport();
	FObjectExport(UObject* InObject, bool bInNotAlwaysLoadedForEditorGame = true);

	// Workaround for clang deprecation warnings for deprecated PackageGuid member in implicit constructors
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FObjectExport(FObjectExport&&) = default;
	FObjectExport(const FObjectExport&) = default;
	FObjectExport& operator=(FObjectExport&&) = default;
	FObjectExport& operator=(const FObjectExport&) = default;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	/** Resets the stored Object and any transient flags */
	COREUOBJECT_API void ResetObject();
	
	/** I/O functions */
	friend COREUOBJECT_API FArchive& operator<<(FArchive& Ar, FObjectExport& E);
	friend COREUOBJECT_API void operator<<(FStructuredArchive::FSlot Slot, FObjectExport& E);
};

/*-----------------------------------------------------------------------------
	FObjectTextExport
-----------------------------------------------------------------------------*/

/**
 * Simple wrapper around a FObjectExport which does the text asset specific serialization of export data
 */
struct FObjectTextExport
{
	/**
	 * Constructor
	 */
	FObjectTextExport(FObjectExport& InExport, UObject* InOuter)
		: Export(InExport)
		, Outer(InOuter)
	{
	}

	/** The export object that we are wrapping */
	FObjectExport& Export;

	/** The outer that this export lives inside */
	UObject* Outer;

	/** String full object path for this export's class */
	FString ClassName;

	/** String full object path for this export's superstruct, if applicable */
	FString SuperStructName;

	/** String full object path for this export's outer, if applicable (i.e. if it's not the package itself) */
	FString OuterName;

	/** Serializer */
	friend COREUOBJECT_API void operator<<(FStructuredArchive::FSlot Slot, FObjectTextExport& E);
};

/*-----------------------------------------------------------------------------
	FObjectImport.
-----------------------------------------------------------------------------*/

/**
 * UObject resource type for objects that are referenced by this package, but contained
 * within another package.
 */
struct FObjectImport : public FObjectResource
{
	/**
	 * The name of the package that contains the class of the UObject represented by this resource.
	 * Serialized
	 */
	FName			ClassPackage;

	/**
	 * The name of the class for the UObject represented by this resource.
	 * Serialized
	 */
	FName			ClassName;

#if WITH_EDITORONLY_DATA
	/**
	 * Package Name this import belongs to. Can be none, in that case follow the outer chain
	 * until a set PackageName is found or until OuterIndex is null
	 * Serialized
	 */
	FName			PackageName;
#endif

	/**
	 * The UObject represented by this resource.  Assigned the first time CreateImport is called for this import.
	 * Transient
	 */
	UObject*		XObject;

	/**
	 * The linker that contains the original FObjectExport resource associated with this import.
	 * Transient
	 */
	FLinkerLoad*	SourceLinker;

	/**
	 * Index into SourceLinker's ExportMap for the export associated with this import's UObject.
	 * Transient
	 */
	int32             SourceIndex;

	bool			bImportPackageHandled;
	bool			bImportSearchedFor;
	bool			bImportFailed;

	/**
	 * Constructors
	 */
	COREUOBJECT_API FObjectImport();
	FObjectImport( UObject* InObject );
	FObjectImport( UObject* InObject, UClass* InClass );

	/**
	 * Accessor function to check if the import has package name set
	 * Handles editor only code.
	 * @returns true if the import has a PackageName set
	 */
	bool HasPackageName() const
	{
#if WITH_EDITORONLY_DATA
		return !PackageName.IsNone();
#else
		return false;
#endif
	}

	/**
	 * Accessor function to get the import package name
	 * Handles editor only code.
	 * @returns the import package name, if any
	 */
	FName GetPackageName() const
	{
#if WITH_EDITORONLY_DATA
		return PackageName;
#else
		return NAME_None;
#endif
	}

	/**
	 * Accessor function to set the import PackageName
	 * Handles editor only code.
	 * @param InPackageName the package name to set
	 */
	void SetPackageName(FName InPackageName)
	{
#if WITH_EDITORONLY_DATA
		PackageName = InPackageName;
#endif
	}
	
	/** I/O functions */
	friend COREUOBJECT_API FArchive& operator<<(FArchive& Ar, FObjectImport& I);
	friend COREUOBJECT_API void operator<<( FStructuredArchive::FSlot Slot, FObjectImport& I );
};
