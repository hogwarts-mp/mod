// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/DataDrivenPlatformInfoRegistry.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"

static const TArray<FString>& GetDataDrivenIniFilenames()
{
	static bool bHasSearchedForFiles = false;
	static TArray<FString> DataDrivenIniFilenames;

	if (bHasSearchedForFiles == false)
	{
		bHasSearchedForFiles = true;

		// look for the special files in any congfig subdirectories
		IFileManager::Get().FindFilesRecursive(DataDrivenIniFilenames, *FPaths::EngineConfigDir(), TEXT("DataDrivenPlatformInfo.ini"), true, false);
		// manually look through the platform directories - we can't use GetExtensionDirs(), since that function uses the results of this function 
		TArray<FString> PlatformDirs;
		IFileManager::Get().FindFiles(PlatformDirs, *FPaths::Combine(FPaths::EnginePlatformExtensionsDir(), TEXT("*")), false, true);
		for (const FString& PlatformDir : PlatformDirs)
		{
			FString IniPath = FPaths::Combine(FPaths::EnginePlatformExtensionsDir(), PlatformDir, TEXT("Config/DataDrivenPlatformInfo.ini"));
			if (IFileManager::Get().FileExists(*IniPath))
			{
				DataDrivenIniFilenames.Add(IniPath);
			}
		}
	}

	return DataDrivenIniFilenames;
}

int32 FDataDrivenPlatformInfoRegistry::GetNumDataDrivenIniFiles()
{
	return GetDataDrivenIniFilenames().Num();
}

bool FDataDrivenPlatformInfoRegistry::LoadDataDrivenIniFile(int32 Index, FConfigFile& IniFile, FString& PlatformName)
{
	const TArray<FString>& IniFilenames = GetDataDrivenIniFilenames();
	if (Index < 0 || Index >= IniFilenames.Num())
	{
		return false;
	}

	// manually load a FConfigFile object from a source ini file so that we don't do any SavedConfigDir processing or anything
	// (there's a possibility this is called before the ProjectDir is set)
	FString IniContents;
	if (FFileHelper::LoadFileToString(IniContents, *IniFilenames[Index]))
	{
		IniFile.ProcessInputFileContents(IniContents);


		// platform extension paths are different (engine/platforms/platform/config, not engine/config/platform)
		if (IniFilenames[Index].StartsWith(FPaths::EnginePlatformExtensionsDir()))
		{
			PlatformName = FPaths::GetCleanFilename(FPaths::GetPath(FPaths::GetPath(IniFilenames[Index])));
		}
		else
		{
			// this could be 'Engine' for a shared DataDrivenPlatformInfo file
			PlatformName = FPaths::GetCleanFilename(FPaths::GetPath(IniFilenames[Index]));
		}

		return true;
	}

	return false;
}

static void DDPIIniRedirect(FString& StringData)
{
	TArray<FString> Tokens;
	StringData.ParseIntoArray(Tokens, TEXT(":"));
	if (Tokens.Num() != 5)
	{
		StringData = TEXT("");
		return;
	}

	// now load a local version of the ini hierarchy
	FConfigFile LocalIni;
	FConfigCacheIni::LoadLocalIniFile(LocalIni, *Tokens[1], true, *Tokens[2]);

	// and get the platform's value (if it's not found, return an empty string)
	FString FoundValue;
	LocalIni.GetString(*Tokens[3], *Tokens[4], FoundValue);
	StringData = FoundValue;
}

static FString DDPITryRedirect(const FConfigFile& IniFile, const TCHAR* Key, bool* OutHadBang=nullptr)
{
	FString StringData;
	if (IniFile.GetString(TEXT("DataDrivenPlatformInfo"), Key, StringData))
	{
		if (StringData.StartsWith(TEXT("ini:")) || StringData.StartsWith(TEXT("!ini:")))
		{
			// check for !'ing a bool
			if (OutHadBang != nullptr)
			{
				*OutHadBang = StringData[0] == TEXT('!');
			}

			// replace the string, overwriting it
			DDPIIniRedirect(StringData);
		}
	}
	return StringData;
}

static void DDPIGetBool(const FConfigFile& IniFile, const TCHAR* Key, bool& OutBool)
{
	bool bHadNot = false;
	FString StringData = DDPITryRedirect(IniFile, Key, &bHadNot);

	// if we ended up with a string, convert it, otherwise leave it alone
	if (StringData.Len() > 0)
	{
		OutBool = bHadNot ? !StringData.ToBool() : StringData.ToBool();
	}
}

static void DDPIGetInt(const FConfigFile& IniFile, const TCHAR* Key, int32& OutInt)
{
	FString StringData = DDPITryRedirect(IniFile, Key);

	// if we ended up with a string, convert it, otherwise leave it alone
	if (StringData.Len() > 0)
	{
		OutInt = FCString::Atoi(*StringData);
	}
}

static void DDPIGetUInt(const FConfigFile& IniFile, const TCHAR* Key, uint32& OutInt)
{
	FString StringData = DDPITryRedirect(IniFile, Key);

	// if we ended up with a string, convert it, otherwise leave it alone
	if (StringData.Len() > 0)
	{
		OutInt = (uint32)FCString::Strtoui64(*StringData, nullptr, 10);
	}
}

static void DDPIGetString(const FConfigFile& IniFile, const TCHAR* Key, FString& OutString)
{
	FString StringData = DDPITryRedirect(IniFile, Key);

	// if we ended up with a string, convert it, otherwise leave it alone
	if (StringData.Len() > 0)
	{
		OutString = StringData;
	}
}

static void DDPIGetStringArray(const FConfigFile& IniFile, const TCHAR* Key, TArray<FString>& OutArray)
{
	// we don't support redirecting arrays
	IniFile.GetArray(TEXT("DataDrivenPlatformInfo"), Key, OutArray);
}

static void LoadDDPIIniSettings(const FConfigFile& IniFile, FDataDrivenPlatformInfoRegistry::FPlatformInfo& Info)
{
	DDPIGetBool(IniFile, TEXT("bIsConfidential"), Info.bIsConfidential);
	DDPIGetString(IniFile, TEXT("AudioCompressionSettingsIniSectionName"), Info.AudioCompressionSettingsIniSectionName);
	DDPIGetString(IniFile, TEXT("HardwareCompressionFormat"), Info.HardwareCompressionFormat);
	DDPIGetStringArray(IniFile, TEXT("AdditionalRestrictedFolders"), Info.AdditionalRestrictedFolders);
	
	DDPIGetBool(IniFile, TEXT("Freezing_b32Bit"), Info.Freezing_b32Bit);
	DDPIGetUInt(IniFile, Info.Freezing_b32Bit ? TEXT("Freezing_MaxFieldAlignment32") : TEXT("Freezing_MaxFieldAlignment64"), Info.Freezing_MaxFieldAlignment);
	DDPIGetBool(IniFile, TEXT("Freezing_bForce64BitMemoryImagePointers"), Info.Freezing_bForce64BitMemoryImagePointers);
	DDPIGetBool(IniFile, TEXT("Freezing_bAlignBases"), Info.Freezing_bAlignBases);
	DDPIGetBool(IniFile, TEXT("Freezing_bWithRayTracing"), Info.Freezing_bWithRayTracing);

	// NOTE: add more settings here!
	DDPIGetBool(IniFile, TEXT("bIsInteractablePlatform"), Info.bIsInteractablePlatform);
	DDPIGetBool(IniFile, TEXT("bHasDedicatedGamepad"), Info.bHasDedicatedGamepad);
	DDPIGetBool(IniFile, TEXT("bDefaultInputStandardKeyboard"), Info.bDefaultInputStandardKeyboard);

	DDPIGetBool(IniFile, TEXT("bInputSupportConfigurable"), Info.bInputSupportConfigurable);
	DDPIGetString(IniFile, TEXT("DefaultInputType"), Info.DefaultInputType);
	DDPIGetBool(IniFile, TEXT("bSupportsMouseAndKeyboard"), Info.bSupportsMouseAndKeyboard);
	DDPIGetBool(IniFile, TEXT("bSupportsGamepad"), Info.bSupportsGamepad);
	DDPIGetBool(IniFile, TEXT("bCanChangeGamepadType"), Info.bCanChangeGamepadType);
	DDPIGetBool(IniFile, TEXT("bSupportsTouch"), Info.bSupportsTouch);


#if DDPI_HAS_EXTENDED_PLATFORMINFO_DATA
	// now look in the PlatformInfo objects in the file for TP and UBT names

	FName TargetPlatformKey("TargetPlatformName");
	FName UBTNameKey("UBTTargetID");
	for (auto& Pair : IniFile)
	{
		if (Pair.Key.StartsWith(TEXT("PlatformInfo")))
		{
			const FConfigValue* PlatformName = Pair.Value.Find(TargetPlatformKey);
			if (PlatformName)
			{
				Info.AllTargetPlatformNames.AddUnique(PlatformName->GetValue());
			}
			
			PlatformName = Pair.Value.Find(UBTNameKey);
			if (PlatformName)
			{
				Info.AllUBTPlatformNames.AddUnique(PlatformName->GetValue());
			}
		}
	}
#endif
}


/**
* Get the global set of data driven platform information
*/
const TMap<FString, FDataDrivenPlatformInfoRegistry::FPlatformInfo>& FDataDrivenPlatformInfoRegistry::GetAllPlatformInfos()
{
	static bool bHasSearchedForPlatforms = false;
	static TMap<FString, FDataDrivenPlatformInfoRegistry::FPlatformInfo> DataDrivenPlatforms;

	// look on disk for special files
	if (bHasSearchedForPlatforms == false)
	{
		bHasSearchedForPlatforms = true;

		int32 NumFiles = FDataDrivenPlatformInfoRegistry::GetNumDataDrivenIniFiles();

		TMap<FString, FString> IniParents;
		for (int32 Index = 0; Index < NumFiles; Index++)
		{
			// load the .ini file
			FConfigFile IniFile;
			FString PlatformName;
			FDataDrivenPlatformInfoRegistry::LoadDataDrivenIniFile(Index, IniFile, PlatformName);

			// platform info is registered by the platform name
			if (IniFile.Contains(TEXT("DataDrivenPlatformInfo")))
			{
				// cache info
				FDataDrivenPlatformInfoRegistry::FPlatformInfo& Info = DataDrivenPlatforms.Add(PlatformName, FDataDrivenPlatformInfoRegistry::FPlatformInfo());
				LoadDDPIIniSettings(IniFile, Info);

				// get the parent to build list later
				FString IniParent;
				IniFile.GetString(TEXT("DataDrivenPlatformInfo"), TEXT("IniParent"), IniParent);
				IniParents.Add(PlatformName, IniParent);
			}
		}

		// now that all are read in, calculate the ini parent chain, starting with parent-most
		for (auto& It : DataDrivenPlatforms)
		{
			// walk up the chain and build up the ini chain of parents
			for (FString CurrentPlatform = IniParents.FindRef(It.Key); CurrentPlatform != TEXT(""); CurrentPlatform = IniParents.FindRef(CurrentPlatform))
			{
				// insert at 0 to reverse the order
				It.Value.IniParentChain.Insert(CurrentPlatform, 0);
			}
		}
	}

	return DataDrivenPlatforms;
}

const TArray<FString>& FDataDrivenPlatformInfoRegistry::GetValidPlatformDirectoryNames()
{
	static bool bHasSearchedForPlatforms = false;
	static TArray<FString> ValidPlatformDirectories;

	if (bHasSearchedForPlatforms == false)
	{
		bHasSearchedForPlatforms = true;

		// look for possible platforms
		const TMap<FString, FDataDrivenPlatformInfoRegistry::FPlatformInfo>& Infos = GetAllPlatformInfos();
		for (auto Pair : Infos)
		{
#if DDPI_HAS_EXTENDED_PLATFORMINFO_DATA
			// if the editor hasn't compiled in support for the platform, it's not "valid"
			if (!HasCompiledSupportForPlatform(Pair.Key, EPlatformNameType::Ini))
			{
				continue;
			}
#endif

			// add ourself as valid
			ValidPlatformDirectories.AddUnique(Pair.Key);

			// now add additional directories
			for (FString& AdditionalDir : Pair.Value.AdditionalRestrictedFolders)
			{
				ValidPlatformDirectories.AddUnique(AdditionalDir);
			}
		}
	}

	return ValidPlatformDirectories;
}


const FDataDrivenPlatformInfoRegistry::FPlatformInfo& FDataDrivenPlatformInfoRegistry::GetPlatformInfo(const FString& PlatformName)
{
	const FPlatformInfo* Info = GetAllPlatformInfos().Find(PlatformName);
	static FPlatformInfo Empty;
	return Info ? *Info : Empty;
}


const TArray<FString>& FDataDrivenPlatformInfoRegistry::GetConfidentialPlatforms()
{
	static bool bHasSearchedForPlatforms = false;
	static TArray<FString> FoundPlatforms;

	// look on disk for special files
	if (bHasSearchedForPlatforms == false)
	{
		for (auto It : GetAllPlatformInfos())
		{
			if (It.Value.bIsConfidential)
			{
				FoundPlatforms.Add(It.Key);
			}
		}

		bHasSearchedForPlatforms = true;
	}

	// return whatever we have already found
	return FoundPlatforms;
}


#if DDPI_HAS_EXTENDED_PLATFORMINFO_DATA
bool FDataDrivenPlatformInfoRegistry::HasCompiledSupportForPlatform(const FString& PlatformName, EPlatformNameType PlatformNameType)
{
	if (PlatformNameType == EPlatformNameType::Ini)
	{
		// get the DDPI info object
		const FPlatformInfo& Info = GetPlatformInfo(PlatformName);

		// look to see if any of the TPs in the Info are valid - if at least one is, we are good
		for (const FString& TPName : Info.AllTargetPlatformNames)
		{
			if (HasCompiledSupportForPlatform(TPName, EPlatformNameType::TargetPlatform))
			{
				return true;
			}
		}
		return false;

	}
	else if (PlatformNameType == EPlatformNameType::UBT)
	{
		FName PlatformFName(*PlatformName);

		// find all the DataDrivenPlatformInfo objects and find a matching the UBT name
		for (auto& Pair : FDataDrivenPlatformInfoRegistry::GetAllPlatformInfos())
		{
			// if this platform contains the UBT platform name, then check the info for it's TPs
			// (we could be tricky and match UBT platforms with TPs just for thse UBT platforms, but that complexity does not seem needed)
			if (Pair.Value.AllUBTPlatformNames.Contains(PlatformName))
			{
				return HasCompiledSupportForPlatform(Pair.Key, EPlatformNameType::Ini);
			}
		}

		return false;
	}
	else if (PlatformNameType == EPlatformNameType::TargetPlatform)
	{
		// was this TP compiled?
		return FModuleManager::Get().ModuleExists(*FString::Printf(TEXT("%sTargetPlatform"), *PlatformName));
	}

	return false;
}
#endif
