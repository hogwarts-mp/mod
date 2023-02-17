// Copyright Epic Games, Inc. All Rights Reserved.
// Copyright 2016 Magic Leap, Inc. All Rights Reserved.

#include "Lumin/LuminPlatformFile.h"
#include "Android/AndroidPlatformMisc.h"
#include "Lumin/LuminLifecycle.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include <sys/stat.h>   // mkdirp()
#include <dirent.h>
#include "Lumin/CAPIShims/LuminAPIFileInfo.h"
#include "Lumin/CAPIShims/LuminAPISharedFile.h"

DEFINE_LOG_CATEGORY_STATIC(LogLuminPlatformFile, Log, All);

FLuminFileInfo::FLuminFileInfo()
: FileHandle(nullptr)
{}

// make an FTimeSpan object that represents the "epoch" for time_t (from a stat struct)
const FDateTime UnixEpoch(1970, 1, 1);

FString AndroidRelativeToAbsolutePath(bool bUseInternalBasePath, FString RelPath)
{
	FString Result = MoveTemp(RelPath);

	if (Result.StartsWith(TEXT("../"), ESearchCase::CaseSensitive))
	{
		do 
		{
			Result.RightChopInline(3, false);
		} while (Result.StartsWith(TEXT("../"), ESearchCase::CaseSensitive));
	}

	// Remove the base app path if present, we will prepend it the correct base path as needed.
	Result.ReplaceInline(*FLuminPlatformMisc::GetApplicationPackageDirectoryPath(), TEXT(""));
	// Remove the writable path if present, we will prepend it the correct base path as needed.
	Result.ReplaceInline(*FLuminPlatformMisc::GetApplicationWritableDirectoryPath(), TEXT(""));

	// Then add it to the app writable directory path.
	FString lhs = FLuminPlatformMisc::GetApplicationWritableDirectoryPath();
	// only convert the unreal path to lowercase, not the sandbox directory (lhs)
	FString rhs = Result.ToLower();
	lhs.RemoveFromEnd(TEXT("/"), ESearchCase::CaseSensitive);
	rhs.RemoveFromStart(TEXT("/"), ESearchCase::CaseSensitive);
	Result = lhs / rhs;

	// only convert the unreal path to lowercase, not the sandbox directory (lhs)
	return Result;
}

namespace
{
	FFileStatData UnixStatToUEFileData(struct stat& FileInfo)
	{
		const bool bIsDirectory = S_ISDIR(FileInfo.st_mode);

		int64 FileSize = -1;
		if (!bIsDirectory)
		{
			FileSize = FileInfo.st_size;
		}

		return FFileStatData(
			UnixEpoch + FTimespan(0, 0, FileInfo.st_ctime),
			UnixEpoch + FTimespan(0, 0, FileInfo.st_atime),
			UnixEpoch + FTimespan(0, 0, FileInfo.st_mtime),
			FileSize,
			bIsDirectory,
			!(FileInfo.st_mode & S_IWUSR)
			);
	}
}

/**
* File handle implementation which limits number of open files per thread. This
* is to prevent running out of system file handles. Should not be neccessary when
* using pak file (e.g., SHIPPING?) so not particularly optimized. Only manages
* files which are opened READ_ONLY.
*/
// @todo lumin - Consider if we need managed handles or not.
// TODO - handle shared files properly when using managed file handles.
#define MANAGE_FILE_HANDLES 0

/**
* File handle implementation
*/
class CORE_API FFileHandleLumin : public IFileHandle
{
	enum {READWRITE_SIZE = 1024 * 1024};

	FORCEINLINE bool IsValid()
	{
		return FileHandle != -1;
	}

public:
	FFileHandleLumin(int32 InFileHandle, const TCHAR* InFilename, bool bIsReadOnly)
		: FileHandle(InFileHandle)
#if MANAGE_FILE_HANDLES
		, Filename(InFilename)
		, HandleSlot(-1)
		, FileOffset(0)
		, FileSize(0)
#endif // MANAGE_FILE_HANDLES
		, SharedFileList(nullptr)
		, Fileinfo(nullptr)
		, bReleaseFileInfo(false)
	{
		check(FileHandle > -1);
#if MANAGE_FILE_HANDLES
		check(Filename.Len() > 0);
#endif // MANAGE_FILE_HANDLES

#if MANAGE_FILE_HANDLES
		// Only files opened for read will be managed
		if (bIsReadOnly)
		{
			ReserveSlot();
			ActiveHandles[HandleSlot] = this;
			struct stat FileInfo;
			fstat(FileHandle, &FileInfo);
			FileSize = FileInfo.st_size;
		}
#endif // MANAGE_FILE_HANDLES
	}

	FFileHandleLumin(int32 InFileHandle, MLSharedFileList* InSharedFileList)
		: FileHandle(InFileHandle)
		, SharedFileList(InSharedFileList)
		, Fileinfo(nullptr)
		, bReleaseFileInfo(false)
	{
		check(FileHandle > -1);
		check(SharedFileList != nullptr)
	}

	FFileHandleLumin(int32 InFileHandle, const MLFileInfo* InFileInfo)
		: FileHandle(InFileHandle)
		, SharedFileList(nullptr)
		, Fileinfo(InFileInfo)
		, bReleaseFileInfo(false)
	{
		check(FileHandle > -1);
		check(Fileinfo != nullptr)
	}

	virtual ~FFileHandleLumin()
	{
#if MANAGE_FILE_HANDLES
		if( IsManaged() )
		{
			if( ActiveHandles[ HandleSlot ] == this )
			{
				close(FileHandle);
				ActiveHandles[ HandleSlot ] = NULL;
			}
		}
		else
#endif // MANAGE_FILE_HANDLES
		{
			if (SharedFileList != nullptr)
			{
				MLResult Result = MLSharedFileListRelease(&SharedFileList);
				UE_CLOG(MLResult_Ok != Result, LogLuminPlatformFile, Error, TEXT("Error %s releasing shared file list for fd %d"), UTF8_TO_TCHAR(MLSharedFileGetResultString(Result)), FileHandle);
			}
			// Close if it's a normal unreal file. MLFileInfo fds are closed via MLLifecycleFreeInitArgList()
			else if (Fileinfo == nullptr)
			{
				close(FileHandle);
			}
		}

		FileHandle = -1;
		SharedFileList = nullptr;
		Fileinfo = nullptr;
	}

	bool SetMLFileInfoFD(MLFileInfo* InFileInfo) const
	{
		MLResult Result = MLFileInfoSetFD(InFileInfo, FileHandle);
		UE_CLOG(MLResult_Ok != Result, LogLuminPlatformFile, Error, TEXT("Error setting MLFileInfo FD : %s"), UTF8_TO_TCHAR(MLGetResultString(Result)));
		return MLResult_Ok == Result;
	}

	virtual int64 Tell() override
	{
#if MANAGE_FILE_HANDLES
		if( IsManaged() )
		{
			return FileOffset;
		}
		else
#endif // MANAGE_FILE_HANDLES
		{
			check(IsValid());
			return lseek(FileHandle, 0, SEEK_CUR);
		}
	}

	virtual bool Seek(int64 NewPosition) override
	{
		check(NewPosition >= 0);

#if MANAGE_FILE_HANDLES
		if( IsManaged() )
		{
			FileOffset = NewPosition >= FileSize ? FileSize - 1 : NewPosition;
			return IsValid() && ActiveHandles[ HandleSlot ] == this ? lseek(FileHandle, FileOffset, SEEK_SET) != -1 : true;
		}
		else
#endif // MANAGE_FILE_HANDLES
		{
			check(IsValid());
			return lseek(FileHandle, NewPosition, SEEK_SET) != -1;
		}
	}

	virtual bool SeekFromEnd(int64 NewPositionRelativeToEnd = 0) override
	{
		check(NewPositionRelativeToEnd <= 0);

#if MANAGE_FILE_HANDLES
		if( IsManaged() )
		{
			FileOffset = (NewPositionRelativeToEnd >= FileSize) ? 0 : ( FileSize + NewPositionRelativeToEnd - 1 );
			return IsValid() && ActiveHandles[ HandleSlot ] == this ? lseek(FileHandle, FileOffset, SEEK_SET) != -1 : true;
		}
		else
#endif // MANAGE_FILE_HANDLES
		{
			check(IsValid());
			return lseek(FileHandle, NewPositionRelativeToEnd, SEEK_END) != -1;
		}
	}

	virtual bool Read(uint8* Destination, int64 BytesToRead) override
	{
#if MANAGE_FILE_HANDLES
		if( IsManaged() )
		{
			ActivateSlot();
			int64 BytesRead = ReadInternal(Destination, BytesToRead);
			FileOffset += BytesRead;
			return BytesRead == BytesToRead;
		}
		else
#endif // MANAGE_FILE_HANDLES
		{
			return ReadInternal(Destination, BytesToRead) == BytesToRead;
		}
	}

	virtual bool Write(const uint8* Source, int64 BytesToWrite) override
	{
		check(IsValid());
		while (BytesToWrite)
		{
			check(BytesToWrite >= 0);
			int64 ThisSize = FMath::Min<int64>(READWRITE_SIZE, BytesToWrite);
			check(Source);
			if (write(FileHandle, Source, ThisSize) != ThisSize)
			{
				return false;
			}
			Source += ThisSize;
			BytesToWrite -= ThisSize;
		}
		return true;
	}

	virtual bool Flush(const bool bFullFlush = false) override
	{
		check(IsValid());
#if MANAGE_FILE_HANDLES
		if (IsManaged())
		{
			return false;
		}
#endif
		return bFullFlush
			? fsync(FileHandle) == 0
			: fdatasync(FileHandle) == 0;
	}

	virtual bool Truncate(int64 NewSize) override
	{
		check(IsValid());
#if MANAGE_FILE_HANDLES
		if (IsManaged())
		{
			return false;
		}
#endif
		int Result = 0;
		do { Result = ftruncate(FileHandle, NewSize); } while (Result < 0 && errno == EINTR);
		return Result == 0;
	}

	virtual int64 Size() override
	{
		#if MANAGE_FILE_HANDLES
		if( IsManaged() )
		{
			return FileSize;
		}
		else
		#endif
		{
			struct stat FileInfo;
			fstat(FileHandle, &FileInfo);
			return FileInfo.st_size;
		}
	}


private:

#if MANAGE_FILE_HANDLES
	FORCEINLINE bool IsManaged()
	{
		return HandleSlot != -1;
	}

	void ActivateSlot()
	{
		if( IsManaged() )
		{
			if( ActiveHandles[ HandleSlot ] != this || (ActiveHandles[ HandleSlot ] && ActiveHandles[ HandleSlot ]->FileHandle == -1) )
			{
				ReserveSlot();

				FileHandle = open(TCHAR_TO_UTF8(*Filename), O_RDONLY | O_CLOEXEC);
				if( FileHandle != -1 )
				{
					lseek(FileHandle, FileOffset, SEEK_SET);
					ActiveHandles[ HandleSlot ] = this;
				}
				else
				{
					UE_LOG(LogLuminPlatformFile, Warning, TEXT("Could not (re)activate slot for file '%s'"), *Filename);
				}
			}
			else
			{
				AccessTimes[ HandleSlot ] = FPlatformTime::Seconds();
			}
		}
	}

	void ReserveSlot()
	{
		HandleSlot = -1;

		// Look for non-reserved slot
		for( int32 i = 0; i < ACTIVE_HANDLE_COUNT; ++i )
		{
			if( ActiveHandles[ i ] == NULL )
			{
				HandleSlot = i;
				break;
			}
		}

		// Take the oldest handle
		if( HandleSlot == -1 )
		{
			int32 Oldest = 0;
			for( int32 i = 1; i < ACTIVE_HANDLE_COUNT; ++i )
			{
				if( AccessTimes[ Oldest ] > AccessTimes[ i ] )
				{
					Oldest = i;
				}
			}

			close( ActiveHandles[ Oldest ]->FileHandle );
			ActiveHandles[ Oldest ]->FileHandle = -1;
			HandleSlot = Oldest;
		}

		ActiveHandles[ HandleSlot ] = NULL;
		AccessTimes[ HandleSlot ] = FPlatformTime::Seconds();
	}
#endif // MANAGE_FILE_HANDLES

	int64 ReadInternal(uint8* Destination, int64 BytesToRead)
	{
		check(IsValid());
		int64 BytesRead = 0;
		while (BytesToRead)
		{
			check(BytesToRead >= 0);
			int64 ThisSize = FMath::Min<int64>(READWRITE_SIZE, BytesToRead);
			check(Destination);
			int64 ThisRead = read(FileHandle, Destination, ThisSize);
			BytesRead += ThisRead;
			if (ThisRead != ThisSize)
			{
				return BytesRead;
			}
			Destination += ThisSize;
			BytesToRead -= ThisSize;
		}
		return BytesRead;
	}

	// Holds the internal file handle.
	int32 FileHandle;

#if MANAGE_FILE_HANDLES
	// Holds the name of the file that this handle represents. Kept around for possible reopen of file.
	FString Filename;

	// Most recent valid slot index for this handle; >=0 for handles which are managed.
	int32 HandleSlot;

	// Current file offset; valid if a managed handle.
	int64 FileOffset;

	// Cached file size; valid if a managed handle.
	int64 FileSize;

	// Each thread keeps a collection of active handles with access times.
	static const int32 ACTIVE_HANDLE_COUNT = 256;
	static __thread FFileHandleLumin* ActiveHandles[ACTIVE_HANDLE_COUNT];
	static __thread double AccessTimes[ACTIVE_HANDLE_COUNT];
#endif // MANAGE_FILE_HANDLES

	MLSharedFileList* SharedFileList;
	const MLFileInfo* Fileinfo;
	bool bReleaseFileInfo;
};

#if MANAGE_FILE_HANDLES
__thread FFileHandleLumin* FFileHandleLumin::ActiveHandles[FFileHandleLumin::ACTIVE_HANDLE_COUNT];
__thread double FFileHandleLumin::AccessTimes[FFileHandleLumin::ACTIVE_HANDLE_COUNT];
#endif // MANAGE_FILE_HANDLES

/**
* Lumin File I/O implementation
**/
FString FLuminPlatformFile::NormalizeFilename(const TCHAR* Filename)
{
	FString Result(Filename);
	FPaths::NormalizeFilename(Result);
	// Don't convert relative path to full path.
	// When jailing is on, the BaseDir() is /package/bin/. The incoming paths are usually of the format ../../../ProjectName/
	// When ConvertRelativePathToFull() tries to collapse the relative path, we run out of the root directory, and hit an edge case and the path is set to /../ProjectName/
	// This still works when jailing is enabled because FLuminPlatformFile::ConvertToLuminPath() gets rid of all relative path prepends and constructs with its own base path.
	// When jailing is disabled, ConvertRelativePathToFull() collapses the incoming path to something else, which is then prepended by FLuminPlatformFile::ConvertToLuminPath()
	// with its own base path and we end up with an invalid path.
	// e.g. when jailing is ofd, BaseDir() is of the format /folder1/folder2/folder3/folder4 and so on. ../../../ProjectName gives us the path as
	// /folder1/ProjectName which is wrong.
	return Result; //FPaths::ConvertRelativePathToFull(Result);
}

FString FLuminPlatformFile::NormalizeDirectory(const TCHAR* Directory)
{
	FString Result(Directory);
	FPaths::NormalizeDirectoryName(Result);
	// Don't convert relative path to full path.
	// See comment in FLuminPlatformFile::NormalizeFilename
	return Result; //FPaths::ConvertRelativePathToFull(Result);
}

bool FLuminPlatformFile::FileExists(const TCHAR* Filename)
{
	FString NormalizedFilename = NormalizeFilename(Filename);

	// Check the read path first, if it doesnt exist, check for the write path instead.
	return (FileExistsInternal(ConvertToLuminPath(NormalizedFilename, false))) ? true : FileExistsInternal(ConvertToLuminPath(NormalizedFilename, true));
}

bool FLuminPlatformFile::FileExistsWithPath(const TCHAR* Filename, FString& OutLuminPath)
{
	bool bExists = false;
	FString NormalizedFilename = NormalizeFilename(Filename);
	FString ReadFilePath = ConvertToLuminPath(NormalizedFilename, false);

	if (FileExistsInternal(ReadFilePath))
	{
		OutLuminPath = ReadFilePath;
		bExists = true;
	}
	else
	{
		FString WriteFilePath = ConvertToLuminPath(NormalizedFilename, true);
		if (FileExistsInternal(WriteFilePath))
		{
			OutLuminPath = WriteFilePath;
			bExists = true;
		}
	}

	return bExists;
}

int64 FLuminPlatformFile::FileSize(const TCHAR* Filename)
{
	// Checking that the file exists will also give us the true location of the file.
	// Which can be either in the read-only or the read-write areas of the application.
	FString LuminPath;
	if (FileExistsWithPath(Filename, LuminPath))
	{
		return FileSizeInterenal(LuminPath);
	}
	return -1;
}

bool FLuminPlatformFile::DeleteFile(const TCHAR* Filename)
{
	// Only delete from write path
	FString IntendedFilename(ConvertToLuminPath(NormalizeFilename(Filename), true));
	return unlink(TCHAR_TO_UTF8(*IntendedFilename)) == 0;
}

bool FLuminPlatformFile::IsReadOnly(const TCHAR* Filename)
{
	// Checking that the file exists will also give us the true location of the file.
	// Which can be either in the read-only or the read-write areas of the application.
	FString LuminPath;
	if (FileExistsWithPath(Filename, LuminPath))
	{
		return IsReadOnlyInternal(LuminPath);
	}
	return false;
}

bool FLuminPlatformFile::MoveFile(const TCHAR* To, const TCHAR* From)
{
	// Move to write path only.
	FString ToLuminFilename = ConvertToLuminPath(NormalizeFilename(To), true);
	FString FromLuminFilename = ConvertToLuminPath(NormalizeFilename(From), true);
	return rename(TCHAR_TO_UTF8(*FromLuminFilename), TCHAR_TO_UTF8(*ToLuminFilename)) == 0;
}

bool FLuminPlatformFile::SetReadOnly(const TCHAR* Filename, bool bNewReadOnlyValue)
{
	FString LuminFilename = ConvertToLuminPath(NormalizeFilename(Filename), false);

	struct stat FileInfo;
	if (stat(TCHAR_TO_UTF8(*LuminFilename), &FileInfo) == 0)
	{
		if (bNewReadOnlyValue)
		{
			FileInfo.st_mode &= ~S_IWUSR;
		}
		else
		{
			FileInfo.st_mode |= S_IWUSR;
		}
		return chmod(TCHAR_TO_UTF8(*LuminFilename), FileInfo.st_mode) == 0;
	}
	return false;
}

FDateTime FLuminPlatformFile::GetTimeStamp(const TCHAR* Filename)
{
	// Checking that the file exists will also give us the true location of the file.
	// Which can be either in the read-only or the read-write areas of the application.
	FString LuminPath;
	if (FileExistsWithPath(Filename, LuminPath))
	{
		return GetTimeStampInternal(LuminPath);
	}
	return FDateTime::MinValue();
}

void FLuminPlatformFile::SetTimeStamp(const TCHAR* Filename, const FDateTime DateTime)
{
	// Update timestamp on a file in the write path only.
	FString LuminFilename = ConvertToLuminPath(NormalizeFilename(Filename), true);

	// get file times
	struct stat FileInfo;
	if(stat(TCHAR_TO_UTF8(*LuminFilename), &FileInfo) != 0)
	{
		return;
	}

	// change the modification time only
	struct utimbuf Times;
	Times.actime = FileInfo.st_atime;
	Times.modtime = (DateTime - UnixEpoch).GetTotalSeconds();
	utime(TCHAR_TO_UTF8(*LuminFilename), &Times);
}

FDateTime FLuminPlatformFile::GetAccessTimeStamp(const TCHAR* Filename)
{
	// Checking that the file exists will also give us the true location of the file.
	// Which can be either in the read-only or the read-write areas of the application.
	FString LuminPath;
	if (FileExistsWithPath(Filename, LuminPath))
	{
		return GetAccessTimeStampInternal(LuminPath);
	}
	return FDateTime::MinValue();
}

FString FLuminPlatformFile::GetFilenameOnDisk(const TCHAR* Filename)
{
	return Filename;
}

IFileHandle* FLuminPlatformFile::OpenRead(const TCHAR* Filename, bool bAllowWrite)
{
	const FString NormalizedFilename = NormalizeFilename(Filename);
	FString LuminFilename = ConvertToLuminPath(NormalizedFilename, false);
	// Check the read path.
	int32 Handle = OpenReadInternal(LuminFilename);
	if (Handle == -1)
	{
		// If not in the read path, check the write path.
		LuminFilename = ConvertToLuminPath(NormalizedFilename, true);
		Handle = OpenReadInternal(LuminFilename);
		if (Handle == -1)
		{
			return nullptr;
		}
	}
	return new FFileHandleLumin(Handle, *LuminFilename, true);
}

IFileHandle* FLuminPlatformFile::OpenWrite(const TCHAR* Filename, bool bAppend, bool bAllowRead)
{
	int Flags = O_CREAT | O_CLOEXEC;	// prevent children from inheriting this

	if (bAllowRead)
	{
		Flags |= O_RDWR;
	}
	else
	{
		Flags |= O_WRONLY;
	}

	// Writable files only in the write path.
	FString LuminFilename = ConvertToLuminPath(Filename, true);

	// create directories if needed.
	if (!CreateDirectoriesFromPath(*LuminFilename))
	{
		return NULL;
	}

	// Caveat: cannot specify O_TRUNC in flags, as this will corrupt the file which may be "locked" by other process. We will ftruncate() it once we "lock" it
	int32 Handle = open(TCHAR_TO_UTF8(*LuminFilename), Flags, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
	if (Handle != -1)
	{
		if (!bAppend)
		{
			if (ftruncate(Handle, 0) != 0)
			{
				int ErrNo = errno;
				UE_LOG(LogLuminPlatformFile, Warning, TEXT("ftruncate() failed for '%s': errno=%d (%s)"),
					*LuminFilename, ErrNo, ANSI_TO_TCHAR(strerror(ErrNo)));
				close(Handle);
				return nullptr;
			}
		}

#if MANAGE_FILE_HANDLES
		FFileHandleLumin* FileHandleLumin = new FFileHandleLumin(Handle, *NormalizeDirectory(*LuminFilename), false);
#else
		FFileHandleLumin* FileHandleLumin = new FFileHandleLumin(Handle, *LuminFilename, false);
#endif // MANAGE_FILE_HANDLES

		if (bAppend)
		{
			FileHandleLumin->SeekFromEnd(0);
		}
		return FileHandleLumin;
	}

	int ErrNo = errno;
	UE_LOG(LogLuminPlatformFile, Warning, TEXT("open('%s', Flags=0x%08X) failed: errno=%d (%s)"), *LuminFilename, Flags, ErrNo, ANSI_TO_TCHAR(strerror(ErrNo)));
	return nullptr;
}

bool FLuminPlatformFile::DirectoryExists(const TCHAR* Directory)
{
	FString NormalizedFilename = NormalizeFilename(Directory);
	// Check the read path first, if it doesnt exist, check for the write path instead.
	return (DirectoryExistsInternal(ConvertToLuminPath(NormalizedFilename, false))) ? true : DirectoryExistsInternal(ConvertToLuminPath(NormalizedFilename, true));
}

bool FLuminPlatformFile::CreateDirectory(const TCHAR* Directory)
{
	// Create directory in write path only.
	FString LuminFilename = ConvertToLuminPath(NormalizeFilename(Directory), true);
	bool result = mkdir(TCHAR_TO_UTF8(*LuminFilename), 0755) == 0;
	return result;
}

bool FLuminPlatformFile::DeleteDirectory(const TCHAR* Directory)
{
	// Delete directory from write path only.
	FString IntendedFilename(ConvertToLuminPath(NormalizeFilename(Directory), true));
	return rmdir(TCHAR_TO_UTF8(*IntendedFilename)) == 0;
}

void FLuminPlatformFile::SetSandboxEnabled(bool bInEnabled)
{
	bIsSandboxEnabled = bInEnabled;
	UE_LOG(LogLuminPlatformFile, Log, TEXT("Application sandbox jail has been %s."), bIsSandboxEnabled ? TEXT("enabled") : TEXT("disabled"));
}

bool FLuminPlatformFile::IsSandboxEnabled() const
{
	return bIsSandboxEnabled;
}

FString FLuminPlatformFile::ConvertToAbsolutePathForExternalAppForWrite(const TCHAR* AbsolutePath)
{
	return ConvertToLuminPath(FString(AbsolutePath), true);
}

FString FLuminPlatformFile::ConvertToAbsolutePathForExternalAppForRead(const TCHAR* AbsolutePath)
{
	return ConvertToLuminPath(FString(AbsolutePath), false);
}

FFileStatData FLuminPlatformFile::GetStatData(const TCHAR* FilenameOrDirectory)
{
	FString NormalizedFilename = NormalizeFilename(FilenameOrDirectory);
	bool found = false;

	// Check the read path first, if it doesnt exist, check for the write path instead.
	FFileStatData result = GetStatDataInternal(ConvertToLuminPath(NormalizedFilename, false), found);
	return (found) ? result : GetStatDataInternal(ConvertToLuminPath(NormalizedFilename, true), found);
}

bool FLuminPlatformFile::IterateDirectory(const TCHAR* Directory, FDirectoryVisitor& Visitor)
{
	const FString DirectoryStr = Directory;
	const FString NormalizedDirectoryStr = NormalizeFilename(Directory);

	return IterateDirectoryCommon(Directory, [&](struct dirent* InEntry) -> bool
	{
		const FString UnicodeEntryName = UTF8_TO_TCHAR(InEntry->d_name);

		bool bIsDirectory = false;
		if (InEntry->d_type != DT_UNKNOWN)
		{
			bIsDirectory = InEntry->d_type == DT_DIR;
		}
		else
		{
			// filesystem does not support d_type, fallback to stat
			struct stat FileInfo;
			const FString AbsoluteUnicodeName = NormalizedDirectoryStr / UnicodeEntryName;
			if (stat(TCHAR_TO_UTF8(*AbsoluteUnicodeName), &FileInfo) != -1)
			{
				bIsDirectory = ((FileInfo.st_mode & S_IFMT) == S_IFDIR);
			}
			else
			{
				int ErrNo = errno;
				UE_LOG(LogLuminPlatformFile, Warning, TEXT( "Cannot determine whether '%s' is a directory - d_type not supported and stat() failed with errno=%d (%s)"), *AbsoluteUnicodeName, ErrNo, UTF8_TO_TCHAR(strerror(ErrNo)));
			}
		}

		return Visitor.Visit(*(DirectoryStr / UnicodeEntryName), bIsDirectory);
	});
}

bool FLuminPlatformFile::IterateDirectoryStat(const TCHAR* Directory, FDirectoryStatVisitor& Visitor)
{
	const FString DirectoryStr = Directory;
	const FString NormalizedDirectoryStr = NormalizeFilename(Directory);

	return IterateDirectoryCommon(Directory, [&](struct dirent* InEntry) -> bool
	{
		const FString UnicodeEntryName = UTF8_TO_TCHAR(InEntry->d_name);

		struct stat FileInfo;
		const FString AbsoluteUnicodeName = NormalizedDirectoryStr / UnicodeEntryName;
		// Check the read path first.
		if (stat(TCHAR_TO_UTF8(*ConvertToLuminPath(AbsoluteUnicodeName, false)), &FileInfo) != -1)
		{
			return Visitor.Visit(*(DirectoryStr / UnicodeEntryName), UnixStatToUEFileData(FileInfo));
		}
		// If it doesnt exist, check for the write path instead.
		else if(stat(TCHAR_TO_UTF8(*ConvertToLuminPath(AbsoluteUnicodeName, true)), &FileInfo) != -1)
		{
			return Visitor.Visit(*(DirectoryStr / UnicodeEntryName), UnixStatToUEFileData(FileInfo));
		}

		return true;
	});
}

bool FLuminPlatformFile::IterateDirectoryCommon(const TCHAR* Directory, const TFunctionRef<bool(struct dirent*)>& Visitor)
{
	bool Result = false;

	FString NormalizedDirectory = NormalizeFilename(Directory);
	// Check the read path first.
	DIR* Handle = opendir(TCHAR_TO_UTF8(*ConvertToLuminPath(NormalizedDirectory, false)));
	if (!Handle)
	{
		// If it doesnt exist, check for the write path instead.
		Handle = opendir(TCHAR_TO_UTF8(*ConvertToLuminPath(NormalizedDirectory, true)));
	}
	if (Handle)
	{
		Result = true;
		struct dirent* Entry;
		while ((Entry = readdir(Handle)) != NULL)
		{
			if (FCString::Strcmp(UTF8_TO_TCHAR(Entry->d_name), TEXT(".")) && FCString::Strcmp(UTF8_TO_TCHAR(Entry->d_name), TEXT("..")))
			{
				Result = Visitor(Entry);
			}
		}
		closedir(Handle);
	}
	return Result;
}

bool FLuminPlatformFile::CreateDirectoriesFromPath(const TCHAR* Path)
{
	// if the file already exists, then directories exist.
	struct stat FileInfo;
	if (stat(TCHAR_TO_UTF8(*NormalizeFilename(Path)), &FileInfo) != -1)
	{
		return true;
	}

	// convert path to native char string.
	int32 Len = strlen(TCHAR_TO_UTF8(*NormalizeFilename(Path)));
	char *DirPath = reinterpret_cast<char *>(FMemory::Malloc((Len+2) * sizeof(char)));
	char *SubPath = reinterpret_cast<char *>(FMemory::Malloc((Len+2) * sizeof(char)));
	strcpy(DirPath, TCHAR_TO_UTF8(*NormalizeFilename(Path)));

	for (int32 i=0; i<Len; ++i)
	{
		SubPath[i] = DirPath[i];

		if (SubPath[i] == '/')
		{
			SubPath[i+1] = 0;

			// directory exists?
			struct stat SubPathFileInfo;
			if (stat(SubPath, &SubPathFileInfo) == -1)
			{
				// nope. create it.
				if (mkdir(SubPath, 0755) == -1)
				{
					int ErrNo = errno;
					UE_LOG(LogLuminPlatformFile, Warning, TEXT("create dir('%s') failed: errno=%d (%s)"),
						UTF8_TO_TCHAR(DirPath), ErrNo, ANSI_TO_TCHAR(strerror(ErrNo)));
					FMemory::Free(DirPath);
					FMemory::Free(SubPath);
					return false;
				}
			}
		}
	}

	FMemory::Free(DirPath);
	FMemory::Free(SubPath);
	return true;
}

FString FLuminPlatformFile::ConvertToLuminPath(const FString& Filename, bool bForWrite) const
{
	if (!IsSandboxEnabled())
	{
		return Filename;
	}
	FString Result = Filename;
	Result.ReplaceInline(TEXT("../"), TEXT(""));
	Result.ReplaceInline(TEXT(".."), TEXT(""));

	// Remove the base app path if present, we will prepend it the correct base path as needed.
	Result.ReplaceInline(*FLuminPlatformMisc::GetApplicationPackageDirectoryPath(), TEXT(""));
	// Remove the writable path if present, we will prepend it the correct base path as needed.
	Result.ReplaceInline(*FLuminPlatformMisc::GetApplicationWritableDirectoryPath(), TEXT(""));

	FString lhs;

	// If the file is for writing, then add it to the app writable directory path.
	if (bForWrite)
	{
		lhs = FLuminPlatformMisc::GetApplicationWritableDirectoryPath();
	}
	else
	{
		// If filehostip exists in the command line, cook on the fly read path should be used.
		FString Value;
		// Cache this value as the command line doesn't change.
		static bool bHasHostIP = FParse::Value(FCommandLine::Get(), TEXT("filehostip"), Value) || FParse::Value(FCommandLine::Get(), TEXT("streaminghostip"), Value);
		static bool bIsIterative = FParse::Value(FCommandLine::Get(), TEXT("iterative"), Value);

		if (bHasHostIP)
		{
			lhs = FLuminPlatformMisc::GetApplicationWritableDirectoryPath();
		}
		else if (bIsIterative)
		{
			lhs = FLuminPlatformMisc::GetApplicationWritableDirectoryPath();
		}
		else
		{
			lhs = FLuminPlatformMisc::GetApplicationPackageDirectoryPath();
		}
	}

#if 0
	FPlatformMisc::LowLevelOutputDebugStringf(TEXT("LOG_LUMIN_PATH Write = %d Input = %s Output = %s"), bForWrite, *Filename, *Result.ToLower());
#endif

	// Lower only unreal path.
	FString rhs = Result.ToLower();
	rhs.RemoveFromStart(TEXT("/"));
	lhs.RemoveFromEnd(TEXT("/"));

	Result = lhs / rhs;

	return Result;
}

IFileHandle* GetHandleForSharedFile(const TCHAR* Filename, bool bForWrite)
{
	const char* Filename_UTF8 = TCHAR_TO_UTF8(Filename);
	MLSharedFileList* SharedFileList = nullptr;
	MLResult Result;
	if (bForWrite)
	{
		Result = MLSharedFileWrite(&Filename_UTF8, 1, &SharedFileList);
	}
	else
	{
		Result = MLSharedFileRead(&Filename_UTF8, 1, &SharedFileList);
	}

	if (Result == MLResult_Ok && SharedFileList != nullptr)
	{
		MLHandle ListLength = 0;
		Result = MLSharedFileGetListLength(SharedFileList, &ListLength);
		if (Result == MLResult_Ok && ListLength > 0)
		{
			MLFileInfo* FileInfo = nullptr;
			Result = MLSharedFileGetMLFileInfoByIndex(SharedFileList, 0, &FileInfo);
			if (Result == MLResult_Ok && FileInfo != nullptr)
			{
				MLFileDescriptor FileHandle = -1;
				Result = MLFileInfoGetFD(FileInfo, &FileHandle);
				if (Result == MLResult_Ok && FileHandle > -1)
				{
					return new FFileHandleLumin(FileHandle, SharedFileList);
				}
			}
		}
	}

	UE_LOG(LogLuminPlatformFile, Error, TEXT("Error %s opening shared file %s for read."), UTF8_TO_TCHAR(MLSharedFileGetResultString(Result)), Filename);
	return nullptr;
}

IFileHandle* FLuminPlatformFile::SharedFileOpenRead(const TCHAR* Filename)
{
	return GetHandleForSharedFile(Filename, false);
}

IFileHandle* FLuminPlatformFile::SharedFileOpenWrite(const TCHAR* Filename)
{
	return GetHandleForSharedFile(Filename, true);
}

IFileHandle* FLuminPlatformFile::GetFileHandleForMLFileInfo(const void* InFileInfo)
{
	const MLFileInfo* Fileinfo = static_cast<const MLFileInfo*>(InFileInfo);
	MLFileDescriptor FileHandle = -1;
	MLResult Result = MLFileInfoGetFD(Fileinfo, &FileHandle);
	if (Result == MLResult_Ok && FileHandle > -1)
	{
		return new FFileHandleLumin(FileHandle, Fileinfo);
	}

	return nullptr;
}

bool FLuminPlatformFile::SetMLFileInfoFD(const IFileHandle* FileHandle, void* InFileInfo)
{
	check(FileHandle);
	check(InFileInfo);
	const FFileHandleLumin* LuminFileHandle = static_cast<const FFileHandleLumin*>(FileHandle);
	return LuminFileHandle->SetMLFileInfoFD(static_cast<MLFileInfo*>(InFileInfo));
}

bool FLuminPlatformFile::FileExistsInternal(const FString& NormalizedFilename) const
{
	struct stat FileInfo;
	if (stat(TCHAR_TO_UTF8(*NormalizedFilename), &FileInfo) != -1)
	{
		return S_ISREG(FileInfo.st_mode);
	}

	return false;
}

int64 FLuminPlatformFile::FileSizeInterenal(const FString& NormalizedFilename) const
{
	struct stat FileInfo;
	FileInfo.st_size = -1;
	if (stat(TCHAR_TO_UTF8(*NormalizedFilename), &FileInfo) != -1)
	{
		// make sure to return -1 for directories
		if (S_ISDIR(FileInfo.st_mode))
		{
			FileInfo.st_size = -1;
		}
	}
	return FileInfo.st_size;
}

bool FLuminPlatformFile::IsReadOnlyInternal(const FString& NormalizedFilename) const
{
	// skipping checking F_OK since this is already taken care of by case mapper
	if (access(TCHAR_TO_UTF8(*NormalizedFilename), W_OK) == -1)
	{
		return errno == EACCES;
	}
	return false;
}

FDateTime FLuminPlatformFile::GetTimeStampInternal(const FString& NormalizedFilename) const
{
	// get file times
	struct stat FileInfo;
	if (stat(TCHAR_TO_UTF8(*NormalizedFilename), &FileInfo) == -1)
	{
		if (errno == EOVERFLOW)
		{
			// hacky workaround for files mounted on Samba (see https://bugzilla.samba.org/show_bug.cgi?id=7707)
			return FDateTime::Now();
		}
		else
		{
			return FDateTime::MinValue();
		}
	}

	// convert _stat time to FDateTime
	FTimespan TimeSinceEpoch(0, 0, FileInfo.st_mtime);
	return UnixEpoch + TimeSinceEpoch;
}

FDateTime FLuminPlatformFile::GetAccessTimeStampInternal(const FString& NormalizedFilename) const
{
	// get file times
	struct stat FileInfo;
	if (stat(TCHAR_TO_UTF8(*NormalizedFilename), &FileInfo) == -1)
	{
		return FDateTime::MinValue();
	}

	// convert _stat time to FDateTime
	FTimespan TimeSinceEpoch(0, 0, FileInfo.st_atime);
	return UnixEpoch + TimeSinceEpoch;
}

FFileStatData FLuminPlatformFile::GetStatDataInternal(const FString& NormalizedFilename, bool& bFound) const
{
	struct stat FileInfo;
	if (stat(TCHAR_TO_UTF8(*NormalizedFilename), &FileInfo) == -1)
	{
		bFound = false;
		return FFileStatData();
	}

	bFound = true;
	return UnixStatToUEFileData(FileInfo);
}

bool FLuminPlatformFile::DirectoryExistsInternal(const FString& NormalizedFilename) const
{
	struct stat FileInfo;
	if (stat(TCHAR_TO_UTF8(*NormalizedFilename), &FileInfo) != -1)
	{
		return S_ISDIR(FileInfo.st_mode);
	}
	return false;
}

int32 FLuminPlatformFile::OpenReadInternal(const FString& NormalizedFilename) const
{
	// We can get some "absolute" filenames like "D:/Blah/" here (e.g. non-Lumin paths to source files embedded in assets).
	// In that case, fail silently.
	if (NormalizedFilename.IsEmpty() || NormalizedFilename[0] != TEXT('/'))
	{
		return -1;
	}

	// try opening right away
	int32 Handle = open(TCHAR_TO_UTF8(*NormalizedFilename), O_RDONLY | O_CLOEXEC);
	// log non-standard errors only
	if (Handle == -1 && ENOENT != errno)
	{
		int ErrNo = errno;
		UE_LOG(LogLuminPlatformFile, Warning, TEXT("open('%s', O_RDONLY | O_CLOEXEC) failed: errno=%d (%s)"), *NormalizedFilename, ErrNo, ANSI_TO_TCHAR(strerror(ErrNo)));
	}
	return Handle;
}

IPlatformFile& IPlatformFile::GetPlatformPhysical()
{
	static FLuminPlatformFile Singleton;
	return Singleton;
}

