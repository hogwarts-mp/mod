// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Misc/Guid.h"
#include "Serialization/CustomVersion.h"
#include "Misc/EngineVersion.h"
#include "Serialization/StructuredArchive.h"

struct FCompressedChunk;

/*----------------------------------------------------------------------------
	Items stored in Unrealfiles.
----------------------------------------------------------------------------*/

/**
 * Revision data for an Unreal package file.
 */
//@todo: shouldn't need ExportCount/NameCount with the linker free package map; if so, clean this stuff up
struct FGenerationInfo
{
	/**
	 * Number of exports in the linker's ExportMap for this generation.
	 */
	int32 ExportCount;

	/**
	 * Number of names in the linker's NameMap for this generation.
	 */
	int32 NameCount;

	/** Constructor */
	FGenerationInfo( int32 InExportCount, int32 InNameCount );

	/** I/O function
	 * we use a function instead of operator<< so we can pass in the package file summary for version tests, since archive version hasn't been set yet
	 */
	void Serialize(FArchive& Ar, const struct FPackageFileSummary& Summary);
	void Serialize(FStructuredArchive::FSlot Slot, const struct FPackageFileSummary& Summary);
};

/**
 * A "table of contents" for an Unreal package file.  Stored at the top of the file.
 */
struct FPackageFileSummary
{
	/**
	* Magic tag compared against PACKAGE_FILE_TAG to ensure that package is an Unreal package.
	*/
	int32		Tag;

private:
	/* UE4 file version */
	int32		FileVersionUE4;
	/* Licensee file version */
	int32		FileVersionLicenseeUE4;
	/* Custom version numbers. Keyed off a unique tag for each custom component. */
	FCustomVersionContainer CustomVersionContainer;

public:
	/**
	* Total size of all information that needs to be read in to create a FLinkerLoad. This includes
	* the package file summary, name table and import & export maps.
	*/
	int32		TotalHeaderSize;

	/**
	* The flags for the package
	*/
	uint32	PackageFlags;

	/**
	* The Generic Browser folder name that this package lives in
	*/
	FString	FolderName;

	/**
	* Number of names used in this package
	*/
	int32		NameCount;

	/**
	* Location into the file on disk for the name data
	*/
	int32 	NameOffset;

	/**
	* Localization ID of this package
	* @note This is copy of the version stored in the package meta-data. It exists here so we can query it without having to load the whole package
	*/
	FString		LocalizationId;

	/**
	* Number of gatherable text data items in this package
	*/
	int32		GatherableTextDataCount;

	/**
	* Location into the file on disk for the gatherable text data items
	*/
	int32 		GatherableTextDataOffset;

	/**
	* Number of exports contained in this package
	*/
	int32		ExportCount;

	/**
	* Location into the file on disk for the ExportMap data
	*/
	int32		ExportOffset;

	/**
	* Number of imports contained in this package
	*/
	int32		ImportCount;

	/**
	* Location into the file on disk for the ImportMap data
	*/
	int32		ImportOffset;

	/**
	* Location into the file on disk for the DependsMap data
	*/
	int32		DependsOffset;

	/**
	* Number of soft package references contained in this package
	*/
	int32		SoftPackageReferencesCount;

	/**
	* Location into the file on disk for the soft package reference list
	*/
	int32		SoftPackageReferencesOffset;

	/**
	* Location into the file on disk for the SearchableNamesMap data
	*/
	int32		SearchableNamesOffset;

	/**
	* Thumbnail table offset
	*/
	int32		ThumbnailTableOffset;

	/**
	* Current id for this package
	*/
	UE_DEPRECATED(4.27, "UPackage::Guid has not been used by the engine for a long time and FPackageFileSummary::Guid will be removed.")
	FGuid	Guid;

#if WITH_EDITORONLY_DATA
	/**
	* Current persistent id for this package
	*/
	FGuid	PersistentGuid;
#endif

	/**
	* Data about previous versions of this package
	*/
	TArray<FGenerationInfo> Generations;

	/**
	* Engine version this package was saved with. This may differ from CompatibleWithEngineVersion for assets saved with a hotfix release.
	*/
	FEngineVersion SavedByEngineVersion;

	/**
	* Engine version this package is compatible with. Assets saved by Hotfix releases and engine versions that maintain binary compatibility will have
	* a CompatibleWithEngineVersion.Patch that matches the original release (as opposed to SavedByEngineVersion which will have a patch version of the new release).
	*/
	FEngineVersion CompatibleWithEngineVersion;

	/**
	* Flags used to compress the file on save and uncompress on load.
	*/
	uint32	CompressionFlags;

	/**
	* Value that is used to determine if the package was saved by Epic (or licensee) or by a modder, etc
	*/
	uint32	PackageSource;

	/**
	* If true, this file will not be saved with version numbers or was saved without version numbers. In this case they are assumed to be the current version.
	* This is only used for full cooks for distribution because it is hard to guarantee correctness
	**/
	bool bUnversioned;

	/**
	* Location into the file on disk for the asset registry tag data
	*/
	int32 	AssetRegistryDataOffset;

	/** Offset to the location in the file where the bulkdata starts */
	int64	BulkDataStartOffset;
	/**
	* Offset to the location in the file where the FWorldTileInfo data starts
	*/
	int32 	WorldTileInfoDataOffset;

	/**
	* Streaming install ChunkIDs
	*/
	TArray<int32>	ChunkIDs;

	int32		PreloadDependencyCount;

	/**
	* Location into the file on disk for the preload dependency data
	*/
	int32		PreloadDependencyOffset;

	/** Constructor */
	COREUOBJECT_API FPackageFileSummary();

	// Workaround for clang deprecation warnings for deprecated Guid member in implicit constructors
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	FPackageFileSummary(FPackageFileSummary&&) = default;
	FPackageFileSummary(const FPackageFileSummary&) = default;
	FPackageFileSummary& operator=(FPackageFileSummary&&) = default;
	FPackageFileSummary& operator=(const FPackageFileSummary&) = default;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	int32 GetFileVersionUE4() const
	{
		return FileVersionUE4;
	}

	int32 GetFileVersionLicenseeUE4() const
	{
		return FileVersionLicenseeUE4;
	}

	const FCustomVersionContainer& GetCustomVersionContainer() const
	{
		return CustomVersionContainer;
	}

	void SetCustomVersionContainer(const FCustomVersionContainer& InContainer);

	void SetFileVersions(const int32 EpicUE4, const int32 LicenseeUE4, const bool bInSaveUnversioned = false)
	{
		FileVersionUE4 = EpicUE4;
		FileVersionLicenseeUE4 = LicenseeUE4;
		bUnversioned = bInSaveUnversioned;
	}

	/** I/O functions */
	friend COREUOBJECT_API FArchive& operator<<(FArchive& Ar, FPackageFileSummary& Sum);
	friend COREUOBJECT_API void operator<<(FStructuredArchive::FSlot Slot, FPackageFileSummary& Sum);
};

