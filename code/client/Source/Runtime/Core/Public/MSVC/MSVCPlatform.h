// Copyright Epic Games, Inc. All Rights Reserved.

/*================================================================================
	MSVCPlatform.h: Setup for any MSVC-using platform
==================================================================================*/

#pragma once

#if _MSC_VER >= 1920
	#define PLATFORM_COMPILER_HAS_IF_CONSTEXPR 1
#else
	#define PLATFORM_COMPILER_HAS_IF_CONSTEXPR 0
#endif

#if defined(__cpp_fold_expressions)
	#define PLATFORM_COMPILER_HAS_FOLD_EXPRESSIONS 1
#else
	#define PLATFORM_COMPILER_HAS_FOLD_EXPRESSIONS 0
#endif
