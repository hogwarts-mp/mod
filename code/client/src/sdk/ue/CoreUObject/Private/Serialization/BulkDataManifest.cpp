// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/BulkDataManifest.h"

#include "CoreGlobals.h"
#include "HAL/FileManager.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/CustomVersion.h"

struct FBulkDataManifestVersion
{
	FBulkDataManifestVersion() = delete;

	enum Type
	{
		// Before any version changes were made
		BeforeCustomVersionWasAdded = 0,

		// -----<new versions can be added above this line>-------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	// The GUID for this custom version number
	const static FGuid GUID;
};

const FGuid FBulkDataManifestVersion::GUID(0x54683250, 0x809948AF, 0x8BC89896, 0xFBADF9B7);
FCustomVersionRegistration GRegisterBulkDataManifestVersion(FBulkDataManifestVersion::GUID, FBulkDataManifestVersion::LatestVersion, TEXT("BulkDataManifestVersion"));

void FPackageStoreBulkDataManifest::FPackageDesc::AddData(EBulkdataType InType, uint64 InChunkId, uint64 InOffset, uint64 InSize, const FString& DebugFilename)
{
	// The ChunkId is supposed to be unique for each type
	auto func = [=](const FBulkDataDesc& Entry) { return Entry.ChunkId == InChunkId && Entry.Type == InType; };
	if (Data.FindByPredicate(func) != nullptr)
	{
		FString Message;
		for (int i = 0; i < Data.Num(); ++i)
		{
			const FBulkDataDesc& Entry = Data[i];
			Message.Appendf(TEXT("[%3d] ID: %20llu Offset: %8llu Size: %8llu Type: %d\n"),
				i, Entry.ChunkId, Entry.Offset, Entry.Size, Entry.Type);
		}

		Message.Appendf(TEXT("[New] ID: %20llu Offset: %8llu Size: %8llu Type: %d\n"),
			InChunkId, InOffset, InSize, InType);

		UE_LOG(LogSerialization, Warning, TEXT("Duplicate BulkData description found in Package '%s', this will cause issues trying to run IoStore!\n%s"), *DebugFilename, *Message);
	}
	else
	{
		FBulkDataDesc& Entry = Data.Emplace_GetRef();

		Entry.ChunkId = InChunkId;
		Entry.Offset = InOffset;
		Entry.Size = InSize;
		Entry.Type = InType;
	}
}

void FPackageStoreBulkDataManifest::FPackageDesc::AddZeroByteData(EBulkdataType InType)
{
	// Make sure we only add one empty read per Bulkdata type!
	auto func = [=](const FBulkDataDesc& Entry) { return Entry.Type == InType && Entry.Size == 0; };
	if (Data.FindByPredicate(func) == nullptr)
	{
		FBulkDataDesc& Entry = Data.Emplace_GetRef();

		Entry.ChunkId = TNumericLimits<uint64>::Max();
		Entry.Offset = 0;
		Entry.Size = 0;
		Entry.Type = InType;
	}
}

FPackageStoreBulkDataManifest::FPackageStoreBulkDataManifest(const FString& ProjectPath)
{
	Filename = ProjectPath / TEXT("Metadata/BulkDataInfo.ubulkmanifest");
}

FArchive& operator<<(FArchive& Ar, FPackageStoreBulkDataManifest::FPackageDesc::FBulkDataDesc& Entry)
{
	Ar << Entry.ChunkId;
	Ar << Entry.Offset;
	Ar << Entry.Size;
	Ar << Entry.Type;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FPackageStoreBulkDataManifest::FPackageDesc& Entry)
{
	Ar << Entry.Data;

	return Ar;
}

bool FPackageStoreBulkDataManifest::Load()
{
	Data.Empty();

	TUniquePtr<FArchive> BinArchive(IFileManager::Get().CreateFileReader(*Filename));
	if (BinArchive != nullptr)
	{
		// Load and apply the custom versions before we load any data
		FCustomVersionContainer CustomVersions;
		CustomVersions.Serialize(*BinArchive);
		BinArchive->SetCustomVersions(CustomVersions);
		
		*BinArchive << Data;
		return true;
	}
	else
	{
		return false;
	}
}

void FPackageStoreBulkDataManifest::Save()
{
	TUniquePtr<FArchive> BinArchive(IFileManager::Get().CreateFileWriter(*Filename));
	BinArchive->UsingCustomVersion(FBulkDataManifestVersion::GUID);

	// Take the versions from the archive and serialize them out
	// NOTE: Serializing out now means we cannot add additional versions while serializing
	// it is assumed that we are only using FBulkDataManifestVersion::GUID
	FCustomVersionContainer CustomVersions = BinArchive->GetCustomVersions();
	CustomVersions.Serialize(*BinArchive);

	*BinArchive << Data;
}

const FPackageStoreBulkDataManifest::FPackageDesc* FPackageStoreBulkDataManifest::Find(const FString& PackageFilename) const
{
	const  FString NormalizedFilename = FixFilename(PackageFilename);
	return Data.Find(NormalizedFilename);
}

void FPackageStoreBulkDataManifest::AddFileAccess(const FString& PackageFilename, EBulkdataType InType, uint64 InChunkId, uint64 InOffset, uint64 InSize)
{
	const FString NormalizedFilename = FixFilename(PackageFilename);

	FPackageDesc& Entry = GetOrCreateFileAccess(NormalizedFilename);

	if (InSize > 0)
	{
		Entry.AddData(InType, InChunkId, InOffset, InSize, NormalizedFilename);
	}
	else
	{
		Entry.AddZeroByteData(InType);
	}
}

FPackageStoreBulkDataManifest::FPackageDesc& FPackageStoreBulkDataManifest::GetOrCreateFileAccess(const FString& PackageFilename)
{
	if (FPackageDesc* Entry = Data.Find(PackageFilename))
	{
		return *Entry;
	}
	else
	{
		return Data.Add(PackageFilename);
	}
}

FString FPackageStoreBulkDataManifest::FixFilename(const FString& InFilename) const
{
	FString OutFilename = InFilename;
	FPaths::NormalizeFilename(OutFilename);
	FPaths::MakePathRelativeTo(OutFilename, *RootPath);

	return OutFilename;
}
