// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/UnrealString.h"
#include "Containers/Map.h"
#include "Containers/Array.h"

#define DDPI_HAS_EXTENDED_PLATFORMINFO_DATA WITH_EDITOR && !IS_MONOLITHIC

struct CORE_API FDataDrivenPlatformInfoRegistry
{
	// Information about a platform loaded from disk
	struct FPlatformInfo
	{
		// cached list of ini parents
		TArray<FString> IniParentChain;

		// is this platform confidential
		bool bIsConfidential = false;

		// the name of the ini section to use to load audio compression settings (used at runtime and cooktime)
		FString AudioCompressionSettingsIniSectionName;

		// the compression format that this platform wants; overrides game unless bForceUseProjectCompressionFormat
		FString HardwareCompressionFormat;

		// list of additonal restricted folders
		TArray<FString> AdditionalRestrictedFolders;

		// MemoryFreezing information, matches FPlatformTypeLayoutParameters - defaults are clang, noneditor
		uint32 Freezing_MaxFieldAlignment = 0xffffffff;
		bool Freezing_b32Bit = false;
		bool Freezing_bForce64BitMemoryImagePointers = false;
		bool Freezing_bAlignBases = false;
		bool Freezing_bWithRayTracing = false;

		// NOTE: add more settings here (and read them in in the LoadDDPIIniSettings() function in the .cpp)

		// True if users will actually interact with this plaform, IE: not a GDK
		bool bIsInteractablePlatform = false;

		// True if this platform has a non-generic gamepad specifically associated with it
		bool bHasDedicatedGamepad = false;

		// True if this platform handles input via standard keyboard layout by default, translates to PC platform
		bool bDefaultInputStandardKeyboard = false;

		// Input-related settings
		bool bInputSupportConfigurable = false;
		FString DefaultInputType = "Gamepad";
		bool bSupportsMouseAndKeyboard = false;
		bool bSupportsGamepad = true;
		bool bCanChangeGamepadType = true;
		bool bSupportsTouch = false;


#if DDPI_HAS_EXTENDED_PLATFORMINFO_DATA
		// list of TP names (WindowsNoEditor, etc)
		TArray<FString> AllTargetPlatformNames;
		// list of UBT platform names (Win32, Win64, etc)
		TArray<FString> AllUBTPlatformNames;
#endif
	};

	/**
	* Get the global set of data driven platform information
	*/
	static const TMap<FString, FDataDrivenPlatformInfoRegistry::FPlatformInfo>& GetAllPlatformInfos();

	/**
	 * Gets a set of platform names based on GetAllPlatformInfos, their AdditionalRestrictedFolders, and possibly filtered based on what editor has support compiled for
	 * This is not necessarily the same as IniParents, although there is overlap - IniParents come from chaining DDPIs, so those will be in GetAllPlatformInfos already to be checked 
	 */
	static const TArray<FString>& GetValidPlatformDirectoryNames();

	/**
	 * Get the data driven platform info for a given platform. If the platform doesn't have any on disk,
	 * this will return a default constructed FConfigDataDrivenPlatformInfo
	 */
	static const FPlatformInfo& GetPlatformInfo(const FString& PlatformName);

	/**
	 * Gets a list of all known confidential platforms (note these are just the platforms you have access to, so, for example PS4 won't be
	 * returned if you are not a PS4 licensee)
	 */
	static const TArray<FString>& GetConfidentialPlatforms();

	/**
	 * Returns the number of discovered ini files that can be loaded with LoadDataDrivenIniFile
	 */
	static int32 GetNumDataDrivenIniFiles();

	/**
	 * Load the given ini file, and 
	 */
	static bool LoadDataDrivenIniFile(int32 Index, FConfigFile& IniFile, FString& PlatformName);


#if DDPI_HAS_EXTENDED_PLATFORMINFO_DATA
	/**
	 * Checks for the existence of compiled modules for a given (usually another, target, platform)
	 * Since there are different types of platform names, it is necessary pass in the type of name
	 */
	enum class EPlatformNameType
	{
		// for instance Win64
		UBT,
		// for instance Windows
		Ini,
		// for instance WindowsNoEditor
		TargetPlatform,
	};
	static bool HasCompiledSupportForPlatform(const FString& PlatformName, EPlatformNameType PlatformNameType);
#endif
};

