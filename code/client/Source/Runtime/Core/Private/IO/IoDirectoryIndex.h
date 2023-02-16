// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/StringView.h"
#include "Misc/AES.h"

class FArchive;

namespace IoDirectoryIndexUtils
{
	FString GetCommonRootPath(const TArray<FString>& Filenames);
}

struct FIoDirectoryIndexEntry
{
	uint32 Name				= ~uint32(0);
	uint32 FirstChildEntry	= ~uint32(0);
	uint32 NextSiblingEntry	= ~uint32(0);
	uint32 FirstFileEntry	= ~uint32(0);

	friend FArchive& operator<<(FArchive& Ar, FIoDirectoryIndexEntry& Entry);
};

struct FIoFileIndexEntry
{
	uint32 Name				= ~uint32(0);
	uint32 NextFileEntry	= ~uint32(0);
	uint32 UserData			= 0;

	friend FArchive& operator<<(FArchive& Ar, FIoFileIndexEntry& Entry);
};

struct FIoDirectoryIndexResource
{
	FString MountPoint;
	TArray<FIoDirectoryIndexEntry> DirectoryEntries;
	TArray<FIoFileIndexEntry> FileEntries;
	TArray<FString> StringTable;

	friend FArchive& operator<<(FArchive& Ar, FIoDirectoryIndexResource& Entry);
};

class FIoDirectoryIndexWriter
{
public:
	FIoDirectoryIndexWriter();

	void SetMountPoint(FString InMountPoint);
	uint32 AddFile(const FString& InFileName);
	void SetFileUserData(uint32 InFileEntryIndex, uint32 InUserData);
	void Flush(TArray<uint8>& OutBuffer, FAES::FAESKey InEncryptionKey);

private:
	uint32 GetDirectory(uint32 DirectoryName, uint32 Parent);
	uint32 CreateDirectory(const FStringView& DirectoryName, uint32 Parent);
	uint32 GetNameIndex(const FStringView& String);
	uint32 AddFile(const FStringView& FileName, uint32 Directory);

	static bool IsValid(uint32 Index)
	{
		return Index != ~uint32(0);
	}

	FString MountPoint;
	TArray<FIoDirectoryIndexEntry> DirectoryEntries;
	TArray<FIoFileIndexEntry> FileEntries;
	TMap<FString, uint32> StringToIndex;
	TArray<FString> Strings;
};
