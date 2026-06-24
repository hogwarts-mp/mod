// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "IO/IoDispatcher.h"

class FPackageStoreBulkDataManifest
{
public:
	enum class EBulkdataType : uint8
	{
		Normal = 0,
		Optional,
		MemoryMapped
	};

	COREUOBJECT_API FPackageStoreBulkDataManifest(const FString& ProjectPath);
	~FPackageStoreBulkDataManifest() = default;

	COREUOBJECT_API bool Load();
	COREUOBJECT_API void Save();

	void AddFileAccess(const FString& PackageFilename, EBulkdataType InType, uint64 InChunkId, uint64 InOffset, uint64 InSize);

	const FString& GetFilename() const { return Filename; }
	class FPackageDesc

	{
	public:

		struct FBulkDataDesc
		{
			uint64			ChunkId;	// Note this is the Offset before the linker BulkDataStartOffset is
										// applied, to make it easier to compute at runtime.
			uint64			Offset;
			uint64			Size;
			EBulkdataType	Type;
		};

		void AddData(EBulkdataType InType, uint64 InChunkId, uint64 InOffset, uint64 InSize, const FString& DebugFilename);
		void AddZeroByteData(EBulkdataType InType);

		const TArray<FBulkDataDesc>& GetDataArray() const { return Data; }
	private:
		friend FArchive& operator<<(FArchive& Ar, FPackageDesc& Entry);
		TArray<FBulkDataDesc> Data;
	};

	COREUOBJECT_API const FPackageDesc* Find(const FString& PackageName) const;

private:
	FPackageDesc& GetOrCreateFileAccess(const FString& PackageFilename);

	FString FixFilename(const FString& InFileName) const;

	FString RootPath;
	FString Filename;
	TMap<FString, FPackageDesc> Data;
};
