// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HAL/Platform.h"
#include "Stats/Stats.h"

#if !defined(WITH_MLSDK) || WITH_MLSDK

#if defined(ML_NO_DEPRECATION_WARNING)
#define LUMIN_MLSDK_API_NO_DEPRECATION_WARNING
#endif
#define ML_NO_DEPRECATION_WARNING
#define ML_NO_DEPRECATION_DISABLED_MSG

#if PLATFORM_WINDOWS

	#ifndef LUMIN_THIRD_PARTY_INCLUDES_START
		#define LUMIN_THIRD_PARTY_INCLUDES_START \
			__pragma(warning(push)) \
			__pragma(warning(disable: 4201)) \
			THIRD_PARTY_INCLUDES_START
	#endif

	#ifndef LUMIN_THIRD_PARTY_INCLUDES_END
		#define LUMIN_THIRD_PARTY_INCLUDES_END \
			__pragma(warning(pop)) \
			THIRD_PARTY_INCLUDES_END
	#endif

#else

	#ifndef LUMIN_THIRD_PARTY_INCLUDES_START
		#define LUMIN_THIRD_PARTY_INCLUDES_START \
			THIRD_PARTY_INCLUDES_START
	#endif

	#ifndef LUMIN_THIRD_PARTY_INCLUDES_END
		#define LUMIN_THIRD_PARTY_INCLUDES_END \
			THIRD_PARTY_INCLUDES_END
	#endif

#endif

LUMIN_THIRD_PARTY_INCLUDES_START
#include <ml_api.h>
LUMIN_THIRD_PARTY_INCLUDES_END

// We default to delay loaded calling as we have multiple
// sets of libraries to load on desktop platforms.
// Enabled for lumin as well to load only necesarry libraries.
#ifndef LUMIN_MLSDK_API_USE_STUBS
#define LUMIN_MLSDK_API_USE_STUBS 1
#endif

#include "LuminAPIImpl.h"

DECLARE_STATS_GROUP(TEXT("MLAPI"), STATGROUP_MLAPI, STATCAT_Advanced);

namespace LUMIN_MLSDK_API
{
#if LUMIN_MLSDK_API_USE_STUBS
	#define CREATE_FUNCTION_SHIM(library_name, return_type, function_name) \
		DECLARE_CYCLE_STAT(TEXT(#function_name), STAT_ ## function_name, STATGROUP_MLAPI); \
		struct library_name; \
		struct function_name ## DelayCall; \
		template<typename... Args> return_type function_name ## Shim(Args... args) \
		{ \
			SCOPE_CYCLE_COUNTER(STAT_ ## function_name); \
			static DelayCall<library_name, function_name ## DelayCall, return_type, Args...> Call{ #library_name, #function_name }; \
			return Call(args...); \
		}

	#define CREATE_GLOBAL_SHIM(library_name, return_type, variable_name) \
		struct library_name; \
		struct variable_name ## DelayCall; \
		inline return_type variable_name ## Shim() \
		{ \
			static DelayValue<library_name, variable_name ## DelayCall, return_type> DelayedValue{ #library_name, #variable_name }; \
			return DelayedValue.Get(); \
		}
	#define CREATE_DEPRECATED_SHIM(library_name, return_type, function_name) \
		DECLARE_CYCLE_STAT(TEXT(#function_name), STAT_ ## function_name, STATGROUP_MLAPI); \
		struct library_name; \
		struct function_name ## DelayCall; \
		template<typename... Args> LUMIN_MLSDK_API_DEPRECATED MLResult function_name ## Shim(Args... args) \
		{ \
			SCOPE_CYCLE_COUNTER(STAT_ ## function_name); \
			static DelayCall<library_name, function_name ## DelayCall, return_type, Args...> Call{ #library_name, #function_name }; \
			return Call(args...); \
		}

	#define CREATE_DEPRECATED_MSG_SHIM(library_name, return_type, function_name, deprecation_msg) \
		DECLARE_CYCLE_STAT(TEXT(#function_name), STAT_ ## function_name, STATGROUP_MLAPI); \
		struct library_name; \
		struct function_name ## DelayCall; \
		template<typename... Args> LUMIN_MLSDK_API_DEPRECATED_MSG(deprecation_msg) MLResult function_name ## Shim(Args... args) \
		{ \
			SCOPE_CYCLE_COUNTER(STAT_ ## function_name); \
			static DelayCall<library_name, function_name ## DelayCall, return_type, Args...> Call{ #library_name, #function_name }; \
			return Call(args...); \
		}
#else
	#define CREATE_FUNCTION_SHIM(library_name, return_type, function_name) \
		DECLARE_CYCLE_STAT(TEXT(#function_name), STAT_ ## function_name, STATGROUP_MLAPI); \
		template<typename... Args>  return_type function_name##Shim(Args... args) \
		{ \
			SCOPE_CYCLE_COUNTER(STAT_ ## function_name); \
			return ::function_name(args...); \
		}
	#define CREATE_GLOBAL_SHIM(library_name, return_type, variable_name) \
		inline return_type variable_name ## Shim() \
		{ \
			return ::variable_name; \
		}
	#define CREATE_DEPRECATED_SHIM(library_name, return_type, function_name) \
		DECLARE_CYCLE_STAT(TEXT(#function_name), STAT_ ## function_name, STATGROUP_MLAPI); \
		template<typename... Args>  LUMIN_MLSDK_API_DEPRECATED return_type function_name##Shim(Args... args) \
		{ \
			SCOPE_CYCLE_COUNTER(STAT_ ## function_name); \
			return ::function_name(args...); \
		}
	#define CREATE_DEPRECATED_MSG_SHIM(library_name, return_type, function_name, deprecation_msg) \
		DECLARE_CYCLE_STAT(TEXT(#function_name), STAT_ ## function_name, STATGROUP_MLAPI); \
		template<typename... Args>  LUMIN_MLSDK_API_DEPRECATED_MSG(deprecation_msg) return_type function_name##Shim(Args... args) \
		{ \
			SCOPE_CYCLE_COUNTER(STAT_ ## function_name); \
			return ::function_name(args...); \
		}
#endif

}

#endif // !defined(WITH_MLSDK) || WITH_MLSDK
