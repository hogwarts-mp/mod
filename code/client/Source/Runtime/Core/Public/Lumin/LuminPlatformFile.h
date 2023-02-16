// Copyright Epic Games, Inc. All Rights Reserved.
// Copyright 2016 Magic Leap, Inc. All Rights Reserved.

/*=============================================================================================
Lumin platform File functions
==============================================================================================*/

#pragma once
#include "GenericPlatform/GenericPlatformFile.h"

struct CORE_API FLuminFileInfo
{
public:
	FLuminFileInfo();

	FString MimeType;
	FString FileName;
	IFileHandle* FileHandle;
};

/**
 * File I/O implementation
**/
class CORE_API FLuminPlatformFile : public IPhysicalPlatformFile
{
protected:
	virtual FString NormalizeFilename(const TCHAR* Filename);
	virtual FString NormalizeDirectory(const TCHAR* Directory);
public:
	//~ For visibility of overloads we don't override
	using IPhysicalPlatformFile::IterateDirectory;
	using IPhysicalPlatformFile::IterateDirectoryStat;

	virtual bool FileExists(const TCHAR* Filename) override;
	bool FileExistsWithPath(const TCHAR* Filename, FString& OutLuminPath);
	virtual int64 FileSize(const TCHAR* Filename) override;
	virtual bool DeleteFile(const TCHAR* Filename) override;
	virtual bool IsReadOnly(const TCHAR* Filename) override;
	virtual bool MoveFile(const TCHAR* To, const TCHAR* From) override;
	virtual bool SetReadOnly(const TCHAR* Filename, bool bNewReadOnlyValue) override;


	virtual FDateTime GetTimeStamp(const TCHAR* Filename) override;

	virtual void SetTimeStamp(const TCHAR* Filename, const FDateTime DateTime) override;

	virtual FDateTime GetAccessTimeStamp(const TCHAR* Filename) override;
	virtual FString GetFilenameOnDisk(const TCHAR* Filename) override;

	virtual IFileHandle* OpenRead(const TCHAR* Filename, bool bAllowWrite = false) override;
	virtual IFileHandle* OpenWrite(const TCHAR* Filename, bool bAppend = false, bool bAllowRead = false) override;
	virtual bool DirectoryExists(const TCHAR* Directory) override;
	virtual bool CreateDirectory(const TCHAR* Directory) override;
	virtual bool DeleteDirectory(const TCHAR* Directory) override;
	/**
	 *	Enables/disables application sandbox jail. When sandboxing is enabled all paths are prepended with
	 *	the app root directory. When disabled paths are not prepended or checked for safety. For example,
	 *	reading files from the directory /system/etc/security/cacerts requires reading from outside the app root directory,
	 *	which cannot be done when sandboxing is enabled. Toggling the sandbox must be done manually by calling this function both
	 *	before and after attempting to access any path outside of the app  root directory. Only disable sandboxing when certain you know what you're doing.
	 *	
	 *	@param bInEnabled True to enable sandboxing, false to disable
	 */
	virtual void SetSandboxEnabled(bool bInEnabled) override;
	/**
	 *	Returns whether sandboxing is enabled or disabled.
	 *
	 *	@return bool Returns true when sandboxing is enabled, otherwise returns false.
	*/
	virtual bool IsSandboxEnabled() const override;
	virtual FString ConvertToAbsolutePathForExternalAppForWrite(const TCHAR* AbsolutePath) override;
	virtual FString ConvertToAbsolutePathForExternalAppForRead(const TCHAR* AbsolutePath) override;

	virtual FFileStatData GetStatData(const TCHAR* FilenameOrDirectory) override;

	bool CreateDirectoriesFromPath(const TCHAR* Path);

	virtual bool IterateDirectory(const TCHAR* Directory, FDirectoryVisitor& Visitor) override;
	virtual bool IterateDirectoryStat(const TCHAR* Directory, FDirectoryStatVisitor& Visitor) override;

	FString ConvertToLuminPath(const FString& Filename, bool bForWrite) const;

	/**
		Return a IFileHandle pointer to read the user shared file (file descriptor recieved from ml_sharedfile api).
		@param FileName Name of the shared file to read.
		@return IFileHandle pointer to read the file. If the application does not have user permission to access the file, nullptr will be returned.
	*/
	IFileHandle* SharedFileOpenRead(const TCHAR* Filename);

	/**
		Return a IFileHandle pointer to write the user shared file (file descriptor recieved from ml_sharedfile api)..
		@param FileName Name of the shared file to write to. Needs to just be a file name, cannot be a path.
		@return IFileHandle pointer to read the file. If the application does not have user permission to access the file, nullptr will be returned.
	*/
	IFileHandle* SharedFileOpenWrite(const TCHAR* Filename);

	IFileHandle* GetFileHandleForMLFileInfo(const void* FileInfo);
	bool SetMLFileInfoFD(const IFileHandle* FileHandle, void* FileInfo);

protected:
	bool IterateDirectoryCommon(const TCHAR* Directory, const TFunctionRef<bool(struct dirent*)>& Visitor);
	bool bIsSandboxEnabled = true;

private:
	bool FileExistsInternal(const FString& NormalizedFilename) const;
	int64 FileSizeInterenal(const FString& NormalizedFilename) const;
	bool IsReadOnlyInternal(const FString& NormalizedFilename) const;
	FDateTime GetTimeStampInternal(const FString& NormalizedFilename) const;
	FDateTime GetAccessTimeStampInternal(const FString& NormalizedFilename) const;
	FFileStatData GetStatDataInternal(const FString& NormalizedFilename, bool& bFound) const;
	bool DirectoryExistsInternal(const FString& NormalizedFilename) const;
	int32 OpenReadInternal(const FString& NormalizedFilename) const;
};
