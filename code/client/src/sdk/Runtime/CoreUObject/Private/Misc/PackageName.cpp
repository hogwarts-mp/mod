// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	UObjectHash.cpp: Unreal object name hashes
=============================================================================*/

#include "Misc/PackageName.h"
#include "Containers/StringView.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Stats/Stats.h"
#include "Misc/CoreDelegates.h"
#include "Misc/App.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"
#include "UObject/PackageFileSummary.h"
#include "UObject/Linker.h"
#include "Interfaces/IPluginManager.h"
#include "Internationalization/PackageLocalizationManager.h"
#include "HAL/CriticalSection.h"
#include "HAL/ThreadHeartBeat.h"
#include "Misc/AutomationTest.h"
#include "Misc/PathViews.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/StringBuilder.h"
#include "IO/IoDispatcher.h"
#include "ProfilingDebugging/LoadTimeTracker.h"

DEFINE_LOG_CATEGORY_STATIC(LogPackageName, Log, All);

FString FPackageName::AssetPackageExtension = TEXT(".uasset");
FString FPackageName::MapPackageExtension = TEXT(".umap");
FString FPackageName::TextAssetPackageExtension = TEXT(".utxt");
FString FPackageName::TextMapPackageExtension = TEXT(".utxtmap");
static FRWLock ContentMountPointCriticalSection;

/** Event that is triggered when a new content path is mounted */
FPackageName::FOnContentPathMountedEvent FPackageName::OnContentPathMountedEvent;

/** Event that is triggered when a content path is dismounted */
FPackageName::FOnContentPathDismountedEvent FPackageName::OnContentPathDismountedEvent;

/** Delegate used to check whether a package exist without using the filesystem. */
FPackageName::FDoesPackageExistOverride FPackageName::DoesPackageExistOverrideDelegate;

namespace PackageNameConstants
{
	// Minimum theoretical package name length ("/A/B") is 4
	const int32 MinPackageNameLength = 4;
}

bool FPackageName::IsShortPackageName(FStringView PossiblyLongName)
{
	// Long names usually have / as first character so check from the front
	for (TCHAR Char : PossiblyLongName)
	{
		if (Char == '/')
		{
			return false;
		}
	}

	return true;
}

bool FPackageName::IsShortPackageName(const FString& PossiblyLongName)
{
	return IsShortPackageName(FStringView(PossiblyLongName));
}

bool FPackageName::IsShortPackageName(FName PossiblyLongName)
{
	// Only get "plain" part of the name. The number suffix, e.g. "_123", can't contain slashes.
	TCHAR Buffer[NAME_SIZE];
	uint32 Len = PossiblyLongName.GetPlainNameString(Buffer);
	return IsShortPackageName(FStringView(Buffer, static_cast<int32>(Len)));
}

FString FPackageName::GetShortName(const FString& LongName)
{
	// Get everything after the last slash
	int32 IndexOfLastSlash = INDEX_NONE;
	LongName.FindLastChar('/', IndexOfLastSlash);
	return LongName.Mid(IndexOfLastSlash + 1);
}

FString FPackageName::GetShortName(const UPackage* Package)
{
	check(Package != NULL);
	return GetShortName(Package->GetName());
}

FString FPackageName::GetShortName(const FName& LongName)
{
	return GetShortName(LongName.ToString());
}

FString FPackageName::GetShortName(const TCHAR* LongName)
{
	return GetShortName(FString(LongName));
}

FName FPackageName::GetShortFName(const FString& LongName)
{
	return GetShortFName(*LongName);
}

FName FPackageName::GetShortFName(const FName& LongName)
{
	TCHAR LongNameStr[FName::StringBufferSize];
	LongName.ToString(LongNameStr);

	if (const TCHAR* Slash = FCString::Strrchr(LongNameStr, '/'))
	{
		return FName(Slash + 1);
	}

	return LongName;
}

FName FPackageName::GetShortFName(const TCHAR* LongName)
{
	if (LongName == nullptr)
	{
		return FName();
	}

	if (const TCHAR* Slash = FCString::Strrchr(LongName, '/'))
	{
		return FName(Slash + 1);
	}

	return FName(LongName);
}

bool FPackageName::TryConvertGameRelativePackagePathToLocalPath(FStringView RelativePackagePath, FString& OutLocalPath)
{
	if (RelativePackagePath.StartsWith(TEXT("/"), ESearchCase::CaseSensitive))
	{
		// If this starts with /, this includes a root like /engine
		FString AbsolutePackagePath(RelativePackagePath);
		if (FPackageName::TryConvertLongPackageNameToFilename(AbsolutePackagePath, OutLocalPath))
		{
			return true;
		}
		// Workaround a problem with TryConvertLongPackageNameToFilename: If the PackagePath is a content root itself (/Some/Content/Root)
		// and is missing a terminating /, it will not match the existing content root which does have the / (/Some/Content/Root/)
		if (!AbsolutePackagePath.EndsWith(TEXT("/")))
		{
			AbsolutePackagePath = AbsolutePackagePath + TEXT("/");
			return FPackageName::TryConvertLongPackageNameToFilename(AbsolutePackagePath, OutLocalPath);
		}
		return false;
	}
	else
	{
		// This is relative to /game
		const FString AbsoluteGameContentDir = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
		OutLocalPath = AbsoluteGameContentDir / FString(RelativePackagePath);
		return true;
	}
}


struct FPathPair
{
	// The virtual path (e.g., "/Engine/")
	const FString RootPath;

	// The physical relative path (e.g., "../../../Engine/Content/")
	const FString ContentPath;

	bool operator ==(const FPathPair& Other) const
	{
		return RootPath == Other.RootPath && ContentPath == Other.ContentPath;
	}

	// Construct a path pair
	FPathPair(const FString& InRootPath, const FString& InContentPath)
		: RootPath(InRootPath)
		, ContentPath(InContentPath)
	{
	}
};

struct FLongPackagePathsSingleton
{
	FString ConfigRootPath;
	FString EngineRootPath;
	FString GameRootPath;
	FString ScriptRootPath;
	FString ExtraRootPath;
	FString MemoryRootPath;
	FString TempRootPath;
	TArray<FString> MountPointRootPaths;

	FString EngineContentPath;
	FString ContentPathShort;
	FString EngineShadersPath;
	FString EngineShadersPathShort;
	FString GameContentPath;
	FString GameConfigPath;
	FString GameScriptPath;
	FString GameExtraPath;
	FString GameSavedPath;
	FString GameContentPathRebased;
	FString GameConfigPathRebased;
	FString GameScriptPathRebased;
	FString GameExtraPathRebased;
	FString GameSavedPathRebased;

	//@TODO: Can probably consolidate these into a single array, if it weren't for EngineContentPathShort
	TArray<FPathPair> ContentRootToPath;
	TArray<FPathPair> ContentPathToRoot;

	// singleton
	static FLongPackagePathsSingleton& Get()
	{
		static FLongPackagePathsSingleton Singleton;
		return Singleton;
	}

	void GetValidLongPackageRoots(TArray<FString>& OutRoots, bool bIncludeReadOnlyRoots) const
	{
		OutRoots.Add(EngineRootPath);
		OutRoots.Add(GameRootPath);

		{
			FReadScopeLock ScopeLock(ContentMountPointCriticalSection);
			OutRoots += MountPointRootPaths;
		}

		if (bIncludeReadOnlyRoots)
		{
			OutRoots.Add(ConfigRootPath);
			OutRoots.Add(ScriptRootPath);
			OutRoots.Add(ExtraRootPath);
			OutRoots.Add(MemoryRootPath);
			OutRoots.Add(TempRootPath);
		}
	}

	// Given a content path ensure it is consistent, specifically with FileManager relative paths 
	static FString ProcessContentMountPoint(const FString& ContentPath)
	{
		FString MountPath = ContentPath;

		// If a relative path is passed, convert to an absolute path 
		if (FPaths::IsRelative(MountPath))
		{
			MountPath = FPaths::ConvertRelativePathToFull(ContentPath);

			// Revert to original path if unable to convert to full path
			if (MountPath.Len() <= 1)
			{
				MountPath = ContentPath;
				UE_LOG(LogPackageName, Warning, TEXT("Unable to convert mount point relative path: %s"), *ContentPath);
			}
		}

		// Convert to a relative path using the FileManager
		return IFileManager::Get().ConvertToRelativePath(*MountPath);
	}


	// This will insert a mount point at the head of the search chain (so it can overlap an existing mount point and win)
	void InsertMountPoint(const FString& RootPath, const FString& ContentPath)
	{	
		// Make sure the content path is stored as a relative path, consistent with the other paths we have
		FString RelativeContentPath = ProcessContentMountPoint(ContentPath);

		// Make sure the path ends in a trailing path separator.  We are expecting that in the InternalFilenameToLongPackageName code.
		if( !RelativeContentPath.EndsWith( TEXT( "/" ), ESearchCase::CaseSensitive ) )
		{
			RelativeContentPath += TEXT( "/" );
		}

		FPathPair Pair(RootPath, RelativeContentPath);
		{
			FWriteScopeLock ScopeLock(ContentMountPointCriticalSection);
			ContentRootToPath.Insert(Pair, 0);
			ContentPathToRoot.Insert(Pair, 0);
			MountPointRootPaths.Add(RootPath);
		}

		// Let subscribers know that a new content path was mounted
		FPackageName::OnContentPathMounted().Broadcast( RootPath, RelativeContentPath);
	}

	// This will remove a previously inserted mount point
	void RemoveMountPoint(const FString& RootPath, const FString& ContentPath)
	{
		// Make sure the content path is stored as a relative path, consistent with the other paths we have
		FString RelativeContentPath = ProcessContentMountPoint(ContentPath);

		// Make sure the path ends in a trailing path separator.  We are expecting that in the InternalFilenameToLongPackageName code.
		if (!RelativeContentPath.EndsWith(TEXT("/"), ESearchCase::CaseSensitive))
		{
			RelativeContentPath += TEXT("/");
		}

		bool bFirePathDismountedDelegate = false;
		{
			FWriteScopeLock ScopeLock(ContentMountPointCriticalSection);
			if ( MountPointRootPaths.Remove(RootPath) > 0 )
			{
				FPathPair Pair(RootPath, RelativeContentPath);
				ContentRootToPath.Remove(Pair);
				ContentPathToRoot.Remove(Pair);
				MountPointRootPaths.Remove(RootPath);

				// Let subscribers know that a new content path was unmounted
				bFirePathDismountedDelegate = true;
			}
		}

		if (bFirePathDismountedDelegate)
		{
			FPackageName::OnContentPathDismounted().Broadcast(RootPath, RelativeContentPath);
		}
	}

	// Checks whether the specific root path is a valid mount point.
	bool MountPointExists(const FString& RootPath)
	{
		FReadScopeLock ScopeLock(ContentMountPointCriticalSection);
		return MountPointRootPaths.Contains(RootPath);
	}

private:
	FLongPackagePathsSingleton()
	{
		ConfigRootPath = TEXT("/Config/");
		EngineRootPath = TEXT("/Engine/");
		GameRootPath   = TEXT("/Game/");
		ScriptRootPath = TEXT("/Script/");
		ExtraRootPath  = TEXT("/Extra/");
		MemoryRootPath = TEXT("/Memory/");
		TempRootPath   = TEXT("/Temp/");

		EngineContentPath      = FPaths::EngineContentDir();
		ContentPathShort       = TEXT("../../Content/");
		EngineShadersPath      = FPaths::EngineDir() / TEXT("Shaders/");
		EngineShadersPathShort = TEXT("../../Shaders/");
		GameContentPath        = FPaths::ProjectContentDir();
		GameConfigPath         = FPaths::ProjectConfigDir();
		GameScriptPath         = FPaths::ProjectDir() / TEXT("Script/");
		GameExtraPath          = FPaths::ProjectDir() / TEXT("Extra/");
		GameSavedPath          = FPaths::ProjectSavedDir();

		FString RebasedGameDir = FString::Printf(TEXT("../../../%s/"), FApp::GetProjectName());

		GameContentPathRebased = RebasedGameDir / TEXT("Content/");
		GameConfigPathRebased  = RebasedGameDir / TEXT("Config/");
		GameScriptPathRebased  = RebasedGameDir / TEXT("Script/");
		GameExtraPathRebased   = RebasedGameDir / TEXT("Extra/");
		GameSavedPathRebased   = RebasedGameDir / TEXT("Saved/");
		
		FWriteScopeLock ScopeLock(ContentMountPointCriticalSection);

		ContentPathToRoot.Empty(13);
		ContentPathToRoot.Emplace(EngineRootPath, EngineContentPath);
		if (FPaths::IsSamePath(GameContentPath, ContentPathShort))
		{
			ContentPathToRoot.Emplace(GameRootPath, ContentPathShort);
		}
		else
		{
			ContentPathToRoot.Emplace(EngineRootPath, ContentPathShort);
		}
		ContentPathToRoot.Emplace(EngineRootPath, EngineShadersPath);
		ContentPathToRoot.Emplace(EngineRootPath, EngineShadersPathShort);
		ContentPathToRoot.Emplace(GameRootPath,   GameContentPath);
		ContentPathToRoot.Emplace(ScriptRootPath, GameScriptPath);
		ContentPathToRoot.Emplace(TempRootPath,   GameSavedPath);
		ContentPathToRoot.Emplace(GameRootPath,   GameContentPathRebased);
		ContentPathToRoot.Emplace(ScriptRootPath, GameScriptPathRebased);
		ContentPathToRoot.Emplace(TempRootPath,   GameSavedPathRebased);
		ContentPathToRoot.Emplace(ConfigRootPath, GameConfigPath);
		ContentPathToRoot.Emplace(ExtraRootPath,  GameExtraPath);
		ContentPathToRoot.Emplace(ExtraRootPath,  GameExtraPathRebased);

		ContentRootToPath.Empty(11);
		ContentRootToPath.Emplace(EngineRootPath, EngineContentPath);
		ContentRootToPath.Emplace(EngineRootPath, EngineShadersPath);
		ContentRootToPath.Emplace(GameRootPath,   GameContentPath);
		ContentRootToPath.Emplace(ScriptRootPath, GameScriptPath);
		ContentRootToPath.Emplace(TempRootPath,   GameSavedPath);
		ContentRootToPath.Emplace(GameRootPath,   GameContentPathRebased);
		ContentRootToPath.Emplace(ScriptRootPath, GameScriptPathRebased);
		ContentRootToPath.Emplace(ExtraRootPath,  GameExtraPath);
		ContentRootToPath.Emplace(ExtraRootPath,  GameExtraPathRebased);
		ContentRootToPath.Emplace(TempRootPath,   GameSavedPathRebased);
		ContentRootToPath.Emplace(ConfigRootPath, GameConfigPathRebased);

		// Allow the plugin manager to mount new content paths by exposing access through a delegate.  PluginManager is 
		// a Core class, but content path functionality is added at the CoreUObject level.
		IPluginManager::Get().SetRegisterMountPointDelegate( IPluginManager::FRegisterMountPointDelegate::CreateStatic( &FPackageName::RegisterMountPoint ) );
		IPluginManager::Get().SetUnRegisterMountPointDelegate( IPluginManager::FRegisterMountPointDelegate::CreateStatic( &FPackageName::UnRegisterMountPoint ) );
	}
};

void FPackageName::InternalFilenameToLongPackageName(FStringView InFilename, FStringBuilderBase& OutPackageName)
{
	const FLongPackagePathsSingleton& Paths = FLongPackagePathsSingleton::Get();
	FString Filename(InFilename);
	FPaths::NormalizeFilename(Filename);

	// Convert to relative path if it's not already a long package name
	bool bIsValidLongPackageName = false;
	{
		FReadScopeLock ScopeLock(ContentMountPointCriticalSection);
		for (const auto& Pair : Paths.ContentRootToPath)
		{
			if (Filename.StartsWith(Pair.RootPath))
			{
				bIsValidLongPackageName = true;
				break;
			}
		}
	}

	if (!bIsValidLongPackageName)
	{
		Filename = IFileManager::Get().ConvertToRelativePath(*Filename);
		if (InFilename.Len() > 0 && InFilename[InFilename.Len() - 1] == '/')
		{
			// If InFilename ends in / but converted doesn't, add the / back
			bool bEndsInSlash = Filename.Len() > 0 && Filename[Filename.Len() - 1] == '/';

			if (!bEndsInSlash)
			{
				Filename += TEXT("/");
			}
		}
	}

	FStringView Result = FPathViews::GetBaseFilenameWithPath(Filename);

	{
		FReadScopeLock ScopeLock(ContentMountPointCriticalSection);
		for (const auto& Pair : Paths.ContentPathToRoot)
		{
			if (Result.StartsWith(Pair.ContentPath))
			{
				OutPackageName << Pair.RootPath << Result.RightChop(Pair.ContentPath.Len());
				return;
			}
		}
	}

	OutPackageName << Result;
}

bool FPackageName::TryConvertFilenameToLongPackageName(const FString& InFilename, FString& OutPackageName, FString* OutFailureReason)
{
	TStringBuilder<256> LongPackageNameBuilder;
	InternalFilenameToLongPackageName(InFilename, LongPackageNameBuilder);
	FStringView LongPackageName = LongPackageNameBuilder.ToString();

	// we don't support loading packages from outside of well defined places
	int32 CharacterIndex;
	const bool bContainsDot = LongPackageName.FindChar(TEXT('.'), CharacterIndex);
	const bool bContainsBackslash = LongPackageName.FindChar(TEXT('\\'), CharacterIndex);
	const bool bContainsColon = LongPackageName.FindChar(TEXT(':'), CharacterIndex);

	if (!(bContainsDot || bContainsBackslash || bContainsColon))
	{
		OutPackageName = LongPackageName;
		return true;
	}

	// if the package name resolution failed and a relative path was provided, convert to an absolute path
	// as content may be mounted in a different relative path to the one given
	if (FPaths::IsRelative(InFilename))
	{
		FString AbsPath = FPaths::ConvertRelativePathToFull(InFilename);
		if (!FPaths::IsRelative(AbsPath) && AbsPath.Len() > 1)
		{
			if (TryConvertFilenameToLongPackageName(AbsPath, OutPackageName, nullptr))
			{
				return true;
			}
		}
	}

	if (OutFailureReason != nullptr)
	{
		FString InvalidChars;
		if (bContainsDot)
		{
			InvalidChars += TEXT(".");
		}
		if (bContainsBackslash)
		{
			InvalidChars += TEXT("\\");
		}
		if (bContainsColon)
		{
			InvalidChars += TEXT(":");
		}
		*OutFailureReason = FString::Printf(TEXT("FilenameToLongPackageName failed to convert '%s'. Attempt result was '%.*s', but the path contains illegal characters '%s'"), *InFilename, LongPackageName.Len(), LongPackageName.GetData(), *InvalidChars);
	}

	return false;
}

FString FPackageName::FilenameToLongPackageName(const FString& InFilename)
{
	FString FailureReason;
	FString Result;
	if (!TryConvertFilenameToLongPackageName(InFilename, Result, &FailureReason))
	{
		UE_LOG(LogPackageName, Fatal, TEXT("%s"), *FailureReason);
	}
	return Result;
}

bool FPackageName::TryConvertLongPackageNameToFilename(const FString& InLongPackageName, FString& OutFilename, const FString& InExtension)
{
	const auto& Paths = FLongPackagePathsSingleton::Get();
	FReadScopeLock ScopeLock(ContentMountPointCriticalSection);
	for (const auto& Pair : Paths.ContentRootToPath)
	{
		if (InLongPackageName.StartsWith(Pair.RootPath))
		{
			OutFilename = Pair.ContentPath + InLongPackageName.Mid(Pair.RootPath.Len()) + InExtension;
			return true;
		}
	}

	// This is not a long package name or the root folder is not handled in the above cases
	return false;
}

bool FPackageName::ConvertRootPathToContentPath( const FString& RootPath, FString& OutContentPath)
{
	const auto& Paths = FLongPackagePathsSingleton::Get();
	FReadScopeLock ScopeLock(ContentMountPointCriticalSection);
	for (const auto& Pair : Paths.ContentRootToPath)
	{
		if (RootPath.StartsWith(Pair.RootPath))
		{
			OutContentPath = Pair.ContentPath;
			return true;
		}
	}

	// This is not a long package name or the root folder is not handled in the above cases
	return false;
}

FString FPackageName::LongPackageNameToFilename(const FString& InLongPackageName, const FString& InExtension)
{
	FString Result;
	if (!TryConvertLongPackageNameToFilename(InLongPackageName, Result, InExtension))
	{
		UE_LOG(LogPackageName, Fatal,TEXT("LongPackageNameToFilename failed to convert '%s'. Path does not map to any roots."), *InLongPackageName);
	}
	return Result;
}

FString FPackageName::GetLongPackagePath(const FString& InLongPackageName)
{
	int32 IndexOfLastSlash = INDEX_NONE;
	if (InLongPackageName.FindLastChar('/', IndexOfLastSlash))
	{
		return InLongPackageName.Left(IndexOfLastSlash);
	}
	else
	{
		return InLongPackageName;
	}
}

bool FPackageName::SplitLongPackageName(const FString& InLongPackageName, FString& OutPackageRoot, FString& OutPackagePath, FString& OutPackageName, const bool bStripRootLeadingSlash)
{
	const FLongPackagePathsSingleton& Paths = FLongPackagePathsSingleton::Get();

	const bool bIncludeReadOnlyRoots = true;
	TArray<FString> ValidRoots;
	Paths.GetValidLongPackageRoots(ValidRoots, bIncludeReadOnlyRoots);

	// Check to see whether our package came from a valid root
	OutPackageRoot.Empty();
	for(auto RootIt = ValidRoots.CreateConstIterator(); RootIt; ++RootIt)
	{
		const FString& PackageRoot = *RootIt;
		if(InLongPackageName.StartsWith(PackageRoot))
		{
			OutPackageRoot = PackageRoot / "";
			break;
		}
	}

	if(OutPackageRoot.IsEmpty() || InLongPackageName.Len() <= OutPackageRoot.Len())
	{
		// Path is not part of a valid root, or the path given is too short to continue; splitting failed
		return false;
	}

	// Use the standard path functions to get the rest
	const FString RemainingPackageName = InLongPackageName.Mid(OutPackageRoot.Len());
	OutPackagePath = FPaths::GetPath(RemainingPackageName) / "";
	OutPackageName = FPaths::GetCleanFilename(RemainingPackageName);

	if(bStripRootLeadingSlash && OutPackageRoot.StartsWith(TEXT("/"), ESearchCase::CaseSensitive))
	{
		OutPackageRoot.RemoveAt(0);
	}

	return true;
}

void FPackageName::SplitFullObjectPath(const FString& InFullObjectPath, FString& OutClassName, FString& OutPackageName, FString& OutObjectName, FString& OutSubObjectName)
{
	FString Sanitized = InFullObjectPath.TrimStartAndEnd();
	const TCHAR* Cur = *Sanitized;

	auto ExtractBeforeDelim = [&Cur](TCHAR Delim, FString& OutString)
	{
		const TCHAR* Start = Cur;
		while (*Cur != '\0' && *Cur != Delim)
		{
			++Cur;
		}

		OutString = FString(Cur - Start, Start);

		if (*Cur == Delim)
		{
			++Cur;
		}
	};

	ExtractBeforeDelim(' ', OutClassName);
	ExtractBeforeDelim('.', OutPackageName);
	ExtractBeforeDelim(':', OutObjectName);
	ExtractBeforeDelim('\0', OutSubObjectName);
}

FString FPackageName::GetLongPackageAssetName(const FString& InLongPackageName)
{
	return GetShortName(InLongPackageName);
}

bool FPackageName::DoesPackageNameContainInvalidCharacters(FStringView InLongPackageName, FText* OutReason /*= NULL*/)
{
	// See if the name contains invalid characters.
	TStringBuilder<32> MatchedInvalidChars;
	for (const TCHAR* InvalidCharacters = INVALID_LONGPACKAGE_CHARACTERS; *InvalidCharacters; ++InvalidCharacters)
	{
		FStringView::SizeType OutIndex;
		if (InLongPackageName.FindChar(*InvalidCharacters, OutIndex))
		{
			MatchedInvalidChars += *InvalidCharacters;
		}
	}
	if (MatchedInvalidChars.Len())
	{
		if (OutReason)
		{
			FFormatNamedArguments Args;
			Args.Add( TEXT("IllegalNameCharacters"), FText::FromString(FString(MatchedInvalidChars)) );
			*OutReason = FText::Format( NSLOCTEXT("Core", "PackageNameContainsInvalidCharacters", "Name may not contain the following characters: '{IllegalNameCharacters}'"), Args );
		}
		return true;
	}
	return false;
}

bool FPackageName::IsValidLongPackageName(const FString& InLongPackageName, bool bIncludeReadOnlyRoots /*= false*/, FText* OutReason /*= NULL*/)
{
	// All package names must contain a leading slash, root, slash and name, at minimum theoretical length ("/A/B") is 4
	if (InLongPackageName.Len() < PackageNameConstants::MinPackageNameLength)
	{
		if (OutReason)
		{
			*OutReason = FText::Format(NSLOCTEXT("Core", "LongPackageNames_PathTooShort", "Path should be no less than {0} characters long."), FText::AsNumber(PackageNameConstants::MinPackageNameLength));
		}
		return false;
	}
	// Package names start with a leading slash.
	if (InLongPackageName[0] != '/')
	{
		if (OutReason)
		{
			*OutReason = NSLOCTEXT("Core", "LongPackageNames_PathWithNoStartingSlash", "Path should start with a '/'");
		}
		return false;
	}
	// Package names do not end with a trailing slash.
	if (InLongPackageName[InLongPackageName.Len() - 1] == '/')
	{
		if (OutReason)
		{
			*OutReason = NSLOCTEXT("Core", "LongPackageNames_PathWithTrailingSlash", "Path may not end with a '/'");
		}
		return false;
	}
	// Check for invalid characters
	if (DoesPackageNameContainInvalidCharacters(InLongPackageName, OutReason))
	{
		return false;
	}
	// Check valid roots
	const FLongPackagePathsSingleton& Paths = FLongPackagePathsSingleton::Get();
	TArray<FString> ValidRoots;
	bool bValidRoot = false;
	Paths.GetValidLongPackageRoots(ValidRoots, bIncludeReadOnlyRoots);
	for (int32 RootIdx = 0; RootIdx < ValidRoots.Num(); ++RootIdx)
	{
		const FString& Root = ValidRoots[RootIdx];
		if (InLongPackageName.StartsWith(Root))
		{
			bValidRoot = true;
			break;
		}
	}
	if (!bValidRoot && OutReason)
	{
		if (ValidRoots.Num() == 0)
		{
			*OutReason = NSLOCTEXT("Core", "LongPackageNames_NoValidRoots", "No valid roots exist!");
		}
		else
		{
			FString ValidRootsString = TEXT("");
			if (ValidRoots.Num() == 1)
			{
				ValidRootsString = FString::Printf(TEXT("'%s'"), *ValidRoots[0]);
			}
			else
			{
				for (int32 RootIdx = 0; RootIdx < ValidRoots.Num(); ++RootIdx)
				{
					if (RootIdx < ValidRoots.Num() - 1)
					{
						ValidRootsString += FString::Printf(TEXT("'%s', "), *ValidRoots[RootIdx]);
					}
					else
					{
						ValidRootsString += FString::Printf(TEXT("or '%s'"), *ValidRoots[RootIdx]);
					}
				}
			}
			*OutReason = FText::Format( NSLOCTEXT("Core", "LongPackageNames_InvalidRoot", "Path does not start with a valid root. Path must begin with: {0}"), FText::FromString( ValidRootsString ) );
		}
	}
	return bValidRoot;
}

bool FPackageName::IsValidObjectPath(const FString& InObjectPath, FText* OutReason)
{
	FString PackageName;
	FString RemainingObjectPath;

	// Check for package delimiter
	int32 ObjectDelimiterIdx;
	if (InObjectPath.FindChar('.', ObjectDelimiterIdx))
	{
		if (ObjectDelimiterIdx == InObjectPath.Len() - 1)
		{
			if (OutReason)
			{
				*OutReason = NSLOCTEXT("Core", "ObjectPath_EndWithPeriod", "Object Path may not end with .");
			}
			return false;
		}

		PackageName = InObjectPath.Mid(0, ObjectDelimiterIdx);
		RemainingObjectPath = InObjectPath.Mid(ObjectDelimiterIdx + 1);
	}
	else
	{
		PackageName = InObjectPath;
	}

	if (!IsValidLongPackageName(PackageName, true, OutReason))
	{
		return false;
	}

	if (RemainingObjectPath.Len() > 0)
	{
		FText PathContext = NSLOCTEXT("Core", "ObjectPathContext", "Object Path");
		if (!FName::IsValidXName(RemainingObjectPath, INVALID_OBJECTPATH_CHARACTERS, OutReason, &PathContext))
		{
			return false;
		}

		TCHAR LastChar = RemainingObjectPath[RemainingObjectPath.Len() - 1];
		if (LastChar == '.' || LastChar == ':')
		{
			if (OutReason)
			{
				*OutReason = NSLOCTEXT("Core", "ObjectPath_PathWithTrailingSeperator", "Object Path may not end with : or .");
			}
			return false;
		}

		int32 SlashIndex;
		if (RemainingObjectPath.FindChar('/', SlashIndex))
		{
			if (OutReason)
			{
				*OutReason = NSLOCTEXT("Core", "ObjectPath_SlashAfterPeriod", "Object Path may not have / after first .");
			}

			return false;
		}
	}

	return true;
}

bool FPackageName::IsValidPath(const FString& InPath)
{
	const FLongPackagePathsSingleton& Paths = FLongPackagePathsSingleton::Get();
	FReadScopeLock ScopeLock(ContentMountPointCriticalSection);
	for (const FPathPair& Pair : Paths.ContentRootToPath)
	{
		if (InPath.StartsWith(Pair.RootPath))
		{
			return true;
		}
	}

	// The root folder is not handled in the above cases
	return false;
}

void FPackageName::RegisterMountPoint(const FString& RootPath, const FString& ContentPath)
{
	FLongPackagePathsSingleton::Get().InsertMountPoint(RootPath, ContentPath);
}

void FPackageName::UnRegisterMountPoint(const FString& RootPath, const FString& ContentPath)
{
	FLongPackagePathsSingleton::Get().RemoveMountPoint(RootPath, ContentPath);
}

bool FPackageName::MountPointExists(const FString& RootPath)
{
	return FLongPackagePathsSingleton::Get().MountPointExists(RootPath);
}

FName FPackageName::GetPackageMountPoint(const FString& InPackagePath, bool InWithoutSlashes)
{
	FLongPackagePathsSingleton& Paths = FLongPackagePathsSingleton::Get();
	
	TArray<FString> MountPoints;
	Paths.GetValidLongPackageRoots(MountPoints, true);

	int32 WithoutSlashes = InWithoutSlashes ? 1 : 0;
	for (auto RootIt = MountPoints.CreateConstIterator(); RootIt; ++RootIt)
	{
		if (InPackagePath.StartsWith(*RootIt))
		{
			return FName(*RootIt->Mid(WithoutSlashes, RootIt->Len() - (2 * WithoutSlashes)));
		}
	}

	return FName();
}

FString FPackageName::ConvertToLongScriptPackageName(const TCHAR* InShortName)
{
	if (IsShortPackageName(FString(InShortName)))
	{
		return FString::Printf(TEXT("/Script/%s"), InShortName);
	}
	else
	{
		return InShortName;
	}
}

// Short to long script package name map.
static TMap<FName, FName> ScriptPackageNames;


// @todo: This stuff needs to be eliminated as soon as we can make sure that no legacy short package names
//        are in use when referencing class names in UObject module "class packages"
void FPackageName::RegisterShortPackageNamesForUObjectModules()
{
	// @todo: Ideally we'd only be processing UObject modules, not every module, but we have
	//        no way of knowing which modules may contain UObjects (without say, having UBT save a manifest.)
	// @todo: This stuff is a bomb waiting to explode.  Because short package names can
	//        take precedent over other object names, modules can reserve names for other types!
	TArray<FName> AllModuleNames;
	FModuleManager::Get().FindModules( TEXT( "*" ), AllModuleNames );
	for( TArray<FName>::TConstIterator ModuleNameIt( AllModuleNames ); ModuleNameIt; ++ModuleNameIt )
	{
		ScriptPackageNames.Add( *ModuleNameIt, *ConvertToLongScriptPackageName( *ModuleNameIt->ToString() ));
	}
}

FName* FPackageName::FindScriptPackageName(FName InShortName)
{
	return ScriptPackageNames.Find(InShortName);
}

bool FPackageName::FindPackageFileWithoutExtension(const FString& InPackageFilename, FString& OutFilename, bool InAllowTextFormats)
{
	auto& FileManager = IFileManager::Get();

	{
		static const FString* PackageExtensions[] =
		{
			&AssetPackageExtension,
			&MapPackageExtension
		};

		// Loop through all known extensions and check if the file exists

		for (int32 ExtensionIndex = 0; ExtensionIndex < UE_ARRAY_COUNT(PackageExtensions); ++ExtensionIndex)
		{
			FString   PackageFilename = InPackageFilename + *PackageExtensions[ExtensionIndex];
			if (FileManager.FileExists(*PackageFilename))
			{
				// The package exists so exit. From now on InPackageFilename can be equal to OutFilename so
				// don't attempt to use it anymore (case where &InPackageFilename == &OutFilename).
				OutFilename = MoveTemp(PackageFilename);
				return true;
			}
		}
	}

#if WITH_TEXT_ARCHIVE_SUPPORT
	if (InAllowTextFormats)
	{
		static const FString* TextPackageExtensions[] =
		{
			&TextAssetPackageExtension,
			&TextMapPackageExtension
		};

		for (int32 ExtensionIndex = 0; ExtensionIndex < UE_ARRAY_COUNT(TextPackageExtensions); ++ExtensionIndex)
		{
			FString   PackageFilename = InPackageFilename + *TextPackageExtensions[ExtensionIndex];
			if (FileManager.FileExists(*PackageFilename))
			{
				// The package exists so exit. From now on InPackageFilename can be equal to OutFilename so
				// don't attempt to use it anymore (case where &InPackageFilename == &OutFilename).
				OutFilename = MoveTemp(PackageFilename);
				return true;
			}
		}
	}
#endif

	return false;
}

bool FPackageName::FixPackageNameCase(FString& LongPackageName, FStringView Extension)
{
	// Find the matching long package root
	const FLongPackagePathsSingleton& Paths = FLongPackagePathsSingleton::Get();
	FReadScopeLock ScopeLock(ContentMountPointCriticalSection);
	for (const FPathPair& Pair : Paths.ContentRootToPath)
	{
		if (LongPackageName.StartsWith(Pair.RootPath))
		{
			FString RelativePackageName = LongPackageName.Mid(Pair.RootPath.Len());
			FString FileName = Pair.ContentPath / RelativePackageName;

			int ExtensionLen = Extension.Len();
			if(Extension.Len() > 0 && Extension[0] != '.')
			{
				FileName.AppendChar('.');
				ExtensionLen++;
			}

			FileName += Extension;

			FString CorrectFileName = IFileManager::Get().GetFilenameOnDisk(*FileName);
			if(CorrectFileName.Len() >= RelativePackageName.Len() + ExtensionLen)
			{
				FString NewRelativePackageName = CorrectFileName.Mid(CorrectFileName.Len() - RelativePackageName.Len() - ExtensionLen, RelativePackageName.Len());
				if(NewRelativePackageName == RelativePackageName)
				{
					LongPackageName.RemoveAt(Pair.RootPath.Len(), LongPackageName.Len() - Pair.RootPath.Len());
					LongPackageName.Append(*NewRelativePackageName);
					return true;
				}
			}
			break;
		}
	}
	return false;
}

bool FPackageName::DoesPackageExist(const FString& LongPackageName, const FGuid* Guid, FString* OutFilename, bool InAllowTextFormats)
{
	SCOPED_LOADTIMER(FPackageName_DoesPackageExist);

	bool bFoundFile = false;

	// Make sure passing filename as LongPackageName is supported.
	FString PackageName;
	FText Reason;

	if (!FPackageName::TryConvertFilenameToLongPackageName(LongPackageName, PackageName))
	{
		verify(!FPackageName::IsValidLongPackageName(LongPackageName, true, &Reason));
		UE_LOG(LogPackageName, Error, TEXT("Illegal call to DoesPackageExist: '%s' is not a standard unreal filename or a long path name. Reason: %s"), *LongPackageName, *Reason.ToString());
		ensureMsgf(false, TEXT("Illegal call to DoesPackageExist: '%s' is not a standard unreal filename or a long path name. Reason: %s"), *LongPackageName, *Reason.ToString());
		return false;
	}
	// Once we have the real Package Name, we can exit early if it's a script package - they exist only in memory.
	if (IsScriptPackage(PackageName))
	{
		return false;
	}

	if (IsMemoryPackage(PackageName))
	{
		return false;
	}

	if ( !FPackageName::IsValidLongPackageName( PackageName, true, &Reason ) )
	{
		UE_LOG(LogPackageName, Error, TEXT( "DoesPackageExist: DoesPackageExist FAILED: '%s' is not a standard unreal filename or a long path name. Reason: %s"), *LongPackageName, *Reason.ToString() );
		return false;
	}

	// Used when I/O dispatcher is enabled
	if (DoesPackageExistOverrideDelegate.IsBound())
	{
		if (DoesPackageExistOverrideDelegate.Execute(FName(*PackageName)))
		{
			if (OutFilename)
			{
				*OutFilename = LongPackageNameToFilename(PackageName, TEXT(""));
			}

			return true;
		}
	
		// Try to find uncooked packages on disk when I/O store is enabled in editor builds
#if !WITH_IOSTORE_IN_EDITOR
		return false; 
#endif
	}

	// Convert to filename (no extension yet).
	FString Filename = LongPackageNameToFilename(PackageName, TEXT(""));

	// Find the filename (with extension).
	bFoundFile = FindPackageFileWithoutExtension(Filename, Filename, InAllowTextFormats);

	// On consoles, we don't support package downloading, so no need to waste any extra cycles/disk io dealing with it
	if (!FPlatformProperties::RequiresCookedData() && bFoundFile && Guid != NULL)
	{
		// @todo: If we could get to list of linkers here, it would be faster to check
		// then to open the file and read it
		FArchive* PackageReader = IFileManager::Get().CreateFileReader(*Filename);
		// This had better open
		check(PackageReader != NULL);

		// Read in the package summary
		FPackageFileSummary Summary;
		*PackageReader << Summary;

		// Compare Guids
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		if (Summary.Guid != *Guid)
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
		{
			bFoundFile = false;
		}

		// Close package file
		delete PackageReader;
	}

	if (OutFilename && bFoundFile)
	{
		*OutFilename = Filename;
	}
	return bFoundFile;
}

bool FPackageName::SearchForPackageOnDisk(const FString& PackageName, FString* OutLongPackageName, FString* OutFilename)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FPackageName::SearchForPackageOnDisk);
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FPackageName::SearchForPackageOnDisk"), STAT_PackageName_SearchForPackageOnDisk, STATGROUP_LoadTime);

	// This function may take a long time to complete, so suspend heartbeat measure while we're here
	FSlowHeartBeatScope SlowHeartBeatScope;

	bool bResult = false;
	double StartTime = FPlatformTime::Seconds();
	if (FPackageName::IsShortPackageName(PackageName) == false)
	{
		// If this is long package name, revert to using DoesPackageExist because it's a lot faster.
		FString Filename;
		if (DoesPackageExist(PackageName, NULL, &Filename))
		{
			if (OutLongPackageName)
			{
				*OutLongPackageName = PackageName;
			}
			if (OutFilename)
			{
				*OutFilename = Filename;
			}
			bResult = true;
		}
	}
	else
	{
		// Attempt to find package by its short name by searching in the known content paths.
		TArray<FString> Paths;		
		{
			TArray<FString> RootContentPaths;
			FPackageName::QueryRootContentPaths( RootContentPaths );
			for( TArray<FString>::TConstIterator RootPathIt( RootContentPaths ); RootPathIt; ++RootPathIt )
			{
				const FString& RootPath = *RootPathIt;
				const FString& ContentFolder = FPackageName::LongPackageNameToFilename(RootPath, TEXT(""));
				Paths.Add( ContentFolder );
			}
		}

		const FString PackageWildcard = (PackageName.Find(TEXT("."), ESearchCase::CaseSensitive) != INDEX_NONE ? PackageName : PackageName + TEXT(".*"));
		TArray<FString> Results;

		for (int32 PathIndex = 0; PathIndex < Paths.Num() && !bResult; ++PathIndex)
		{
			// Search directly on disk. Very slow!
			IFileManager::Get().FindFilesRecursive(Results, *Paths[PathIndex], *PackageWildcard, true, false);

			for (int32 FileIndex = 0; FileIndex < Results.Num(); ++FileIndex)
			{			
				FString Filename(Results[FileIndex]);
				if (IsPackageFilename(Results[FileIndex]))
				{
					// Convert to long package name.
					FString LongPackageName;
					if (TryConvertFilenameToLongPackageName(Filename, LongPackageName))
					{
						if (OutLongPackageName)
						{
							if (bResult)
							{
								UE_LOG(LogPackageName, Warning, TEXT("Found ambiguous long package name for '%s'. Returning '%s', but could also be '%s'."), *PackageName, **OutLongPackageName, *LongPackageName );
							}
							else
							{
								*OutLongPackageName = LongPackageName;
							}
						}
						if (OutFilename)
						{
							FPaths::MakeStandardFilename(Filename);
							if (bResult)
							{
								UE_LOG(LogPackageName, Warning, TEXT("Found ambiguous file name for '%s'. Returning '%s', but could also be '%s'."), *PackageName, **OutFilename, *Filename);
							}
							else
							{
								*OutFilename = Filename;
							}
						}
						bResult = true;
					}
				}
			}
		}
	}
	float ThisTime = FPlatformTime::Seconds() - StartTime;

	if ( bResult )
	{
		UE_LOG(LogPackageName, Log, TEXT("SearchForPackageOnDisk took %7.3fs to resolve %s."), ThisTime, *PackageName);
	}
	else
	{
		UE_LOG(LogPackageName, Log, TEXT("SearchForPackageOnDisk took %7.3fs, but failed to resolve %s."), ThisTime, *PackageName);
	}

	return bResult;
}

bool FPackageName::TryConvertShortPackagePathToLongInObjectPath(const FString& ObjectPath, FString& ConvertedObjectPath)
{
	FString PackagePath;
	FString ObjectName;

	int32 DotPosition = ObjectPath.Find(TEXT("."), ESearchCase::CaseSensitive);
	if (DotPosition != INDEX_NONE)
	{
		PackagePath = ObjectPath.Mid(0, DotPosition);
		ObjectName = ObjectPath.Mid(DotPosition + 1);
	}
	else
	{
		PackagePath = ObjectPath;
	}

	FString LongPackagePath;
	if (!SearchForPackageOnDisk(PackagePath, &LongPackagePath))
	{
		return false;
	}

	ConvertedObjectPath = FString::Printf(TEXT("%s.%s"), *LongPackagePath, *ObjectName);
	return true;
}

FString FPackageName::GetNormalizedObjectPath(const FString& ObjectPath)
{
	if (!ObjectPath.IsEmpty() && FPackageName::IsShortPackageName(ObjectPath))
	{
		FString LongPath;

		UE_LOG(LogPackageName, Warning, TEXT("Asset path \"%s\" is in short form, which is unsupported and -- even if valid -- resolving it will be really slow."), *ObjectPath);
		UE_LOG(LogPackageName, Warning, TEXT("Please consider resaving package in order to speed-up loading."));
		
		if (!FPackageName::TryConvertShortPackagePathToLongInObjectPath(ObjectPath, LongPath))
		{
			UE_LOG(LogPackageName, Warning, TEXT("Asset path \"%s\" could not be resolved."), *ObjectPath);
		}

		return LongPath;
	}
	else
	{
		return ObjectPath;
	}
}

FString FPackageName::GetDelegateResolvedPackagePath(const FString& InSourcePackagePath)
{
	if (FCoreDelegates::PackageNameResolvers.Num() > 0)
	{
		bool WasResolved = false;

		// If the path is /Game/Path/Foo.Foo only worry about resolving the /Game/Path/Foo
		FString PathName = InSourcePackagePath;
		FString ObjectName;
		int32 DotIndex = INDEX_NONE;

		if (PathName.FindChar('.', DotIndex))
		{
			ObjectName = PathName.Mid(DotIndex + 1);
			PathName.LeftInline(DotIndex, false);
		}

		for (auto Delegate : FCoreDelegates::PackageNameResolvers)
		{
			FString ResolvedPath;
			if (Delegate.Execute(PathName, ResolvedPath))
			{
				UE_LOG(LogPackageName, Display, TEXT("Package '%s' was resolved to '%s'"), *PathName, *ResolvedPath);
				PathName = ResolvedPath;
				WasResolved = true;
			}
		}

		if (WasResolved)
		{
			// If package was passed in with an object, add that back on by deriving it from the package name
			if (ObjectName.Len())
			{
				int32 LastSlashIndex = INDEX_NONE;
				if (PathName.FindLastChar('/', LastSlashIndex))
				{
					ObjectName = PathName.Mid(LastSlashIndex + 1);
				}

				PathName += TEXT(".");
				PathName += ObjectName;
			}

			return PathName;
		}
	}

	return InSourcePackagePath;
}

FString FPackageName::GetSourcePackagePath(const FString& InLocalizedPackagePath)
{
	// This function finds the start and end point of the "/L10N/<culture>" part of the path so that it can be removed
	auto GetL10NTrimRange = [](const FString& InPath, int32& OutL10NStart, int32& OutL10NLength)
	{
		const TCHAR* CurChar = *InPath;

		// Must start with a slash
		if (*CurChar++ != TEXT('/'))
		{
			return false;
		}

		// Find the end of the first part of the path, eg /Game/
		while (*CurChar && *CurChar++ != TEXT('/')) {}
		if (!*CurChar)
		{
			// Found end-of-string
			return false;
		}

		if (FCString::Strnicmp(CurChar, TEXT("L10N/"), 5) == 0) // StartsWith "L10N/"
		{
			CurChar -= 1; // -1 because we need to eat the slash before L10N
			OutL10NStart = (CurChar - *InPath);
			OutL10NLength = 6; // "/L10N/"

			// Walk to the next slash as that will be the end of the culture code
			CurChar += OutL10NLength;
			while (*CurChar && *CurChar++ != TEXT('/')) { ++OutL10NLength; }

			return true;
		}
		else if (FCString::Stricmp(CurChar, TEXT("L10N")) == 0) // Is "L10N"
		{
			CurChar -= 1; // -1 because we need to eat the slash before L10N
			OutL10NStart = (CurChar - *InPath);
			OutL10NLength = 5; // "/L10N"

			return true;
		}

		return false;
	};

	FString SourcePackagePath = InLocalizedPackagePath;

	int32 L10NStart = INDEX_NONE;
	int32 L10NLength = 0;
	if (GetL10NTrimRange(SourcePackagePath, L10NStart, L10NLength))
	{
		SourcePackagePath.RemoveAt(L10NStart, L10NLength);
	}

	return SourcePackagePath;
}

FString FPackageName::GetLocalizedPackagePath(const FString& InSourcePackagePath)
{
	const FName LocalizedPackageName = FPackageLocalizationManager::Get().FindLocalizedPackageName(*InSourcePackagePath);
	return (LocalizedPackageName.IsNone()) ? InSourcePackagePath : LocalizedPackageName.ToString();
}

FString FPackageName::GetLocalizedPackagePath(const FString& InSourcePackagePath, const FString& InCultureName)
{
	const FName LocalizedPackageName = FPackageLocalizationManager::Get().FindLocalizedPackageNameForCulture(*InSourcePackagePath, InCultureName);
	return (LocalizedPackageName.IsNone()) ? InSourcePackagePath : LocalizedPackageName.ToString();
}

FString FPackageName::PackageFromPath(const TCHAR* InPathName)
{
	FString PackageName;
	if (FPackageName::TryConvertFilenameToLongPackageName(InPathName, PackageName))
	{
		return PackageName;
	}
	else
	{
		// Not a valid package filename
		return InPathName;
	}
}

bool FPackageName::IsTextPackageExtension(const TCHAR* Ext)
{
	return IsTextAssetPackageExtension(Ext) || IsTextMapPackageExtension(Ext);
}

bool FPackageName::IsTextAssetPackageExtension(const TCHAR* Ext)
{
	if (*Ext != TEXT('.'))
	{
		return (TextAssetPackageExtension.EndsWith(Ext));
	}
	else
	{
		return (TextAssetPackageExtension == Ext);
	}
}

bool FPackageName::IsTextMapPackageExtension(const TCHAR* Ext)
{
	if (*Ext != TEXT('.'))
	{
		return (TextMapPackageExtension.EndsWith(Ext));
	}
	else
	{
		return (TextMapPackageExtension == Ext);
	}
}

bool FPackageName::IsPackageExtension( const TCHAR* Ext )
{
	return IsAssetPackageExtension(Ext) || IsMapPackageExtension(Ext);
}

bool FPackageName::IsAssetPackageExtension(const TCHAR* Ext)
{
	if (*Ext != TEXT('.'))
	{
		return (AssetPackageExtension.EndsWith(Ext));
	}
	else
	{
		return (AssetPackageExtension == Ext);
	}
}

bool FPackageName::IsMapPackageExtension(const TCHAR* Ext)
{
	if (*Ext != TEXT('.'))
	{
		return (MapPackageExtension.EndsWith(Ext));
	}
	else
	{
		return (MapPackageExtension == Ext);
	}
}

bool FPackageName::FindPackagesInDirectory( TArray<FString>& OutPackages, const FString& RootDir )
{
	UE_CLOG(FIoDispatcher::IsInitialized(), LogPackageName, Error, TEXT("Can't search for packages using the filesystem when I/O dispatcher is enabled"));

	FString LocalPathToRootDir;
	if (!FPackageName::TryConvertLongPackageNameToFilename(RootDir / TEXT(""), LocalPathToRootDir))
	{
		LocalPathToRootDir = RootDir;
	}
	LocalPathToRootDir = FPaths::ConvertRelativePathToFull(MoveTemp(LocalPathToRootDir));

	// Find all files in RootDir, then filter by extension (we have two package extensions so they can't
	// be included in the search wildcard.
	TArray<FString> AllFiles;
	IFileManager::Get().FindFilesRecursive(AllFiles, *LocalPathToRootDir, TEXT("*.*"), true, false);
	// Keep track if any package has been found. Can't rely only on OutPackages.Num() > 0 as it may not be empty.
	const int32 PreviousPackagesCount = OutPackages.Num();
	for (int32 FileIndex = 0; FileIndex < AllFiles.Num(); FileIndex++)
	{
		const FString& Filename = AllFiles[FileIndex];
		if (IsPackageFilename(Filename))
		{
			OutPackages.Add(Filename);
		}
	}
	return OutPackages.Num() > PreviousPackagesCount;
}

bool FPackageName::FindPackagesInDirectories(TArray<FString>& OutPackages, const TArrayView<const FString>& RootDirs)
{
	TSet<FString> Packages;
	TArray<FString> DirPackages;
	for (const FString& RootDir : RootDirs)
	{
		DirPackages.Reset();
		FindPackagesInDirectory(DirPackages, RootDir);
		for (FString& DirPackage : DirPackages)
		{
			Packages.Add(MoveTemp(DirPackage));
		}
	}
	OutPackages.Reserve(Packages.Num() + OutPackages.Num());
	for (FString& Package : Packages)
	{
		OutPackages.Add(MoveTemp(Package));
	}
	return Packages.Num() > 0;
}


void FPackageName::IteratePackagesInDirectory(const FString& RootDir, const FPackageNameVisitor& Callback)
{
	class FPackageVisitor : public IPlatformFile::FDirectoryVisitor
	{
	public:
		const FPackageNameVisitor& Callback;
		explicit FPackageVisitor(const FPackageNameVisitor& InCallback)
			: Callback(InCallback)
		{
		}
		virtual bool Visit(const TCHAR* FilenameOrDirectory, bool bIsDirectory) override
		{
			bool Result = true;
			if (!bIsDirectory && IsPackageFilename(FilenameOrDirectory))
			{
				Result = Callback(FilenameOrDirectory);
			}
			return Result;
		}
	};

	FPackageVisitor PackageVisitor(Callback);
	IFileManager::Get().IterateDirectoryRecursively(*RootDir, PackageVisitor);
}

void FPackageName::IteratePackagesInDirectory(const FString& RootDir, const FPackageNameStatVisitor& Callback)
{
	class FPackageStatVisitor : public IPlatformFile::FDirectoryStatVisitor
	{
	public:
		const FPackageNameStatVisitor& Callback;
		explicit FPackageStatVisitor(const FPackageNameStatVisitor& InCallback)
			: Callback(InCallback)
		{
		}
		virtual bool Visit(const TCHAR* FilenameOrDirectory, const FFileStatData& StatData) override
		{
			bool Result = true;
			if (!StatData.bIsDirectory && IsPackageFilename(FilenameOrDirectory))
			{
				Result = Callback(FilenameOrDirectory, StatData);
			}
			return Result;
		}
	};

	FPackageStatVisitor PackageVisitor(Callback);
	IFileManager::Get().IterateDirectoryStatRecursively(*RootDir, PackageVisitor);
}

void FPackageName::QueryRootContentPaths(TArray<FString>& OutRootContentPaths, bool bIncludeReadOnlyRoots, bool bWithoutLeadingSlashes, bool bWithoutTrailingSlashes)
{
	const FLongPackagePathsSingleton& Paths = FLongPackagePathsSingleton::Get();
	Paths.GetValidLongPackageRoots( OutRootContentPaths, bIncludeReadOnlyRoots );

	if (bWithoutTrailingSlashes || bWithoutLeadingSlashes)
	{
		for (FString& It : OutRootContentPaths)
		{
			if (bWithoutTrailingSlashes && It.Len() > 1 && It[It.Len() - 1] == TEXT('/'))
			{
				It.RemoveAt(It.Len() - 1, /*Count*/ 1, /*bAllowShrinking*/ false);
			}

			if (bWithoutLeadingSlashes && It.Len() > 1 && It[0] == TEXT('/'))
			{
				It.RemoveAt(0, /*Count*/ 1, /*bAllowShrinking*/ false);
			}
		}
	}
}

void FPackageName::EnsureContentPathsAreRegistered()
{
	SCOPED_BOOT_TIMING("FPackageName::EnsureContentPathsAreRegistered");
	FLongPackagePathsSingleton::Get();
}

bool FPackageName::ParseExportTextPath(const FString& InExportTextPath, FString* OutClassName, FString* OutObjectPath)
{
	if (InExportTextPath.Split(TEXT("'"), OutClassName, OutObjectPath, ESearchCase::CaseSensitive))
	{
		if ( OutObjectPath )
		{
			FString& OutObjectPathRef = *OutObjectPath;
			if ( OutObjectPathRef.EndsWith(TEXT("'"), ESearchCase::CaseSensitive) )
			{
				OutObjectPathRef.LeftChopInline(1, false);
			}
		}

		return true;
	}
	
	return false;
}

template<class T>
bool ParseExportTextPathImpl(const T& InExportTextPath, T* OutClassName, T* OutObjectPath)
{
	int32 Index;
	if (InExportTextPath.FindChar('\'', Index))
	{
		if (OutClassName)
		{
			*OutClassName = InExportTextPath.Left(Index);
		}

		if (OutObjectPath)
		{
			*OutObjectPath = InExportTextPath.Mid(Index + 1);
			OutObjectPath->RemoveSuffix(InExportTextPath.EndsWith('\''));
		}

		return true;
	}
	
	return false;
}

bool FPackageName::ParseExportTextPath(FWideStringView InExportTextPath, FWideStringView* OutClassName, FWideStringView* OutObjectPath)
{
	return ParseExportTextPathImpl(InExportTextPath, OutClassName, OutObjectPath);
}

bool FPackageName::ParseExportTextPath(FAnsiStringView InExportTextPath, FAnsiStringView* OutClassName, FAnsiStringView* OutObjectPath)
{
	return ParseExportTextPathImpl(InExportTextPath, OutClassName, OutObjectPath);
}

bool FPackageName::ParseExportTextPath(const TCHAR* InExportTextPath, FStringView* OutClassName, FStringView* OutObjectPath)
{
	return ParseExportTextPath(FStringView(InExportTextPath), OutClassName, OutObjectPath);
}

template <class T>
T ExportTextPathToObjectPathImpl(const T& InExportTextPath)
{
	T ObjectPath;
	if (FPackageName::ParseExportTextPath(InExportTextPath, nullptr, &ObjectPath))
	{
		return ObjectPath;
	}
	
	// Could not parse the export text path. Could already be an object path, just return it back.
	return InExportTextPath;
}

FWideStringView FPackageName::ExportTextPathToObjectPath(FWideStringView InExportTextPath)
{
	return ExportTextPathToObjectPathImpl(InExportTextPath);
}

FAnsiStringView FPackageName::ExportTextPathToObjectPath(FAnsiStringView InExportTextPath)
{
	return ExportTextPathToObjectPathImpl(InExportTextPath);
}

FString FPackageName::ExportTextPathToObjectPath(const FString& InExportTextPath)
{
	return ExportTextPathToObjectPathImpl(InExportTextPath);
}

FString FPackageName::ExportTextPathToObjectPath(const TCHAR* InExportTextPath)
{
	return ExportTextPathToObjectPath(FString(InExportTextPath));
}

template<class T>
T ObjectPathToPackageNameImpl(const T& InObjectPath)
{
	// Check for package delimiter
	int32 ObjectDelimiterIdx;
	if (InObjectPath.FindChar('.', ObjectDelimiterIdx))
	{
		return InObjectPath.Mid(0, ObjectDelimiterIdx);
	}

	// No object delimiter. The path must refer to the package name directly.
	return InObjectPath;
}

FWideStringView FPackageName::ObjectPathToPackageName(FWideStringView InObjectPath)
{
	return ObjectPathToPackageNameImpl(InObjectPath);
}

FAnsiStringView FPackageName::ObjectPathToPackageName(FAnsiStringView InObjectPath)
{
	return ObjectPathToPackageNameImpl(InObjectPath);
}

FString FPackageName::ObjectPathToPackageName(const FString& InObjectPath)
{
	return ObjectPathToPackageNameImpl(InObjectPath);
}

template<class T>
T ObjectPathToObjectNameImpl(const T& InObjectPath)
{
	// Check for a subobject
	int32 SubObjectDelimiterIdx;
	if ( InObjectPath.FindChar(':', SubObjectDelimiterIdx) )
	{
		return InObjectPath.Mid(SubObjectDelimiterIdx + 1);
	}

	// Check for a top level object
	int32 ObjectDelimiterIdx;
	if ( InObjectPath.FindChar('.', ObjectDelimiterIdx) )
	{
		return InObjectPath.Mid(ObjectDelimiterIdx + 1);
	}

	// No object or subobject delimiters. The path must refer to the object name directly (i.e. a package).
	return InObjectPath;
}

FString FPackageName::ObjectPathToObjectName(const FString& InObjectPath)
{
	return ObjectPathToObjectNameImpl(InObjectPath);
}

FWideStringView FPackageName::ObjectPathToObjectName(FWideStringView InObjectPath)
{
	return ObjectPathToObjectNameImpl(InObjectPath);
}

bool FPackageName::IsExtraPackage(FStringView InPackageName)
{
	return InPackageName.StartsWith(FLongPackagePathsSingleton::Get().ExtraRootPath);
}

bool FPackageName::IsScriptPackage(FStringView InPackageName)
{
	return InPackageName.StartsWith(FLongPackagePathsSingleton::Get().ScriptRootPath);
}

bool FPackageName::IsMemoryPackage(FStringView InPackageName)
{
	return InPackageName.StartsWith(FLongPackagePathsSingleton::Get().MemoryRootPath);
}

bool FPackageName::IsTempPackage(FStringView InPackageName)
{
	return InPackageName.StartsWith(FLongPackagePathsSingleton::Get().TempRootPath);
}

bool FPackageName::IsLocalizedPackage(FStringView InPackageName)
{
	// Minimum valid package name length is "/A/L10N"
	if (InPackageName.Len() < 7)
	{
		return false;
	}

	const TCHAR* CurChar = InPackageName.GetData();
	const TCHAR* EndChar = InPackageName.GetData() + InPackageName.Len();

	// Must start with a slash
	if (CurChar == EndChar || *CurChar++ != TEXT('/'))
	{
		return false;
	}

	// Find the end of the first part of the path, eg /Game/
	while (CurChar != EndChar && *CurChar++ != TEXT('/')) {}
	if (CurChar == EndChar)
	{
		// Found end-of-string
		return false;
	}

	// Are we part of the L10N folder?
	FStringView Remaining(CurChar, EndChar - CurChar);
	// Is "L10N" or StartsWith "L10N/" 
	return Remaining.StartsWith(TEXT("L10N"_SV), ESearchCase::IgnoreCase) && (Remaining.Len() == 4 || Remaining[4] == '/');
}


#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPackageNameTests, "System.Core.Misc.PackageNames", EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::SmokeFilter)

bool FPackageNameTests::RunTest(const FString& Parameters)
{
	// Localized paths tests
	{
		auto TestIsLocalizedPackage = [&](const FString& InPath, const bool InExpected)
		{
			const bool bResult = FPackageName::IsLocalizedPackage(InPath);
			if (bResult != InExpected)
			{
				AddError(FString::Printf(TEXT("Path '%s' failed FPackageName::IsLocalizedPackage (got '%d', expected '%d')."), *InPath, bResult, InExpected));
			}
		};
		
		TestIsLocalizedPackage(TEXT("/Game"), false);
		TestIsLocalizedPackage(TEXT("/Game/MyAsset"), false);
		TestIsLocalizedPackage(TEXT("/Game/L10N"), true);
		TestIsLocalizedPackage(TEXT("/Game/L10N/en"), true);
		TestIsLocalizedPackage(TEXT("/Game/L10N/en/MyAsset"), true);
	}

	// Source path tests
	{
		auto TestGetSourcePackagePath = [this](const FString& InPath, const FString& InExpected)
		{
			const FString Result = FPackageName::GetSourcePackagePath(InPath);
			if (Result != InExpected)
			{
				AddError(FString::Printf(TEXT("Path '%s' failed FPackageName::GetSourcePackagePath (got '%s', expected '%s')."), *InPath, *Result, *InExpected));
			}
		};

		TestGetSourcePackagePath(TEXT("/Game"), TEXT("/Game"));
		TestGetSourcePackagePath(TEXT("/Game/MyAsset"), TEXT("/Game/MyAsset"));
		TestGetSourcePackagePath(TEXT("/Game/L10N"), TEXT("/Game"));
		TestGetSourcePackagePath(TEXT("/Game/L10N/en"), TEXT("/Game"));
		TestGetSourcePackagePath(TEXT("/Game/L10N/en/MyAsset"), TEXT("/Game/MyAsset"));
	}

	return true;
}

#endif //WITH_DEV_AUTOMATION_TESTS
