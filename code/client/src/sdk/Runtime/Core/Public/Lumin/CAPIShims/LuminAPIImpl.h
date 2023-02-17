// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Misc/CString.h"
#include "Templates/Atomic.h"
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Lumin/CAPIShims/IMagicLeapLibraryLoader.h"
// for std::enable_if
#include <type_traits>

DEFINE_LOG_CATEGORY_STATIC(LogLuminAPI, Display, All);

namespace LUMIN_MLSDK_API
{

#if PLATFORM_LUMIN
// Don't inherit from IMagicLeapLibraryLoader to avoid virtual func calls on Lumin
class LuminLibraryLoader
{
public:
	static const LuminLibraryLoader & Ref()
	{
		static LuminLibraryLoader LibLoader;
		return LibLoader;
	}

	/** Reads the config file and environment variable for the MLSDK package path and sets up the correct environment to load the libraries from. */
	LuminLibraryLoader()
	: DllSearchPaths( { TEXT("/system/lib64") } )
	{}

	/**
	  Loads the given library from the correct path.
	  @param Name Name of library to load, without any prefix or extension. e.g."ml_perception_client".
	  @return True if the library was succesfully loaded. A false value generally indicates that the MLSDK path is not set correctly.
	*/
	void* LoadDLL(const FString& Name) const
	{
		const FString DLLName = FString(FPlatformProcess::GetModulePrefix()) + Name + TEXT(".") + FPlatformProcess::GetModuleExtension();
		for (const FString& path : DllSearchPaths)
		{
			void* dll = FPlatformProcess::GetDllHandle(*FPaths::Combine(*path, *DLLName));
			if (dll != nullptr)
			{
				UE_LOG(LogLuminAPI, Display, TEXT("Dll loaded: %s"), *FPaths::Combine(*path, *DLLName));
				return dll;
			}
		}
		
		return nullptr;
	}

private:
	TArray<FString> DllSearchPaths;
};
#endif // PLATFORM_LUMIN

/** Manages a single API library to load it on demand when retrieving an entry in that library.
	The library is designated with a type key to statically bind the loaded instance to
	only one of these. */
template <typename LibKey>
class Library
{
public:
	~Library()
	{
		if (DllHandle)
		{
			FPlatformProcess::FreeDllHandle(DllHandle);
			DllHandle = nullptr;
		}
	}

	// The singleton for the library.
	static Library & Ref()
	{
		static Library LibraryInstance;
		return LibraryInstance;
	}

	// Set the name of the DLLs (or SO, or DYLIB) to load when fetching symbols.
	void SetName(const char * Name)
	{
		if (!LibName) LibName = Name;
	}

	void * GetEntry(const char * Name)
	{
		if (!DllHandle)
		{
			// The library name need to be set for us to load it. I.e. someone needs to
			// call SetName before calling GetEntry. Normally this is done by the
			// DelayCall class below.
			check(LibName != nullptr);
#if PLATFORM_LUMIN
			const LuminLibraryLoader* LibraryLoader = &LuminLibraryLoader::Ref();
#else
			const IMagicLeapLibraryLoader* LibraryLoader = static_cast<const IMagicLeapLibraryLoader*>(FModuleManager::Get().LoadModule(TEXT("MLSDK")));
#endif // PLATFORM_LUMIN
			if (LibraryLoader != nullptr)
			{
				DllHandle = LibraryLoader->LoadDLL(ANSI_TO_TCHAR(LibName));
			}
		}
		if (DllHandle)
		{
			check(Name != nullptr);
			return FPlatformProcess::GetDllExport(DllHandle, ANSI_TO_TCHAR(Name));
		}
		return nullptr;
	}

private:
	const char * LibName = nullptr;
	void * DllHandle = nullptr;
};

#if LUMIN_MLSDK_API_USE_STUBS
// Special case for void return type
template<typename ReturnType> inline typename std::enable_if<std::is_void<ReturnType>::value, ReturnType>::type DefaultReturn()
{
}
// Special case for all pointer return types
template<typename ReturnType> inline typename std::enable_if<std::is_pointer<ReturnType>::value, ReturnType>::type DefaultReturn()
{
	return nullptr;
}
// Value type cases
template<typename ReturnType> inline ReturnType DefaultValue()
{
	ReturnType returnVal;
	FMemory::Memzero(&returnVal, sizeof(ReturnType));
	return returnVal;
}
template<> inline MLResult DefaultValue()
{
	return MLResult_NotImplemented;
}
template<typename ReturnType> inline typename std::enable_if<!std::is_void<ReturnType>::value && !std::is_pointer<ReturnType>::value, ReturnType>::type DefaultReturn()
{
	return DefaultValue<ReturnType>();
}
#endif // LUMIN_MLSDK_API_USE_STUBS

/** This is a single delay loaded entry value. The class in keyed on both the library and function,
	as types. When first created it will try and load the pointer to the named global value. */
template <typename LibKey, typename Key, typename T>
class DelayValue
{
public:
	DelayValue(const char* LibName, const char* EntryName)
	{
		if (!Value) 
		{
			Library<LibKey>::Ref().SetName(LibName);
			Value = static_cast<T*>(Library<LibKey>::Ref().GetEntry(EntryName));
		}
	}

	T Get() 
	{
		return Value ? *Value : DefaultReturn<T>();
	}
	
private:
	static T* Value;

};

template <typename LibKey, typename Key, typename T>
T* DelayValue<LibKey, Key, T>::Value = nullptr;

/** This is a single delay loaded entry call. The class in keyed on both the library and function,
	as types. When first used as a function it will attempt to retrieve the foreign entry and
	call it. Onward the retrieved entry is called directly. */
template <typename LibKey, typename Key, typename Result, typename... Args>
class DelayCall
{
public:
	// On construction we use the given LibName to set the library name of that singleton.
	DelayCall(const char *LibName, const char *EntryName)
		: EntryName(EntryName)
	{
		Library<LibKey>::Ref().SetName(LibName);
		Self() = this;
	}

	Result operator()(Args... args)
	{
#if LUMIN_MLSDK_API_USE_STUBS
		if (*Call == nullptr)
		{
			return DefaultReturn<Result>();
		}
#endif // LUMIN_MLSDK_API_USE_STUBS
		return (*Call)(args...);
	}

private:
	typedef Result (*CallPointer)(Args...);

	const char *EntryName = nullptr;
	CallPointer Call = &LoadAndCall;

	static DelayCall * & Self()
	{
		static DelayCall * DelayCallInstance = nullptr;
		return DelayCallInstance;
	}

	// This is the default for a call. After it's called the call destination becomes the
	// call entry in the loaded library. Which bypasses this call to avoid as much overhead
	// as possible.
	static Result LoadAndCall(Args... args)
	{
		Self()->Call = (CallPointer)(Library<LibKey>::Ref().GetEntry(Self()->EntryName));
#if LUMIN_MLSDK_API_USE_STUBS
		if (Self()->Call == nullptr)
		{
			return DefaultReturn<Result>();
		}
#endif // LUMIN_MLSDK_API_USE_STUBS
		return (*Self()->Call)(args...);
	}
};

} // namespace LUMIN_MLSDK_API

#if defined(LUMIN_MLSDK_API_NO_DEPRECATION_WARNING)
#define LUMIN_MLSDK_API_DEPRECATED_MSG(msg)
#define LUMIN_MLSDK_API_DEPRECATED
#else
#if defined(_MSC_VER) && _MSC_VER
#define LUMIN_MLSDK_API_DEPRECATED_MSG(msg) __declspec(deprecated(msg))
#define LUMIN_MLSDK_API_DEPRECATED __declspec(deprecated)
#else
#define LUMIN_MLSDK_API_DEPRECATED_MSG(msg) __attribute__((deprecated(msg)))
#define LUMIN_MLSDK_API_DEPRECATED __attribute__((deprecated))
#endif
#endif
