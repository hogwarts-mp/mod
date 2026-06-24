// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/ConfigCacheIni.h"

#include "Containers/StringView.h"
#include "Misc/DateTime.h"
#include "Misc/MessageDialog.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Math/Vector4.h"
#include "Stats/Stats.h"
#include "HAL/IConsoleManager.h"
#include "HAL/LowLevelMemTracker.h"
#include "Misc/CoreDelegates.h"
#include "Misc/App.h"
#include "Misc/RemoteConfigIni.h"
#include "Misc/DefaultValueHelper.h"
#include "Misc/ConfigManifest.h"
#include "Misc/DataDrivenPlatformInfoRegistry.h"
#include "Misc/StringBuilder.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/LargeMemoryReader.h"

#if WITH_EDITOR
	#define INI_CACHE 1
#else
	#define INI_CACHE 0
#endif

#ifndef DISABLE_GENERATED_INI_WHEN_COOKED
#define DISABLE_GENERATED_INI_WHEN_COOKED 0
#endif

namespace
{
	static FName VersionName("Version");
	static FName PreserveName("Preserve");
	static FString LegacyIniVersionString = TEXT("IniVersion");
	static FString LegacyEngineString = TEXT("Engine.Engine");
	static FString CurrentIniVersionString = TEXT("CurrentIniVersion");
}

DEFINE_LOG_CATEGORY(LogConfig);
namespace 
{
	FString GenerateHierarchyCacheKey(const FConfigFileHierarchy& IniHierarchy, const FString& IniPath, const FString& BaseIniName)
	{
#if !INI_CACHE
		return TEXT("");
#else
		// A Hierarchy Key is a combined list of all ini file paths that affect that inis data set.
		FString HierKey;
		//
		auto KeyLen = IniPath.Len();
		KeyLen += BaseIniName.Len();
		for (const auto& Ini : IniHierarchy)
		{
			KeyLen += Ini.Value.Filename.Len();
		}
		HierKey.Reserve(KeyLen);
		HierKey += BaseIniName;
		for (const auto& Ini : IniHierarchy)
		{
			HierKey += Ini.Value.Filename;
		}
		HierKey += IniPath;

		return HierKey;
#endif
	}
#if INI_CACHE
	TMap<FString, FConfigFile> HierarchyCache;
#endif
}

/*-----------------------------------------------------------------------------
FConfigValue
-----------------------------------------------------------------------------*/

struct FConfigExpansion
{
	template<int N>
	FConfigExpansion(const TCHAR(&Var)[N], FString&& Val)
		: Variable(Var)
		, Value(Val)
		, VariableLen(N - 1)
	{}

	const TCHAR* Variable;
	FString Value;
	int VariableLen;
};

static FString GetApplicationSettingsDirNormalized()
{
	FString Dir = FPlatformProcess::ApplicationSettingsDir();
	FPaths::NormalizeFilename(Dir);
	return Dir;
}

static const FConfigExpansion* MatchExpansions(const TCHAR* PotentialVariable)
{
	// Allocate replacement value strings once
	static const FConfigExpansion Expansions[] =
	{
		FConfigExpansion(TEXT("%GAME%"), FString(FApp::GetProjectName())),
		FConfigExpansion(TEXT("%GAMEDIR%"), FPaths::ProjectDir()),
		FConfigExpansion(TEXT("%ENGINEDIR%"), FPaths::EngineDir()),
		FConfigExpansion(TEXT("%ENGINEUSERDIR%"), FPaths::EngineUserDir()),
		FConfigExpansion(TEXT("%ENGINEVERSIONAGNOSTICUSERDIR%"), FPaths::EngineVersionAgnosticUserDir()),
		FConfigExpansion(TEXT("%APPSETTINGSDIR%"), GetApplicationSettingsDirNormalized()),
	};

	for (const FConfigExpansion& Expansion : Expansions)
	{
		if (FCString::Strncmp(Expansion.Variable, PotentialVariable, Expansion.VariableLen) == 0)
		{
			return &Expansion;
		}
	}

	return nullptr;
}

static const FConfigExpansion* FindNextExpansion(const TCHAR* Str, const TCHAR*& OutMatch)
{
	for (const TCHAR* It = FCString::Strchr(Str, '%'); It; It = FCString::Strchr(It + 1, '%'))
	{
		if (const FConfigExpansion* Expansion = MatchExpansions(It))
		{
			OutMatch = It;
			return Expansion;
		}
	}

	return nullptr;
}

bool FConfigValue::ExpandValue(const FString& InCollapsedValue, FString& OutExpandedValue)
{
	struct FSubstring
	{
		const TCHAR* Begin;
		const TCHAR* End;

		int32 Len() const { return UE_PTRDIFF_TO_INT32(End - Begin); }
	};

	// Find substrings of input and expansions to concatenate to final output string
	TArray<FSubstring, TFixedAllocator<7>> Substrings;
	const TCHAR* It = *InCollapsedValue;
	while (true)
	{
		const TCHAR* Match;
		if (const FConfigExpansion* Expansion = FindNextExpansion(It, Match))
		{
			Substrings.Add({ It, Match });
			Substrings.Add({ *Expansion->Value, (*Expansion->Value) + Expansion->Value.Len() });

			It = Match + Expansion->VariableLen;
		}
		else if (Substrings.Num() == 0)
		{
			// No expansions matched, skip concatenation and return input string
			OutExpandedValue = InCollapsedValue;
			return false;
		}
		else
		{
			Substrings.Add({ It, *InCollapsedValue + InCollapsedValue.Len() });
			break;
		}
	}

	// Concat
	int32 OutLen = 0;
	for (const FSubstring& Substring : Substrings)
	{
		OutLen += Substring.Len();
	}

	OutExpandedValue.Reserve(OutLen);
	for (const FSubstring& Substring : Substrings)
	{
		OutExpandedValue.AppendChars(Substring.Begin, Substring.Len());
	}

	return true;
}

FString FConfigValue::ExpandValue(const FString& InCollapsedValue)
{
	FString OutExpandedValue;
	ExpandValue(InCollapsedValue, OutExpandedValue);
	return OutExpandedValue;
}

void FConfigValue::ExpandValueInternal()
{
	const TCHAR* Dummy;
	if (FindNextExpansion(*SavedValue, Dummy))
	{
		ExpandValue(SavedValue, /* out */ ExpandedValue);
	}
}

bool FConfigValue::CollapseValue(const FString& InExpandedValue, FString& OutCollapsedValue)
{
	int32 NumReplacements = 0;
	OutCollapsedValue = InExpandedValue;

	auto ExpandPathValueInline = [&](const FString& InPath, const TCHAR* InReplacement)
	{
		if (OutCollapsedValue.StartsWith(InPath, ESearchCase::CaseSensitive))
		{
			NumReplacements += OutCollapsedValue.ReplaceInline(*InPath, InReplacement, ESearchCase::CaseSensitive);
		}
		else if (FPaths::IsRelative(InPath))
		{
			const FString AbsolutePath = FPaths::ConvertRelativePathToFull(InPath);
			if (OutCollapsedValue.StartsWith(AbsolutePath, ESearchCase::CaseSensitive))
			{
				NumReplacements += OutCollapsedValue.ReplaceInline(*AbsolutePath, InReplacement, ESearchCase::CaseSensitive);
			}
		}
	};

	// Replace the game directory with %GAMEDIR%.
	ExpandPathValueInline(FPaths::ProjectDir(), TEXT("%GAMEDIR%"));

	// Replace the user's engine directory with %ENGINEUSERDIR%.
	ExpandPathValueInline(FPaths::EngineUserDir(), TEXT("%ENGINEUSERDIR%"));

	// Replace the user's engine agnostic directory with %ENGINEVERSIONAGNOSTICUSERDIR%.
	ExpandPathValueInline(FPaths::EngineVersionAgnosticUserDir(), TEXT("%ENGINEVERSIONAGNOSTICUSERDIR%"));

	// Replace the application settings directory with %APPSETTINGSDIR%.
	FString AppSettingsDir = FPlatformProcess::ApplicationSettingsDir();
	FPaths::NormalizeFilename(AppSettingsDir);
	ExpandPathValueInline(AppSettingsDir, TEXT("%APPSETTINGSDIR%"));

	// Note: We deliberately don't replace the game name with %GAME% here, as the game name may exist in many places (including paths)

	return NumReplacements > 0;
}

FString FConfigValue::CollapseValue(const FString& InExpandedValue)
{
	FString CollapsedValue;
	CollapseValue(InExpandedValue, CollapsedValue);
	return CollapsedValue;
}

#if !UE_BUILD_SHIPPING
/**
* Checks if the section name is the expected name format (long package name or simple name)
*/
static void CheckLongSectionNames(const TCHAR* Section, const FConfigFile* File)
{
	if (!FPlatformProperties::RequiresCookedData())
	{
		// Guard against short names in ini files.
		if (FCString::Strnicmp(Section, TEXT("/Script/"), 8) == 0)
		{
			// Section is a long name
			if (File->Find(Section + 8))
			{
				UE_LOG(LogConfig, Fatal, TEXT("Short config section found while looking for %s"), Section);
			}
		}
		else
		{
			// Section is a short name
			FString LongName = FString(TEXT("/Script/")) + Section;
			if (File->Find(*LongName))
			{
				UE_LOG(LogConfig, Fatal, TEXT("Short config section used instead of long %s"), Section);
			}
		}
	}
}
#endif // UE_BUILD_SHIPPING

/*-----------------------------------------------------------------------------
	FConfigSection
-----------------------------------------------------------------------------*/


bool FConfigSection::HasQuotes( const FString& Test )
{
	if (Test.Len() < 2)
	{
		return false;
	}

	return Test.Left(1) == TEXT("\"") && Test.Right(1) == TEXT("\"");
}

bool FConfigSection::operator==( const FConfigSection& Other ) const
{
	if ( Pairs.Num() != Other.Pairs.Num() )
	{
		return 0;
	}

	FConfigSectionMap::TConstIterator My(*this), Their(Other);
	while ( My && Their )
	{
		if (My.Key() != Their.Key())
		{
			return 0;
		}

		const FString& MyValue = My.Value().GetValue(), &TheirValue = Their.Value().GetValue();
		if ( FCString::Strcmp(*MyValue,*TheirValue) &&
			(!HasQuotes(MyValue) || FCString::Strcmp(*TheirValue,*MyValue.Mid(1,MyValue.Len()-2))) &&
			(!HasQuotes(TheirValue) || FCString::Strcmp(*MyValue,*TheirValue.Mid(1,TheirValue.Len()-2))) )
		{
			return 0;
		}

		++My, ++Their;
	}
	return 1;
}

bool FConfigSection::operator!=( const FConfigSection& Other ) const
{
	return ! (FConfigSection::operator==(Other));
}

FArchive& operator<<(FArchive& Ar, FConfigSection& ConfigSection)
{
	Ar << static_cast<FConfigSection::Super&>(ConfigSection);
	Ar << ConfigSection.ArrayOfStructKeys;
	return Ar;
}

// Pull out a property from a Struct property, StructKeyMatch should be in the form "MyProp=". This reduces
// memory allocations for each attempted match
static void ExtractPropertyValue(const FString& FullStructValue, const FString& StructKeyMatch, FString& Out)
{
	Out.Reset();

	int32 MatchLoc = FullStructValue.Find(StructKeyMatch);
	// we only look for matching StructKeys if the incoming Value had a key
	if (MatchLoc >= 0)
	{
		// skip to after the match string
		MatchLoc += StructKeyMatch.Len();

		const TCHAR* Start = &FullStructValue.GetCharArray()[MatchLoc];
		bool bInQuotes = false;
		// skip over an open quote
		if (*Start == '\"')
		{
			Start++;
			bInQuotes = true;
		}
		const TCHAR* Travel = Start;

		// look for end of token, using " if it started with one
		while (*Travel && ((bInQuotes && *Travel != '\"') || (!bInQuotes && (FChar::IsAlnum(*Travel) || *Travel == '_'))))
		{
			Travel++;
		}

		// pull out the token
		Out.AppendChars(*FullStructValue + MatchLoc, UE_PTRDIFF_TO_INT32(Travel - Start));
	}
}

void FConfigSection::HandleAddCommand(FName Key, FString&& Value, bool bAppendValueIfNotArrayOfStructsKeyUsed)
{
	if (!HandleArrayOfKeyedStructsCommand(Key, Forward<FString&&>(Value)))
	{
		if (bAppendValueIfNotArrayOfStructsKeyUsed)
		{
			Add(Key, MoveTemp(Value));
		}
		else
		{
			AddUnique(Key, MoveTemp(Value));
		}
	}
}

bool FConfigSection::HandleArrayOfKeyedStructsCommand(FName Key, FString&& Value)
{
	FString* StructKey = ArrayOfStructKeys.Find(Key);
	bool bHandledWithKey = false;
	if (StructKey)
	{
		// look at the incoming value for the StructKey
		FString StructKeyMatch = *StructKey + "=";

		// pull out the token that matches the StructKey (a property name) from the full struct property string
		FString StructKeyValueToMatch;
		ExtractPropertyValue(Value, StructKeyMatch, StructKeyValueToMatch);

		if (StructKeyValueToMatch.Len() > 0)
		{
			FString ExistingStructValueKey;
			// if we have a key for this array, then we look for it in the Value for each array entry
			for (FConfigSection::TIterator It(*this); It; ++It)
			{
				// only look at matching keys
				if (It.Key() == Key)
				{
					// now look for the matching ArrayOfStruct Key as the incoming KeyValue
					ExtractPropertyValue(It.Value().GetValue(), StructKeyMatch, ExistingStructValueKey);
					if (ExistingStructValueKey == StructKeyValueToMatch)
					{
						// we matched the key, so replace the existing value in place (so as not to reorder)
						It.Value() = Value;

						// mark that the key was found and the add has been processed
						bHandledWithKey = true;
						break;
					}
				}
			}
		}
	}

	return bHandledWithKey;
}

// Look through the file's per object config ArrayOfStruct keys and see if this section matches
static void FixupArrayOfStructKeysForSection(FConfigSection* Section, const FString& SectionName, const TMap<FString, TMap<FName, FString> >& PerObjectConfigKeys)
{
	for (TMap<FString, TMap<FName, FString> >::TConstIterator It(PerObjectConfigKeys); It; ++It)
	{
		if (SectionName.EndsWith(It.Key()))
		{
			for (TMap<FName, FString>::TConstIterator It2(It.Value()); It2; ++It2)
			{
				Section->ArrayOfStructKeys.Add(It2.Key(), It2.Value());
			}
		}
	}
}


/**
 * Check if an ini file exists, allowing a delegate to determine if it will handle loading it
 */
static bool DoesConfigFileExistWrapper(const TCHAR* IniFile)
{
	// will any delegates return contents via PreLoadConfigFileDelegate()?
	int32 ResponderCount = 0;
	FCoreDelegates::CountPreLoadConfigFileRespondersDelegate.Broadcast(IniFile, ResponderCount);

	if (ResponderCount > 0)
	{
		return true;
	}

	// otherwise just look for the normal file to exist
	return IFileManager::Get().FileSize(IniFile) >= 0;
}

/**
 * Load ini file, but allowing a delegate to handle the loading instead of the standard file load
 */
static bool LoadConfigFileWrapper(const TCHAR* IniFile, FString& Contents, bool bIsOverride = false)
{
	// let other systems load the file instead of the standard load below
	FCoreDelegates::PreLoadConfigFileDelegate.Broadcast(IniFile, Contents);

	// if this loaded any text, we are done, and we won't override the contents with standard ini file data
	if (Contents.Len())
	{
		return true;
	}

#if ALLOW_INI_OVERRIDE_FROM_COMMANDLINE
	if (bIsOverride)
	{
		// Make sure we bypass the pak layer because if our override is likely under root the pak layer will
		// just resolve it even if it's an absolute path.

		return FFileHelper::LoadFileToString(Contents, &IPlatformFile::GetPlatformPhysical(), IniFile);
	}
#endif

	// note: we don't check if FileOperations are disabled because downloadable content calls this directly (which
	// needs file ops), and the other caller of this is already checking for disabled file ops
	// and don't read from the file, if the delegate got anything loaded
	return FFileHelper::LoadFileToString(Contents, IniFile);
}

/**
 * Save an ini file, with delegates also saving the file (its safe to allow both to happen, even tho loading doesn't behave this way)
 */
static bool SaveConfigFileWrapper(const TCHAR* IniFile, const FString& Contents)
{
	// let anyone that needs to save it, do so (counting how many did)
	int32 SavedCount = 0;
	FCoreDelegates::PreSaveConfigFileDelegate.Broadcast(IniFile, Contents, SavedCount);

	// save it even if a delegate did as well
	bool bLocalWriteSucceeded = FFileHelper::SaveStringToFile(Contents, IniFile, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	// success is based on a delegate or file write working (or both)
	return SavedCount > 0 || bLocalWriteSucceeded;
}


enum class EConfigLayerFlags : int32
{
	None						= 0,
	Required					= (1 << 0),
	AllowCommandLineOverride	= (1 << 1),
	DedicatedServerOnly			= (1 << 2), // replaces Default, Base, and (NOT {PLATFORM} yet) with an empty string
	GenerateCacheKey			= (1 << 3),
	NoExpand					= (1 << 4),
};
ENUM_CLASS_FLAGS(EConfigLayerFlags);

/**
 * Structure to define all the layers of the config system. Layers can be expanded by expansion files (NoRedist, etc), or by ini platform parents
 * (coming soon from another branch)
 */
struct FConfigLayer
{
	// Used by the editor to display in the ini-editor
	const TCHAR* EditorName;
	// Path to the ini file (with variables)
	const TCHAR* Path;
	// Special flag
	EConfigLayerFlags Flag;

} GConfigLayers[] =
{
	/**************************************************
	**** CRITICAL NOTES
	**** If you change this array, you need to also change EnumerateConfigFileLocations() in ConfigHierarchy.cs!!!
	**** And maybe UObject::GetDefaultConfigFilename(), UObject::GetGlobalUserConfigFilename()
	**************************************************/

	// Engine/Base.ini
	{ TEXT("AbsoluteBase"),				TEXT("{ENGINE}/Config/Base.ini"), EConfigLayerFlags::Required | EConfigLayerFlags::NoExpand},
	
	// Engine/Base*.ini
	{ TEXT("Base"),						TEXT("{ENGINE}/Config/Base{TYPE}.ini") },
	// Engine/Platform/BasePlatform*.ini
	{ TEXT("BasePlatform"),				TEXT("{ENGINE}/Config/{PLATFORM}/Base{PLATFORM}{TYPE}.ini")  },
	// Project/Default*.ini
	{ TEXT("ProjectDefault"),			TEXT("{PROJECT}/Config/Default{TYPE}.ini"), EConfigLayerFlags::AllowCommandLineOverride | EConfigLayerFlags::GenerateCacheKey },
	// Project/Generated*.ini Reserved for files generated by build process and should never be checked in 
	{ TEXT("ProjectGenerated"),			TEXT("{PROJECT}/Config/Generated{TYPE}.ini"), EConfigLayerFlags::GenerateCacheKey },
	// Engine/Platform/Platform*.ini
	{ TEXT("EnginePlatform"),			TEXT("{ENGINE}/Config/{PLATFORM}/{PLATFORM}{TYPE}.ini") },
	// Project/Platform/Platform*.ini
	{ TEXT("ProjectPlatform"),			TEXT("{PROJECT}/Config/{PLATFORM}/{PLATFORM}{TYPE}.ini") },
	// Project/Generated*.ini Reserved for files generated by build process and should never be checked in 
	{ TEXT("ProjectPlatformGenerated"),	TEXT("{PROJECT}/Config/{PLATFORM}/Generated{PLATFORM}{TYPE}.ini") },
	// UserSettings/.../User*.ini
	{ TEXT("UserSettingsDir"),			TEXT("{USERSETTINGS}Unreal Engine/Engine/Config/User{TYPE}.ini"), EConfigLayerFlags::NoExpand },
	// UserDir/.../User*.ini
	{ TEXT("UserDir"),					TEXT("{USER}Unreal Engine/Engine/Config/User{TYPE}.ini"), EConfigLayerFlags::NoExpand },
	// Project/User*.ini
	{ TEXT("GameDirUser"),				TEXT("{PROJECT}/Config/User{TYPE}.ini"), EConfigLayerFlags::GenerateCacheKey | EConfigLayerFlags::NoExpand },
};


/**
 * This describes extra files per layer, to deal with restricted and NDA covered platform files that can't have the settings
 * be in the Base/Default ini files.
 * Note that we treat DedicatedServer as a "Platform" where it will have it's own directory of files, like a platform
 */
struct FConfigLayerExpansion
{
	// a set of replacements from the source file to possible other files
	const TCHAR* Before1;
	const TCHAR* After1;
	const TCHAR* Before2;
	const TCHAR* After2;
};

/**************************************************
**** CRITICAL NOTES
**** If you change these arrays, you need to also change EnumerateConfigFileLocations() in ConfigHierarchy.cs!!!
**************************************************/
static FConfigLayerExpansion GConfigExpansions[] =
{

	// No replacements
	{},
	// Restricted Locations
	{ TEXT("{ENGINE}/"), TEXT("{ENGINE}/Restricted/NotForLicensees/"),	TEXT("{PROJECT}/Config/"), TEXT("{RESTRICTEDPROJECT_NFL}/Config/") },
	{ TEXT("{ENGINE}/"), TEXT("{ENGINE}/Restricted/NoRedist/"),			TEXT("{PROJECT}/Config/"), TEXT("{RESTRICTEDPROJECT_NR}/Config/") },
	// Platform Extensions
	{ TEXT("{ENGINE}/Config/{PLATFORM}/"), TEXT("{EXTENGINE}/Config/"),	TEXT("{PROJECT}/Config/{PLATFORM}/"), TEXT("{EXTPROJECT}/Config/") },
	// Platform Extensions in Restricted Locations
	{ TEXT("{ENGINE}/Config/{PLATFORM}/"), TEXT("{ENGINE}/Restricted/NotForLicensees/Platforms/{PLATFORM}/Config/"),	TEXT("{PROJECT}/Config/{PLATFORM}/"), TEXT("{RESTRICTEDPROJECT_NFL}/Platforms/{PLATFORM}/Config/") },
	{ TEXT("{ENGINE}/Config/{PLATFORM}/"), TEXT("{ENGINE}/Restricted/NoRedist/Platforms/{PLATFORM}/Config/"),			TEXT("{PROJECT}/Config/{PLATFORM}/"), TEXT("{RESTRICTEDPROJECT_NR}/Platforms/{PLATFORM}/Config/") },
};

constexpr int32 MaxPlatformIndex = 99;

constexpr int32 GetStaticKey(int32 LayerIndex, int32 ReplacementIndex, int32 PlatformIndex)
{
	return LayerIndex * 10000 + ReplacementIndex * 100 + PlatformIndex;
}

constexpr int32 MaxStaticHierarchyKey = GetStaticKey(UE_ARRAY_COUNT(GConfigLayers) - 1, UE_ARRAY_COUNT(GConfigExpansions) - 1, MaxPlatformIndex);

/*-----------------------------------------------------------------------------
	FConfigFileHierarchy
-----------------------------------------------------------------------------*/
FConfigFileHierarchy::FConfigFileHierarchy() : KeyGen(MaxStaticHierarchyKey)
{
}


int32 FConfigFileHierarchy::GenerateDynamicKey()
{
	return ++KeyGen;
}

int32 FConfigFileHierarchy::AddStaticLayer(FIniFilename Filename, int32 LayerIndex, int32 ExpansionIndex /*= 0*/, int32 PlatformIndex /*= 0*/)
{
	int32 Key = GetStaticKey(LayerIndex, ExpansionIndex, PlatformIndex);
	Emplace(Key, MoveTemp(Filename));
	return Key;
}

int32 FConfigFileHierarchy::AddDynamicLayer(FIniFilename Filename)
{
	int32 Key = GenerateDynamicKey();
	Emplace(Key, MoveTemp(Filename));
	return Key;
}

/*-----------------------------------------------------------------------------
	FConfigFile
-----------------------------------------------------------------------------*/
FConfigFile::FConfigFile()
: Dirty( false )
, NoSave( false )
, Name( NAME_None )
, SourceConfigFile(nullptr)
{

	if (FCoreDelegates::OnFConfigCreated.IsBound())
	{
		FCoreDelegates::OnFConfigCreated.Broadcast(this);
	}
}

FConfigFile::~FConfigFile()
{

	if (FCoreDelegates::OnFConfigDeleted.IsBound() && !(GExitPurge))
	{
		FCoreDelegates::OnFConfigDeleted.Broadcast(this);
	}

	if (SourceConfigFile != nullptr)
	{
		delete SourceConfigFile;
		SourceConfigFile = nullptr;
	}
}
bool FConfigFile::operator==( const FConfigFile& Other ) const
{
	if ( Pairs.Num() != Other.Pairs.Num() )
		return 0;

	for ( TMap<FString,FConfigSection>::TConstIterator It(*this), OtherIt(Other); It && OtherIt; ++It, ++OtherIt)
	{
		if ( It.Key() != OtherIt.Key() )
			return 0;

		if ( It.Value() != OtherIt.Value() )
			return 0;
	}

	return 1;
}

bool FConfigFile::operator!=( const FConfigFile& Other ) const
{
	return ! (FConfigFile::operator==(Other));
}

FConfigSection* FConfigFile::FindOrAddSection(const FString& SectionName)
{
	FConfigSection* Section = Find(SectionName);
	if (Section == nullptr)
	{
		Section = &Add(SectionName, FConfigSection());
	}
	return Section;
}

bool FConfigFile::Combine(const FString& Filename)
{
	FString FinalFileName = Filename;
	bool bFoundOverride = OverrideFileFromCommandline(FinalFileName);

	FString Text;
	if (LoadConfigFileWrapper(*FinalFileName, Text, bFoundOverride))
	{
		if (Text.StartsWith("#!"))
		{
			// this will import/"execute" another .ini file before this one - useful for subclassing platforms, like tvOS extending iOS
			// the text following the #! is a relative path to another .ini file
			FString TheLine;
			int32 LinesConsumed = 0;
			// skip over the #!
			const TCHAR* Ptr = *Text + 2;
			FParse::LineExtended(&Ptr, TheLine, LinesConsumed, false);
			TheLine = TheLine.TrimEnd();
		
			// now import the relative path'd file (TVOS would have #!../IOS) recursively
			Combine(FPaths::GetPath(Filename) / TheLine);
		}

		CombineFromBuffer(Text);
		return true;
	}
	else
	{
		checkf(!bFoundOverride, TEXT("Failed to Load config override %s"), *FinalFileName);
	}

	return false;
}


void FConfigFile::CombineFromBuffer(const FString& Buffer)
{
	const TCHAR* Ptr = *Buffer;
	FConfigSection* CurrentSection = nullptr;
	FString CurrentSectionName;
	FString TheLine;
	bool Done = false;
	while( !Done )
	{
		// Advance past new line characters
		while( *Ptr=='\r' || *Ptr=='\n' )
		{
			Ptr++;
		}

		// read the next line
		int32 LinesConsumed = 0;
		FParse::LineExtended(&Ptr, /* reset */ TheLine, LinesConsumed, false);
		if (Ptr == nullptr || *Ptr == 0)
		{
			Done = true;
		}
		TCHAR* Start = const_cast<TCHAR*>(*TheLine);

		// Strip trailing spaces from the current line
		while( *Start && FChar::IsWhitespace(Start[FCString::Strlen(Start)-1]) )
		{
			Start[FCString::Strlen(Start)-1] = 0;
		}

		// If the first character in the line is [ and last char is ], this line indicates a section name
		if( *Start=='[' && Start[FCString::Strlen(Start)-1]==']' )
		{
			// Remove the brackets
			Start++;
			Start[FCString::Strlen(Start)-1] = 0;

			// If we don't have an existing section by this name, add one
			CurrentSection = FindOrAddSection( Start );
			CurrentSectionName = Start;

			// make sure the CurrentSection has any of the special ArrayOfStructKeys added
			FixupArrayOfStructKeysForSection(CurrentSection, Start, PerObjectConfigArrayOfStructKeys);
		}

		// Otherwise, if we're currently inside a section, and we haven't reached the end of the stream
		else if( CurrentSection && *Start )
		{
			TCHAR* Value = 0;

			// ignore [comment] lines that start with ;
			if(*Start != (TCHAR)';')
			{
				Value = FCString::Strstr(Start,TEXT("="));
			}

			// Ignore any lines that don't contain a key-value pair
			if( Value )
			{
				// Terminate the property name, advancing past the =
				*Value++ = 0;

				// strip leading whitespace from the property name
				while ( *Start && FChar::IsWhitespace(*Start) )
				{						
					Start++;
				}

				// ~ is a packaging and should be skipped at runtime
				if (Start[0] == '~')
				{
					Start++;
				}

				// determine how this line will be merged
				TCHAR Cmd = Start[0];
				if ( Cmd=='+' || Cmd=='-' || Cmd=='.' || Cmd == '!' || Cmd == '@' || Cmd == '*' )
				{
					Start++;
				}
				else
				{
					Cmd=' ';
				}

				// Strip trailing spaces from the property name.
				while( *Start && FChar::IsWhitespace(Start[FCString::Strlen(Start)-1]) )
				{
					Start[FCString::Strlen(Start)-1] = 0;
				}

				FString ProcessedValue;

				// Strip leading whitespace from the property value
				while ( *Value && FChar::IsWhitespace(*Value) )
				{
					Value++;
				}

				// strip trailing whitespace from the property value
				while( *Value && FChar::IsWhitespace(Value[FCString::Strlen(Value)-1]) )
				{
					Value[FCString::Strlen(Value)-1] = 0;
				}

				// If this line is delimited by quotes
				if( *Value=='\"' )
				{
					FParse::QuotedString(Value, ProcessedValue);
				}
				else
				{
					ProcessedValue = Value;
				}

				if (Cmd == '+')
				{
					// Add if not already present.
					CurrentSection->HandleAddCommand( Start, MoveTemp(ProcessedValue), false );
				}
				else if( Cmd=='-' )	
				{
					// Remove if present.
					CurrentSection->RemoveSingle( Start, ProcessedValue );
					CurrentSection->CompactStable();
				}
				else if ( Cmd=='.' )
				{
					CurrentSection->HandleAddCommand( Start, MoveTemp(ProcessedValue), true );
				}
				else if( Cmd=='!' )
				{
					CurrentSection->Remove( Start );
				}
				else if (Cmd == '@')
				{
					// track a key to show uniqueness for arrays of structs
					CurrentSection->ArrayOfStructKeys.Add(Start, MoveTemp(ProcessedValue));
				}
				else if (Cmd == '*')
				{
					// track a key to show uniqueness for arrays of structs
					TMap<FName, FString>& POCKeys = PerObjectConfigArrayOfStructKeys.FindOrAdd(CurrentSectionName);
					POCKeys.Add(Start, MoveTemp(ProcessedValue));
				}
				else
				{
					// First see if this can be processed as an array of keyed structs command
					if (!CurrentSection->HandleArrayOfKeyedStructsCommand(Start, MoveTemp(ProcessedValue)))
					{
						// Add if not present and replace if present.
						FConfigValue* ConfigValue = CurrentSection->Find(Start);
						if (!ConfigValue)
						{
							CurrentSection->Add(Start, MoveTemp(ProcessedValue));
						}
						else
						{
							*ConfigValue = FConfigValue(MoveTemp(ProcessedValue));
						}
					}
				}

				// Mark as dirty so "Write" will actually save the changes.
				Dirty = true;
			}
		}
	}

	// Avoid memory wasted in array slack.
	Shrink();
	for( TMap<FString,FConfigSection>::TIterator It(*this); It; ++It )
	{
		It.Value().Shrink();
	}
}

/**
 * Process the contents of an .ini file that has been read into an FString
 * 
 * @param Contents Contents of the .ini file
 */
void FConfigFile::ProcessInputFileContents(const FString& Contents)
{
	const TCHAR* Ptr = Contents.Len() > 0 ? *Contents : nullptr;
	FConfigSection* CurrentSection = nullptr;
	bool Done = false;
	while( !Done && Ptr != nullptr )
	{
		// Advance past new line characters
		while( *Ptr=='\r' || *Ptr=='\n' )
		{
			Ptr++;
		}			
		// read the next line
		FString TheLine;
		int32 LinesConsumed = 0;
		FParse::LineExtended(&Ptr, TheLine, LinesConsumed, false);
		if (Ptr == nullptr || *Ptr == 0)
		{
			Done = true;
		}
		TCHAR* Start = const_cast<TCHAR*>(*TheLine);

		// Strip trailing spaces from the current line
		while( *Start && FChar::IsWhitespace(Start[FCString::Strlen(Start)-1]) )
		{
			Start[FCString::Strlen(Start)-1] = 0;
		}

		// If the first character in the line is [ and last char is ], this line indicates a section name
		if( *Start=='[' && Start[FCString::Strlen(Start)-1]==']' )
		{
			// Remove the brackets
			Start++;
			Start[FCString::Strlen(Start)-1] = 0;

			// If we don't have an existing section by this name, add one
			CurrentSection = FindOrAddSection( Start );
		}

		// Otherwise, if we're currently inside a section, and we haven't reached the end of the stream
		else if( CurrentSection && *Start )
		{
			TCHAR* Value = 0;

			// ignore [comment] lines that start with ;
			if(*Start != (TCHAR)';')
			{
				Value = FCString::Strstr(Start,TEXT("="));
			}

			// Ignore any lines that don't contain a key-value pair
			if( Value )
			{
				// Terminate the propertyname, advancing past the =
				*Value++ = 0;

				// strip leading whitespace from the property name
				while ( *Start && FChar::IsWhitespace(*Start) )
					Start++;

				// Strip trailing spaces from the property name.
				while( *Start && FChar::IsWhitespace(Start[FCString::Strlen(Start)-1]) )
					Start[FCString::Strlen(Start)-1] = 0;

				// Strip leading whitespace from the property value
				while ( *Value && FChar::IsWhitespace(*Value) )
					Value++;

				// strip trailing whitespace from the property value
				while( *Value && FChar::IsWhitespace(Value[FCString::Strlen(Value)-1]) )
					Value[FCString::Strlen(Value)-1] = 0;

				// If this line is delimited by quotes
				if( *Value=='\"' )
				{
					FString ProcessedValue;
					FParse::QuotedString(Value, ProcessedValue);

					// Add this pair to the current FConfigSection
					CurrentSection->Add(Start, *ProcessedValue);
				}
				else
				{
					// Add this pair to the current FConfigSection
					CurrentSection->Add(Start, Value);
				}
			}
		}
	}

	// Avoid memory wasted in array slack.
	Shrink();
	for( TMap<FString,FConfigSection>::TIterator It(*this); It; ++It )
	{
		It.Value().Shrink();
	}
}

void FConfigFile::Read( const FString& Filename )
{
	// we can't read in a file if file IO is disabled
	if (GConfig == nullptr || !GConfig->AreFileOperationsDisabled())
	{
		Empty();
		FString Text;

		FString FinalFileName = Filename;
		bool bFoundOverride = OverrideFileFromCommandline(FinalFileName);
	
		if (LoadConfigFileWrapper(*FinalFileName, Text, bFoundOverride))
		{
			// process the contents of the string
			ProcessInputFileContents(Text);
		}
		else
		{
			checkf(!bFoundOverride, TEXT("Failed to Load config override %s"), *FinalFileName);
		}
	}
}

bool FConfigFile::ShouldExportQuotedString(const FString& PropertyValue)
{
	bool bEscapeNextChar = false;
	bool bIsWithinQuotes = false;

	// The value should be exported as quoted string if...
	const TCHAR* const DataPtr = *PropertyValue;
	for (const TCHAR* CharPtr = DataPtr; *CharPtr; ++CharPtr)
	{
		const TCHAR ThisChar = *CharPtr;
		const TCHAR NextChar = *(CharPtr + 1);

		const bool bIsFirstChar = CharPtr == DataPtr;
		const bool bIsLastChar = NextChar == 0;

		if (ThisChar == TEXT('"') && !bEscapeNextChar)
		{
			bIsWithinQuotes = !bIsWithinQuotes;
		}
		bEscapeNextChar = ThisChar == TEXT('\\') && bIsWithinQuotes && !bEscapeNextChar;

		// ... it begins or ends with a space (which is stripped on import)
		if (ThisChar == TEXT(' ') && (bIsFirstChar || bIsLastChar))
		{
			return true;
		}

		// ... it begins with a '"' (which would be treated as a quoted string)
		if (ThisChar == TEXT('"') && bIsFirstChar)
		{
			return true;
		}

		// ... it ends with a '\' (which would be treated as a line extension)
		if (ThisChar == TEXT('\\') && bIsLastChar)
		{
			return true;
		}

		// ... it contains unquoted '{' or '}' (which are stripped on import)
		if ((ThisChar == TEXT('{') || ThisChar == TEXT('}')) && !bIsWithinQuotes)
		{
			return true;
		}
		
		// ... it contains unquoted '//' (interpreted as a comment when importing)
		if ((ThisChar == TEXT('/') && NextChar == TEXT('/')) && !bIsWithinQuotes)
		{
			return true;
		}

		// ... it contains an unescaped new-line
		if (!bEscapeNextChar && (NextChar == TEXT('\r') || NextChar == TEXT('\n')))
		{
			return true;
		}
	}

	return false;
}

FString FConfigFile::GenerateExportedPropertyLine(const FString& PropertyName, const FString& PropertyValue)
{
	FString Out;
	AppendExportedPropertyLine(Out, PropertyName, PropertyValue);
	return Out;
}

void FConfigFile::AppendExportedPropertyLine(FString& Out, const FString& PropertyName, const FString& PropertyValue)
{
	// Append has been measured to be twice as fast as Appendf here
	Out.Append(PropertyName);

	Out.AppendChar(TEXT('='));

	if (FConfigFile::ShouldExportQuotedString(PropertyValue))
	{
		Out.AppendChar(TEXT('"'));
		Out.Append(PropertyValue.ReplaceCharWithEscapedChar());
		Out.AppendChar(TEXT('"'));
	}
	else
	{
		Out.Append(PropertyValue);
	}

	static const int32 LineTerminatorLen = FCString::Strlen(LINE_TERMINATOR);
	Out.Append(LINE_TERMINATOR, LineTerminatorLen);
}

#if ALLOW_INI_OVERRIDE_FROM_COMMANDLINE

/** A collection of identifiers which will help us parse the commandline opions. */
namespace CommandlineOverrideSpecifiers
{
	// -ini:IniName:[Section1]:Key1=Value1,[Section2]:Key2=Value2
	const TCHAR	IniFileOverrideIdentifier[] = TEXT("-iniFile=");
	const TCHAR IniSwitchIdentifier[]     = TEXT("-ini:");
	const TCHAR IniNameEndIdentifier[]    = TEXT(":[");
	const TCHAR SectionStartIdentifier[]  = TEXT("[");
	const TCHAR PropertyStartIdentifier[] = TEXT("]:");
	const TCHAR PropertySeperator[]       = TEXT(",");
}

#endif
bool FConfigFile::OverrideFileFromCommandline(FString& Filename)
{
#if ALLOW_INI_OVERRIDE_FROM_COMMANDLINE
	// look for this filename on the commandline in the format:
	//		-iniFile=<PatFile1>,<PatFile2>,<PatFile3>
	// for example:
	//		-iniFile=D:\FN-Main\FortniteGame\Config\Windows\WindowsDeviceProfiles.ini
	//       
	//		Description: 
	//          The FortniteGame\Config\Windows\WindowsDeviceProfiles.ini contained in the pak file will
	//          be replace with D:\FN-Main\FortniteGame\Config\Windows\WindowsDeviceProfiles.ini.

	//			Note: You will need the same base file path for this to work. If you
	//                want to override Engine/Config/BaseEngine.ini, you will need to place the override file 
	//                under the same folder structure. 
	//          Ex1: D:\<some_folder>\Engine\Config\BaseEngine.ini
	//			Ex2: D:\<some_folder>\FortniteGame\Config\Windows\WindowsEngine.ini
	FString StagedFilePaths;
	if(FParse::Value(FCommandLine::Get(), CommandlineOverrideSpecifiers::IniFileOverrideIdentifier, StagedFilePaths, false))
	{ 
		FString RelativePath = Filename;
		if (FPaths::IsUnderDirectory(RelativePath, FPaths::RootDir()))
		{
			FPaths::MakePathRelativeTo(RelativePath, *FPaths::RootDir());

			TArray<FString> Files;
			StagedFilePaths.ParseIntoArray(Files, TEXT(","), true);
			for (int32 Index = 0; Index < Files.Num(); Index++)
			{
				FString NormalizedOverride = Files[Index];
				FPaths::NormalizeFilename(NormalizedOverride);
				if (NormalizedOverride.EndsWith(RelativePath))
				{
					Filename = Files[Index];
					UE_LOG(LogConfig, Warning, TEXT("Loading override ini file: %s "), *Files[Index]);
					return true;
				}
			}
		}
	}
#endif

	return false;
}
/**
* Looks for any overrides on the commandline for this file
*
* @param File Config to possibly modify
* @param Filename Name of the .ini file to look for overrides
*/
void FConfigFile::OverrideFromCommandline(FConfigFile* File, const FString& Filename)
{
#if ALLOW_INI_OVERRIDE_FROM_COMMANDLINE
	FString Settings;
	// look for this filename on the commandline in the format:
	//		-ini:IniName:[Section1]:Key1=Value1,[Section2]:Key2=Value2
	// for example:
	//		-ini:Engine:[/Script/Engine.Engine]:bSmoothFrameRate=False,[TextureStreaming]:PoolSize=100
	//			(will update the cache after the final combined engine.ini)
	const TCHAR* CommandlineStream = FCommandLine::Get();
	while(FParse::Value(CommandlineStream, *FString::Printf(TEXT("%s%s"), CommandlineOverrideSpecifiers::IniSwitchIdentifier, *FPaths::GetBaseFilename(Filename)), Settings, false))
	{
		// break apart on the commas
		TArray<FString> SettingPairs;
		Settings.ParseIntoArray(SettingPairs, CommandlineOverrideSpecifiers::PropertySeperator, true);
		for (int32 Index = 0; Index < SettingPairs.Num(); Index++)
		{
			// set each one, by splitting on the =
			FString SectionAndKey, Value;
			if (SettingPairs[Index].Split(TEXT("="), &SectionAndKey, &Value))
			{
				// now we need to split off the key from the rest of the section name
				int32 SectionNameEndIndex = SectionAndKey.Find(CommandlineOverrideSpecifiers::PropertyStartIdentifier, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
				// check for malformed string
				if (SectionNameEndIndex == INDEX_NONE || SectionNameEndIndex == 0)
				{
					continue;
				}

				// Create the commandline override object
				FConfigCommandlineOverride& CommandlineOption = File->CommandlineOptions[File->CommandlineOptions.Emplace()];
				CommandlineOption.BaseFileName = *FPaths::GetBaseFilename(Filename);
				CommandlineOption.Section = SectionAndKey.Left(SectionNameEndIndex);
				
				// Remove commandline syntax from the section name.
				CommandlineOption.Section = CommandlineOption.Section.Replace(CommandlineOverrideSpecifiers::IniNameEndIdentifier, TEXT(""));
				CommandlineOption.Section = CommandlineOption.Section.Replace(CommandlineOverrideSpecifiers::PropertyStartIdentifier, TEXT(""));
				CommandlineOption.Section = CommandlineOption.Section.Replace(CommandlineOverrideSpecifiers::SectionStartIdentifier, TEXT(""));

				CommandlineOption.PropertyKey = SectionAndKey.Mid(SectionNameEndIndex + UE_ARRAY_COUNT(CommandlineOverrideSpecifiers::PropertyStartIdentifier) - 1);
				CommandlineOption.PropertyValue = Value;

				// now put it into this into the cache
				File->SetString(*CommandlineOption.Section, *CommandlineOption.PropertyKey, *CommandlineOption.PropertyValue);
			}
		}

		// Keep searching for more instances of -ini
		CommandlineStream = FCString::Stristr(CommandlineStream, CommandlineOverrideSpecifiers::IniSwitchIdentifier);
		check(CommandlineStream);
		CommandlineStream++;
	}
#endif
}


void FConfigFile::AddDynamicLayerToHeirarchy(const FString& Filename)
{
	// Don't allow dynamic layers in editor
	if (GIsEditor)
		return;

	FString ConfigContent;
	if (!FFileHelper::LoadFileToString(ConfigContent, *Filename))
		return;

	if (SourceConfigFile)
	{
		SourceConfigFile->SourceIniHierarchy.AddDynamicLayer(FIniFilename(Filename));
		SourceConfigFile->CombineFromBuffer(ConfigContent);
	}

	SourceIniHierarchy.AddDynamicLayer(FIniFilename(Filename));
	CombineFromBuffer(ConfigContent);

	// Disable saving since dynamic layers are only for runtime
	NoSave = true;
}

/**
 * This will completely load .ini file hierarchy into the passed in FConfigFile. The passed in FConfigFile will then
 * have the data after combining all of those .ini 
 *
 * @param FilenameToLoad - this is the path to the file to 
 * @param ConfigFile - This is the FConfigFile which will have the contents of the .ini loaded into and Combined()
 *
 **/
static bool LoadIniFileHierarchy(const FConfigFileHierarchy& HierarchyToLoad, FConfigFile& ConfigFile, const bool bUseCache)
{
	// if the file does not exist then return
	if (HierarchyToLoad.Num() == 0)
	{
		//UE_LOG(LogConfig, Warning, TEXT( "LoadIniFileHierarchy was unable to find FilenameToLoad: %s "), *FilenameToLoad);
		return true;
	}
	else
	{
		// If no inis exist or only engine (Base*.ini) inis exist, don't load anything
		bool bOptionalIniFound = false;
		for (const TPair<int32, FIniFilename>& Pair : HierarchyToLoad)
		{
			const FIniFilename& IniToLoad = Pair.Value;
			if (IniToLoad.bRequired == false &&
				(!IsUsingLocalIniFile(*IniToLoad.Filename, nullptr) || DoesConfigFileExistWrapper(*IniToLoad.Filename)))
			{
				bOptionalIniFound = true;
				break;
			}
		}
		if (!bOptionalIniFound)
		{
			// No point in generating ini
			return true;
		}
	}

	int32 FirstCacheIndex = 0;
#if INI_CACHE
	if (bUseCache && HierarchyCache.Num() > 0)
	{
		// Find the last value in the hierarchy that is cached. We can start the load from there
		for (auto& HierarchyIt : HierarchyToLoad)
		{
			if (HierarchyCache.Find(HierarchyIt.Value.CacheKey)) 
			{
				FirstCacheIndex = HierarchyIt.Key;
			}
		}
	}
#endif

	TArray<FDateTime> TimestampsOfInis;
	
	// Making a copy of the HierarchyToLoad so we can loop and make changes to ConfigFile without breaking the iteration.
	const FConfigFileHierarchy TempHierarchyToLoad = HierarchyToLoad;

	// Traverse ini list back to front, merging along the way.
	for (auto& HierarchyIt : TempHierarchyToLoad)
	{
		if (FirstCacheIndex <= HierarchyIt.Key)
		{
			const FIniFilename& IniToLoad = HierarchyIt.Value;
			const FString& IniFileName = IniToLoad.Filename;
			bool bDoProcess = true;
#if INI_CACHE
			bool bShouldCache = IniToLoad.CacheKey.Len() > 0;
			bShouldCache &= bUseCache;
			if ( bShouldCache ) // if we are forcing a load don't mess with the cache
			{
				auto* CachedConfigFile = HierarchyCache.Find(IniToLoad.CacheKey);
				if (CachedConfigFile) 
				{
					ConfigFile = *CachedConfigFile;
					bDoProcess = false;
				}
				ConfigFile.CacheKey = IniToLoad.CacheKey;
			}
			else
			{
				ConfigFile.CacheKey = TEXT("");
			}
#endif
			if (bDoProcess)
			{
				// Spit out friendly error if there was a problem locating .inis (e.g. bad command line parameter or missing folder, ...).
				if (IsUsingLocalIniFile(*IniFileName, nullptr) && !DoesConfigFileExistWrapper(*IniFileName))
				{
					if (IniToLoad.bRequired)
					{
						//UE_LOG(LogConfig, Error, TEXT("Couldn't locate '%s' which is required to run '%s'"), *IniToLoad.Filename, FApp::GetProjectName() );
						return false;
					}
					else
					{
#if INI_CACHE
						// missing file just add the current config file to the cache
						if ( bShouldCache )
						{
								HierarchyCache.Add(IniToLoad.CacheKey, ConfigFile);
						}
#endif
						continue;
					}
				}

				bool bDoEmptyConfig = false;
				bool bDoCombine = (HierarchyIt.Key != 0);
				//UE_LOG(LogConfig, Log,  TEXT( "Combining configFile: %s" ), *IniList(IniIndex) );
				ProcessIniContents(*(HierarchyIt.Value.Filename), *IniFileName, &ConfigFile, bDoEmptyConfig, bDoCombine);
#if INI_CACHE
				if ( bShouldCache )
				{
					HierarchyCache.Add(IniToLoad.CacheKey, ConfigFile);
				}
#endif
			}
		}
	}

	// Set this configs files source ini hierarchy to show where it was loaded from.
	ConfigFile.SourceIniHierarchy = TempHierarchyToLoad;

	return true;
}

/**
 * Check if the provided config section has a property which matches the one we are providing
 *
 * @param InSection			- The section which we are looking for a match in
 * @param InPropertyName	- The name of the property we are looking to match
 * @param InPropertyValue	- The value of the property which, if found, we are checking a match
 *
 * @return True if a property was found in the InSection which matched the Property Name and Value.
 */
bool DoesConfigPropertyValueMatch(const FConfigSection* InSection, const FName& InPropertyName, const FString& InPropertyValue )
{
	bool bFoundAMatch = false;

	if (InSection)
	{
		const bool bIsInputStringValidFloat = FDefaultValueHelper::IsStringValidFloat(InPropertyValue);

		// Start Array check, if the property is in an array, we need to iterate over all properties.
		for (FConfigSection::TConstKeyIterator It(*InSection, InPropertyName); It && !bFoundAMatch; ++It)
		{
			const FString& PropertyValue = It.Value().GetSavedValue();
			bFoundAMatch = 
				PropertyValue.Len() == InPropertyValue.Len() &&
				PropertyValue == InPropertyValue;
				
			// if our properties don't match, run further checks
			if( !bFoundAMatch )
			{
				// Check that the mismatch isn't just a string comparison issue with floats
				if (bIsInputStringValidFloat && FDefaultValueHelper::IsStringValidFloat( PropertyValue ))
				{
					bFoundAMatch = FCString::Atof( *PropertyValue ) == FCString::Atof( *InPropertyValue );
				}
			}
		}
	}


	return bFoundAMatch;
}


/**
 * Check if the provided property information was set as a commandline override
 *
 * @param InConfigFile		- The config file which we want to check had overridden values
 * @param InSectionName		- The name of the section which we are checking for a match
 * @param InPropertyName		- The name of the property which we are checking for a match
 * @param InPropertyValue	- The value of the property which we are checking for a match
 *
 * @return True if a commandline option was set that matches the input parameters
 */
bool PropertySetFromCommandlineOption(const FConfigFile* InConfigFile, const FString& InSectionName, const FName& InPropertyName, const FString& InPropertyValue)
{
	bool bFromCommandline = false;

#if ALLOW_INI_OVERRIDE_FROM_COMMANDLINE
	for (const FConfigCommandlineOverride& CommandlineOverride : InConfigFile->CommandlineOptions)
	{
		if (CommandlineOverride.PropertyKey.Equals(InPropertyName.ToString(), ESearchCase::IgnoreCase) &&
			CommandlineOverride.PropertyValue.Equals(InPropertyValue, ESearchCase::IgnoreCase) &&
			CommandlineOverride.Section.Equals(InSectionName, ESearchCase::IgnoreCase) &&
			CommandlineOverride.BaseFileName.Equals(FPaths::GetBaseFilename(InConfigFile->Name.ToString()), ESearchCase::IgnoreCase))
		{
			bFromCommandline = true;
		}
	}
#endif // ALLOW_INI_OVERRIDE_FROM_COMMANDLINE

	return bFromCommandline;
}

/**
 * Clear the hierarchy cache
 * cos nobody want dat junk no more! bro
 *
 * @param Base ini name of the file hierarchy that we want to clear the cache for
 */
static void ClearHierarchyCache( const TCHAR* BaseIniName )
{
#if INI_CACHE
	// if we are forcing reload from disk then clear the cached hierarchy files
	for ( TMap<FString, FConfigFile>::TIterator It(HierarchyCache); It; ++It )
	{
		if ( It.Key().StartsWith( BaseIniName ) )
		{
			It.RemoveCurrent();
		}
	}
#endif
}

bool FConfigFile::Write(const FString& Filename, bool bDoRemoteWrite/* = true*/, const FString& PrefixText/*=FString()*/)
{
	TMap<FString, FString> SectionTexts;
	TArray<FString> SectionOrder;
	if (!PrefixText.IsEmpty())
	{
		SectionTexts.Add(FString(), PrefixText);
	}
	return Write(Filename, bDoRemoteWrite, SectionTexts, SectionOrder);
}

bool FConfigFile::Write(const FString& Filename, bool bDoRemoteWrite, TMap<FString, FString>& InOutSectionTexts, const TArray<FString>& InSectionOrder)
{
	if( !Dirty || NoSave || FParse::Param( FCommandLine::Get(), TEXT("nowrite")) || 
		(FParse::Param( FCommandLine::Get(), TEXT("Multiprocess"))  && !FParse::Param( FCommandLine::Get(), TEXT("MultiprocessSaveConfig"))) // Is can be useful to save configs with multiprocess if they are given INI overrides
		) 
		return true;

	bool bAcquiredIniCombineThreshold = false;	// avoids extra work when writing multiple properties
	int32 IniCombineThreshold = MAX_int32;

	FString Text;
	// Estimate max size to reduce re-allocations (does not inspect actual properties for performance)
	int32 InitialEstimatedFinalTextSize = 0;
	int32 HighestPropertiesInSection = 0;
	for (TIterator SectionIterator(*this); SectionIterator; ++SectionIterator)
	{
		HighestPropertiesInSection = FMath::Max(HighestPropertiesInSection, SectionIterator.Value().Num());
		InitialEstimatedFinalTextSize += ((SectionIterator.Value().Num() + 1) * 90);
	}
	// Limit size estimate to avoid pre-allocating too much memory
	InitialEstimatedFinalTextSize = FMath::Min(InitialEstimatedFinalTextSize, 128 * 1024 * 1024);
	Text.Reserve(InitialEstimatedFinalTextSize);

	TArray<FString> SectionOrder;
	SectionOrder.Reserve(InSectionOrder.Num() + this->Num());
	SectionOrder.Append(InSectionOrder);
	InOutSectionTexts.Reserve(InSectionOrder.Num() + this->Num());

	bool bIsADefaultIniWrite = false;
	{
		// If we are writing to a default config file and this property is an array, we need to be careful to remove those from higher up the hierarchy
		const FString AbsoluteFilename = FPaths::ConvertRelativePathToFull(Filename);
		const FString AbsoluteGameGeneratedConfigDir = FPaths::ConvertRelativePathToFull(FPaths::GeneratedConfigDir());
		const FString AbsoluteGameAgnosticGeneratedConfigDir = FPaths::ConvertRelativePathToFull(FPaths::Combine(*FPaths::GameAgnosticSavedDir(), TEXT("Config")) + TEXT("/"));
		bIsADefaultIniWrite = !AbsoluteFilename.Contains(AbsoluteGameGeneratedConfigDir) && !AbsoluteFilename.Contains(AbsoluteGameAgnosticGeneratedConfigDir);
	}

	TArray<const FConfigValue*> CompletePropertyToWrite;
	FString PropertyNameString;
	TSet<FName> PropertiesAddedLookup;
	PropertiesAddedLookup.Reserve(HighestPropertiesInSection);
	int32 EstimatedFinalTextSize = 0;

	for( TIterator SectionIterator(*this); SectionIterator; ++SectionIterator )
	{
		const FString& SectionName = SectionIterator.Key();
		const FConfigSection& Section = SectionIterator.Value();

		// If we have a config file to check against, have a look.
		FConfigSection* SourceConfigSection = nullptr;
		if (SourceConfigFile)
		{
			// Check the sections which could match our desired section name
			SourceConfigSection = SourceConfigFile->Find(SectionName);

#if !UE_BUILD_SHIPPING
			if (!SourceConfigSection && FPlatformProperties::RequiresCookedData() == false && SectionName.StartsWith(TEXT("/Script/")))
			{
				// Guard against short names in ini files
				const FString ShortSectionName = SectionName.Replace(TEXT("/Script/"), TEXT("")); 
				if (SourceConfigFile->Find(ShortSectionName) != nullptr)
				{
					UE_LOG(LogConfig, Fatal, TEXT("Short config section found while looking for %s"), *SectionName);
				}
			}
#endif
		}

		Text.Reset();
		PropertiesAddedLookup.Reset();

		for( FConfigSection::TConstIterator It2(Section); It2; ++It2 )
		{
			const FName PropertyName = It2.Key();
			// Use GetSavedValueForWriting rather than GetSavedValue to avoid having this save operation mark the values as having been accessed for dependency tracking
			const FString& PropertyValue = UE::ConfigCacheIni::Private::FAccessor::GetSavedValueForWriting(It2.Value());

			// Check if the we've already processed a property of this name. If it was part of an array we may have already written it out.
			if( !PropertiesAddedLookup.Contains( PropertyName ) )
			{
				// check whether the option we are attempting to write out, came from the commandline as a temporary override.
				const bool bOptionIsFromCommandline = PropertySetFromCommandlineOption(this, SectionName, PropertyName, PropertyValue);

				// We ALWAYS want to write CurrentIniVersion.
				const bool bIsCurrentIniVersion = (SectionName == CurrentIniVersionString);

				// Check if the property matches the source configs. We do not wanna write it out if so.
				if ((bIsADefaultIniWrite || bIsCurrentIniVersion || !DoesConfigPropertyValueMatch(SourceConfigSection, PropertyName, PropertyValue)) && !bOptionIsFromCommandline)
				{
					// If this is the first property we are writing of this section, then print the section name
					if( Text.Len() == 0 )
					{
						Text.Appendf(TEXT("[%s]") LINE_TERMINATOR, *SectionName);

						// and if the section has any array of struct uniqueness keys, add them here
						for (auto It = Section.ArrayOfStructKeys.CreateConstIterator(); It; ++It)
						{
							Text.Appendf(TEXT("@%s=%s") LINE_TERMINATOR, *It.Key().ToString(), *It.Value());
						}
					}

					// Write out our property, if it is an array we need to write out the entire array.
					CompletePropertyToWrite.Reset();
					Section.MultiFindPointer( PropertyName, CompletePropertyToWrite, true );

					if( bIsADefaultIniWrite )
					{
						if (!bAcquiredIniCombineThreshold)
						{
							// find the filename in ini hierarchy
							FString IniName = FPaths::GetCleanFilename(Filename);
							for (const auto& HierarchyFileIt : SourceIniHierarchy)
							{
								if (FPaths::GetCleanFilename(HierarchyFileIt.Value.Filename) == IniName)
								{
									IniCombineThreshold = HierarchyFileIt.Key;
									break;
								}
							}

							bAcquiredIniCombineThreshold = true;
						}
						ProcessPropertyAndWriteForDefaults(IniCombineThreshold, CompletePropertyToWrite, Text, SectionName, PropertyName.ToString());
					}
					else
					{
						PropertyNameString.Reset(FName::StringBufferSize);
						PropertyName.AppendString(PropertyNameString);
						for (const FConfigValue* ConfigValue : CompletePropertyToWrite)
						{
							// Use GetSavedValueForWriting rather than GetSavedValue to avoid marking these values used during save to disk as having been accessed for dependency tracking
							AppendExportedPropertyLine(Text, PropertyNameString, UE::ConfigCacheIni::Private::FAccessor::GetSavedValueForWriting(*ConfigValue));
						}
					}

					PropertiesAddedLookup.Add( PropertyName );
				}
			}
		}

		// If we didn't decide to write any properties on this section, then we don't add the section
		// to the destination file
		if (Text.Len() > 0)
		{
			InOutSectionTexts.FindOrAdd(SectionName) = Text;

			// Add the Section to SectionOrder in case it's not already there
			SectionOrder.Add(SectionName);

			EstimatedFinalTextSize += Text.Len() + 4;
		}
		else
		{
			InOutSectionTexts.Remove(SectionName);
		}
	}

	// Join all of the sections together
	Text.Reset(EstimatedFinalTextSize);
	TSet<FString> SectionNamesLeftToWrite;
	SectionNamesLeftToWrite.Reserve(InOutSectionTexts.Num());
	for (TPair<FString,FString>& kvpair : InOutSectionTexts)
	{
		SectionNamesLeftToWrite.Add(kvpair.Key);
	}

	static const FString BlankLine(LINE_TERMINATOR LINE_TERMINATOR);
	auto AddSectionToText = [&Text, &InOutSectionTexts, &SectionNamesLeftToWrite](const FString& SectionName)
	{
		FString* SectionText = InOutSectionTexts.Find(SectionName);
		if (!SectionText)
		{
			return;
		}
		if (SectionNamesLeftToWrite.Remove(SectionName) == 0)
		{
			// We already wrote this section
			return;
		}
		Text.Append(*SectionText);
		if (!Text.EndsWith(BlankLine, ESearchCase::CaseSensitive))
		{
			Text.Append(LINE_TERMINATOR);
		}
	};

	// First add the empty section
	AddSectionToText(FString());

	// Second add all the sections in SectionOrder; this includes any sections in *this that were not in InSectionOrder, because we added them during the loop
	for (FString& SectionName : SectionOrder)
	{
		AddSectionToText(SectionName);
	}

	// Third add any remaining sections that were passed in in InOutSectionTexts but were not specified in InSectionOrder and were not in *this
	if (SectionNamesLeftToWrite.Num() > 0)
	{
		TArray<FString> RemainingNames;
		RemainingNames.Reserve(SectionNamesLeftToWrite.Num());
		for (FString& SectionName : SectionNamesLeftToWrite)
		{
			RemainingNames.Add(SectionName);
		}
		RemainingNames.Sort();
		for (FString& SectionName : RemainingNames)
		{
			AddSectionToText(SectionName);
		}
	}

	// Ensure We have at least something to write
	if (Text.Len() == 0)
	{
		Text.Append(LINE_TERMINATOR);
	}

	if (bDoRemoteWrite)
	{
		// Write out the remote version (assuming it was loaded)
		FRemoteConfig::Get()->Write(*Filename, Text);
	}

	bool bResult = SaveConfigFileWrapper(*Filename, Text);

#if INI_CACHE
	// if we wrote the config successfully
	if ( bResult && CacheKey.Len() > 0 )
	{
		check( Name != NAME_None );
		ClearHierarchyCache(*Name.ToString());
	}
	
	/*if (bResult && CacheKey.Len() > 0)
	{
		HierarchyCache.Add(CacheKey, *this);
	}*/
#endif

	// File is still dirty if it didn't save.
	Dirty = !bResult;

	// Return if the write was successful
	return bResult;
}



/** Adds any properties that exist in InSourceFile that this config file is missing */
void FConfigFile::AddMissingProperties( const FConfigFile& InSourceFile )
{
	for( TConstIterator SourceSectionIt( InSourceFile ); SourceSectionIt; ++SourceSectionIt )
	{
		const FString& SourceSectionName = SourceSectionIt.Key();
		const FConfigSection& SourceSection = SourceSectionIt.Value();

		{
			// If we don't already have this section, go ahead and add it now
			FConfigSection* DestSection = FindOrAddSection( SourceSectionName );

			for( FConfigSection::TConstIterator SourcePropertyIt( SourceSection ); SourcePropertyIt; ++SourcePropertyIt )
			{
				const FName SourcePropertyName = SourcePropertyIt.Key();
				
				// If we don't already have this property, go ahead and add it now
				if( DestSection->Find( SourcePropertyName ) == nullptr )
				{
					TArray<FConfigValue> Results;
					SourceSection.MultiFind(SourcePropertyName, Results, true);
					for (const FConfigValue& Result : Results)
					{

						DestSection->Add(SourcePropertyName, Result.GetSavedValue());
						Dirty = true;
					}
				}
			}
		}
	}
}



void FConfigFile::Dump(FOutputDevice& Ar)
{
	Ar.Logf( TEXT("FConfigFile::Dump") );

	for( TMap<FString,FConfigSection>::TIterator It(*this); It; ++It )
	{
		Ar.Logf( TEXT("[%s]"), *It.Key() );
		TArray<FName> KeyNames;

		FConfigSection& Section = It.Value();
		Section.GetKeys(KeyNames);
		for(TArray<FName>::TConstIterator KeyNameIt(KeyNames);KeyNameIt;++KeyNameIt)
		{
			const FName KeyName = *KeyNameIt;

			TArray<FConfigValue> Values;
			Section.MultiFind(KeyName,Values,true);

			if ( Values.Num() > 1 )
			{
				for ( int32 ValueIndex = 0; ValueIndex < Values.Num(); ValueIndex++ )
				{
					Ar.Logf(TEXT("	%s[%i]=%s"), *KeyName.ToString(), ValueIndex, *Values[ValueIndex].GetValue().ReplaceCharWithEscapedChar());
				}
			}
			else
			{
				Ar.Logf(TEXT("	%s=%s"), *KeyName.ToString(), *Values[0].GetValue().ReplaceCharWithEscapedChar());
			}
		}

		Ar.Log( LINE_TERMINATOR );
	}
}

bool FConfigFile::GetString( const TCHAR* Section, const TCHAR* Key, FString& Value ) const
{
	const FConfigSection* Sec = Find( Section );
	if( Sec == nullptr )
	{
		return false;
	}
	const FConfigValue* PairString = Sec->Find( Key );
	if( PairString == nullptr )
	{
		return false;
	}
	Value = PairString->GetValue();
	return true;
}

bool FConfigFile::GetText( const TCHAR* Section, const TCHAR* Key, FText& Value ) const
{
	const FConfigSection* Sec = Find( Section );
	if( Sec == nullptr )
	{
		return false;
	}
	const FConfigValue* PairString = Sec->Find( Key );
	if( PairString == nullptr )
	{
		return false;
	}
	return FTextStringHelper::ReadFromBuffer( *PairString->GetValue(), Value, Section ) != nullptr;
}

bool FConfigFile::GetInt(const TCHAR* Section, const TCHAR* Key, int32& Value) const
{
	FString Text;
	if (GetString(Section, Key, Text))
	{
		Value = FCString::Atoi(*Text);
		return true;
	}
	return false;
}

bool FConfigFile::GetFloat(const TCHAR* Section, const TCHAR* Key, float& Value) const
{
	FString Text;
	if (GetString(Section, Key, Text))
	{
		Value = FCString::Atof(*Text);
		return true;
	}
	return false;
}

bool FConfigFile::GetInt64( const TCHAR* Section, const TCHAR* Key, int64& Value ) const
{
	FString Text; 
	if( GetString( Section, Key, Text ) )
	{
		Value = FCString::Atoi64(*Text);
		return true;
	}
	return false;
}
bool FConfigFile::GetBool(const TCHAR* Section, const TCHAR* Key, bool& Value ) const
{
	FString Text;
	if ( GetString(Section, Key, Text ))
	{
		Value = FCString::ToBool(*Text);
		return 1;
	}
	return 0;
}

int32 FConfigFile::GetArray(const TCHAR* Section, const TCHAR* Key, TArray<FString>& Value) const
{
	Value.Empty();
	const FConfigSection* Sec = Find(Section);
	if (Sec != nullptr)
	{
		Sec->MultiFind(Key, Value, true);
	}
#if !UE_BUILD_SHIPPING
	else
	{
		CheckLongSectionNames(Section, this);
	}
#endif

	return Value.Num();
}



void FConfigFile::SetString( const TCHAR* Section, const TCHAR* Key, const TCHAR* Value )
{
	FConfigSection* Sec = FindOrAddSection( Section );

	FConfigValue* ConfigValue = Sec->Find( Key );
	if( ConfigValue == nullptr )
	{
		Sec->Add( Key, Value );
		Dirty = true;
	}
	// Use GetSavedValueForWriting rather than GetSavedValue to avoid having the is-it-dirty query mark the values as having been accessed for dependency tracking
	else if( FCString::Strcmp(*UE::ConfigCacheIni::Private::FAccessor::GetSavedValueForWriting(*ConfigValue),Value)!=0 )
	{
		Dirty = true;
		*ConfigValue = FConfigValue(Value);
	}
}

void FConfigFile::SetText( const TCHAR* Section, const TCHAR* Key, const FText& Value )
{
	FConfigSection* Sec = FindOrAddSection( Section );

	FString StrValue;
	FTextStringHelper::WriteToBuffer(StrValue, Value);

	FConfigValue* ConfigValue = Sec->Find( Key );
	if( ConfigValue == nullptr )
	{
		Sec->Add( Key, StrValue );
		Dirty = true;
	}
	// Use GetSavedValueForWriting rather than GetSavedValue to avoid having the is-it-dirty query mark the values as having been accessed for dependency tracking
	else if( FCString::Strcmp(*UE::ConfigCacheIni::Private::FAccessor::GetSavedValueForWriting(*ConfigValue), *StrValue)!=0 )
	{
		Dirty = true;
		*ConfigValue = FConfigValue(StrValue);
	}
}

void FConfigFile::SetInt64( const TCHAR* Section, const TCHAR* Key, int64 Value )
{
	TCHAR Text[MAX_SPRINTF]=TEXT("");
	FCString::Sprintf( Text, TEXT("%lld"), Value );
	SetString( Section, Key, Text );
}


void FConfigFile::SetArray(const TCHAR* Section, const TCHAR* Key, const TArray<FString>& Value)
{
	FConfigSection* Sec = FindOrAddSection(Section);

	if (Sec->Remove(Key) > 0)
	{
		Dirty = true;
	}

	for (int32 i = 0; i < Value.Num(); i++)
	{
		Sec->Add(Key, *Value[i]);
		Dirty = true;
	}
}

void FConfigFile::SaveSourceToBackupFile()
{
	FString Text;

	FString BetweenRunsDir = (FPaths::ProjectIntermediateDir() / TEXT("Config/CoalescedSourceConfigs/"));
	FString Filename = FString::Printf( TEXT( "%s%s.ini" ), *BetweenRunsDir, *Name.ToString() );

	for( TMap<FString,FConfigSection>::TIterator SectionIterator(*SourceConfigFile); SectionIterator; ++SectionIterator )
	{
		const FString& SectionName = SectionIterator.Key();
		const FConfigSection& Section = SectionIterator.Value();

		Text += FString::Printf( TEXT("[%s]") LINE_TERMINATOR, *SectionName);

		for( FConfigSection::TConstIterator PropertyIterator(Section); PropertyIterator; ++PropertyIterator )
		{
			const FName PropertyName = PropertyIterator.Key();
			// Use GetSavedValueForWriting rather than GetSavedValue to avoid having this save operation mark the values as having been accessed for dependency tracking
			const FString& PropertyValue = UE::ConfigCacheIni::Private::FAccessor::GetSavedValueForWriting(PropertyIterator.Value());
			Text += FConfigFile::GenerateExportedPropertyLine(PropertyName.ToString(), PropertyValue);
		}
		Text += LINE_TERMINATOR;
	}

	if(!SaveConfigFileWrapper(*Filename, Text))
	{
		UE_LOG(LogConfig, Warning, TEXT("Failed to saved backup for config[%s]"), *Filename);
	}
}


void FConfigFile::ProcessSourceAndCheckAgainstBackup()
{
	if (!FPlatformProperties::RequiresCookedData())
	{
		FString BetweenRunsDir = (FPaths::ProjectIntermediateDir() / TEXT("Config/CoalescedSourceConfigs/"));
		FString BackupFilename = FString::Printf( TEXT( "%s%s.ini" ), *BetweenRunsDir, *Name.ToString() );

		FConfigFile BackupFile;
		ProcessIniContents(*BackupFilename, *BackupFilename, &BackupFile, false, false);

		for( TMap<FString,FConfigSection>::TIterator SectionIterator(*SourceConfigFile); SectionIterator; ++SectionIterator )
		{
			const FString& SectionName = SectionIterator.Key();
			const FConfigSection& SourceSection = SectionIterator.Value();
			const FConfigSection* BackupSection = BackupFile.Find( SectionName );

			if( BackupSection && SourceSection != *BackupSection )
			{
				this->Remove( SectionName );
				this->Add( SectionName, SourceSection );
			}
		}

		SaveSourceToBackupFile();
	}
}

void FConfigFile::ProcessPropertyAndWriteForDefaults(int IniCombineThreshold, const TArray<const FConfigValue*>& InCompletePropertyToProcess, FString& OutText, const FString& SectionName, const FString& PropertyName)
{
	// Only process against a hierarchy if this config file has one.
	if (SourceIniHierarchy.Num() > 0)
	{
		// Handle array elements from the configs hierarchy.
		if (PropertyName.StartsWith(TEXT("+")) || InCompletePropertyToProcess.Num() > 1)
		{
			// Build a config file out of this default configs hierarchy.
			FConfigCacheIni Hierarchy(EConfigCacheType::Temporary);

			int32 HighestFileIndex = 0;
			TArray<int32> ExistingEntries;
			SourceIniHierarchy.GetKeys(ExistingEntries);
			for (const int32& NextEntry : ExistingEntries)
			{
				HighestFileIndex = NextEntry > HighestFileIndex ? NextEntry : HighestFileIndex;
			}

			const FString& LastFileInHierarchy = SourceIniHierarchy.FindChecked(HighestFileIndex).Filename;
			FConfigFile& DefaultConfigFile = Hierarchy.Add(LastFileInHierarchy, FConfigFile());

			for (const auto& HierarchyFileIt : SourceIniHierarchy)
			{
				// Combine everything up to the level we're writing, but not including it.
				// Inclusion would result in a bad feedback loop where on subsequent writes 
				// we would be diffing against the same config we've just written to.
				if (HierarchyFileIt.Key < IniCombineThreshold)
				{
					DefaultConfigFile.Combine(HierarchyFileIt.Value.Filename);
				}
			}

			// Remove any array elements from the default configs hierearchy, we will add these in below
			// Note.	This compensates for an issue where strings in the hierarchy have a slightly different format
			//			to how the config system wishes to serialize them.
			TArray<FString> ArrayProperties;
			Hierarchy.GetArray(*SectionName, *PropertyName.Replace(TEXT("+"), TEXT("")), ArrayProperties, *LastFileInHierarchy);

			for (const FString& NextElement : ArrayProperties)
			{
				FString PropertyNameWithRemoveOp = PropertyName.Replace(TEXT("+"), TEXT("-"));
				OutText.Append(GenerateExportedPropertyLine(PropertyNameWithRemoveOp, NextElement));
			}
		}
	}

	// Write the properties out to a file.
	for ( const FConfigValue* PropertyIt : InCompletePropertyToProcess)
	{
		OutText.Append(GenerateExportedPropertyLine(PropertyName, PropertyIt->GetSavedValue()));
	}
}


/*-----------------------------------------------------------------------------
	FConfigCacheIni
-----------------------------------------------------------------------------*/

FConfigCacheIni::FConfigCacheIni(EConfigCacheType InType)
	: bAreFileOperationsDisabled(false)
	, bIsReadyForUse(false)
	, Type(InType)
{
}

FConfigCacheIni::FConfigCacheIni()
{
	EnsureRetrievingVTablePtrDuringCtor(TEXT("FConfigCacheIni()"));
}

FConfigCacheIni::~FConfigCacheIni()
{
	Flush( 1 );
}

FConfigFile* FConfigCacheIni::FindConfigFile( const FString& Filename )
{
	return TMap<FString,FConfigFile>::Find( Filename );
}

FConfigFile* FConfigCacheIni::Find( const FString& Filename, bool CreateIfNotFound )
{	
	// check for non-filenames
	if(Filename.Len() == 0)
	{
		return nullptr;
	}

	// Get file.
	FConfigFile* Result = TMap<FString,FConfigFile>::Find( Filename );
	// this is || filesize so we load up .int files if file IO is allowed
	if( !Result && !bAreFileOperationsDisabled && (CreateIfNotFound || DoesConfigFileExistWrapper(*Filename) ) )
	{
		Result = &Add( Filename, FConfigFile() );
		Result->Read( Filename );
		UE_LOG(LogConfig, Verbose, TEXT( "GConfig::Find has loaded file:  %s" ), *Filename );
	}
	return Result;
}

FConfigFile* FConfigCacheIni::FindConfigFileWithBaseName(FName BaseName)
{
	for (TPair<FString,FConfigFile>& CurrentFilePair : *this)
	{
		if (CurrentFilePair.Value.Name == BaseName)
		{
			return &CurrentFilePair.Value;
		}
	}
	return nullptr;
}

bool FConfigCacheIni::ContainsConfigFile(const FConfigFile* ConfigFile) const
{
	// Check the normal inis. Note that the FConfigFiles in the map
	// could have been reallocated if new inis were added to the map
	// since the point at which the caller received the ConfigFile pointer
	// they are testing. It is the caller's responsibility to not try to hold
	// on to the ConfigFile pointer during writes to this ConfigCacheIni
	for (const TPair<FString, FConfigFile>& CurrentFilePair : *this)
	{
		if (ConfigFile == &CurrentFilePair.Value)
		{
			return true;
		}
	}

	return false;
}

void FConfigCacheIni::Flush( bool Read, const FString& Filename )
{
	// never Flush temporary cache objects
	if (Type == EConfigCacheType::Temporary)
	{
		return;
	}

	// write out the files if we can
	if (!bAreFileOperationsDisabled)
	{
		for (TIterator It(*this); It; ++It)
		{
			if (Filename.Len() == 0 || It.Key()==Filename)
			{
				It.Value().Write(*It.Key());
			}
		}
	}
	if( Read )
	{
		// we can't read it back in if file operations are disabled
		if (bAreFileOperationsDisabled)
		{
			UE_LOG(LogConfig, Warning, TEXT("Tried to flush the config cache and read it back in, but File Operations are disabled!!"));
			return;
		}

		if (Filename.Len() != 0)
		{
			Remove(Filename);
		}
		else
		{
			Empty();
		}
	}
}

/**
 * Disables any file IO by the config cache system
 */
void FConfigCacheIni::DisableFileOperations()
{
	bAreFileOperationsDisabled = true;
}

/**
 * Re-enables file IO by the config cache system
 */
void FConfigCacheIni::EnableFileOperations()
{
	bAreFileOperationsDisabled = false;
}

/**
 * Returns whether or not file operations are disabled
 */
bool FConfigCacheIni::AreFileOperationsDisabled()
{
	return bAreFileOperationsDisabled;
}

/**
 * Parses apart an ini section that contains a list of 1-to-N mappings of names in the following format
 *	 [PerMapPackages]
 *	 .MapName1=Map1
 *	 .Package1=PackageA
 *	 .Package1=PackageB
 *	 .MapName2=Map2
 *	 .Package2=PackageC
 *	 .Package2=PackageD
 * 
 * @param Section Name of section to look in
 * @param KeyOne Key to use for the 1 in the 1-to-N (MapName in the above example - the number suffix gets ignored but helps to keep ordering)
 * @param KeyN Key to use for the N in the 1-to-N (Package in the above example - the number suffix gets ignored but helps to keep ordering)
 * @param OutMap Map containing parsed results
 * @param Filename Filename to use to find the section
 *
 * NOTE: The function naming is weird because you can't apparently have an overridden function differnt only by template type params
 */
void FConfigCacheIni::Parse1ToNSectionOfNames(const TCHAR* Section, const TCHAR* KeyOne, const TCHAR* KeyN, TMap<FName, TArray<FName> >& OutMap, const FString& Filename)
{
	// find the config file object
	FConfigFile* ConfigFile = Find(Filename, 0);
	if (!ConfigFile)
	{
		return;
	}

	// find the section in the file
	FConfigSectionMap* ConfigSection = ConfigFile->Find(Section);
	if (!ConfigSection)
	{
		return;
	}

	TArray<FName>* WorkingList = nullptr;
	for( FConfigSectionMap::TIterator It(*ConfigSection); It; ++It )
	{
		// is the current key the 1 key?
		if (It.Key().ToString().StartsWith(KeyOne))
		{
			const FName KeyName(*It.Value().GetValue());

			// look for existing set in the map
			WorkingList = OutMap.Find(KeyName);

			// make a new one if it wasn't there
			if (WorkingList == nullptr)
			{
				WorkingList = &OutMap.Add(KeyName, TArray<FName>());
			}
		}
		// is the current key the N key?
		else if (It.Key().ToString().StartsWith(KeyN) && WorkingList != nullptr)
		{
			// if so, add it to the N list for the current 1 key
			WorkingList->Add(FName(*It.Value().GetValue()));
		}
		// if it's neither, then reset
		else
		{
			WorkingList = nullptr;
		}
	}
}

/**
 * Parses apart an ini section that contains a list of 1-to-N mappings of strings in the following format
 *	 [PerMapPackages]
 *	 .MapName1=Map1
 *	 .Package1=PackageA
 *	 .Package1=PackageB
 *	 .MapName2=Map2
 *	 .Package2=PackageC
 *	 .Package2=PackageD
 * 
 * @param Section Name of section to look in
 * @param KeyOne Key to use for the 1 in the 1-to-N (MapName in the above example - the number suffix gets ignored but helps to keep ordering)
 * @param KeyN Key to use for the N in the 1-to-N (Package in the above example - the number suffix gets ignored but helps to keep ordering)
 * @param OutMap Map containing parsed results
 * @param Filename Filename to use to find the section
 *
 * NOTE: The function naming is weird because you can't apparently have an overridden function differnt only by template type params
 */
void FConfigCacheIni::Parse1ToNSectionOfStrings(const TCHAR* Section, const TCHAR* KeyOne, const TCHAR* KeyN, TMap<FString, TArray<FString> >& OutMap, const FString& Filename)
{
	// find the config file object
	FConfigFile* ConfigFile = Find(Filename, 0);
	if (!ConfigFile)
	{
		return;
	}

	// find the section in the file
	FConfigSectionMap* ConfigSection = ConfigFile->Find(Section);
	if (!ConfigSection)
	{
		return;
	}

	TArray<FString>* WorkingList = nullptr;
	for( FConfigSectionMap::TIterator It(*ConfigSection); It; ++It )
	{
		// is the current key the 1 key?
		if (It.Key().ToString().StartsWith(KeyOne))
		{
			// look for existing set in the map
			WorkingList = OutMap.Find(It.Value().GetValue());

			// make a new one if it wasn't there
			if (WorkingList == nullptr)
			{
				WorkingList = &OutMap.Add(It.Value().GetValue(), TArray<FString>());
			}
		}
		// is the current key the N key?
		else if (It.Key().ToString().StartsWith(KeyN) && WorkingList != nullptr)
		{
			// if so, add it to the N list for the current 1 key
			WorkingList->Add(It.Value().GetValue());
		}
		// if it's neither, then reset
		else
		{
			WorkingList = nullptr;
		}
	}
}

void FConfigCacheIni::LoadFile( const FString& Filename, const FConfigFile* Fallback, const TCHAR* PlatformString )
{
	// if the file has some data in it, read it in
	if( !IsUsingLocalIniFile(*Filename, nullptr) || DoesConfigFileExistWrapper(*Filename) )
	{
		FConfigFile* Result = &Add(Filename, FConfigFile());
		bool bDoEmptyConfig = false;
		bool bDoCombine = false;
		ProcessIniContents(*Filename, *Filename, Result, bDoEmptyConfig, bDoCombine);
		UE_LOG(LogConfig, Verbose, TEXT( "GConfig::LoadFile has loaded file:  %s" ), *Filename);
	}
	else if( Fallback )
	{
		Add( *Filename, *Fallback );
		UE_LOG(LogConfig, Verbose, TEXT( "GConfig::LoadFile associated file:  %s" ), *Filename);
	}
	else
	{
		UE_LOG(LogConfig, Warning, TEXT( "FConfigCacheIni::LoadFile failed loading file as it was 0 size.  Filename was:  %s" ), *Filename);
	}

	// Avoid memory wasted in array slack.
	Shrink();
}


void FConfigCacheIni::SetFile( const FString& Filename, const FConfigFile* NewConfigFile )
{
	Add(Filename, *NewConfigFile);
}


void FConfigCacheIni::UnloadFile(const FString& Filename)
{
	FConfigFile* File = Find(Filename, 0);
	if( File )
		Remove( Filename );
}

void FConfigCacheIni::Detach(const FString& Filename)
{
	FConfigFile* File = Find(Filename, 1);
	if( File )
		File->NoSave = 1;
}

bool FConfigCacheIni::GetString( const TCHAR* Section, const TCHAR* Key, FString& Value, const FString& Filename )
{
	FRemoteConfig::Get()->FinishRead(*Filename); // Ensure the remote file has been loaded and processed
	FConfigFile* File = Find( Filename, 0 );
	if( !File )
	{
		return false;
	}
	FConfigSection* Sec = File->Find( Section );
	if( !Sec )
	{
#if !UE_BUILD_SHIPPING
		CheckLongSectionNames( Section, File );
#endif
		return false;
	}
	const FConfigValue* ConfigValue = Sec->Find( Key );
	if( !ConfigValue )
	{
		return false;
	}
	Value = ConfigValue->GetValue();

	FCoreDelegates::OnConfigValueRead.Broadcast(*Filename, Section, Key);

	return true;
}

bool FConfigCacheIni::GetText( const TCHAR* Section, const TCHAR* Key, FText& Value, const FString& Filename )
{
	FRemoteConfig::Get()->FinishRead(*Filename); // Ensure the remote file has been loaded and processed
	FConfigFile* File = Find( Filename, 0 );
	if( !File )
	{
		return false;
	}
	FConfigSection* Sec = File->Find( Section );
	if( !Sec )
	{
#if !UE_BUILD_SHIPPING
		CheckLongSectionNames( Section, File );
#endif
		return false;
	}
	const FConfigValue* ConfigValue = Sec->Find( Key );
	if( !ConfigValue )
	{
		return false;
	}
	if (FTextStringHelper::ReadFromBuffer(*ConfigValue->GetValue(), Value, Section) == nullptr)
	{
		return false;
	}

	FCoreDelegates::OnConfigValueRead.Broadcast(*Filename, Section, Key);

	return true;
}

bool FConfigCacheIni::GetSection( const TCHAR* Section, TArray<FString>& Result, const FString& Filename )
{
	FRemoteConfig::Get()->FinishRead(*Filename); // Ensure the remote file has been loaded and processed
	Result.Reset();
	FConfigFile* File = Find( Filename, false );
	if (!File)
	{
		return false;
	}
	FConfigSection* Sec = File->Find( Section );
	if (!Sec)
	{
		return false;
	}
	Result.Reserve(Sec->Num());
	for (FConfigSection::TIterator It(*Sec); It; ++It)
	{
		Result.Add(FString::Printf(TEXT("%s=%s"), *It.Key().ToString(), *It.Value().GetValue()));
	}

	FCoreDelegates::OnConfigSectionRead.Broadcast(*Filename, Section);

	return true;
}

FConfigSection* FConfigCacheIni::GetSectionPrivate( const TCHAR* Section, const bool Force, const bool Const, const FString& Filename )
{
	FRemoteConfig::Get()->FinishRead(*Filename); // Ensure the remote file has been loaded and processed
	FConfigFile* File = Find( Filename, Force );
	if (!File)
	{
		return nullptr;
	}
	FConfigSection* Sec = File->Find( Section );
	if (!Sec && Force)
	{
		Sec = &File->Add(Section, FConfigSection());
	}
	if (Sec && (Force || !Const))
	{
		File->Dirty = true;
	}

	if (Sec)
	{
		FCoreDelegates::OnConfigSectionRead.Broadcast(*Filename, Section);
	}

	return Sec;
}

bool FConfigCacheIni::DoesSectionExist(const TCHAR* Section, const FString& Filename)
{
	bool bReturnVal = false;

	FRemoteConfig::Get()->FinishRead(*Filename); // Ensure the remote file has been loaded and processed
	FConfigFile* File = Find(Filename, false);

	bReturnVal = (File != nullptr && File->Find(Section) != nullptr);

	if (bReturnVal)
	{
		FCoreDelegates::OnConfigSectionNameRead.Broadcast(*Filename, Section);
	}

	return bReturnVal;
}

void FConfigCacheIni::SetString( const TCHAR* Section, const TCHAR* Key, const TCHAR* Value, const FString& Filename )
{
	FConfigFile* File = Find( Filename, 1 );

	if ( !File )
	{
		return;
	}

	FConfigSection* Sec = File->FindOrAddSection( Section );

	FConfigValue* ConfigValue = Sec->Find( Key );
	if( !ConfigValue )
	{
		Sec->Add( Key, Value );
		File->Dirty = true;
	}
	// Use GetSavedValueForWriting rather than GetSavedValue to avoid having the is-it-dirty query mark the values as having been accessed for dependency tracking
	else if( FCString::Strcmp(*UE::ConfigCacheIni::Private::FAccessor::GetSavedValueForWriting(*ConfigValue),Value)!=0 )
	{
		File->Dirty = true;
		*ConfigValue = FConfigValue(Value);
	}
}

void FConfigCacheIni::SetText( const TCHAR* Section, const TCHAR* Key, const FText& Value, const FString& Filename )
{
	FConfigFile* File = Find( Filename, 1 );

	if ( !File )
	{
		return;
	}

	FConfigSection* Sec = File->FindOrAddSection( Section );

	FString StrValue;
	FTextStringHelper::WriteToBuffer(StrValue, Value);

	FConfigValue* ConfigValue = Sec->Find( Key );
	if( !ConfigValue )
	{
		Sec->Add( Key, StrValue );
		File->Dirty = true;
	}
	// Use GetSavedValueForWriting rather than GetSavedValue to avoid having the is-it-dirty query mark the values as having been accessed for dependency tracking
	else if( FCString::Strcmp(*UE::ConfigCacheIni::Private::FAccessor::GetSavedValueForWriting(*ConfigValue), *StrValue)!=0 )
	{
		File->Dirty = true;
		*ConfigValue = FConfigValue(StrValue);
	}
}

bool FConfigCacheIni::RemoveKey( const TCHAR* Section, const TCHAR* Key, const FString& Filename )
{
	FConfigFile* File = Find( Filename, 1 );
	if( File )
	{
		FConfigSection* Sec = File->Find( Section );
		if( Sec )
		{
			if( Sec->Remove(Key) > 0 )
			{
				File->Dirty = 1;
				return true;
			}
		}
	}
	return false;
}

bool FConfigCacheIni::EmptySection( const TCHAR* Section, const FString& Filename )
{
	FConfigFile* File = Find( Filename, 0 );
	if( File )
	{
		FConfigSection* Sec = File->Find( Section );
		// remove the section name if there are no more properties for this section
		if( Sec )
		{
			if ( FConfigSection::TIterator(*Sec) )
			{
				Sec->Empty();
			}
			File->Remove(Section);
			if (bAreFileOperationsDisabled == false)
			{
				if (File->Num())
				{
					File->Dirty = 1;
					Flush(0, Filename);
				}
				else
				{
					IFileManager::Get().Delete(*Filename);	
				}
			}
			return true;
		}
	}
	return false;
}

bool FConfigCacheIni::EmptySectionsMatchingString( const TCHAR* SectionString, const FString& Filename )
{
	bool bEmptied = false;
	FConfigFile* File = Find( Filename, 0 );
	if (File)
	{
		bool bSaveOpsDisabled = bAreFileOperationsDisabled;
		bAreFileOperationsDisabled = true;
		for (FConfigFile::TIterator It(*File); It; ++It)
		{
			if (It.Key().Contains(SectionString) )
			{
				bEmptied |= EmptySection(*(It.Key()), Filename);
			}
		}
		bAreFileOperationsDisabled = bSaveOpsDisabled;
	}
	return bEmptied;
}

/**
 * Retrieve a list of all of the config files stored in the cache
 *
 * @param ConfigFilenames Out array to receive the list of filenames
 */
void FConfigCacheIni::GetConfigFilenames(TArray<FString>& ConfigFilenames)
{
	// copy from our map to the array
	for (FConfigCacheIni::TIterator It(*this); It; ++It)
	{
		ConfigFilenames.Add(*(It.Key()));
	}
}

/**
 * Retrieve the names for all sections contained in the file specified by Filename
 *
 * @param	Filename			the file to retrieve section names from
 * @param	out_SectionNames	will receive the list of section names
 *
 * @return	true if the file specified was successfully found;
 */
bool FConfigCacheIni::GetSectionNames( const FString& Filename, TArray<FString>& out_SectionNames )
{
	bool bResult = false;

	FConfigFile* File = Find(Filename, false);
	if ( File != nullptr )
	{
		out_SectionNames.Empty(Num());
		for ( FConfigFile::TIterator It(*File); It; ++It )
		{
			out_SectionNames.Add(It.Key());

			FCoreDelegates::OnConfigSectionNameRead.Broadcast(*Filename, *It.Key());
		}
		bResult = true;
	}

	return bResult;
}

/**
 * Retrieve the names of sections which contain data for the specified PerObjectConfig class.
 *
 * @param	Filename			the file to retrieve section names from
 * @param	SearchClass			the name of the PerObjectConfig class to retrieve sections for.
 * @param	out_SectionNames	will receive the list of section names that correspond to PerObjectConfig sections of the specified class
 * @param	MaxResults			the maximum number of section names to retrieve
 *
 * @return	true if the file specified was found and it contained at least 1 section for the specified class
 */
bool FConfigCacheIni::GetPerObjectConfigSections( const FString& Filename, const FString& SearchClass, TArray<FString>& out_SectionNames, int32 MaxResults )
{
	bool bResult = false;

	MaxResults = FMath::Max(0, MaxResults);
	FConfigFile* File = Find(Filename, false);
	if ( File != nullptr )
	{
		out_SectionNames.Empty();
		for ( FConfigFile::TIterator It(*File); It && out_SectionNames.Num() < MaxResults; ++It )
		{
			const FString& SectionName = It.Key();
			
			// determine whether this section corresponds to a PerObjectConfig section
			int32 POCClassDelimiter = SectionName.Find(TEXT(" "));
			if ( POCClassDelimiter != INDEX_NONE )
			{
				// the section name contained a space, which for now we'll assume means that we've found a PerObjectConfig section
				// see if the remainder of the section name matches the class name we're searching for
				if ( SectionName.Mid(POCClassDelimiter + 1) == SearchClass )
				{
					// found a PerObjectConfig section for the class specified - add it to the list
					out_SectionNames.Insert(SectionName,0);
					bResult = true;

					FCoreDelegates::OnConfigSectionNameRead.Broadcast(*Filename, *SectionName);
				}
			}
		}
	}

	return bResult;
}

void FConfigCacheIni::Exit()
{
	Flush( 1 );
}

void FConfigCacheIni::Dump(FOutputDevice& Ar, const TCHAR* BaseIniName)
{
	if (BaseIniName == nullptr)
	{
		Ar.Log( TEXT("Files map:") );
		TMap<FString,FConfigFile>::Dump( Ar );
	}

	for ( TIterator It(*this); It; ++It )
	{
		if (BaseIniName == nullptr || FPaths::GetBaseFilename(It.Key()) == BaseIniName)
		{
			Ar.Logf(TEXT("FileName: %s"), *It.Key());
			FConfigFile& File = It.Value();
			for ( FConfigFile::TIterator FileIt(File); FileIt; ++FileIt )
			{
				FConfigSection& Sec = FileIt.Value();
				Ar.Logf(TEXT("   [%s]"), *FileIt.Key());
				for (FConfigSectionMap::TConstIterator SecIt(Sec); SecIt; ++SecIt)
				{
					Ar.Logf(TEXT("   %s=%s"), *SecIt.Key().ToString(), *SecIt.Value().GetValue());
				}

				Ar.Log(LINE_TERMINATOR);
			}
		}
	}
}

// Derived functions.
FString FConfigCacheIni::GetStr( const TCHAR* Section, const TCHAR* Key, const FString& Filename )
{
	FString Result;
	GetString( Section, Key, Result, Filename );
	return Result;
}
bool FConfigCacheIni::GetInt
(
	const TCHAR*		Section,
	const TCHAR*		Key,
	int32&				Value,
	const FString&	Filename
)
{
	FString Text; 
	if( GetString( Section, Key, Text, Filename ) )
	{
		Value = FCString::Atoi(*Text);
		return 1;
	}
	return 0;
}
bool FConfigCacheIni::GetFloat
(
	const TCHAR*		Section,
	const TCHAR*		Key,
	float&				Value,
	const FString&	Filename
)
{
	FString Text; 
	if( GetString( Section, Key, Text, Filename ) )
	{
		Value = FCString::Atof(*Text);
		return 1;
	}
	return 0;
}
bool FConfigCacheIni::GetDouble
	(
	const TCHAR*		Section,
	const TCHAR*		Key,
	double&				Value,
	const FString&	Filename
	)
{
	FString Text; 
	if( GetString( Section, Key, Text, Filename ) )
	{
		Value = FCString::Atod(*Text);
		return 1;
	}
	return 0;
}


bool FConfigCacheIni::GetBool
(
	const TCHAR*		Section,
	const TCHAR*		Key,
	bool&				Value,
	const FString&	Filename
)
{
	FString Text; 
	if( GetString( Section, Key, Text, Filename ) )
	{
		Value = FCString::ToBool(*Text);
		return 1;
	}
	return 0;
}
int32 FConfigCacheIni::GetArray
(
	const TCHAR*		Section,
	const TCHAR*		Key,
	TArray<FString>&	out_Arr,
	const FString&	Filename
)
{
	FRemoteConfig::Get()->FinishRead(*Filename); // Ensure the remote file has been loaded and processed
	out_Arr.Empty();
	FConfigFile* File = Find( Filename, 0 );
	if ( File != nullptr )
	{
		File->GetArray(Section, Key, out_Arr);
	}

	if (out_Arr.Num())
	{
		FCoreDelegates::OnConfigValueRead.Broadcast(*Filename, Section, Key);
	}

	return out_Arr.Num();
}
/** Loads a "delimited" list of string
 * @param Section - Section of the ini file to load from
 * @param Key - The key in the section of the ini file to load
 * @param out_Arr - Array to load into
 * @param Delimiter - Break in the strings
 * @param Filename - Ini file to load from
 */
int32 FConfigCacheIni::GetSingleLineArray
(
	const TCHAR*		Section,
	const TCHAR*		Key,
	TArray<FString>&	out_Arr,
	const FString&	Filename
)
{
	FString FullString;
	bool bValueExisted = GetString(Section, Key, FullString, Filename);
	const TCHAR* RawString = *FullString;

	//tokenize the string into out_Arr
	FString NextToken;
	while ( FParse::Token(RawString, NextToken, false) )
	{
		new(out_Arr) FString(NextToken);
	}
	return bValueExisted;
}

bool FConfigCacheIni::GetColor
(
 const TCHAR*		Section,
 const TCHAR*		Key,
 FColor&			Value,
 const FString&	Filename
 )
{
	FString Text; 
	if( GetString( Section, Key, Text, Filename ) )
	{
		return Value.InitFromString(Text);
	}
	return false;
}

bool FConfigCacheIni::GetVector2D(
	const TCHAR*   Section,
	const TCHAR*   Key,
	FVector2D&     Value,
	const FString& Filename)
{
	FString Text;
	if (GetString(Section, Key, Text, Filename))
	{
		return Value.InitFromString(Text);
	}
	return false;
}


bool FConfigCacheIni::GetVector
(
 const TCHAR*		Section,
 const TCHAR*		Key,
 FVector&			Value,
 const FString&	Filename
 )
{
	FString Text; 
	if( GetString( Section, Key, Text, Filename ) )
	{
		return Value.InitFromString(Text);
	}
	return false;
}

bool FConfigCacheIni::GetVector4
(
 const TCHAR*		Section,
 const TCHAR*		Key,
 FVector4&			Value,
 const FString&	Filename
)
{
	FString Text;
	if(GetString(Section, Key, Text, Filename))
	{
		return Value.InitFromString(Text);
	}
	return false;
}

bool FConfigCacheIni::GetRotator
(
 const TCHAR*		Section,
 const TCHAR*		Key,
 FRotator&			Value,
 const FString&	Filename
 )
{
	FString Text; 
	if( GetString( Section, Key, Text, Filename ) )
	{
		return Value.InitFromString(Text);
	}
	return false;
}

void FConfigCacheIni::SetInt
(
	const TCHAR*	Section,
	const TCHAR*	Key,
	int32				Value,
	const FString&	Filename
)
{
	TCHAR Text[MAX_SPRINTF]=TEXT("");
	FCString::Sprintf( Text, TEXT("%i"), Value );
	SetString( Section, Key, Text, Filename );
}
void FConfigCacheIni::SetFloat
(
	const TCHAR*		Section,
	const TCHAR*		Key,
	float				Value,
	const FString&	Filename
)
{
	TCHAR Text[MAX_SPRINTF]=TEXT("");
	FCString::Sprintf( Text, TEXT("%f"), Value );
	SetString( Section, Key, Text, Filename );
}
void FConfigCacheIni::SetDouble
(
	const TCHAR*		Section,
	const TCHAR*		Key,
	double				Value,
	const FString&	Filename
)
{
	TCHAR Text[MAX_SPRINTF]=TEXT("");
	FCString::Sprintf( Text, TEXT("%f"), Value );
	SetString( Section, Key, Text, Filename );
}
void FConfigCacheIni::SetBool
(
	const TCHAR*		Section,
	const TCHAR*		Key,
	bool				Value,
	const FString&	Filename
)
{
	SetString( Section, Key, Value ? TEXT("True") : TEXT("False"), Filename );
}

void FConfigCacheIni::SetArray
(
	const TCHAR*			Section,
	const TCHAR*			Key,
	const TArray<FString>&	Value,
	const FString&		Filename
)
{
	FConfigFile* File = Find( Filename, 1 );
	if (!File)
	{
		return;
	}

	File->SetArray(Section, Key, Value);
}
/** Saves a "delimited" list of strings
 * @param Section - Section of the ini file to save to
 * @param Key - The key in the section of the ini file to save
 * @param In_Arr - Array to save from
 * @param Filename - Ini file to save to
 */
void FConfigCacheIni::SetSingleLineArray
(
	const TCHAR*			Section,
	const TCHAR*			Key,
	const TArray<FString>&	In_Arr,
	const FString&		Filename
)
{
	FString FullString;

	//append all strings to single string
	for (int32 i = 0; i < In_Arr.Num(); ++i)
	{
		FullString += In_Arr[i];
		FullString += TEXT(" ");
	}

	//save to ini file
	SetString(Section, Key, *FullString, Filename);
}

void FConfigCacheIni::SetColor
(
 const TCHAR*		Section,
 const TCHAR*		Key,
 const FColor		Value,
 const FString&	Filename
 )
{
	SetString( Section, Key, *Value.ToString(), Filename );
}

void FConfigCacheIni::SetVector2D(
	const TCHAR*   Section,
	const TCHAR*   Key,
	FVector2D      Value,
	const FString& Filename)
{
	SetString(Section, Key, *Value.ToString(), Filename);
}

void FConfigCacheIni::SetVector
(
 const TCHAR*		Section,
 const TCHAR*		Key,
 const FVector		 Value,
 const FString&	Filename
 )
{
	SetString( Section, Key, *Value.ToString(), Filename );
}

void FConfigCacheIni::SetVector4
(
 const TCHAR*		Section,
 const TCHAR*		Key,
 const FVector4&	 Value,
 const FString&	Filename
)
{
	SetString(Section, Key, *Value.ToString(), Filename);
}

void FConfigCacheIni::SetRotator
(
 const TCHAR*		Section,
 const TCHAR*		Key,
 const FRotator		Value,
 const FString&	Filename
 )
{
	SetString( Section, Key, *Value.ToString(), Filename );
}


/**
 * Archive for counting config file memory usage.
 */
class FArchiveCountConfigMem : public FArchive
{
public:
	FArchiveCountConfigMem()
	:	Num(0)
	,	Max(0)
	{
		ArIsCountingMemory = true;
	}
	SIZE_T GetNum()
	{
		return Num;
	}
	SIZE_T GetMax()
	{
		return Max;
	}
	void CountBytes( SIZE_T InNum, SIZE_T InMax )
	{
		Num += InNum;
		Max += InMax;
	}
protected:
	SIZE_T Num, Max;
};


/**
 * Tracks the amount of memory used by a single config or loc file
 */
struct FConfigFileMemoryData
{
	FString	ConfigFilename;
	SIZE_T		CurrentSize;
	SIZE_T		MaxSize;

	FConfigFileMemoryData( const FString& InFilename, SIZE_T InSize, SIZE_T InMax )
	: ConfigFilename(InFilename), CurrentSize(InSize), MaxSize(InMax)
	{}
};

/**
 * Tracks the memory data recorded for all loaded config files.
 */
struct FConfigMemoryData
{
	int32 NameIndent;
	int32 SizeIndent;
	int32 MaxSizeIndent;

	TArray<FConfigFileMemoryData> MemoryData;

	FConfigMemoryData()
	: NameIndent(0), SizeIndent(0), MaxSizeIndent(0)
	{}

	void AddConfigFile( const FString& ConfigFilename, FArchiveCountConfigMem& MemAr )
	{
		SIZE_T TotalMem = MemAr.GetNum();
		SIZE_T MaxMem = MemAr.GetMax();

		NameIndent = FMath::Max(NameIndent, ConfigFilename.Len());
		SizeIndent = FMath::Max(SizeIndent, FString::FromInt((int32)TotalMem).Len());
		MaxSizeIndent = FMath::Max(MaxSizeIndent, FString::FromInt((int32)MaxMem).Len());
		
		new(MemoryData) FConfigFileMemoryData( ConfigFilename, TotalMem, MaxMem );
	}

	void SortBySize()
	{
		struct FCompareFConfigFileMemoryData
		{
			FORCEINLINE bool operator()( const FConfigFileMemoryData& A, const FConfigFileMemoryData& B ) const
			{
				return ( B.CurrentSize == A.CurrentSize ) ? ( B.MaxSize < A.MaxSize ) : ( B.CurrentSize < A.CurrentSize );
			}
		};
		MemoryData.Sort( FCompareFConfigFileMemoryData() );
	}
};

/**
 * Dumps memory stats for each file in the config cache to the specified archive.
 *
 * @param	Ar	the output device to dump the results to
 */
void FConfigCacheIni::ShowMemoryUsage( FOutputDevice& Ar )
{
	FConfigMemoryData ConfigCacheMemoryData;

	for ( TIterator FileIt(*this); FileIt; ++FileIt )
	{
		FString Filename = FileIt.Key();
		FConfigFile& ConfigFile = FileIt.Value();

		FArchiveCountConfigMem MemAr;

		// count the bytes used for storing the filename
		MemAr << Filename;

		// count the bytes used for storing the array of SectionName->Section pairs
		MemAr << ConfigFile;
		
		ConfigCacheMemoryData.AddConfigFile(Filename, MemAr);
	}

	// add a little extra spacing between the columns
	ConfigCacheMemoryData.SizeIndent += 10;
	ConfigCacheMemoryData.MaxSizeIndent += 10;

	// record the memory used by the FConfigCacheIni's TMap
	FArchiveCountConfigMem MemAr;
	CountBytes(MemAr);

	SIZE_T TotalMemoryUsage=MemAr.GetNum();
	SIZE_T MaxMemoryUsage=MemAr.GetMax();

	Ar.Log(TEXT("Config cache memory usage:"));
	// print out the header
	Ar.Logf(TEXT("%*s %*s %*s"), ConfigCacheMemoryData.NameIndent, TEXT("FileName"), ConfigCacheMemoryData.SizeIndent, TEXT("NumBytes"), ConfigCacheMemoryData.MaxSizeIndent, TEXT("MaxBytes"));

	ConfigCacheMemoryData.SortBySize();
	for ( int32 Index = 0; Index < ConfigCacheMemoryData.MemoryData.Num(); Index++ )
	{
		FConfigFileMemoryData& ConfigFileMemoryData = ConfigCacheMemoryData.MemoryData[Index];
			Ar.Logf(TEXT("%*s %*u %*u"), 
			ConfigCacheMemoryData.NameIndent, *ConfigFileMemoryData.ConfigFilename,
			ConfigCacheMemoryData.SizeIndent, (uint32)ConfigFileMemoryData.CurrentSize,
			ConfigCacheMemoryData.MaxSizeIndent, (uint32)ConfigFileMemoryData.MaxSize);

		TotalMemoryUsage += ConfigFileMemoryData.CurrentSize;
		MaxMemoryUsage += ConfigFileMemoryData.MaxSize;
	}

	Ar.Logf(TEXT("%*s %*u %*u"), 
		ConfigCacheMemoryData.NameIndent, TEXT("Total"),
		ConfigCacheMemoryData.SizeIndent, (uint32)TotalMemoryUsage,
		ConfigCacheMemoryData.MaxSizeIndent, (uint32)MaxMemoryUsage);
}



SIZE_T FConfigCacheIni::GetMaxMemoryUsage()
{
	// record the memory used by the FConfigCacheIni's TMap
	FArchiveCountConfigMem MemAr;
	CountBytes(MemAr);

	SIZE_T TotalMemoryUsage=MemAr.GetNum();
	SIZE_T MaxMemoryUsage=MemAr.GetMax();


	FConfigMemoryData ConfigCacheMemoryData;

	for ( TIterator FileIt(*this); FileIt; ++FileIt )
	{
		FString Filename = FileIt.Key();
		FConfigFile& ConfigFile = FileIt.Value();

		FArchiveCountConfigMem FileMemAr;

		// count the bytes used for storing the filename
		FileMemAr << Filename;

		// count the bytes used for storing the array of SectionName->Section pairs
		FileMemAr << ConfigFile;

		ConfigCacheMemoryData.AddConfigFile(Filename, FileMemAr);
	}

	for ( int32 Index = 0; Index < ConfigCacheMemoryData.MemoryData.Num(); Index++ )
	{
		FConfigFileMemoryData& ConfigFileMemoryData = ConfigCacheMemoryData.MemoryData[Index];

		TotalMemoryUsage += ConfigFileMemoryData.CurrentSize;
		MaxMemoryUsage += ConfigFileMemoryData.MaxSize;
	}

	return MaxMemoryUsage;
}

bool FConfigCacheIni::ForEachEntry(const FKeyValueSink& Visitor, const TCHAR* Section, const FString& Filename)
{
	FConfigFile* File = Find(Filename, 0);
	if(!File)
	{
		return false;
	}

	FConfigSection* Sec = File->Find(Section);
	if(!Sec)
	{
		return false;
	}

	for(FConfigSectionMap::TIterator It(*Sec); It; ++It)
	{
		Visitor.Execute(*It.Key().GetPlainNameString(), *It.Value().GetValue());
	}

	return true;
}

/**
 * This will completely load a single .ini file into the passed in FConfigFile.
 *
 * @param FilenameToLoad - this is the path to the file to 
 * @param ConfigFile - This is the FConfigFile which will have the contents of the .ini loaded into
 *
 **/
static void LoadAnIniFile(const FString& FilenameToLoad, FConfigFile& ConfigFile)
{
	if( !IsUsingLocalIniFile(*FilenameToLoad, nullptr) || DoesConfigFileExistWrapper(*FilenameToLoad) )
	{
		ProcessIniContents(*FilenameToLoad, *FilenameToLoad, &ConfigFile, false, false);
	}
	else
	{
		//UE_LOG(LogConfig, Warning, TEXT( "LoadAnIniFile was unable to find FilenameToLoad: %s "), *FilenameToLoad);
	}
}

/**
 * This will load up two .ini files and then determine if the destination one is outdated.
 * Outdatedness is determined by the following mechanic:
 *
 * When a generated .ini is written out it will store the timestamps of the files it was generated
 * from.  This way whenever the Default__.inis are modified the Generated .ini will view itself as
 * outdated and regenerate it self.
 *
 * Outdatedness also can be affected by commandline params which allow one to delete all .ini, have
 * automated build system etc.
 *
 * @param DestConfigFile			The FConfigFile that will be filled out with the most up to date ini contents
 * @param DestIniFilename			The name of the destination .ini file - it's loaded and checked against source (e.g. Engine.ini)
 * @param SourceIniFilename			The name of the source .ini file (e.g. DefaultEngine.ini)
 */
static bool GenerateDestIniFile(FConfigFile& DestConfigFile, const FString& DestIniFilename, const FConfigFileHierarchy& SourceIniHierarchy, bool bAllowGeneratedINIs, const bool bUseHierarchyCache)
{
	bool bResult = LoadIniFileHierarchy(SourceIniHierarchy, *DestConfigFile.SourceConfigFile, bUseHierarchyCache);
	if ( bResult == false )
	{
		return false;
	}
	if (!FPlatformProperties::RequiresCookedData() || bAllowGeneratedINIs)
	{
		LoadAnIniFile(DestIniFilename, DestConfigFile);
	}

#if ALLOW_INI_OVERRIDE_FROM_COMMANDLINE
	// process any commandline overrides
	FConfigFile::OverrideFromCommandline(&DestConfigFile, DestIniFilename);
#endif

	bool bForceRegenerate = false;
	bool bShouldUpdate = FPlatformProperties::RequiresCookedData();

	// New versioning
	int32 SourceConfigVersionNum = -1;
	int32 CurrentIniVersion = -1;
	bool bVersionChanged = false;

	// Lambda for functionality that we can do in more than one place.
	auto RegenerateFileLambda = [](const FConfigFileHierarchy& InSourceIniHierarchy, FConfigFile& InDestConfigFile, const bool bInUseCache)
	{
		// Regenerate the file.
		bool bReturnValue = LoadIniFileHierarchy(InSourceIniHierarchy, InDestConfigFile, bInUseCache);
		if (InDestConfigFile.SourceConfigFile)
		{
			delete InDestConfigFile.SourceConfigFile;
			InDestConfigFile.SourceConfigFile = nullptr;
		}
		InDestConfigFile.SourceConfigFile = new FConfigFile(InDestConfigFile);

		// mark it as dirty (caller may want to save)
		InDestConfigFile.Dirty = true;

		return bReturnValue;
	};

	// Don't try to load any generated files from disk in cooked builds. We will always use the re-generated INIs.
	if (!FPlatformProperties::RequiresCookedData() || bAllowGeneratedINIs)
	{
		// We need to check if the user is using the version of the config system which had the entire contents of the coalesced
		// Source ini hierarchy output, if so we need to update, as it will cause issues with the new way we handle saved config files.
		
		// Legacy
		bool bIsLegacyConfigSystem = false;

		for ( TMap<FString,FConfigSection>::TConstIterator It(DestConfigFile); It; ++It )
		{
			FString SectionName = It.Key();
			if (SectionName == LegacyEngineString || SectionName == LegacyIniVersionString)
			{
				bIsLegacyConfigSystem = true;
				UE_LOG( LogInit, Warning, TEXT( "%s is out of date. It will be regenerated." ), *FPaths::ConvertRelativePathToFull(DestIniFilename) );
				break;
			}
			else if (SectionName == CurrentIniVersionString)
			{
				FConfigSection ConfigSectionIniVersion = It.Value();
				if (const FConfigValue* ConfigValue = ConfigSectionIniVersion.Find(VersionName))
				{
					FString VersionString = ConfigValue->GetSavedValue();
					CurrentIniVersion = FCString::Atoi(*VersionString);
				}
			}
		}

		// Test the version of the source config file to see if we should update.
		if (FConfigSection* SourceConfigSectionIniVersion = DestConfigFile.SourceConfigFile->Find(CurrentIniVersionString))
		{
			if (const FConfigValue* ConfigValue = SourceConfigSectionIniVersion->Find(VersionName))
			{
				FString VersionString = ConfigValue->GetSavedValue();
				SourceConfigVersionNum = FCString::Atoi(*VersionString);
				if (SourceConfigVersionNum > CurrentIniVersion)
				{
					UE_LOG(LogInit, Log, TEXT("%s version has been updated. It will be regenerated."), *FPaths::ConvertRelativePathToFull(DestIniFilename));
					bVersionChanged = true;
				}
				else if (SourceConfigVersionNum < CurrentIniVersion)
				{
					UE_LOG(LogInit, Warning, TEXT("%s version is later than the source. Since the versions are out of sync, nothing will be done."), *FPaths::ConvertRelativePathToFull(DestIniFilename));
				}
			}
		}

		// Regenerate the ini file?
		if (bIsLegacyConfigSystem || FParse::Param(FCommandLine::Get(), TEXT("REGENERATEINIS")) == true)
		{
			bForceRegenerate = true;
		}
		else if (FParse::Param(FCommandLine::Get(), TEXT("NOAUTOINIUPDATE")))
		{
			// Flag indicating whether the user has requested 'Yes/No To All'.
			static int32 GIniYesNoToAll = -1;
			// Make sure GIniYesNoToAll's 'uninitialized' value is kosher.
			static_assert(EAppReturnType::YesAll != -1, "EAppReturnType::YesAll must not be -1.");
			static_assert(EAppReturnType::NoAll != -1, "EAppReturnType::NoAll must not be -1.");

			// The file exists but is different.
			// Prompt the user if they haven't already responded with a 'Yes/No To All' answer.
			uint32 YesNoToAll;
			if (GIniYesNoToAll != EAppReturnType::YesAll && GIniYesNoToAll != EAppReturnType::NoAll)
			{
				YesNoToAll = FMessageDialog::Open(EAppMsgType::YesNoYesAllNoAll, FText::Format(NSLOCTEXT("Core", "IniFileOutOfDate", "Your ini ({0}) file is outdated. Do you want to automatically update it saving the previous version? Not doing so might cause crashes!"), FText::FromString(DestIniFilename)));
				// Record whether the user responded with a 'Yes/No To All' answer.
				if (YesNoToAll == EAppReturnType::YesAll || YesNoToAll == EAppReturnType::NoAll)
				{
					GIniYesNoToAll = YesNoToAll;
				}
			}
			else
			{
				// The user has already responded with a 'Yes/No To All' answer, so note it 
				// in the output arg so that calling code can operate on its value.
				YesNoToAll = GIniYesNoToAll;
			}
			// Regenerate the file if approved by the user.
			bShouldUpdate = (YesNoToAll == EAppReturnType::Yes) || (YesNoToAll == EAppReturnType::YesAll);
		}
		else
		{
			// If the version changes, we regenerate, so no need to do this.
			if (!bVersionChanged)
			{
				bShouldUpdate = true;
			}
		}
	}
	
	// Order is important, we want to let force regenerate happen before version change, in case we're trying to wipe everything.
	//	Version tries to save some info.
	if (DestConfigFile.Num() == 0 && DestConfigFile.SourceConfigFile->Num() == 0)
	{
		// If both are empty, don't save
		return false;
	}
	else if (bForceRegenerate)
	{
		bResult = RegenerateFileLambda(SourceIniHierarchy, DestConfigFile, bUseHierarchyCache);
	}
	else if (bVersionChanged)
	{
		// Clear out everything but the preserved sections with the properties in that section, then update the version.
		//	The ini syntax is Preserve=Section=<section name, like /Scipt/FortniteGame.FortConsole>.
		//	Go through and save the preserved sections before we regenerate the file. We'll re-add those after.
		FConfigSection PreservedConfigSectionData;
		if (FConfigSection* SourceConfigSectionIniVersion = DestConfigFile.SourceConfigFile->Find(CurrentIniVersionString))
		{
			for (FConfigSectionMap::TConstIterator ItSourceConfigSectionIniVersion(*SourceConfigSectionIniVersion); ItSourceConfigSectionIniVersion; ++ItSourceConfigSectionIniVersion)
			{
				if (ItSourceConfigSectionIniVersion.Key() == PreserveName)
				{
					PreservedConfigSectionData.Add(ItSourceConfigSectionIniVersion.Key(), ItSourceConfigSectionIniVersion.Value());
				}
			}
		}

		FConfigFile PreservedConfigFileData;
		for (FConfigSectionMap::TConstIterator ItPreservedConfigSectionData(PreservedConfigSectionData); ItPreservedConfigSectionData; ++ItPreservedConfigSectionData)
		{
			FString SectionString = ItPreservedConfigSectionData.Value().GetSavedValue();
			if (FConfigSection* FoundSection = DestConfigFile.Find(SectionString))
			{
				for (FConfigSectionMap::TConstIterator ItFoundSection(*FoundSection); ItFoundSection; ++ItFoundSection)
				{
					if (FConfigSection* CreatedSection = PreservedConfigFileData.FindOrAddSection(SectionString))
					{
						CreatedSection->Add(ItFoundSection.Key(), ItFoundSection.Value());
					}
				}
			}
		}

		// Remove everything before we regenerate.
		DestConfigFile.Empty();

		// Regnerate.
		bResult = RegenerateFileLambda(SourceIniHierarchy, DestConfigFile, bUseHierarchyCache);

		// Add back the CurrentIniVersion section.
		if (FConfigSection* DestConfigSectionIniVersion = DestConfigFile.FindOrAddSection(CurrentIniVersionString))
		{
			// Update the version. If it's already there then good but if not, we add it.
			DestConfigSectionIniVersion->FindOrAdd(VersionName, FConfigValue(FString::FromInt(SourceConfigVersionNum)));
		}

		// Add back any preserved sections.
		for (TMap<FString, FConfigSection>::TConstIterator ItPreservedConfigFileData(PreservedConfigFileData); ItPreservedConfigFileData; ++ItPreservedConfigFileData)
		{
			if (FConfigSection* DestConfigSectionPreserved = DestConfigFile.FindOrAddSection(ItPreservedConfigFileData.Key()))
			{
				FConfigSection PreservedConfigFileSection = ItPreservedConfigFileData.Value();
				for (FConfigSectionMap::TConstIterator ItPreservedConfigFileSection(PreservedConfigFileSection); ItPreservedConfigFileSection; ++ItPreservedConfigFileSection)
				{
					DestConfigSectionPreserved->Add(ItPreservedConfigFileSection.Key(), ItPreservedConfigFileSection.Value());
				}
			}
		}
	}
	else if (bShouldUpdate)
	{
		// Merge the .ini files by copying over properties that exist in the default .ini but are
		// missing from the generated .ini
		// NOTE: Most of the time there won't be any properties to add here, since LoadAnIniFile will
		//		 combine properties in the Default .ini with those in the Project .ini
		DestConfigFile.AddMissingProperties(*DestConfigFile.SourceConfigFile);

		// mark it as dirty (caller may want to save)
		DestConfigFile.Dirty = true;
	}
		
	if (!IsUsingLocalIniFile(*DestIniFilename, nullptr))
	{
		// Save off a copy of the local file prior to overwriting it with the contents of a remote file
		MakeLocalCopy(*DestIniFilename);
	}

	return bResult;
}

/**
 * Allows overriding the (default) .ini file for a given base (ie Engine, Game, etc)
 */
static void ConditionalOverrideIniFilename(FString& IniFilename, const TCHAR* BaseIniName)
{
#if !UE_BUILD_SHIPPING
	// Figure out what to look for on the commandline for an override. Disabled in shipping builds for security reasons
	const FString CommandLineSwitch = FString::Printf(TEXT("DEF%sINI="), BaseIniName);	
	FParse::Value(FCommandLine::Get(), *CommandLineSwitch, IniFilename);
#endif
}


static FString PerformBasicReplacements(const FString& InString, const TCHAR* BaseIniName)
{
	FString OutString = InString.Replace(TEXT("{TYPE}"), BaseIniName, ESearchCase::CaseSensitive);
	OutString = OutString.Replace(TEXT("{USERSETTINGS}"), FPlatformProcess::UserSettingsDir(), ESearchCase::CaseSensitive);
	OutString = OutString.Replace(TEXT("{USER}"), FPlatformProcess::UserDir(), ESearchCase::CaseSensitive);

	return OutString;
}


static FString PerformExpansionReplacements(const FConfigLayerExpansion& Expansion, const FString& InString)
{
	// if there's replacement to do, the output is just the output
	if (Expansion.Before1 == nullptr)
	{
		return InString;
	}

	// if nothing to replace, then skip it entirely
	if (!InString.Contains(Expansion.Before1) && (Expansion.Before2 == nullptr || !InString.Contains(Expansion.Before2)))
	{
		return TEXT("");
	}

	// replace the directory bits
	FString OutString = InString.Replace(Expansion.Before1, Expansion.After1, ESearchCase::CaseSensitive);
	if (Expansion.Before2 != nullptr)
	{
		OutString = OutString.Replace(Expansion.Before2, Expansion.After2, ESearchCase::CaseSensitive);
	}
	return OutString;
}

static FString PerformFinalExpansions(const FString InString, const FString& PlatformName, const TCHAR* EngineConfigDir, const TCHAR* SourceConfigDir)
{
	static FString LastPlatform;
	static FString PlatformExtensionEngineDir;
	static FString PlatformExtensionProjectDir;
	static FString ProjectNotForLicenseesDir;
	static FString ProjectNoRedistDir;

	if (LastPlatform != PlatformName)
	{
		LastPlatform = PlatformName;
		PlatformExtensionEngineDir = FPaths::Combine(*FPaths::EnginePlatformExtensionsDir(), *PlatformName);
		PlatformExtensionProjectDir = FPaths::Combine(*FPaths::ProjectPlatformExtensionsDir(), *PlatformName);
	}

	// cache some slow operations
	if (ProjectNotForLicenseesDir.Len() == 0)
	{
		if (FPaths::IsUnderDirectory(FPaths::ProjectDir(), FPaths::EngineDir()))
		{
			FString RelativeDir = FPaths::ProjectDir();
			FPaths::MakePathRelativeTo(RelativeDir, *FPaths::EngineDir());
			ProjectNotForLicenseesDir = FPaths::Combine(FPaths::EngineDir(), TEXT("Restricted/NotForLicensees"), RelativeDir);
			ProjectNoRedistDir = FPaths::Combine(FPaths::EngineDir(), TEXT("Restricted/NoRedist"), RelativeDir);
		}
		else
		{
			ProjectNotForLicenseesDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Restricted/NotForLicensees"));
			ProjectNoRedistDir = FPaths::Combine(FPaths::ProjectDir(), TEXT("Restricted/NoRedist"));
		}
	}

	FString OutString = InString.Replace(TEXT("{ENGINE}"), EngineConfigDir);
	OutString = OutString.Replace(TEXT("{EXTENGINE}"), *PlatformExtensionEngineDir);
	OutString = OutString.Replace(TEXT("{PROJECT}"), SourceConfigDir);
	OutString = OutString.Replace(TEXT("{EXTPROJECT}"), *PlatformExtensionProjectDir);
	OutString = OutString.Replace(TEXT("{PLATFORM}"), *PlatformName);
	OutString = OutString.Replace(TEXT("{RESTRICTEDPROJECT_NFL}"), *ProjectNotForLicenseesDir);
	OutString = OutString.Replace(TEXT("{RESTRICTEDPROJECT_NR}"), *ProjectNoRedistDir);

	return OutString;
}

/**
 * Creates a chain of ini filenames to load and combine.
 *
 * @param InBaseIniName Ini name.
 * @param InPlatformName Platform name, nullptr means to use the current platform
 * @param OutHierarchy An array which is to receive the generated hierachy of ini filenames.
 */
void FConfigFile::AddStaticLayersToHierarchy(const TCHAR* InBaseIniName, const TCHAR* InPlatformName, const TCHAR* EngineConfigDir, const TCHAR* SourceConfigDir)
{
	SourceEngineConfigDir = EngineConfigDir;
	SourceProjectConfigDir = SourceConfigDir;

	// for the replacement, we need to have a directory called Config - or we will have to do extra processing for these non-standard cases
	check(SourceEngineConfigDir.EndsWith(TEXT("Config/")));
	check(SourceProjectConfigDir.EndsWith(TEXT("Config/")));

	FString UsedEngineDir = FPaths::GetPath(FPaths::GetPath(SourceEngineConfigDir));
	FString UsedProjectDir = FPaths::GetPath(FPaths::GetPath(SourceProjectConfigDir));

	// get the platform name
	const FString PlatformName(InPlatformName ? InPlatformName : ANSI_TO_TCHAR(FPlatformProperties::IniPlatformName()));

	// string that can have a reference to it, lower down
	const FString DedicatedServerString = IsRunningDedicatedServer() ? TEXT("DedicatedServer") : TEXT("");

	// cache some platform extension information that can be used inside the loops

	// go over all the config layers
	for (int32 LayerIndex = 0; LayerIndex < UE_ARRAY_COUNT(GConfigLayers); LayerIndex++)
	{
		const FConfigLayer& Layer = GConfigLayers[LayerIndex];

		// start replacing basic variables
		FString LayerPath = PerformBasicReplacements(Layer.Path, InBaseIniName);
		bool bHasPlatformTag = LayerPath.Contains(TEXT("{PLATFORM}"));

		// PROGRAMs don't require any ini files
#if IS_PROGRAM
		const bool bIsRequired = false;
#else
		const bool bIsRequired = EnumHasAnyFlags(Layer.Flag, EConfigLayerFlags::Required) && (EngineConfigDir == FPaths::EngineConfigDir());
#endif

		// expand if it it has {ED} or {EF} expansion tags
		if (!EnumHasAnyFlags(Layer.Flag, EConfigLayerFlags::NoExpand))
		{
			// we assume none of the more special tags in expanded ones
			checkfSlow(FCString::Strstr(Layer.Path, TEXT("{USERSETTINGS}")) == nullptr && FCString::Strstr(Layer.Path, TEXT("{USER}")) == nullptr, TEXT("Expanded config %s shouldn't have a {USER*} tags in it"), *Layer.Path);
			// last layer is expected to not be expanded
			checkfSlow(LayerIndex < UE_ARRAY_COUNT(GConfigLayers) - 1, TEXT("Final layer %s shouldn't be an expansion layer, as it needs to generate the  hierarchy cache key"), *Layer.Path);

			// loop over all the possible expansions
			for (int32 ExpansionIndex = 0; ExpansionIndex < UE_ARRAY_COUNT(GConfigExpansions); ExpansionIndex++)
			{
				FString ExpandedPath = PerformExpansionReplacements(GConfigExpansions[ExpansionIndex], LayerPath);

				// if we didn't replace anything, skip it
				if (ExpandedPath.Len() == 0)
				{
					continue;
				}
				
				// allow for override, only on BASE EXPANSION!
				if (EnumHasAnyFlags(Layer.Flag, EConfigLayerFlags::AllowCommandLineOverride) && ExpansionIndex == 0)
				{
					checkfSlow(!bHasPlatformTag, TEXT("EConfigLayerFlags::AllowCommandLineOverride config %s shouldn't have a PLATFORM in it"), Layer.Path);

					ConditionalOverrideIniFilename(ExpandedPath, InBaseIniName);
				}

				// check if we should be generating the cache key - only at the end of all expansions
				bool bGenerateCacheKey = EnumHasAnyFlags(Layer.Flag, EConfigLayerFlags::GenerateCacheKey) && ExpansionIndex == UE_ARRAY_COUNT(GConfigExpansions) - 1;
				checkfSlow(!(bGenerateCacheKey && bHasPlatformTag), TEXT("EConfigLayerFlags::GenerateCacheKey shouldn't have a platform tag"));

				const FDataDrivenPlatformInfoRegistry::FPlatformInfo& Info = FDataDrivenPlatformInfoRegistry::GetPlatformInfo(PlatformName);

				// go over parents, and then this platform, unless there's no platform tag, then we simply want to run through the loop one time to add it to the
				int32 NumPlatforms = bHasPlatformTag ? Info.IniParentChain.Num() + 1 : 1;
				int32 CurrentPlatformIndex = NumPlatforms - 1;
				int32 DedicatedServerIndex = -1;
				
				// make DedicatedServer another platform
				if (bHasPlatformTag && IsRunningDedicatedServer())
				{
					NumPlatforms++;
					DedicatedServerIndex = CurrentPlatformIndex + 1;
				}

				check(NumPlatforms < MaxPlatformIndex);
				for (int PlatformIndex = 0; PlatformIndex < NumPlatforms; PlatformIndex++)
				{
					const FString& CurrentPlatform = 
						(PlatformIndex == DedicatedServerIndex) ? DedicatedServerString :
						(PlatformIndex == CurrentPlatformIndex) ? PlatformName : 
						Info.IniParentChain[PlatformIndex];

					FString PlatformPath = PerformFinalExpansions(ExpandedPath, CurrentPlatform, *UsedEngineDir, *UsedProjectDir);

					// @todo restricted - ideally, we would move DedicatedServer files into a directory, like platforms are, but for short term compat,
					// convert the path back to the original (DedicatedServer/DedicatedServerEngine.ini -> DedicatedServerEngine.ini)
					if (PlatformIndex == DedicatedServerIndex)
					{
						PlatformPath.ReplaceInline(TEXT("Config/DedicatedServer/"), TEXT("Config/"));
					}

					// add this to the list!
					SourceIniHierarchy.AddStaticLayer(
						FIniFilename(PlatformPath, bIsRequired, bGenerateCacheKey ?
							GenerateHierarchyCacheKey(SourceIniHierarchy, PlatformPath, InBaseIniName) :
							FString(TEXT(""))),
						LayerIndex, ExpansionIndex, PlatformIndex);
				}
			}
		}
		// if no expansion, just process the special tags (assume no PLATFORM tags)
		else
		{
			checkfSlow(!bHasPlatformTag, TEXT("Non-expanded config %s shouldn't have a PLATFORM in it"), *Layer.Path);
			checkfSlow(!EnumHasAnyFlags(Layer.Flag, EConfigLayerFlags::AllowCommandLineOverride), TEXT("Non-expanded config can't have a EConfigLayerFlags::AllowCommandLineOverride"));

			FString FinalPath = PerformFinalExpansions(LayerPath, TEXT(""), *UsedEngineDir, *UsedProjectDir);

			// final layer needs to generate a hierarchy cache
			SourceIniHierarchy.AddStaticLayer(
				FIniFilename(FinalPath, bIsRequired, EnumHasAnyFlags(Layer.Flag, EConfigLayerFlags::GenerateCacheKey) ?
					GenerateHierarchyCacheKey(SourceIniHierarchy, FinalPath, InBaseIniName) :
					FString(TEXT(""))),
				LayerIndex);
		}
	}
}

FString FConfigCacheIni::GetDestIniFilename(const TCHAR* BaseIniName, const TCHAR* PlatformName, const TCHAR* GeneratedConfigDir)
{
	// figure out what to look for on the commandline for an override
	FString CommandLineSwitch = FString::Printf(TEXT("%sINI="), BaseIniName);
	
	// if it's not found on the commandline, then generate it
	FString IniFilename;
	if (FParse::Value(FCommandLine::Get(), *CommandLineSwitch, IniFilename) == false)
	{
		FString Name(PlatformName ? PlatformName : ANSI_TO_TCHAR(FPlatformProperties::PlatformName()));

		// if the BaseIniName doens't contain the config dir, put it all together
		if (FCString::Stristr(BaseIniName, GeneratedConfigDir) != nullptr)
		{
			IniFilename = BaseIniName;
		}
		else
		{
			IniFilename = FString::Printf(TEXT("%s%s/%s.ini"), GeneratedConfigDir, *Name, BaseIniName);
		}
	}

	// standardize it!
	FPaths::MakeStandardFilename(IniFilename);
	return IniFilename;
}

void FConfigCacheIni::SaveCurrentStateForBootstrap(const TCHAR* Filename)
{
	TArray<uint8> FileContent;
	{
		// Use FMemoryWriter because FileManager::CreateFileWriter doesn't serialize FName as string and is not overridable
		FMemoryWriter MemoryWriter(FileContent, true);
		SerializeStateForBootstrap_Impl(MemoryWriter);
	}

	FFileHelper::SaveArrayToFile(FileContent, Filename);
}

void FConfigCacheIni::Serialize(FArchive& Ar)
{
	FConfigCacheIni::Super* Parent = static_cast<FConfigCacheIni::Super*>(this);
	Ar << *Parent;
	Ar << bAreFileOperationsDisabled;
	Ar << bIsReadyForUse;
	Ar << Type;
}

void FConfigCacheIni::SerializeStateForBootstrap_Impl(FArchive& Ar)
{
	// This implementation is meant to stay private and be used for 
	// bootstrapping another processes' config cache with a serialized state.
	// It doesn't include any versioning as it is used with the
	// the same binary executable for both the parent and 
	// children processes. It also takes care of saving/restoring
	// global ini variables.
	Serialize(Ar);
	Ar << GEditorIni;
	Ar << GEditorKeyBindingsIni;
	Ar << GEditorLayoutIni;
	Ar << GEditorSettingsIni;
	Ar << GEditorPerProjectIni;
	Ar << GCompatIni;
	Ar << GLightmassIni;
	Ar << GScalabilityIni;
	Ar << GHardwareIni;
	Ar << GInputIni;
	Ar << GGameIni;
	Ar << GGameUserSettingsIni;
	Ar << GRuntimeOptionsIni;
	Ar << GEngineIni;
}


void FConfigCacheIni::InitializePlatformConfigSystem(const TCHAR* PlatformName, FConfigNamesForAllPlatforms& FinalConfigFilenames)
{
#if WITH_EDITOR
	FString FinalConfigDir = FPaths::GeneratedConfigDir();

	FConfigCacheIni::LoadGlobalIniFile(FinalConfigFilenames.EngineIni, TEXT("Engine"), PlatformName, false, false, false, false, *FinalConfigDir, this);
	FConfigCacheIni::LoadGlobalIniFile(FinalConfigFilenames.GameIni, TEXT("Game"), PlatformName, false, false, false, false, *FinalConfigDir, this);
	FConfigCacheIni::LoadGlobalIniFile(FinalConfigFilenames.InputIni, TEXT("Input"), PlatformName, false, false, false, false, *FinalConfigDir, this);
	FConfigCacheIni::LoadGlobalIniFile(FinalConfigFilenames.ScalabilityIni, TEXT("Scalability"), PlatformName, false, false, false, false, *FinalConfigDir, this);
	FConfigCacheIni::LoadGlobalIniFile(FinalConfigFilenames.HardwareIni, TEXT("Hardware"), PlatformName, false, false, false, false, *FinalConfigDir, this);
	FConfigCacheIni::LoadGlobalIniFile(FinalConfigFilenames.RuntimeOptionsIni, TEXT("RuntimeOptions"), PlatformName, false, false, false, false, *FinalConfigDir, this);
	FConfigCacheIni::LoadGlobalIniFile(FinalConfigFilenames.InstallBundleIni, TEXT("InstallBundle"), PlatformName, false, false, false, false, *FinalConfigDir, this);
	FConfigCacheIni::LoadGlobalIniFile(FinalConfigFilenames.DeviceProfilesIni, TEXT("DeviceProfiles"), PlatformName, false, false, false, false, *FinalConfigDir, this);
	FConfigCacheIni::LoadGlobalIniFile(FinalConfigFilenames.GameUserSettingsIni, TEXT("GameUserSettings"), PlatformName, false, false, false, false, *FinalConfigDir, this);
	FConfigCacheIni::LoadGlobalIniFile(FinalConfigFilenames.GameplayTagsIni, TEXT("GameplayTags"), PlatformName, false, false, false, false, *FinalConfigDir, this);

	bIsReadyForUse = true;
#endif
}


bool FConfigCacheIni::CreateGConfigFromSaved(const TCHAR* Filename)
{
	SCOPED_BOOT_TIMING("FConfigCacheIni::CreateGConfigFromSaved");
	// get the already loaded file
	TArray<uint8> BinaryConfigData;
	FString BinaryConfigFile = FPaths::Combine(FPaths::SourceConfigDir(), TEXT("BinaryConfig.ini"));
	if (FFileHelper::LoadFileToArray(BinaryConfigData, *BinaryConfigFile) == false)
	{
		return false;
	}

	UE_LOG(LogInit, Display, TEXT("Loading binary GConfig...."));

	// serialize right out of the preloaded data
	FLargeMemoryReader MemoryReader((uint8*)BinaryConfigData.GetData(), BinaryConfigData.Num());
	FConfigNamesForAllPlatforms Names;
	GConfig = new FConfigCacheIni(EConfigCacheType::Temporary);

	// make an object that we can use to pass to delegates for any extra binary data they want to write
	FCoreDelegates::FExtraBinaryConfigData ExtraData(*GConfig, false);

	GConfig->Serialize(MemoryReader);

	// Forced to be disk backed so that GameUserSettings does get written out.
	GConfig->Type = EConfigCacheType::DiskBacked;
	MemoryReader << Names << ExtraData.Data;

	GEngineIni = Names.EngineIni;
	GGameIni = Names.GameIni;
	GInputIni = Names.InputIni;
	GScalabilityIni = Names.ScalabilityIni;
	GHardwareIni = Names.HardwareIni;
	GRuntimeOptionsIni = Names.RuntimeOptionsIni;
	GInstallBundleIni = Names.InstallBundleIni;
	GDeviceProfilesIni = Names.DeviceProfilesIni;
	GGameUserSettingsIni = Names.GameUserSettingsIni;
	GGameplayTagsIni = Names.GameplayTagsIni;

	// now let the delegates pull their data out, after GConfig is set up
	FCoreDelegates::AccessExtraBinaryConfigData.Broadcast(ExtraData);

	FCoreDelegates::ConfigReadyForUse.Broadcast();

	return true;
}

void FConfigCacheIni::InitializeConfigSystem()
{
#if PLATFORM_SUPPORTS_BINARYCONFIG
	// attempt to load from staged binary config data
	FString BinaryConfigFile = FPaths::Combine(FPaths::SourceConfigDir(), TEXT("BinaryConfig.ini"));
	FPlatformMisc::LowLevelOutputDebugStringf(TEXT("Looking for binary: %s\n"), *BinaryConfigFile);

	if (!FParse::Param(FCommandLine::Get(), TEXT("textconfig")) &&
		IFileManager::Get().FileExists(*BinaryConfigFile) &&
		FConfigCacheIni::CreateGConfigFromSaved(*BinaryConfigFile))
	{
		// Force reload GameUserSettings because they may be saved to disk on consoles/similar platforms
		// So the safest thing to do is to re-read the file after binary configs load.
		FConfigCacheIni::LoadGlobalIniFile(GGameUserSettingsIni, TEXT("GameUserSettings"));
		return;
	}
#endif

	// Bootstrap the Ini config cache
	FString IniBootstrapFilename;
	if (FParse::Value( FCommandLine::Get(), TEXT("IniBootstrap="), IniBootstrapFilename))
	{
		TArray<uint8> FileContent;
		if (FFileHelper::LoadFileToArray(FileContent, *IniBootstrapFilename, FILEREAD_Silent))
		{
			FMemoryReader MemoryReader(FileContent, true);
			GConfig = new FConfigCacheIni(EConfigCacheType::Temporary);
			GConfig->SerializeStateForBootstrap_Impl(MemoryReader);
			GConfig->bIsReadyForUse = true;
			FCoreDelegates::ConfigReadyForUse.Broadcast();
			return;
		}
		else
		{
			UE_LOG(LogInit, Display, TEXT("Unable to bootstrap from archive %s, will fallback on normal initialization"), *IniBootstrapFilename);
		}
	}

	UE_LOG(LogInit, Display, TEXT("Loading text-based GConfig...."));

	// Perform any upgrade we need before we load any configuration files
	FConfigManifest::UpgradeFromPreviousVersions();

	// create GConfig
	GConfig = new FConfigCacheIni(EConfigCacheType::DiskBacked);

	// load the main .ini files (unless we're running a program or a gameless UE4Editor.exe, DefaultEngine.ini is required).
	const bool bIsGamelessExe = !FApp::HasProjectName();
	const bool bDefaultEngineIniRequired = !bIsGamelessExe && (GIsGameAgnosticExe || FApp::IsProjectNameEmpty());
	bool bEngineConfigCreated = FConfigCacheIni::LoadGlobalIniFile(GEngineIni, TEXT("Engine"), nullptr, bDefaultEngineIniRequired);

	if ( !bIsGamelessExe )
	{
		// Now check and see if our game is correct if this is a game agnostic binary
		if (GIsGameAgnosticExe && !bEngineConfigCreated)
		{
			const FText AbsolutePath = FText::FromString( IFileManager::Get().ConvertToAbsolutePathForExternalAppForRead(*FPaths::GetPath(GEngineIni)) );
			//@todo this is too early to localize
			const FText Message = FText::Format( NSLOCTEXT("Core", "FirstCmdArgMustBeGameName", "'{0}' must exist and contain a DefaultEngine.ini."), AbsolutePath );
			if (!GIsBuildMachine)
			{
				FMessageDialog::Open(EAppMsgType::Ok, Message);
			}
			FApp::SetProjectName(TEXT("")); // this disables part of the crash reporter to avoid writing log files to a bogus directory
			if (!GIsBuildMachine)
			{
				exit(1);
			}
			UE_LOG(LogInit, Fatal,TEXT("%s"), *Message.ToString());
		}
	}

	FConfigCacheIni::LoadGlobalIniFile(GGameIni, TEXT("Game"));
	FConfigCacheIni::LoadGlobalIniFile(GInputIni, TEXT("Input"));
#if WITH_EDITOR
	// load some editor specific .ini files

	FConfigCacheIni::LoadGlobalIniFile(GEditorIni, TEXT("Editor"));

	// Upgrade editor user settings before loading the editor per project user settings
	FConfigManifest::MigrateEditorUserSettings();
	FConfigCacheIni::LoadGlobalIniFile(GEditorPerProjectIni, TEXT("EditorPerProjectUserSettings"));

	// Project agnostic editor ini files
	const FString EditorSettingsDir = FPaths::EngineEditorSettingsDir();
	FConfigCacheIni::LoadGlobalIniFile(GEditorSettingsIni, TEXT("EditorSettings"), nullptr, false, false, true, true, *EditorSettingsDir);
	FConfigCacheIni::LoadGlobalIniFile(GEditorKeyBindingsIni, TEXT("EditorKeyBindings"), nullptr, false, false, true, true, *EditorSettingsDir);
	FConfigCacheIni::LoadGlobalIniFile(GEditorLayoutIni, TEXT("EditorLayout"), nullptr, false, false, true, true, *EditorSettingsDir);

#endif
#if PLATFORM_DESKTOP
	// load some desktop only .ini files
	FConfigCacheIni::LoadGlobalIniFile(GCompatIni, TEXT("Compat"));
	FConfigCacheIni::LoadGlobalIniFile(GLightmassIni, TEXT("Lightmass"));
#endif

	// check for scalability platform override.
	const TCHAR* ScalabilityPlatformOverride = nullptr;
#if !UE_BUILD_SHIPPING && WITH_EDITOR
	FString ScalabilityPlatformOverrideCommandLine;
	FParse::Value(FCommandLine::Get(), TEXT("ScalabilityIniPlatformOverride="), ScalabilityPlatformOverrideCommandLine);
	ScalabilityPlatformOverride = ScalabilityPlatformOverrideCommandLine.Len() ? *ScalabilityPlatformOverrideCommandLine : nullptr;
#endif

	// Load scalability settings.
	FConfigCacheIni::LoadGlobalIniFile(GScalabilityIni, TEXT("Scalability"), ScalabilityPlatformOverride);
	// Load driver blacklist
	FConfigCacheIni::LoadGlobalIniFile(GHardwareIni, TEXT("Hardware"));	
	// Load runtime options
	FConfigCacheIni::LoadGlobalIniFile(GRuntimeOptionsIni, TEXT("RuntimeOptions"));
	// Load install bundle config
	FConfigCacheIni::LoadGlobalIniFile(GInstallBundleIni, TEXT("InstallBundle"));
	// Load device profiles for current platform
	FConfigCacheIni::LoadGlobalIniFile(GDeviceProfilesIni, TEXT("DeviceProfiles"));
	// Load gameplay tags
	FConfigCacheIni::LoadGlobalIniFile(GGameplayTagsIni, TEXT("GameplayTags"));



	// Load user game settings .ini, allowing merging. This also updates the user .ini if necessary.
	FConfigCacheIni::LoadGlobalIniFile(GGameUserSettingsIni, TEXT("GameUserSettings"));

	// now we can make use of GConfig
	GConfig->bIsReadyForUse = true;
	FCoreDelegates::ConfigReadyForUse.Broadcast();
}

bool FConfigCacheIni::LoadGlobalIniFile(FString& FinalIniFilename, const TCHAR* BaseIniName, const TCHAR* Platform, bool bForceReload, bool bRequireDefaultIni, bool bAllowGeneratedIniWhenCooked, bool bAllowRemoteConfig, const TCHAR* GeneratedConfigDir, FConfigCacheIni* ConfigSystem)
{
	// figure out where the end ini file is
	FinalIniFilename = GetDestIniFilename(BaseIniName, Platform, GeneratedConfigDir);
	
	// Start the loading process for the remote config file when appropriate
	if (FRemoteConfig::Get()->ShouldReadRemoteFile(*FinalIniFilename))
	{
		FRemoteConfig::Get()->Read(*FinalIniFilename, BaseIniName);
	}

	FRemoteConfigAsyncIOInfo* RemoteInfo = FRemoteConfig::Get()->FindConfig(*FinalIniFilename);
	if (RemoteInfo && (!RemoteInfo->bWasProcessed || !FRemoteConfig::Get()->IsFinished(*FinalIniFilename)))
	{
		// Defer processing this remote config file to until it has finish its IO operation
		return false;
	}

	// need to check to see if the file already exists in the GConfigManager's cache
	// if it does exist then we are done, nothing else to do
	if (!bForceReload && ConfigSystem->FindConfigFile(*FinalIniFilename) != nullptr)
	{
		//UE_LOG(LogConfig, Log,  TEXT( "Request to load a config file that was already loaded: %s" ), GeneratedIniFile );
		return true;
	}

	FString EngineConfigDir = FPaths::EngineConfigDir();
	FString SourceConfigDir = FPaths::SourceConfigDir();

	if (bForceReload) // If reloading we should preserve the existing config dirs
	{
		// If base ini, try to use an existing GConfig file to set the config directories instead of assuming defaults
		FConfigFile* BaseConfig = ConfigSystem->FindConfigFileWithBaseName(BaseIniName);
		if (BaseConfig)
		{
			if (BaseConfig->SourceEngineConfigDir.Len())
			{
				EngineConfigDir = BaseConfig->SourceEngineConfigDir;
			}

			if (BaseConfig->SourceProjectConfigDir.Len())
			{
				SourceConfigDir = BaseConfig->SourceProjectConfigDir;
			}
		}
	}

	// make a new entry in GConfig (overwriting what's already there)
	FConfigFile& NewConfigFile = ConfigSystem->Add(FinalIniFilename, FConfigFile());

	return LoadExternalIniFile(
		NewConfigFile, 
		BaseIniName, 
		*EngineConfigDir, 
		*SourceConfigDir, 
		/*bIsBaseIniName*/ true,
		Platform, 
		bForceReload, 
		/*bWriteDestIni*/ true,
		bAllowGeneratedIniWhenCooked, 
		GeneratedConfigDir);
}

bool FConfigCacheIni::LoadLocalIniFile(FConfigFile& ConfigFile, const TCHAR* IniName, bool bIsBaseIniName, const TCHAR* Platform, bool bForceReload )
{
	DECLARE_SCOPE_CYCLE_COUNTER( TEXT( "FConfigCacheIni::LoadLocalIniFile" ), STAT_FConfigCacheIni_LoadLocalIniFile, STATGROUP_LoadTime );

	FString EngineConfigDir = FPaths::EngineConfigDir();
	FString SourceConfigDir = FPaths::SourceConfigDir();

	if (bIsBaseIniName)
	{
		// If base ini, try to use an existing GConfig file to set the config directories instead of assuming defaults
		FConfigFile* BaseConfig = GConfig ? GConfig->FindConfigFileWithBaseName(IniName) : nullptr;
		if (BaseConfig)
		{
			if (BaseConfig->SourceEngineConfigDir.Len())
			{
				EngineConfigDir = BaseConfig->SourceEngineConfigDir;
			}

			if (BaseConfig->SourceProjectConfigDir.Len())
			{
				SourceConfigDir = BaseConfig->SourceProjectConfigDir;
			}
		}
	}

	return LoadExternalIniFile(
		ConfigFile, 
		IniName, 
		*EngineConfigDir, 
		*SourceConfigDir, 
		bIsBaseIniName, 
		Platform, 
		bForceReload, 
		/*bWriteDestIni*/ false);
}

bool FConfigCacheIni::LoadExternalIniFile(FConfigFile& ConfigFile, const TCHAR* IniName, const TCHAR* EngineConfigDir, const TCHAR* SourceConfigDir, bool bIsBaseIniName, const TCHAR* Platform, bool bForceReload, bool bWriteDestIni, bool bAllowGeneratedIniWhenCooked, const TCHAR* GeneratedConfigDir)
{
	LLM_SCOPE(ELLMTag::ConfigSystem);

	// if bIsBaseIniName is false, that means the .ini is a ready-to-go .ini file, and just needs to be loaded into the FConfigFile
	if (!bIsBaseIniName)
	{
		// generate path to the .ini file (not a Default ini, IniName is the complete name of the file, without path)
		FString SourceIniFilename = FString::Printf(TEXT("%s/%s.ini"), SourceConfigDir, IniName);

		// load the .ini file straight up
		LoadAnIniFile(*SourceIniFilename, ConfigFile);

		ConfigFile.Name = IniName;
	}
	else
	{
#if DISABLE_GENERATED_INI_WHEN_COOKED
		if (FCString::Strcmp(IniName, TEXT("GameUserSettings")) != 0)
		{
			// If we asked to disable ini when cooked, disable all ini files except GameUserSettings, which stores user preferences
			bAllowGeneratedIniWhenCooked = false;
			if (FPlatformProperties::RequiresCookedData())
			{
				ConfigFile.NoSave = true;
			}
		}
#endif
		FString DestIniFilename = GetDestIniFilename(IniName, Platform, GeneratedConfigDir);

		ConfigFile.AddStaticLayersToHierarchy( IniName, Platform, EngineConfigDir, SourceConfigDir );

		if (bForceReload)
		{
			ClearHierarchyCache(IniName);
		}

		// Keep a record of the original settings, delete
		if (ConfigFile.SourceConfigFile)
		{
			delete ConfigFile.SourceConfigFile;
		}

		ConfigFile.SourceConfigFile = new FConfigFile();

		// now generate and make sure it's up to date (using IniName as a Base for an ini filename)
		// @todo This bNeedsWrite afaict is always true even if it loaded a completely valid generated/final .ini, and the write below will
		// just write out the exact same thing it read in!
		bool bNeedsWrite = GenerateDestIniFile(ConfigFile, DestIniFilename, ConfigFile.SourceIniHierarchy, bAllowGeneratedIniWhenCooked, true);

		ConfigFile.Name = IniName;

		// don't write anything to disk in cooked builds - we will always use re-generated INI files anyway.
		// Note: Unfortunately bAllowGeneratedIniWhenCooked is often true even in shipping builds with cooked data
		// due to default parameters. We don't dare change this now.
		//
		// Check GIsInitialLoad since no INI changes that should be persisted could have occurred this early.
		// INI changes from code, environment variables, CLI parameters, etc should not be persisted. 
		if (!GIsInitialLoad && bWriteDestIni && (!FPlatformProperties::RequiresCookedData() || bAllowGeneratedIniWhenCooked)
			// We shouldn't save config files when in multiprocess mode,
			// otherwise we get file contention in XGE shader builds.
			&& !FParse::Param(FCommandLine::Get(), TEXT("Multiprocess")))
		{
			// Check the config system for any changes made to defaults and propagate through to the saved.
			ConfigFile.ProcessSourceAndCheckAgainstBackup();

			if (bNeedsWrite)
			{
				// if it was dirtied during the above function, save it out now
				ConfigFile.Write(DestIniFilename);
			}
		}
	}

	// GenerateDestIniFile returns true if nothing is loaded, so check if we actually loaded something
	return ConfigFile.Num() > 0;
}

void FConfigCacheIni::LoadConsoleVariablesFromINI()
{
	FString ConsoleVariablesPath = FPaths::EngineDir() + TEXT("Config/ConsoleVariables.ini");

#if !DISABLE_CHEAT_CVARS
	// First we read from "../../../Engine/Config/ConsoleVariables.ini" [Startup] section if it exists
	// This is the only ini file where we allow cheat commands (this is why it's not there for UE_BUILD_SHIPPING || UE_BUILD_TEST)
	ApplyCVarSettingsFromIni(TEXT("Startup"), *ConsoleVariablesPath, ECVF_SetByConsoleVariablesIni, true);
#endif // !DISABLE_CHEAT_CVARS

	// We also apply from Engine.ini [ConsoleVariables] section
	ApplyCVarSettingsFromIni(TEXT("ConsoleVariables"), *GEngineIni, ECVF_SetBySystemSettingsIni);

#if WITH_EDITOR
	// We also apply from DefaultEditor.ini [ConsoleVariables] section
	ApplyCVarSettingsFromIni(TEXT("ConsoleVariables"), *GEditorIni, ECVF_SetBySystemSettingsIni);
#endif	//WITH_EDITOR

	IConsoleManager::Get().CallAllConsoleVariableSinks();
}

FArchive& operator<<(FArchive& Ar, FConfigFile& ConfigFile)
{
	bool bHasSourceConfigFile = ConfigFile.SourceConfigFile != nullptr;

	Ar << static_cast<FConfigFile::Super&>(ConfigFile);
	Ar << ConfigFile.Dirty;
	Ar << ConfigFile.NoSave;
	Ar << ConfigFile.Name;
	Ar << ConfigFile.SourceIniHierarchy;
	Ar << ConfigFile.SourceEngineConfigDir;
	Ar << bHasSourceConfigFile;
	if (bHasSourceConfigFile)
	{
		// Handle missing instance for the loading case
		if (ConfigFile.SourceConfigFile == nullptr)
		{
			ConfigFile.SourceConfigFile = new FConfigFile();
		}

		Ar << *ConfigFile.SourceConfigFile;
	}
	Ar << ConfigFile.SourceProjectConfigDir;
	Ar << ConfigFile.CacheKey;
	Ar << ConfigFile.PerObjectConfigArrayOfStructKeys;

	return Ar;
}

void FConfigFile::UpdateSections(const TCHAR* DiskFilename, const TCHAR* IniRootName/*=nullptr*/, const TCHAR* OverridePlatform/*=nullptr*/)
{
	// Since we don't want any modifications to other sections, we manually process the file, not read into sections, etc
	// Keep track of existing SectionTexts and orders so that we can preserve the order of the sections in Write to reduce the diff we make to the file on disk
	FString DiskFile;
	FString NewFile;
	TStringBuilder<128> SectionText;
	TMap<FString, FString> SectionTexts;
	TArray<FString> SectionOrder;
	FString SectionName;

	auto AddSectionText = [this, &SectionTexts, &SectionOrder, &SectionName, &SectionText]()
	{
		if (SectionText.Len() == 0)
		{
			// No text in the section, not even a section header, e.g. because this is the prefix section and there was no prefix.
			// Skip the section - add it to SectionTexts or SectionOrder
		}
		else
		{
			if (Contains(SectionName))
			{
				// Do not add the section to SectionTexts, so that Write will skip writing it at all if it is empty
				// But do add it to SectionOrder, so that Write will write it to the right location if it is non-empty
			}
			else
			{
				// Check for duplicate sections in the file-on-disk; handle these by combining them.  This will modify the file, but will guarantee that we don't lose data
				FString* ExistingSectionText = SectionTexts.Find(SectionName);
				if (ExistingSectionText)
				{
					ExistingSectionText->Append(SectionText.ToString(), SectionText.Len());
				}
				else
				{
					SectionTexts.Add(SectionName, FString(SectionText.ToString()));
				}
			}

			SectionOrder.Add(SectionName);
		}

		// Clear the name and text for the next section
		SectionName.Reset();
		SectionText.Reset();
	};

	SectionName = FString(); // The lines we read before we encounter a section header should be preserved as prefix lines; we implement this by storing them in an empty SectionName
	if (LoadConfigFileWrapper(DiskFilename, DiskFile))
	{
		// walk each line
		const TCHAR* Ptr = DiskFile.Len() > 0 ? *DiskFile : nullptr;
		bool bDone = Ptr ? false : true;
		bool bIsSkippingSection = true;
		while (!bDone)
		{
			// read the next line
			FString TheLine;
			if (FParse::Line(&Ptr, TheLine, true) == false)
			{
				bDone = true;
			}
			else
			{
				// is this line a section? (must be at least [x])
				if (TheLine.Len() > 3 && TheLine[0] == '[' && TheLine[TheLine.Len() - 1] == ']')
				{
					// Add the old section we just finished reading
					AddSectionText();

					// Set SectionName to the name of new section we are about to read
					SectionName = TheLine.Mid(1, TheLine.Len() - 2);
				}

				SectionText.Append(TheLine);
				SectionText.Append(LINE_TERMINATOR);
			}
		}
	}

	// Add the last section we read
	AddSectionText();

	// load the hierarchy up to right before this file
	if (IniRootName != nullptr)
	{
		// get the standard ini files
		SourceIniHierarchy.Empty();
		AddStaticLayersToHierarchy(IniRootName, OverridePlatform, *FPaths::EngineConfigDir(), *FPaths::SourceConfigDir());

		// now chop off this file and any after it
		TArray<int32> Keys;
		SourceIniHierarchy.GetKeys(Keys);
		bool bStartDeleting = false;
		for (int32 Key : Keys)
		{
			if (!bStartDeleting && SourceIniHierarchy.Find(Key)->Filename == DiskFilename)
			{
				bStartDeleting = true;
				}

			if (bStartDeleting)
			{
				SourceIniHierarchy.Remove(Key);
			}
		}

		ClearHierarchyCache(IniRootName);

		// Get a collection of the source hierarchy properties
		if (SourceConfigFile)
		{
			delete SourceConfigFile;
		}
		SourceConfigFile = new FConfigFile();

		// now when Write it called below, it will diff against SourceIniHierarchy
		LoadIniFileHierarchy(SourceIniHierarchy, *SourceConfigFile, true);
	}

	Write(DiskFilename, true, SectionTexts, SectionOrder);
}


/**
 * Functionality to assist with updating a config file with one property value change.
 */
class FSinglePropertyConfigHelper
{
public:
	

	/**
	 * We need certain information for the helper to be useful.
	 *
	 * @Param InIniFilename - The disk location of the file we wish to edit.
	 * @Param InSectionName - The section the property belongs to.
	 * @Param InPropertyName - The name of the property that has been edited.
	 * @Param InPropertyValue - The new value of the property that has been edited.
	 */
	FSinglePropertyConfigHelper(const FString& InIniFilename, const FString& InSectionName, const FString& InPropertyName, const FString& InPropertyValue)
		: IniFilename(InIniFilename)
		, SectionName(InSectionName)
		, PropertyName(InPropertyName)
		, PropertyValue(InPropertyValue)
	{
		// Split the file into the necessary parts.
		PopulateFileContentHelper();
	}

	/**
	 * Perform the action of updating the config file with the new property value.
	 */
	bool UpdateConfigFile()
	{
		UpdatePropertyInSection();
		// Rebuild the file with the updated section.

		FString NewFile = IniFileMakeup.BeforeSection + IniFileMakeup.Section + IniFileMakeup.AfterSection;
		if (!NewFile.EndsWith(LINE_TERMINATOR LINE_TERMINATOR))
		{
			NewFile.AppendChars(LINE_TERMINATOR, TCString<TCHAR>::Strlen(LINE_TERMINATOR));
		}
		return SaveConfigFileWrapper(*IniFilename, NewFile);
	}


private:

	/**
	 * Clear any trailing whitespace from the end of the output.
	 */
	void ClearTrailingWhitespace(FString& InStr)
	{
		const FString Endl(LINE_TERMINATOR);
		while (InStr.EndsWith(LINE_TERMINATOR, ESearchCase::CaseSensitive))
		{
			InStr.LeftChopInline(Endl.Len(), false);
		}
	}
	
	/**
	 * Update the section with the new value for the property.
	 */
	void UpdatePropertyInSection()
	{
		FString UpdatedSection;
		if (IniFileMakeup.Section.IsEmpty())
		{
			const FString DecoratedSectionName = FString::Printf(TEXT("[%s]"), *SectionName);
			
			ClearTrailingWhitespace(IniFileMakeup.BeforeSection);
			UpdatedSection += LINE_TERMINATOR;
			UpdatedSection += LINE_TERMINATOR;
			UpdatedSection += DecoratedSectionName;
			AppendPropertyLine(UpdatedSection);
		}
		else
		{
			FString SectionLine;
			const TCHAR* Ptr = *IniFileMakeup.Section;
			bool bWrotePropertyOnPass = false;
			while (Ptr != nullptr && FParse::Line(&Ptr, SectionLine, true))
			{
				if (SectionLine.StartsWith(FString::Printf(TEXT("%s="), *PropertyName)))
				{
					UpdatedSection += FConfigFile::GenerateExportedPropertyLine(PropertyName, PropertyValue);
					bWrotePropertyOnPass = true;
				}
				else
				{
					UpdatedSection += SectionLine;
					UpdatedSection += LINE_TERMINATOR;
				}
			}

			// If the property wasnt found in the text of the existing section content,
			// append it to the end of the section.
			if (!bWrotePropertyOnPass)
			{
				AppendPropertyLine(UpdatedSection);
			}
			else
			{
				UpdatedSection += LINE_TERMINATOR;
			}
		}

		IniFileMakeup.Section = UpdatedSection;
	}
	
	/**
	 * Split the file up into parts:
	 * -> Before the section we wish to edit, which will remain unaltered,
	 * ->-> The section we wish to edit, we only seek to edit the single property,
	 * ->->-> After the section we wish to edit, which will remain unaltered.
	 */
	void PopulateFileContentHelper()
	{
		FString UnprocessedFileContents;
		if (LoadConfigFileWrapper(*IniFilename, UnprocessedFileContents))
		{
			// Find the section in the file text.
			const FString DecoratedSectionName = FString::Printf(TEXT("[%s]"), *SectionName);

			const int32 DecoratedSectionNameStartIndex = UnprocessedFileContents.Find(DecoratedSectionName);
			if (DecoratedSectionNameStartIndex != INDEX_NONE)
			{
				// If we found the section, cache off the file text before the section.
				IniFileMakeup.BeforeSection = UnprocessedFileContents.Left(DecoratedSectionNameStartIndex);
				UnprocessedFileContents.RemoveAt(0, IniFileMakeup.BeforeSection.Len());

				// For the rest of the file, split it into the section we are editing and the rest of the file after.
				const TCHAR* Ptr = UnprocessedFileContents.Len() > 0 ? *UnprocessedFileContents : nullptr;
				FString NextUnprocessedLine;
				bool bReachedNextSection = false;
				while (Ptr != nullptr && FParse::Line(&Ptr, NextUnprocessedLine, true))
				{
					bReachedNextSection |= (NextUnprocessedLine.StartsWith(TEXT("[")) && NextUnprocessedLine != DecoratedSectionName);
					if (bReachedNextSection)
					{
						IniFileMakeup.AfterSection += NextUnprocessedLine;
						IniFileMakeup.AfterSection += LINE_TERMINATOR;
					}
					else
					{
						IniFileMakeup.Section += NextUnprocessedLine;
						IniFileMakeup.Section += LINE_TERMINATOR;
					}
				}
			}
			else
			{
				IniFileMakeup.BeforeSection = UnprocessedFileContents;
			}
		}
	}
	
	/**
	 * Append the property entry to the section
	 */
	void AppendPropertyLine(FString& PreText)
	{
		// Make sure we dont leave much whitespace, and append the property name/value entry
		ClearTrailingWhitespace(PreText);
		PreText += LINE_TERMINATOR;
		PreText += FConfigFile::GenerateExportedPropertyLine(PropertyName, PropertyValue);
		PreText += LINE_TERMINATOR;
	}


private:
	// The disk location of the ini file we seek to edit
	FString IniFilename;

	// The section in the config file
	FString SectionName;

	// The name of the property that has been changed
	FString PropertyName;

	// The new value, in string format, of the property that has been changed
	FString PropertyValue;

	// Helper struct that holds the makeup of the ini file.
	struct IniFileContent
	{
		// The section we wish to edit
		FString Section;

		// The file contents before the section we are editing
		FString BeforeSection;

		// The file contents after the section we are editing
		FString AfterSection;
	} IniFileMakeup; // Instance of the helper to maintain file structure.
};


bool FConfigFile::UpdateSinglePropertyInSection(const TCHAR* DiskFilename, const TCHAR* PropertyName, const TCHAR* SectionName)
{
	// Result of whether the file has been updated on disk.
	bool bSuccessfullyUpdatedFile = false;

	FString PropertyValue;
	if (const FConfigSection* LocalSection = this->Find(SectionName))
	{
		if (const FConfigValue* ConfigValue = LocalSection->Find(PropertyName))
		{
			// Use GetSavedValueForWriting rather than GetSavedValue to avoid having this save operation mark the value as having been accessed for dependency tracking
			PropertyValue = UE::ConfigCacheIni::Private::FAccessor::GetSavedValueForWriting(*ConfigValue);
			FSinglePropertyConfigHelper SinglePropertyConfigHelper(DiskFilename, SectionName, PropertyName, PropertyValue);
			bSuccessfullyUpdatedFile = SinglePropertyConfigHelper.UpdateConfigFile();
		}
	}

	return bSuccessfullyUpdatedFile;
}

const TCHAR* ConvertValueFromHumanFriendlyValue( const TCHAR* Value )
{
	static const TCHAR* OnValue = TEXT("1");
	static const TCHAR* OffValue = TEXT("0");

	// allow human friendly names
	if (FCString::Stricmp(Value, TEXT("True")) == 0
		|| FCString::Stricmp(Value, TEXT("Yes")) == 0
		|| FCString::Stricmp(Value, TEXT("On")) == 0)
	{
		return OnValue;
	}
	else if (FCString::Stricmp(Value, TEXT("False")) == 0
		|| FCString::Stricmp(Value, TEXT("No")) == 0
		|| FCString::Stricmp(Value, TEXT("Off")) == 0)
	{
		return OffValue;
	}
	return Value;
}


// @param IniFile for error reporting
// to have one single function to set a cvar from ini (handing friendly names, cheats for shipping and message about cheats in non shipping)
CORE_API void OnSetCVarFromIniEntry(const TCHAR *IniFile, const TCHAR *Key, const TCHAR* Value, uint32 SetBy, bool bAllowCheating)
{
	check(IniFile && Key && Value);
	check((SetBy & ECVF_FlagMask) == 0);

	Value = ConvertValueFromHumanFriendlyValue(Value);

	IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(Key); 

	if(CVar)
	{
		bool bCheatFlag = CVar->TestFlags(EConsoleVariableFlags::ECVF_Cheat); 
		
		if(SetBy == ECVF_SetByScalability)
		{
			if(!CVar->TestFlags(EConsoleVariableFlags::ECVF_Scalability) && !CVar->TestFlags(EConsoleVariableFlags::ECVF_ScalabilityGroup))
			{
				ensureMsgf(false, TEXT("Scalability.ini can only set ECVF_Scalability console variables ('%s'='%s' is ignored)"), Key, Value);
				return;
			}
		}

		bool bAllowChange = !bCheatFlag || bAllowCheating;

		if(bAllowChange)
		{
			UE_LOG(LogConfig, Log, TEXT("Setting CVar [[%s:%s]]"), Key, Value);
			if (SetBy == ECVF_SetByMask)
			{
				CVar->SetWithCurrentPriority(Value);
			}
			else
			{
				CVar->Set(Value, (EConsoleVariableFlags)SetBy);
			}
		}
		else
		{
#if !DISABLE_CHEAT_CVARS
			if(bCheatFlag)
			{
				// We have one special cvar to test cheating and here we don't want to both the user of the engine
				if(FCString::Stricmp(Key, TEXT("con.DebugEarlyCheat")) != 0)
				{
					ensureMsgf(false, TEXT("The ini file '%s' tries to set the console variable '%s' marked with ECVF_Cheat, this is only allowed in consolevariables.ini"),
						IniFile, Key);
				}
			}
#endif // !DISABLE_CHEAT_CVARS
		}
	}
	else
	{
		// Create a dummy that is used when someone registers the variable later on.
		// this is important for variables created in external modules, such as the game module
		IConsoleManager::Get().RegisterConsoleVariable(Key, Value, TEXT("IAmNoRealVariable"),
			(uint32)ECVF_Unregistered | (uint32)ECVF_CreatedFromIni | SetBy);
	}
}


void ApplyCVarSettingsFromIni(const TCHAR* InSectionName, const TCHAR* InIniFilename, uint32 SetBy, bool bAllowCheating)
{
	FCoreDelegates::OnApplyCVarFromIni.Broadcast(InSectionName, InIniFilename, SetBy, bAllowCheating);

	UE_LOG(LogConfig,Log,TEXT("Applying CVar settings from Section [%s] File [%s]"),InSectionName,InIniFilename);

	if(FConfigSection* Section = GConfig->GetSectionPrivate(InSectionName, false, true, InIniFilename))
	{
		for(FConfigSectionMap::TConstIterator It(*Section); It; ++It)
		{
			const FString& KeyString = It.Key().GetPlainNameString(); 
			const FString& ValueString = It.Value().GetValue();

			OnSetCVarFromIniEntry(InIniFilename, *KeyString, *ValueString, SetBy, bAllowCheating);
		}
	}
}

void ForEachCVarInSectionFromIni(const TCHAR* InSectionName, const TCHAR* InIniFilename, TFunction<void(IConsoleVariable* CVar, const FString& KeyString, const FString& ValueString)> InEvaluationFunction)
{
	if (FConfigSection* Section = GConfig->GetSectionPrivate(InSectionName, false, true, InIniFilename))
	{
		for (FConfigSectionMap::TConstIterator It(*Section); It; ++It)
		{
			const FString& KeyString = It.Key().GetPlainNameString();
			const FString& ValueString = ConvertValueFromHumanFriendlyValue(*It.Value().GetValue());

			IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*KeyString);
			if (CVar)
			{
				InEvaluationFunction(CVar, KeyString, ValueString);
			}
		}
	}
}


void ApplyCVarSettingsGroupFromIni(const TCHAR* InSectionBaseName, int32 InGroupNumber, const TCHAR* InIniFilename, uint32 SetBy)
{
	// Lookup the config section for this section and group number
	FString SectionName = FString::Printf(TEXT("%s@%d"), InSectionBaseName, InGroupNumber);
	ApplyCVarSettingsFromIni(*SectionName,InIniFilename, SetBy);
}

void ApplyCVarSettingsGroupFromIni(const TCHAR* InSectionBaseName, const TCHAR* InSectionTag, const TCHAR* InIniFilename, uint32 SetBy)
{
	// Lookup the config section for this section and group number
	FString SectionName = FString::Printf(TEXT("%s@%s"), InSectionBaseName, InSectionTag);
	ApplyCVarSettingsFromIni(*SectionName, InIniFilename, SetBy);
}





class FCVarIniHistoryHelper
{
private:
	struct FCVarIniHistory
	{
		FCVarIniHistory(const FString& InSectionName, const FString& InIniName, uint32 InSetBy, bool InbAllowCheating) :
			SectionName(InSectionName), FileName(InIniName), SetBy(InSetBy), bAllowCheating(InbAllowCheating)
		{}

		FString SectionName;
		FString FileName;
		uint32 SetBy;
		bool bAllowCheating;
	};
	TArray<FCVarIniHistory> CVarIniHistory;
	bool bRecurseCheck;

	void OnApplyCVarFromIniCallback(const TCHAR* SectionName, const TCHAR* IniFilename, uint32 SetBy, bool bAllowCheating)
	{
		if ( bRecurseCheck == true )
		{
			return;
		}
		CVarIniHistory.Add(FCVarIniHistory(SectionName, IniFilename, SetBy, bAllowCheating));
	}


public:
	FCVarIniHistoryHelper() : bRecurseCheck( false )
	{
		FCoreDelegates::OnApplyCVarFromIni.AddRaw(this, &FCVarIniHistoryHelper::OnApplyCVarFromIniCallback);
	}

	~FCVarIniHistoryHelper()
	{
		FCoreDelegates::OnApplyCVarFromIni.RemoveAll(this);
	}

	void ReapplyIniHistory()
	{
		for (const FCVarIniHistory& IniHistory : CVarIniHistory)
		{
			const FString& SectionName = IniHistory.SectionName;
			const FString& IniFilename = IniHistory.FileName;
			const uint32& SetBy = IniHistory.SetBy;
			if (FConfigSection* Section = GConfig->GetSectionPrivate(*SectionName, false, true, *IniFilename))
			{
				for (FConfigSectionMap::TConstIterator It(*Section); It; ++It)
				{
					const FString& KeyString = It.Key().GetPlainNameString();
					const FString& ValueString = It.Value().GetValue();


					IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*KeyString);

					if ( !CVar )
					{
						continue;
					}

					// if this cvar was last set by this config setting 
					// then we want to reapply any new changes
					if (!CVar->TestFlags((EConsoleVariableFlags)SetBy))
					{
						continue;
					}

					// convert to human friendly string
					const TCHAR* HumanFriendlyValue = ConvertValueFromHumanFriendlyValue(*ValueString);
					const FString CurrentValue = CVar->GetString();
					if (CurrentValue.Compare(HumanFriendlyValue, ESearchCase::CaseSensitive) == 0)
					{
						continue;
					}
					else
					{
						// this is more expensive then a CaseSensitive version and much less likely
						if (CurrentValue.Compare(HumanFriendlyValue, ESearchCase::IgnoreCase) == 0)
						{
							continue;
						}
					}

					if (CVar->TestFlags(EConsoleVariableFlags::ECVF_ReadOnly) )
					{
						UE_LOG(LogConfig, Warning, TEXT("Failed to change Readonly CVAR value %s %s -> %s Config %s %s"),
							*KeyString, *CurrentValue, HumanFriendlyValue, *IniFilename, *SectionName);

						continue;
					}

					UE_LOG(LogConfig, Display, TEXT("Applied changed CVAR value %s %s -> %s Config %s %s"), 
						*KeyString, *CurrentValue, HumanFriendlyValue, *IniFilename, *SectionName);

					OnSetCVarFromIniEntry(*IniFilename, *KeyString, *ValueString, SetBy, IniHistory.bAllowCheating);
				}
			}
		}
		bRecurseCheck=false;
	}
};
TUniquePtr<FCVarIniHistoryHelper> IniHistoryHelper;

#if !UE_BUILD_SHIPPING
class FConfigHistoryHelper
{
private:
	enum class HistoryType
	{
		Value,
		Section,
		SectionName,
		Count
	};
	friend const TCHAR* LexToString(HistoryType Type)
	{
		static const TCHAR* Strings[] = {
			TEXT("Value"),
			TEXT("Section"),
			TEXT("SectionName")
		};
		using UnderType = __underlying_type(HistoryType);
		static_assert(static_cast<UnderType>(HistoryType::Count) == UE_ARRAY_COUNT(Strings), "");
		return Strings[static_cast<UnderType>(Type)];
	}

	struct FConfigHistory
	{
		HistoryType Type = HistoryType::Value;

		FString FileName;
		FString SectionName;
		FString Key;
	};
	friend uint32 GetTypeHash(const FConfigHistory& CH)
	{
		uint32 Hash = ::GetTypeHash(CH.Type);
		Hash = HashCombine(Hash, ::GetTypeHash(CH.FileName));
		Hash = HashCombine(Hash, ::GetTypeHash(CH.SectionName));
		Hash = HashCombine(Hash, ::GetTypeHash(CH.Key));
		return Hash;
	}
	friend bool operator==(const FConfigHistory& A, const FConfigHistory& B)
	{
		return
			A.Type == B.Type &&
			A.FileName == B.FileName &&
			A.SectionName == B.SectionName &&
			A.Key == B.Key;
	}

	TSet<FConfigHistory> History;

	void OnConfigValueRead(const TCHAR* FileName, const TCHAR* SectionName, const TCHAR* Key)
	{
		History.Emplace(FConfigHistory{ HistoryType::Value, FileName, SectionName, Key });
	}

	void OnConfigSectionRead(const TCHAR* FileName, const TCHAR* SectionName)
	{
		History.Emplace(FConfigHistory{ HistoryType::Section, FileName, SectionName });
	}

	void OnConfigSectionNameRead(const TCHAR* FileName, const TCHAR* SectionName)
	{
		History.Emplace(FConfigHistory{ HistoryType::SectionName, FileName, SectionName });
	}

public:
	FConfigHistoryHelper()
	{
		FCoreDelegates::OnConfigValueRead.AddRaw(this, &FConfigHistoryHelper::OnConfigValueRead);
		FCoreDelegates::OnConfigSectionRead.AddRaw(this, &FConfigHistoryHelper::OnConfigSectionRead);
		FCoreDelegates::OnConfigSectionNameRead.AddRaw(this, &FConfigHistoryHelper::OnConfigSectionNameRead);
	}

	~FConfigHistoryHelper()
	{
		FCoreDelegates::OnConfigValueRead.RemoveAll(this);
		FCoreDelegates::OnConfigSectionRead.RemoveAll(this);
		FCoreDelegates::OnConfigSectionNameRead.RemoveAll(this);
	}

	void DumpHistory()
	{
		const FString SavePath = FPaths::ProjectLogDir() / TEXT("ConfigHistory.csv");

		FArchive* Writer = IFileManager::Get().CreateFileWriter(*SavePath, FILEWRITE_NoFail);

		auto WriteLine = [Writer](FString&& Line)
		{
			UE_LOG(LogConfig, Display, TEXT("%s"), *Line);
			FTCHARToUTF8 UTF8String(*(MoveTemp(Line) + LINE_TERMINATOR));
			Writer->Serialize((UTF8CHAR*)UTF8String.Get(), UTF8String.Length());
		};

		UE_LOG(LogConfig, Display, TEXT("Dumping History of Config Reads to %s"), *SavePath);
		UE_LOG(LogConfig, Display, TEXT("Begin History of Config Reads"));
		UE_LOG(LogConfig, Display, TEXT("------------------------------------------------------"));
		WriteLine(FString::Printf(TEXT("Type, File, Section, Key")));
		for (const FConfigHistory& CH : History)
		{
			switch (CH.Type)
			{
			case HistoryType::Value:
				WriteLine(FString::Printf(TEXT("%s, %s, %s, %s"), LexToString(CH.Type), *CH.FileName, *CH.SectionName, *CH.Key));
				break;
			case HistoryType::Section:
			case HistoryType::SectionName:
				WriteLine(FString::Printf(TEXT("%s, %s, %s, None"), LexToString(CH.Type), *CH.FileName, *CH.SectionName));
				break;
			}
		}
		UE_LOG(LogConfig, Display, TEXT("------------------------------------------------------"));
		UE_LOG(LogConfig, Display, TEXT("End History of Config Reads"));

		delete Writer;
		Writer = nullptr;
	}
};
TUniquePtr<FConfigHistoryHelper> ConfigHistoryHelper;
#endif // !UE_BUILD_SHIPPING


void RecordApplyCVarSettingsFromIni()
{
	check(IniHistoryHelper == nullptr);
	IniHistoryHelper = MakeUnique<FCVarIniHistoryHelper>();
}

void ReapplyRecordedCVarSettingsFromIni()
{
	// first we need to reload the inis 
	for (TPair<FString, FConfigFile>& IniPair : *GConfig)
	{
		FConfigFile& ConfigFile = IniPair.Value;
		if (ConfigFile.Num() > 0)
		{
			FName BaseName = ConfigFile.Name;
			// Must call LoadLocalIniFile (NOT LoadGlobalIniFile) to preserve original enginedir/sourcedir for plugins
			verify(FConfigCacheIni::LoadLocalIniFile(ConfigFile, *BaseName.ToString(), true, nullptr, true));
		}
	}

	check(IniHistoryHelper != nullptr);
	IniHistoryHelper->ReapplyIniHistory();
}

void DeleteRecordedCVarSettingsFromIni()
{
	check(IniHistoryHelper != nullptr);
	IniHistoryHelper = nullptr;
}

void RecordConfigReadsFromIni()
{
#if !UE_BUILD_SHIPPING
	check(ConfigHistoryHelper == nullptr);
	ConfigHistoryHelper = MakeUnique<FConfigHistoryHelper>();
#endif
}

void DumpRecordedConfigReadsFromIni()
{
#if !UE_BUILD_SHIPPING
	check(ConfigHistoryHelper);
	ConfigHistoryHelper->DumpHistory();
#endif
}

void DeleteRecordedConfigReadsFromIni()
{
#if !UE_BUILD_SHIPPING
	check(ConfigHistoryHelper);
	ConfigHistoryHelper = nullptr;
#endif
}
