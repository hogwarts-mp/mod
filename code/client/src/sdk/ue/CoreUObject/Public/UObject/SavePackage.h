// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/Set.h"
#include "Containers/Map.h"
#include "UObject/NameTypes.h"
#include "Serialization/FileRegions.h"
#include "Misc/DateTime.h"
#include "ObjectMacros.h"

#if !defined(UE_WITH_SAVEPACKAGE)
#	define UE_WITH_SAVEPACKAGE 1
#endif

class FArchive;
class FIoBuffer;
class FPackageStoreBulkDataManifest;
class FSavePackageContext;
class FArchiveDiffMap;
class FOutputDevice;

/**
 * Struct to encapsulate arguments specific to saving one package
 */
struct FPackageSaveInfo
{
	class UPackage* Package = nullptr;
	class UObject* Asset = nullptr;
	FString Filename;
};

/**
 * Struct to encapsulate UPackage::Save arguments. 
 * These arguments are shared between packages when saving multiple packages concurrently.
 */
struct FSavePackageArgs
{
	class ITargetPlatform* TargetPlatform = nullptr;
	EObjectFlags TopLevelFlags = RF_NoFlags;
	uint32 SaveFlags = 0;
	bool bForceByteSwapping = false; // for FLinkerSave
	bool bWarnOfLongFilename = false;
	bool bSlowTask = true;
	FDateTime FinalTimeStamp;
	FOutputDevice* Error = nullptr;
	FArchiveDiffMap* DiffMap = nullptr;
	FSavePackageContext* SavePackageContext = nullptr;
};

class FPackageStoreWriter
{
public:
	COREUOBJECT_API			FPackageStoreWriter();
	COREUOBJECT_API virtual ~FPackageStoreWriter();

	struct HeaderInfo
	{
		FName	PackageName;
		FString	LooseFilePath;
	};

	/** Write 'uasset' data
	  */
	virtual void WriteHeader(const HeaderInfo& Info, const FIoBuffer& HeaderData) = 0;

	struct ExportsInfo
	{
		FName	PackageName;
		FString	LooseFilePath;
		uint64  RegionsOffset;

		TArray<FIoBuffer> Exports;
	};

	/** Write 'uexp' data
	  */
	virtual void WriteExports(const ExportsInfo& Info, const FIoBuffer& ExportsData, const TArray<FFileRegion>& FileRegions) = 0;

	struct FBulkDataInfo
	{
		enum EType 
		{
			Standard,
			Mmap,
			Optional
		};

		FName	PackageName;
		EType	BulkdataType = Standard;
		FString	LooseFilePath;
	};

	/** Write 'ubulk' data
	  */
	virtual void WriteBulkdata(const FBulkDataInfo& Info, const FIoBuffer& BulkData, const TArray<FFileRegion>& FileRegions) = 0;
};

class FLooseFileWriter : public FPackageStoreWriter
{
public:
	COREUOBJECT_API FLooseFileWriter();
	COREUOBJECT_API ~FLooseFileWriter();

	COREUOBJECT_API virtual void WriteHeader(const HeaderInfo& Info, const FIoBuffer& HeaderData) override;
	COREUOBJECT_API virtual void WriteExports(const ExportsInfo& Info, const FIoBuffer& ExportsData, const TArray<FFileRegion>& FileRegions) override;
	COREUOBJECT_API virtual void WriteBulkdata(const FBulkDataInfo& Info, const FIoBuffer& BulkData, const TArray<FFileRegion>& FileRegions) override;

private:
};

class FSavePackageContext
{
public:
	FSavePackageContext(FPackageStoreWriter* InPackageStoreWriter, FPackageStoreBulkDataManifest* InBulkDataManifest, bool InbForceLegacyOffsets)
	: PackageStoreWriter(InPackageStoreWriter) 
	, BulkDataManifest(InBulkDataManifest)
	, bForceLegacyOffsets(InbForceLegacyOffsets)
	{
	}

	COREUOBJECT_API ~FSavePackageContext();

	FPackageStoreWriter* PackageStoreWriter;
	FPackageStoreBulkDataManifest* BulkDataManifest;
	bool bForceLegacyOffsets;
};
