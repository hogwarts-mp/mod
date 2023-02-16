// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Misc/AssertionMacros.h"
#include "HAL/UnrealMemory.h"
#include "Templates/UnrealTemplate.h"
#include "Math/UnrealMathUtility.h"
#include "Serialization/Archive.h"
#include "Containers/UnrealString.h"
#include "Misc/Parse.h"
#include "Logging/LogMacros.h"
#include "Misc/Paths.h"
#include "Misc/DateTime.h"
#include "Misc/ConfigCacheIni.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Templates/UniquePtr.h"
#include "HAL/FileManager.h"
#include "Misc/ScopeRWLock.h"
#include "Async/Async.h"

DECLARE_LOG_CATEGORY_EXTERN(LogPlatformFileManagedStorage, Display, All);

struct FPersistentStorageCategory
{
public:
	FPersistentStorageCategory(const FString& InCategoryName, const int64 InQuota, const TArray<FString>& InDirectories) :
		CategoryName(InCategoryName),
		UsedQuota(0),
		StorageQuota(InQuota),
		Directories(InDirectories)
	{
	}

	FORCEINLINE const FString& GetCategoryName() const
	{
		return CategoryName;
	}

	FORCEINLINE int64 GetCategoryQuota() const
	{
		return StorageQuota;
	}

	FORCEINLINE int64 GetUsedSize() const
	{
		return UsedQuota;
	}

	FORCEINLINE int64 GetAvailableSize() const
	{
		int64 ActualStorageQuota = StorageQuota >= 0 ? StorageQuota : MAX_int64;
		return ActualStorageQuota - UsedQuota;
	}

	FORCEINLINE bool IsCategoryFull() const
	{
		return GetAvailableSize() <= 0;
	}

	bool TryAddFileToCategory(const FString& Filename, const int64 FileSize, bool bForceAdd = false)
	{
		if ((bForceAdd || ShouldManageFile(Filename)) && !FileSizes.Contains(Filename))
		{
			// TODO: Add critical section if scan is moved to async and make UsedQuota volatile
			FileSizes.Add(Filename, FileSize);
			UsedQuota += FileSize;
			UE_LOG(LogPlatformFileManagedStorage, Log, TEXT("File %s is added to category %s"), *Filename, *CategoryName);

			return true;
		}

		return false;
	}

	bool TryRemoveFileFromCategory(const FString& Filename)
	{
		if (FileSizes.Contains(Filename))
		{
			// TODO: Add critical section if scan is moved to async
			int64 FileSize = FileSizes[Filename];
			if (FileSizes.Remove(Filename) > 0)
			{
				UsedQuota -= FileSize;
				UE_LOG(LogPlatformFileManagedStorage, Log, TEXT("File %s is removed from category %s"), *Filename, *CategoryName);

				return true;
			}
		}

		return false;
	}

	bool UpdateFileSize(const FString& Filename, const int64 FileSize, bool bFailIfUsedQuotaExceedsLimit = false)
	{
		int64* FileSizePtr= FileSizes.Find(Filename);
		if (FileSizePtr)
		{
			int64 OldFileSize = *FileSizePtr;
			int64 NewUsedQuota = UsedQuota - OldFileSize + FileSize;

			if (!bFailIfUsedQuotaExceedsLimit || StorageQuota < 0 || NewUsedQuota <= StorageQuota)
			{
				*FileSizePtr = FileSize;
				UsedQuota = NewUsedQuota;
				return true;
			}
		}
		else
		{
			// New file, update anyway.
			FileSizes.Add(Filename, FileSize);
			UsedQuota += FileSize;
			return true;
		}

		return false;
	}

	void RecalculateUsedQuota()
	{
		int64 TotalSize = 0LL;

		for (auto& FileSizePair : FileSizes)
		{
			TotalSize += FileSizePair.Value;
		}

		UsedQuota = TotalSize;
	}

	struct CategoryStat
	{
	public:
		CategoryStat(const FString& InCategoryName, int64 InUsedSize, int64 InTotalSize) :
			CategoryName(InCategoryName),
			UsedSize(InUsedSize),
			TotalSize(InTotalSize)
		{
		}

		FString Print() const
		{
			return FString::Printf(TEXT("Category %s: %.3f MB/%.3f MB used"), *CategoryName, (float)UsedSize / 1024.f / 1024.f, (float)TotalSize / 1024.f / 1024.f);
		}

	public:
		FString CategoryName;
		int64 UsedSize;
		int64 TotalSize;
	};

private:
	FString CategoryName;

	int64 UsedQuota;
	int64 StorageQuota;

	// List of all directories managed by this category
	TArray<FString> Directories;

	// Map from file name to file size
	TMap<FString, int64> FileSizes;

	bool ShouldManageFile(const FString& Filename) const
	{
		for (const FString& Directory : Directories)
		{
			if (FPaths::IsUnderDirectory(Filename, Directory))
			{
				return true;
			}
		}

		return false;
	}
};

using FPersistentStorageCategorySharedPtr = TSharedPtr<struct FPersistentStorageCategory, ESPMode::ThreadSafe>;

class FPersistentStorageManager
{
public:
	/** Singleton access **/
	static FPersistentStorageManager& Get()
	{
		static FPersistentStorageManager Singleton;
		return Singleton;
	}

	FPersistentStorageManager() :
		bInitialized(false)
	{
	}

	void Initialize()
	{
		if (bInitialized)
		{
			return;
		}

		// Load categories from config files
		ParseConfig();

		// TODO: Serialize

		// TODO: Make scan asynchronous
		ScanPersistentStorage();

		bInitialized = true;
	}

	bool AddFileToManager(const FString& Filename, const int64 FileSize)
	{
		FString NormalizedPath(Filename);
		FPaths::NormalizeFilename(NormalizedPath);

		FRWScopeLock WriteLock(CategoryLock, SLT_Write);

		if (FileCategoryMap.Contains(NormalizedPath))
		{
			return true;
		}

		for (auto& Category : Categories)
		{
			if (Category.Value->TryAddFileToCategory(NormalizedPath, FileSize))
			{
				FileCategoryMap.FindOrAdd(NormalizedPath) = Category.Key;
				return true;
			}
		}

		// Add to default category
		if (DefaultCategoryName.Len() && Categories.Contains(DefaultCategoryName))
		{
			if (Categories[DefaultCategoryName]->TryAddFileToCategory(NormalizedPath, FileSize, true))
			{
				FileCategoryMap.FindOrAdd(NormalizedPath) = DefaultCategoryName;
				return true;
			}
		}

		return false;
	}

	bool RemoveFileFromManager(const FString& Filename)
	{
		FString NormalizedPath(Filename);
		FPaths::NormalizeFilename(NormalizedPath);

		FPersistentStorageCategorySharedPtr CategorySharedPtr = FindCategoryForFile(NormalizedPath);
		if (CategorySharedPtr.IsValid())
		{
			FRWScopeLock WriteLock(CategoryLock, SLT_Write);

			FileCategoryMap.Remove(NormalizedPath);
			return CategorySharedPtr->TryRemoveFileFromCategory(NormalizedPath);
		}

		return false;
	}

	bool MoveFileInManager(const FString& From, const FString& To)
	{
		int64 FileSize = IFileManager::Get().FileSize(*To);
		bool bRemoveSuccess = RemoveFileFromManager(From);
		bool bAddSuccess = AddFileToManager(To, FileSize);

		return bRemoveSuccess && bAddSuccess;
	}

	bool UpdateFileSize(const FString& Filename, const int64 FileSize, bool bFailIfExceedsQuotaLimit = false)
	{
		FString NormalizedPath(Filename);
		FPaths::NormalizeFilename(NormalizedPath);

		FPersistentStorageCategorySharedPtr CategorySharedPtr = FindCategoryForFile(NormalizedPath);
		if (CategorySharedPtr.IsValid())
		{
			return CategorySharedPtr->UpdateFileSize(NormalizedPath, FileSize, bFailIfExceedsQuotaLimit);
		}

		return true;
	}

	int64 GetTotalUsedSize() const
	{
		FRWScopeLock ReadLock(CategoryLock, SLT_ReadOnly);

		int64 TotalUsedSize = 0LL;
		for (auto& Category : Categories)
		{
			TotalUsedSize += Category.Value->GetUsedSize();
		}

		return TotalUsedSize;
	}

	FORCEINLINE bool IsInitialized() const
	{
		return bInitialized;
	}

	bool IsCategoryForFileFull(const FString& Filename) const
	{
		FString NormalizedPath(Filename);
		FPaths::NormalizeFilename(NormalizedPath);

		FPersistentStorageCategorySharedPtr CategorySharedPtr = FindCategoryForFile(NormalizedPath);
		if (CategorySharedPtr.IsValid())
		{
			return CategorySharedPtr->IsCategoryFull();
		}

		return false;
	}

	void ScanDirectory(const FString& Directory)
	{
		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Directory, this]()
		{
			// TODO: Add critical section here so that only one scan can run at a time to mitigate impact on IO?
			UE_LOG(LogPlatformFileManagedStorage, Log, TEXT("Scan directory %s"), *Directory);

			// Check for added files
			IFileManager::Get().IterateDirectoryStatRecursively(*Directory, [this](const TCHAR* FilenameOrDirectory, const FFileStatData& StatData)
			{
				if (FilenameOrDirectory && !StatData.bIsDirectory && StatData.FileSize != -1)
				{
					AddFileToManager(FilenameOrDirectory, StatData.FileSize);
				}

				return true;
			});

			// Check for deleted files
			TArray<FString> FilesToRemove;
			{
				FRWScopeLock ReadLock(CategoryLock, SLT_ReadOnly);

				for (auto& FilePair : FileCategoryMap)
				{
					if (FPaths::IsUnderDirectory(FilePair.Key, Directory) &&
						!IFileManager::Get().FileExists(*FilePair.Key))
					{
						FilesToRemove.Add(FilePair.Key);
					}
				}
			}

			for (const FString& FileToRemove : FilesToRemove)
			{
				RemoveFileFromManager(FileToRemove);
			}
		});
	}

	void ScanPersistentStorage()
	{
		ScanDirectory(FPaths::ProjectPersistentDownloadDir());
	}

	TMap<FString, FPersistentStorageCategory::CategoryStat> GenerateCategoryStats()
	{
		TMap<FString, FPersistentStorageCategory::CategoryStat> CategoryStats;

		FRWScopeLock ReadLock(CategoryLock, SLT_ReadOnly);

		for (const auto& CategoryPair : Categories)
		{
			CategoryStats.Add(CategoryPair.Key, FPersistentStorageCategory::CategoryStat(CategoryPair.Key, CategoryPair.Value->GetUsedSize(), CategoryPair.Value->GetCategoryQuota()));
		}

		return CategoryStats;
	}

private:
	bool bInitialized;

	// Name of the default category.  Files that don't match the directories of any categories will be added to default category as a fallback.
	FString DefaultCategoryName;

	// Map from category name to catogory struct
	TMap<FString, FPersistentStorageCategorySharedPtr> Categories;

	// Map from file name to category name.
	TMap<FString, FString> FileCategoryMap;

	/** RWLock for accessing Categories and FileCategoryMap. */
	mutable FRWLock CategoryLock;

	bool ParseConfig()
	{
		if (GConfig)
		{
			FRWScopeLock WriteLock(CategoryLock, SLT_Write);

			// Clear Categories
			Categories.Empty();

			// Takes on the pattern
			// (Name="CategoryName",QuotaMB=100,Directories=("Dir1","Dir2","Dir3"))
			TArray<FString> CategoryConfigs;
			GConfig->GetArray(TEXT("PersistentStorageManager"), TEXT("Categories"), CategoryConfigs, GEngineIni);
			for (const FString& Category : CategoryConfigs)
			{
				FString TrimmedCategory = Category;
				TrimmedCategory.TrimStartAndEndInline();
				if (TrimmedCategory.Left(1) == TEXT("("))
				{
					TrimmedCategory.RightChopInline(1, false);
				}
				if (TrimmedCategory.Right(1) == TEXT(")"))
				{
					TrimmedCategory.LeftChopInline(1, false);
				}

				// Find all custom chunks and parse
				const TCHAR* PropertyName = TEXT("Name=");
				const TCHAR* PropertyQuotaMB = TEXT("QuotaMB=");
				const TCHAR* PropertyDirectories = TEXT("Directories=");
				FString CategoryName;
				int64 QuotaInMB;
				FString DirectoryNames;

				if (FParse::Value(*TrimmedCategory, PropertyName, CategoryName) &&
					FParse::Value(*TrimmedCategory, PropertyQuotaMB, QuotaInMB) &&
					FParse::Value(*TrimmedCategory, PropertyDirectories, DirectoryNames, false))
				{
					CategoryName.ReplaceInline(TEXT("\""), TEXT(""));

					// Split Directories
					if (DirectoryNames.Left(1) == TEXT("("))
					{
						DirectoryNames.RightChopInline(1, false);
					}
					if (DirectoryNames.Right(1) == TEXT(")"))
					{
						DirectoryNames.LeftChopInline(1, false);
					}

					TArray<FString> Directories;
					DirectoryNames.ParseIntoArray(Directories, TEXT(","));
					for(FString& DirectoryName : Directories)
					{
						DirectoryName = FPaths::ProjectPersistentDownloadDir() / DirectoryName.Replace(TEXT("\""), TEXT(""));
					}

					int64 Quota = (QuotaInMB >= 0) ? QuotaInMB * 1024 * 1024 : -1;	// Quota being negative means infinite quota
					Categories.Add(CategoryName, MakeShared<FPersistentStorageCategory, ESPMode::ThreadSafe>(CategoryName, Quota, Directories));
				}
			}

			GConfig->GetString(TEXT("PersistentStorageManager"), TEXT("DefaultCategoryName"), DefaultCategoryName, GEngineIni);
			if (!Categories.Contains(DefaultCategoryName))
			{
				UE_LOG(LogPlatformFileManagedStorage, Error, TEXT("Default category %s doesn't exist"), *DefaultCategoryName);
				DefaultCategoryName.Empty();
			}

			return true;
		}

		return false;
	}

	FPersistentStorageCategorySharedPtr FindCategoryForFile(const FString& Filename)
	{
		FRWScopeLock ReadLock(CategoryLock, SLT_ReadOnly);

		const FString* CategoryNamePtr = FileCategoryMap.Find(Filename);
		if (CategoryNamePtr != nullptr)
		{
			return FPersistentStorageCategorySharedPtr(*Categories.Find(*CategoryNamePtr));
		}

		return nullptr;
	}

	const FPersistentStorageCategorySharedPtr FindCategoryForFile(const FString& Filename) const
	{
		return const_cast<FPersistentStorageManager&>(*this).FindCategoryForFile(Filename);
	}
};

// Only write handle 
class CORE_API FManagedStorageFileHandle : public IFileHandle
{
public:
	FManagedStorageFileHandle(IFileHandle* InFileHandle, const FString& InFilename, bool InIsWriteHandle) :
		FileHandle(InFileHandle),
		FileSize(InFileHandle->Size()),
		bWriteHandle(InIsWriteHandle)
	{
		FString NormalizedPath(InFilename);
		FPaths::NormalizeFilename(NormalizedPath);

		Filename = NormalizedPath;
	}

	virtual ~FManagedStorageFileHandle()
	{
		if (bWriteHandle)
		{
			FPersistentStorageManager::Get().UpdateFileSize(Filename, FileSize);
		}
	}

	virtual int64 Tell() override
	{
		return FileHandle->Tell();
	}

	virtual bool Seek(int64 NewPosition) override
	{
		return FileHandle->Seek(NewPosition);
	}

	virtual bool SeekFromEnd(int64 NewPositionRelativeToEnd = 0) override
	{
		return FileHandle->SeekFromEnd(NewPositionRelativeToEnd);
	}

	virtual bool Read(uint8* Destination, int64 BytesToRead) override
	{
		return FileHandle->Read(Destination, BytesToRead);
	}

	virtual bool Write(const uint8* Source, int64 BytesToWrite) override
	{
		if (bWriteHandle)
		{
			bool bIsFileCategoryFull = !FPersistentStorageManager::Get().UpdateFileSize(Filename, FileSize + BytesToWrite, true);
			if (bIsFileCategoryFull)
			{
				UE_LOG(LogPlatformFileManagedStorage, Error, TEXT("Failed to write to file %s.  The category of the file has reach quota limit in peristent storage."), *Filename);
				return false;
			}

			bool bSuccess = FileHandle->Write(Source, BytesToWrite);
			FileSize += BytesToWrite;

			return true;
		}

		return false;
	}

	virtual int64 Size() override
	{
		return FileSize;
	}

	virtual bool Flush(const bool bFullFlush = false) override
	{
		if (FPersistentStorageManager::Get().IsCategoryForFileFull(Filename))
		{
			UE_LOG(LogPlatformFileManagedStorage, Error, TEXT("Failed to flush file %s.  The category of the file has reach quota limit in peristent storage."), *Filename);
			return false;
		}

		bool bSuccess = FileHandle->Flush(bFullFlush);
		FPersistentStorageManager::Get().UpdateFileSize(Filename, FileSize);

		return bSuccess;
	}

	virtual bool Truncate(int64 NewSize) override
	{
		if (FPersistentStorageManager::Get().IsCategoryForFileFull(Filename))
		{
			UE_LOG(LogPlatformFileManagedStorage, Error, TEXT("Failed to truncate file %s.  The category of the file has reach quota limit in peristent storage."), *Filename);
			return false;
		}

		if (FileHandle->Truncate(NewSize))
		{
			FileSize = FileHandle->Size();
			FPersistentStorageManager::Get().UpdateFileSize(Filename, FileSize);
			return true;
		}

		return false;
	}

	virtual void ShrinkBuffers() override
	{
		FileHandle->ShrinkBuffers();
	}

private:
	TUniquePtr<IFileHandle>	FileHandle;
	int64					FileSize;
	FString					Filename;
	bool					bWriteHandle;
};

class CORE_API FManagedStoragePlatformFile : public IPlatformFile
{
private:
	IPlatformFile* LowerLevel;

public:
	static const TCHAR* GetTypeName()
	{
		return TEXT("ManagedStoragePlatformFile");
	}

	FManagedStoragePlatformFile() = delete;

	FManagedStoragePlatformFile(IPlatformFile* Inner) :
		LowerLevel(Inner)
	{
	}

	//~ For visibility of overloads we don't override
	using IPlatformFile::IterateDirectory;
	using IPlatformFile::IterateDirectoryRecursively;
	using IPlatformFile::IterateDirectoryStat;
	using IPlatformFile::IterateDirectoryStatRecursively;

	virtual bool Initialize(IPlatformFile* Inner, const TCHAR* CommandLineParam) override
	{
		// Inner is required.
		check(Inner != nullptr);
		LowerLevel = Inner;
		return !!LowerLevel;
	}

	virtual bool ShouldBeUsed(IPlatformFile* Inner, const TCHAR* CmdLine) const override
	{
		bool bResult = PLATFORM_USE_PLATFORM_FILE_MANAGED_STORAGE_WRAPPER;

		return bResult;
	}

	virtual IPlatformFile* GetLowerLevel() override
	{
		return LowerLevel;
	}

	virtual void SetLowerLevel(IPlatformFile* NewLowerLevel) override
	{
		LowerLevel = NewLowerLevel;
	}

	virtual const TCHAR* GetName() const override
	{
		return FManagedStoragePlatformFile::GetTypeName();
	}

	virtual bool FileExists(const TCHAR* Filename) override
	{
		return LowerLevel->FileExists(Filename);
	}

	virtual int64 FileSize(const TCHAR* Filename) override
	{
		return LowerLevel->FileSize(Filename);
	}

	virtual bool DeleteFile(const TCHAR* Filename) override
	{
		bool bSuccess = LowerLevel->DeleteFile(Filename);
		if (bSuccess)
		{
			FPersistentStorageManager::Get().RemoveFileFromManager(Filename);
		}

		return bSuccess;
	}

	virtual bool IsReadOnly(const TCHAR* Filename) override
	{
		return LowerLevel->IsReadOnly(Filename);
	}

	virtual bool MoveFile(const TCHAR* To, const TCHAR* From) override
	{
		if (FPersistentStorageManager::Get().IsCategoryForFileFull(To))
		{
			UE_LOG(LogPlatformFileManagedStorage, Error, TEXT("Failed to move file to %s.  The target category of the destination has reach quota limit in peristent storage."), To);
			return false;
		}

		bool bSuccess = LowerLevel->MoveFile(To, From);
		if (bSuccess)
		{
			FPersistentStorageManager::Get().MoveFileInManager(From, To);
		}

		return bSuccess;
	}

	virtual bool SetReadOnly(const TCHAR* Filename, bool bNewReadOnlyValue) override
	{
		return LowerLevel->SetReadOnly(Filename, bNewReadOnlyValue);
	}

	virtual FDateTime GetTimeStamp(const TCHAR* Filename) override
	{
		return LowerLevel->GetTimeStamp(Filename);
	}

	virtual void SetTimeStamp(const TCHAR* Filename, FDateTime DateTime) override
	{
		LowerLevel->SetTimeStamp(Filename, DateTime);
	}

	virtual FDateTime GetAccessTimeStamp(const TCHAR* Filename) override
	{
		return LowerLevel->GetAccessTimeStamp(Filename);
	}

	virtual FString	GetFilenameOnDisk(const TCHAR* Filename) override
	{
		return LowerLevel->GetFilenameOnDisk(Filename);
	}

	virtual IFileHandle* OpenRead(const TCHAR* Filename, bool bAllowWrite) override
	{
		return LowerLevel->OpenRead(Filename, bAllowWrite);
	}

	virtual IFileHandle* OpenWrite(const TCHAR* Filename, bool bAppend = false, bool bAllowRead = false) override
	{
		if (FPersistentStorageManager::Get().IsCategoryForFileFull(Filename))
		{
			UE_LOG(LogPlatformFileManagedStorage, Error, TEXT("Failed to open file %s for write.  The category of the file has reach quota limit in peristent storage."), Filename);
			return nullptr;
		}

		IFileHandle* InnerHandle = LowerLevel->OpenWrite(Filename, bAppend, bAllowRead);
		if (!InnerHandle)
		{
			return nullptr;
		}

		bool bShouldManageFile = FPersistentStorageManager::Get().AddFileToManager(Filename, 0);
		if (bShouldManageFile)
		{
			return new FManagedStorageFileHandle(InnerHandle, Filename, true);
		}
		else
		{
			return InnerHandle;
		}
	}

	virtual bool DirectoryExists(const TCHAR* Directory) override
	{
		return LowerLevel->DirectoryExists(Directory);
	}

	virtual bool CreateDirectory(const TCHAR* Directory) override
	{
		return LowerLevel->CreateDirectory(Directory);
	}

	virtual bool DeleteDirectory(const TCHAR* Directory) override
	{
		bool bSuccess = LowerLevel->DeleteDirectory(Directory);

		// Need to rescan the directory
		FPersistentStorageManager::Get().ScanDirectory(Directory);

		return bSuccess;
	}

	virtual FFileStatData GetStatData(const TCHAR* FilenameOrDirectory) override
	{
		return LowerLevel->GetStatData(FilenameOrDirectory);
	}

	virtual bool IterateDirectory(const TCHAR* Directory, IPlatformFile::FDirectoryVisitor& Visitor) override
	{
		return LowerLevel->IterateDirectory(Directory, Visitor);
	}

	virtual bool IterateDirectoryRecursively(const TCHAR* Directory, IPlatformFile::FDirectoryVisitor& Visitor) override
	{
		return LowerLevel->IterateDirectoryRecursively(Directory, Visitor);
	}

	virtual bool IterateDirectoryStat(const TCHAR* Directory, IPlatformFile::FDirectoryStatVisitor& Visitor) override
	{
		return LowerLevel->IterateDirectoryStat(Directory, Visitor);
	}

	virtual bool IterateDirectoryStatRecursively(const TCHAR* Directory, IPlatformFile::FDirectoryStatVisitor& Visitor) override
	{
		return LowerLevel->IterateDirectoryStatRecursively(Directory, Visitor);
	}

	virtual void FindFiles(TArray<FString>& FoundFiles, const TCHAR* Directory, const TCHAR* FileExtension)
	{
		return LowerLevel->FindFiles(FoundFiles, Directory, FileExtension);
	}

	virtual void FindFilesRecursively(TArray<FString>& FoundFiles, const TCHAR* Directory, const TCHAR* FileExtension)
	{
		return LowerLevel->FindFilesRecursively(FoundFiles, Directory, FileExtension);
	}

	virtual bool DeleteDirectoryRecursively(const TCHAR* Directory) override
	{
		bool bSuccess = LowerLevel->DeleteDirectoryRecursively(Directory);

		// Need to rescan the directory, since it might be partially deleted.
		FPersistentStorageManager::Get().ScanDirectory(Directory);

		return bSuccess;
	}

	virtual bool CopyFile(const TCHAR* To, const TCHAR* From, EPlatformFileRead ReadFlags = EPlatformFileRead::None, EPlatformFileWrite WriteFlags = EPlatformFileWrite::None) override
	{
		if (FPersistentStorageManager::Get().IsCategoryForFileFull(To))
		{
			UE_LOG(LogPlatformFileManagedStorage, Error, TEXT("Failed to copy file to %s.  The category of the destination has reach quota limit in peristent storage."), To);
			return false;
		}

		bool bSuccess = LowerLevel->CopyFile(To, From, ReadFlags, WriteFlags);
		if (bSuccess)
		{
			int64 FileSize = IFileManager::Get().FileSize(To);
			FPersistentStorageManager::Get().AddFileToManager(To, FileSize);
		}

		return bSuccess;
	}

	virtual bool CreateDirectoryTree(const TCHAR* Directory) override
	{
		return LowerLevel->CreateDirectoryTree(Directory);
	}

	virtual bool CopyDirectoryTree(const TCHAR* DestinationDirectory, const TCHAR* Source, bool bOverwriteAllExisting) override
	{
		bool bSuccess = LowerLevel->CopyDirectoryTree(DestinationDirectory, Source, bOverwriteAllExisting);

		// Need to rescan the new directory
		FPersistentStorageManager::Get().ScanDirectory(DestinationDirectory);

		return bSuccess;
	}

	virtual FString ConvertToAbsolutePathForExternalAppForRead(const TCHAR* Filename) override
	{
		return LowerLevel->ConvertToAbsolutePathForExternalAppForRead(Filename);
	}

	virtual FString ConvertToAbsolutePathForExternalAppForWrite(const TCHAR* Filename) override
	{
		return LowerLevel->ConvertToAbsolutePathForExternalAppForWrite(Filename);
	}

	virtual bool SendMessageToServer(const TCHAR* Message, IFileServerMessageHandler* Handler) override
	{
		return LowerLevel->SendMessageToServer(Message, Handler);
	}

	virtual IAsyncReadFileHandle* OpenAsyncRead(const TCHAR* Filename) override
	{
		return LowerLevel->OpenAsyncRead(Filename);
	}

	virtual IMappedFileHandle* OpenMapped(const TCHAR* Filename) override
	{
		return LowerLevel->OpenMapped(Filename);
	}

	virtual void SetAsyncMinimumPriority(EAsyncIOPriorityAndFlags MinPriority) override
	{
		LowerLevel->SetAsyncMinimumPriority(MinPriority);
	}
};