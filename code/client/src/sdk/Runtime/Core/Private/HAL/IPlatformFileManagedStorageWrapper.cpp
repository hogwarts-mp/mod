// Copyright Epic Games, Inc. All Rights Reserved.

#include "HAL/IPlatformFileManagedStorageWrapper.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY(LogPlatformFileManagedStorage);

static FAutoConsoleCommand PersistentStorageCategoryStatsCommand
(
	TEXT("PersistentStorageCategoryStats"),
	TEXT("Get the stat of each persistent storage stats\n"),
	FConsoleCommandDelegate::CreateStatic([]()
{
	for (auto& CategoryStat : FPersistentStorageManager::Get().GenerateCategoryStats())
	{
		UE_LOG(LogPlatformFileManagedStorage, Display, TEXT("%s"), *CategoryStat.Value.Print());
	}
})
);

static FAutoConsoleCommand CreateDummyFileInPersistentStorageCommand(
	TEXT("CreateDummyFileInPersistentStorage"),
	TEXT("Create a dummy file with specified size in specified persistent storage folder"),
	FConsoleCommandWithArgsDelegate::CreateLambda([](const TArray<FString>& Args)
{
	if (Args.Num() < 2)
	{
		UE_LOG(LogPlatformFileManagedStorage, Error, TEXT("Not enough parameters to run console command CreateDummyFileInPersistentStorage"));
		return;
	}

	// Args[0]: FilePath
	// Args[1]: Size
	const FString& DummyFilePath = Args[0];
	if (!FPaths::IsUnderDirectory(DummyFilePath, TEXT("/download0")))
	{
		UE_LOG(LogPlatformFileManagedStorage, Error, TEXT("Failed to write dummy file %s.  File path is not under /download0"), *DummyFilePath);
		return;
	}

	int32 FileSize;;
	LexFromString(FileSize, *Args[1]);
	int32 BufferSize = 16 * 1024;
	TArray<uint8> DummyBuffer;
	DummyBuffer.SetNum(BufferSize);

	TUniquePtr<FArchive> Ar = TUniquePtr<FArchive>(IFileManager::Get().CreateFileWriter(*DummyFilePath, 0));
	if (!Ar)
	{
		UE_LOG(LogPlatformFileManagedStorage, Error, TEXT("Failed to write dummy file %s."), *DummyFilePath);
		return;
	}

	int32 RemainingBytesToWrite = FileSize;
	while (RemainingBytesToWrite > 0)
	{
		int32 SizeToWrite = FMath::Min(RemainingBytesToWrite, BufferSize);
		Ar->Serialize(const_cast<uint8*>(DummyBuffer.GetData()), SizeToWrite);
		RemainingBytesToWrite -= SizeToWrite;
	}

	if(!Ar->Close())
	{
		UE_LOG(LogPlatformFileManagedStorage, Error, TEXT("There was an error writing to file %s."), *DummyFilePath);
	}
}),
ECVF_Default);