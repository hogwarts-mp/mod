// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	PackageName.h: Unreal package name utility functions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "Containers/StringView.h"

struct FFileStatData;

class COREUOBJECT_API FPackageName
{
public:

	/**
	 * Helper function for converting short to long script package name (InputCore -> /Script/InputCore)
	 *
	 * @param InShortName Short package name.
	 * @return Long package name.
	 */
	static FString ConvertToLongScriptPackageName(const TCHAR* InShortName);

	/**
	 * Registers all short package names found in ini files.
	 */
	static void RegisterShortPackageNamesForUObjectModules();

	/**
	 * Finds long script package name associated with a short package name.
	 *
	 * @param InShortName Short script package name.
	 * @return Long script package name (/Script/Package) associated with short name or NULL.
	 */
	static FName* FindScriptPackageName(FName InShortName);

	/** 
	 * Tries to convert the supplied relative or absolute filename to a long package name/path starting with a root like /game
	 * This works on both package names and directories, and it does not validate that it actually exists on disk.
	 * 
	 * @param InFilename Filename to convert.
	 * @param OutPackageName The resulting long package name if the conversion was successful.
	 * @param OutFailureReason Description of an error if the conversion failed.
	 * @return Returns true if the supplied filename properly maps to one of the long package roots.
	 */
	static bool TryConvertFilenameToLongPackageName(const FString& InFilename, FString& OutPackageName, FString* OutFailureReason = nullptr);

	/** 
	 * Converts the supplied filename to long package name.
	 * Throws a fatal error if the conversion is not successful.
	 * 
	 * @param InFilename Filename to convert.
	 * @return Long package name.
	 */
	static FString FilenameToLongPackageName(const FString& InFilename);

	/** 
	 * Tries to convert a long package name to a file name with the supplied extension.
	 * This can be called on package paths as well, provide no extension in that case
	 *
	 * @param InLongPackageName Long Package Name
	 * @param InExtension Package extension.
	 * @return Package filename.
	 */
	static bool TryConvertLongPackageNameToFilename(const FString& InLongPackageName, FString& OutFilename, const FString& InExtension = TEXT(""));

	/** 
	 * Converts a long package name to a file name with the supplied extension.
	 * Throws a fatal error if the conversion is not successful.
	 *
	 * @param InLongPackageName Long Package Name
	 * @param InExtension Package extension.
	 * @return Package filename.
	 */
	static FString LongPackageNameToFilename(const FString& InLongPackageName, const FString& InExtension = TEXT(""));

	/** 
	 * Returns the path to the specified package, excluding the short package name
	 *
	 * @param InLongPackageName Long Package Name.
	 * @return The path containing the specified package.
	 */
	static FString GetLongPackagePath(const FString& InLongPackageName);

	/** 
	 * Returns the clean asset name for the specified package, same as GetShortName
	 *
	 * @param InLongPackageName Long Package Name
	 * @return Clean asset name.
	 */
	static FString GetLongPackageAssetName(const FString& InLongPackageName);

	/** 
	 * Convert a long package name into root, path, and name components
	 *
	 * @param InLongPackageName Package Name.
	 * @param OutPackageRoot The package root path, eg "/Game/"
	 * @param OutPackagePath The path from the mount point to the package, eg "Maps/TestMaps/
	 * @param OutPackageName The name of the package, including its extension, eg "MyMap.umap"
	 * @param bStripRootLeadingSlash String any leading / character from the returned root
	 * @return True if the conversion was possible, false otherwise
	 */
	static bool SplitLongPackageName(const FString& InLongPackageName, FString& OutPackageRoot, FString& OutPackagePath, FString& OutPackageName, const bool bStripRootLeadingSlash = false);

	/**
	 * Split a full object path (Class /Path/To/A/Package.Object:SubObject) into its constituent pieces
	 *  
	 * @param InFullObjectPath			Full object path we want to split
	 * @param OutClassName				The extracted class name (Class)
	 * @param OutPackageName			The extracted package name (/Path/To/A/Package)
	 * @param OutObjectName				The extracted object name (Object)
	 * @param OutSubObjectName			The extracted subobject name (SubObject)
	 */
	static void SplitFullObjectPath(const FString& InFullObjectPath, FString& OutClassName, FString& OutPackageName, FString& OutObjectName, FString& OutSubObjectName);

	/** 
	 * Returns true if the path starts with a valid root (i.e. /Game/, /Engine/, etc) and contains no illegal characters.
	 *
	 * @param InLongPackageName			The package name to test
	 * @param bIncludeReadOnlyRoots		If true, will include roots that you should not save to. (/Temp/, /Script/)
	 * @param OutReason					When returning false, this will provide a description of what was wrong with the name.
	 * @return							true if a valid long package name
	 */
	static bool IsValidLongPackageName(const FString& InLongPackageName, bool bIncludeReadOnlyRoots = false, FText* OutReason = nullptr);

	/**
	 * Returns true if the path starts with a valid root (i.e. /Game/, /Engine/, etc) and contains no illegal characters.
	 * This validates that the packagename is valid, and also makes sure the object after package name is also correct.
	 * This will return false if passed a path starting with Classname'
	 *
	 * @param InObjectPath				The object path to test
	 * @param OutReason					When returning false, this will provide a description of what was wrong with the name.
	 * @return							true if a valid object path
	 */
	static bool IsValidObjectPath(const FString& InObjectPath, FText* OutReason = nullptr);

	/**
	 * Returns true if the path starts with a valid root (i.e. /Game/, /Engine/, etc).
	 * 
	 *
	 * @param InObjectPath				The object path to test
	 * @return							true if a valid object path
	 */
	static bool IsValidPath(const FString& InPath);

	/**
	 * Checks if the given string is a long package name or not.
	 *
	 * @param PossiblyLongName Package name.
	 * @return true if the given name is a long package name, false otherwise.
	 */
	static bool IsShortPackageName(const FString& PossiblyLongName);
	static bool IsShortPackageName(const FName PossiblyLongName);
	static bool IsShortPackageName(FStringView PossiblyLongName);

	/**
	 * Converts package name to short name.
	 *
	 * @param Package Package with name to convert.
	 * @return Short package name.
	 */
	static FString GetShortName(const UPackage* Package);

	/**
	 * Converts package name to short name.
	 *
	 * @param LongName Package name to convert.
	 * @return Short package name.
	 */
	static FString GetShortName(const FString& LongName);
	static FString GetShortName(const FName& LongName);
	static FString GetShortName(const TCHAR* LongName);

	/**
	 * Converts package name to short name.
	 *
	 * @param LongName Package name to convert.
	 * @return Short package name.
	 */
	static FName GetShortFName(const FString& LongName);
	static FName GetShortFName(const FName& LongName);
	static FName GetShortFName(const TCHAR* LongName);

	/**
	 * Tries to convert a file or directory in game-relative package name format to the corresponding local path
	 * Game-relative package names can be a full package path (/Game/Folder/File, /Engine/Folder/File, /PluginName/Folder/File) or
	 * a relative path (Folder/File).
	 * Full package paths must be in a mounted directory to be successfully converted.
	 *
	 * @param RelativePackagePath The path in game-relative package format (allowed to have or not have an extension).
	 * @param OutLocalPath The corresponding local-path file (with the extension or lack of extension from the input).
	 * @return Whether the conversion was successful.
	 */
	static bool TryConvertGameRelativePackagePathToLocalPath(FStringView RelativePackagePath, FString& OutLocalPath);

	/**
	 * This will insert a mount point at the head of the search chain (so it can overlap an existing mount point and win).
	 *
	 * @param RootPath Logical Root Path.
	 * @param ContentPath Content Path on disk.
	 */
	static void RegisterMountPoint(const FString& RootPath, const FString& ContentPath);

	/**
	 * This will remove a previously inserted mount point.
	 *
	 * @param RootPath Logical Root Path.
	 * @param ContentPath Content Path on disk.
	 */
	static void UnRegisterMountPoint(const FString& RootPath, const FString& ContentPath);

	/**
	 * Returns whether the specific logical root path is a valid mount point.
	 */
	static bool MountPointExists(const FString& RootPath);

	/**
	 * Get the mount point for a given package path
	 * 
	 * @param InPackagePath The package path to get the mount point for
	 * @param InWithoutSlashes Optional parameters that keeps the slashes around the mount point if false
	 * @return FName corresponding to the mount point, or Empty if invalid
	 */
	static FName GetPackageMountPoint(const FString& InPackagePath, bool InWithoutSlashes = true);

	/**
	 * Checks if the package exists on disk.
	 * 
	 * @param LongPackageName Package name.
	 * @param OutFilename Package filename on disk.
	 * @param InAllowTextFormats Detect text format packages as well as binary (priority to text)
	 * @return true if the specified package name points to an existing package, false otherwise.
	 **/
	static bool DoesPackageExist(const FString& LongPackageName, const FGuid* Guid = NULL, FString* OutFilename = NULL, bool InAllowTextFormats = true);

	/**
	 * Attempts to find a package given its short name on disk (very slow).
	 * 
	 * @param PackageName Package to find.
	 * @param OutLongPackageName Long package name corresponding to the found file (if any).
	 * @return true if the specified package name points to an existing package, false otherwise.
	 **/
	static bool SearchForPackageOnDisk(const FString& PackageName, FString* OutLongPackageName = NULL, FString* OutFilename = NULL);

	/**
	 * Tries to convert object path with short package name to object path with long package name found on disk (very slow)
	 *
	 * @param ObjectPath Path to the object.
	 * @param OutLongPackageName Converted object path.
	 *
	 * @returns True if succeeded. False otherwise.
	 */
	static bool TryConvertShortPackagePathToLongInObjectPath(const FString& ObjectPath, FString& ConvertedObjectPath);

	/**
	 * Gets normalized object path i.e. with long package format.
	 *
	 * @param ObjectPath Path to the object.
	 *
	 * @returns Normalized path (or empty path, if short object path was given and it wasn't found on the disk).
	 */
	static FString GetNormalizedObjectPath(const FString& ObjectPath);

	/**
	 * Gets the resolved path of a long package as determined by the delegates registered with FCoreDelegates::PackageNameResolvers.
	 * This allows systems such as localization to redirect requests for a package to a more appropriate alternative, or to 
	 * nix the request altogether.
	 *
	 * @param InSourcePackagePath	Path to the source package.
	 *
	 * @returns Resolved package path, or the source package path if there is no resolution occurs.
	 */
	static FString GetDelegateResolvedPackagePath(const FString& InSourcePackagePath);

	/**
	 * Gets the source version of a localized long package path (it is also safe to pass non-localized paths into this function).
	 *
	 * @param InLocalizedPackagePath Path to the localized package.
	 *
	 * @returns Source package path.
	 */
	static FString GetSourcePackagePath(const FString& InLocalizedPackagePath);

	/**
	 * Gets the localized version of a long package path for the current culture, or returns the source package if there is no suitable localized package.
	 *
	 * @param InSourcePackagePath	Path to the source package.
	 *
	 * @returns Localized package path, or the source package path if there is no suitable localized package.
	 */
	static FString GetLocalizedPackagePath(const FString& InSourcePackagePath);

	/**
	 * Gets the localized version of a long package path for the given culture, or returns the source package if there is no suitable localized package.
	 *
	 * @param InSourcePackagePath	Path to the source package.
	 * @param InCultureName			Culture name to get the localized package for.
	 *
	 * @returns Localized package path, or the source package path if there is no suitable localized package.
	 */
	static FString GetLocalizedPackagePath(const FString& InSourcePackagePath, const FString& InCultureName);

	/** 
	 * Returns the file extension for packages containing assets.
	 *
	 * @return	file extension for asset packages ( dot included )
	 */
	static FORCEINLINE const FString& GetAssetPackageExtension()
	{
		return AssetPackageExtension;
	}
	/** 
	 * Returns the file extension for packages containing assets.
	 *
	 * @return	file extension for asset packages ( dot included )
	 */
	static FORCEINLINE const FString& GetMapPackageExtension()
	{
		return MapPackageExtension;
	}

	/**
	* Returns the file extension for packages containing text assets.
	*
	* @return	file extension for text asset packages ( dot included )
	*/
	static FORCEINLINE const FString& GetTextAssetPackageExtension()
	{
		return TextAssetPackageExtension;
	}

	/**
	* Returns the file extension for packages containing text maps.
	*
	* @return	file extension for text map packages ( dot included )
	*/
	static FORCEINLINE const FString& GetTextMapPackageExtension()
	{
		return TextMapPackageExtension;
	}

	/**
	 * Returns whether the passed in extension is a valid text package
	 * extension. Extensions with and without trailing dots are supported.
	 *
	 * @param	Extension to test.
	 * @return	True if Ext is either an text asset or a text map extension, otherwise false
	 */
	static bool IsTextPackageExtension(const TCHAR* Ext);

	/**
	 * Returns whether the passed in extension is a valid text asset package
	 * extension. Extensions with and without trailing dots are supported.
	 *
	 * @param	Extension to test.
	 * @return	True if Ext is a text asset extension, otherwise false
	 */
	static bool IsTextAssetPackageExtension(const TCHAR* Ext);

	/**
	 * Returns whether the passed in extension is a valid text map package
	 * extension. Extensions with and without trailing dots are supported.
	 *
	 * @param	Extension to test.
	 * @return	True if Ext is a text map extension, otherwise false
	 */
	static bool IsTextMapPackageExtension(const TCHAR* Ext);

	/** 
	 * Returns whether the passed in extension is a valid binary package
	 * extension. Extensions with and without trailing dots are supported.
	 *
	 * @param	Extension to test. 
	 * @return	True if Ext is either a binary  asset or map extension, otherwise false
	 */
	static bool IsPackageExtension(const TCHAR* Ext);

	/**
	 * Returns whether the passed in extension is a valid binary asset package
	 * extension. Extensions with and without trailing dots are supported.
	 *
	 * @param	Extension to test.
	 * @return	True if Ext is a binary asset extension, otherwise false
	 */
	static bool IsAssetPackageExtension(const TCHAR* Ext);

	/**
	 * Returns whether the passed in extension is a valid binary map package
	 * extension. Extensions with and without trailing dots are supported.
	 *
	 * @param	Extension to test.
	 * @return	True if Ext is a binary asset extension, otherwise false
	 */
	static bool IsMapPackageExtension(const TCHAR* Ext);

	/** 
	 * Returns whether the passed in filename ends with any of the known
	 * package extensions.
	 *
	 * @param	Filename to test. 
	 * @return	True if the filename ends with a package extension.
	 */
	static FORCEINLINE bool IsPackageFilename(const FString& Filename)
	{
		return Filename.EndsWith(AssetPackageExtension) || Filename.EndsWith(MapPackageExtension);
	}

	/**
	 * This will recurse over a directory structure looking for packages.
	 * 
	 * @param	OutPackages			The output array that is filled out with the discovered file paths
	 * @param	RootDir				The root of the directory structure to recurse through
	 * @return	Returns true if any packages have been found, otherwise false
	 */
	static bool FindPackagesInDirectory(TArray<FString>& OutPackages, const FString& RootDir);

	/**
	 * This will recurse over the given list of directory structures looking for packages.
	 *
	 * @param	OutPackages			The output array that is filled out with the discovered file paths
	 * @param	RootDirss			The roots of the directory structures to recurse through
	 * @return	Returns true if any packages have been found, otherwise false
	 */
	static bool FindPackagesInDirectories(TArray<FString>& OutPackages, const TArrayView<const FString>& RootDirs);

	/**
	 * This will recurse over a directory structure looking for packages.
	 * 
	 * @param	RootDirectory		The root of the directory structure to recurse through
	 * @param	Visitor				Visitor to call for each package file found (takes the package filename, and optionally the stat data for the file - returns true to continue iterating)
	 */
	typedef TFunctionRef<bool(const TCHAR*)> FPackageNameVisitor;
	typedef TFunctionRef<bool(const TCHAR*, const FFileStatData&)> FPackageNameStatVisitor;
	static void IteratePackagesInDirectory(const FString& RootDir, const FPackageNameVisitor& Visitor);
	static void IteratePackagesInDirectory(const FString& RootDir, const FPackageNameStatVisitor& Visitor);

	/** Event that is triggered when a new content path is mounted */
	DECLARE_MULTICAST_DELEGATE_TwoParams( FOnContentPathMountedEvent, const FString& /* Asset path */, const FString& /* ContentPath */ );
	static FOnContentPathMountedEvent& OnContentPathMounted()
	{
		return OnContentPathMountedEvent;
	}

	/** Event that is triggered when a new content path is removed */
	DECLARE_MULTICAST_DELEGATE_TwoParams(FOnContentPathDismountedEvent, const FString& /* Asset path */, const FString& /* ContentPath */ );
	static FOnContentPathDismountedEvent& OnContentPathDismounted()
	{
		return OnContentPathDismountedEvent;
	}

	/**
	 * Queries all of the root content paths, like "/Game/", "/Engine/", and any dynamically added paths
	 *
	 * @param	OutRootContentPaths	[Out] List of content paths
	 * @param	bIncludeReadOnlyRoots	  Include read only root content paths such as "/Temp/"
	 * @param	bWithoutLeadingSlashes	  Strip slash at start of each path to end up with "Game/"
	 * @param	bWithoutTrailingSlashes	  Strip trailing slash at end of each path to end up with "/Game"
	 */
	static void QueryRootContentPaths( TArray<FString>& OutRootContentPaths, bool bIncludeReadOnlyRoots = false, bool bWithoutLeadingSlashes = false, bool bWithoutTrailingSlashes = false);
	
	/** If the FLongPackagePathsSingleton is not created yet, this function will create it and thus allow mount points to be added */
	static void EnsureContentPathsAreRegistered();

	/** 
	 * Converts the supplied export text path to an object path and class name.
	 * 
	 * @param InExportTextPath The export text path for an object. Takes on the form: ClassName'ObjectPath'
	 * @param OutClassName The name of the class at the start of the path.
	 * @param OutObjectPath The path to the object.
	 * @return True if the supplied export text path could be parsed
	 */
	static bool ParseExportTextPath(FWideStringView InExportTextPath, FWideStringView* OutClassName, FWideStringView* OutObjectPath);
	static bool ParseExportTextPath(FAnsiStringView InExportTextPath, FAnsiStringView* OutClassName, FAnsiStringView* OutObjectPath);
	static bool ParseExportTextPath(const FString& InExportTextPath, FString* OutClassName, FString* OutObjectPath);	
	static bool ParseExportTextPath(const TCHAR* InExportTextPath, FStringView* OutClassName, FStringView* OutObjectPath);


	/** 
	 * Returns the path to the object referred to by the supplied export text path, excluding the class name.
	 * 
	 * @param InExportTextPath The export text path for an object. Takes on the form: ClassName'ObjectPath'
	 * @return The path to the object referred to by the supplied export path.
	 */
	static FWideStringView	ExportTextPathToObjectPath(FWideStringView InExportTextPath);
	static FAnsiStringView	ExportTextPathToObjectPath(FAnsiStringView InExportTextPath);
	static FString			ExportTextPathToObjectPath(const FString& InExportTextPath);
	static FString			ExportTextPathToObjectPath(const TCHAR* InExportTextPath);

	/** 
	 * Returns the name of the package referred to by the specified object path
	 */
	static FWideStringView ObjectPathToPackageName(FWideStringView InObjectPath);
	static FAnsiStringView ObjectPathToPackageName(FAnsiStringView InObjectPath);
	static FString ObjectPathToPackageName(const FString& InObjectPath);

	/** 
	 * Returns the name of the object referred to by the specified object path
	 */
	static FWideStringView ObjectPathToObjectName(FWideStringView InObjectPath);
	static FAnsiStringView ObjectPathToObjectName(FAnsiStringView InObjectPath);
	static FString ObjectPathToObjectName(const FString& InObjectPath);

	/**
	 * Checks the root of the package's path to see if it's an extra package
	 */
	static bool IsExtraPackage(FStringView InPackageName);

	/**
	 * Checks the root of the package's path to see if it is a script package
	 * @return true if the root of the path matches the script path
	 */
	static bool IsScriptPackage(FStringView InPackageName);

	/**
	 * Checks the root of the package's path to see if it's a memory package
	 * This should be set for packages that reside in memory and not on disk, we treat them similar to a script package
	 * @return true if the root of the patch matches the memory path
	 */
	static bool IsMemoryPackage(FStringView InPackageName);

	/**
	 * Checks the root of the package's path to see if it is a temp package
	 * Temp packages are sometimes saved to disk, and sometimes only exist in memory. They are never in source control
	 * @return true if the root of the patch matches the temp path
	 */
	static bool IsTempPackage(FStringView InPackageName);

	/**
	 * Checks the root of the package's path to see if it is a localized package
	 * @return true if the root of the path matches any localized root path
	 */
	static bool IsLocalizedPackage(FStringView InPackageName);

	/**
	 * Checks if a package name contains characters that are invalid for package names.
	 */
	static bool DoesPackageNameContainInvalidCharacters(FStringView InLongPackageName, FText* OutReason = NULL);
	
	/**
	* Checks if a package can be found using known package extensions.
	*
	* @param InPackageFilename Package filename without the extension.
	* @param OutFilename If the package could be found, filename with the extension.
	* @param InAllowTextFormats Detect text format packages as well as binary (priority to text)
	* @return true if the package could be found on disk.
	*/
	static bool FindPackageFileWithoutExtension(const FString& InPackageFilename, FString& OutFilename, bool InAllowTextFormats = true);

	/**
	 * Converts a long package name to the case it exists as on disk.
	 *
	 * @param LongPackageName The long package name
	 * @param Extension The extension for this package
	 * @return True if the long package name was fixed up, false otherwise
	 */
	static bool FixPackageNameCase(FString& LongPackageName, FStringView Extension);

	UE_DEPRECATED(4.17, "Deprecated. Call TryConvertLongPackageNameToFilename instead, which also works on nested paths")
	static bool ConvertRootPathToContentPath(const FString& RootPath, FString& OutContentPath);

	UE_DEPRECATED(4.17, "Deprecated. Call TryConvertFilenameToLongPackageName instead")
	static FString PackageFromPath(const TCHAR* InPathName);

	/** Override whether a package exist or not. */
	DECLARE_DELEGATE_RetVal_OneParam(bool, FDoesPackageExistOverride, FName);
	static FDoesPackageExistOverride& DoesPackageExistOverride()
	{
		return DoesPackageExistOverrideDelegate;
	}

private:

	static FString AssetPackageExtension;
	static FString MapPackageExtension;
	static FString TextAssetPackageExtension;
	static FString TextMapPackageExtension;

	/**
	 * Internal function used to rename filename to long package name.
	 *
	 * @param InFilename
	 * @param OutPackageName Long package name.
	 */
	static void InternalFilenameToLongPackageName(FStringView InFilename, FStringBuilderBase& OutPackageName);

	/** Event that is triggered when a new content path is mounted */
	static FOnContentPathMountedEvent OnContentPathMountedEvent;

	/** Event that is triggered when a new content path is removed */
	static FOnContentPathDismountedEvent OnContentPathDismountedEvent;

	/** Delegate used to check whether a package exist without using the filesystem. */
	static FDoesPackageExistOverride DoesPackageExistOverrideDelegate;
};

