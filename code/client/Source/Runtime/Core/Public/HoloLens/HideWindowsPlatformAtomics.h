// Copyright Epic Games, Inc. All Rights Reserved.

#ifdef WINDOWS_PLATFORM_ATOMICS_GUARD
	#undef WINDOWS_PLATFORM_ATOMICS_GUARD
#else
	#error Mismatched HideWindowsPlatformAtomics.h detected.
#endif

#undef InterlockedIncrement
#undef InterlockedDecrement
#undef InterlockedAdd
#undef InterlockedExchange
#undef InterlockedExchangeAdd
#undef InterlockedCompareExchange
#undef InterlockedCompareExchangePointer
#undef InterlockedExchange64
#undef InterlockedExchangeAdd64
#undef InterlockedCompareExchange64
#undef InterlockedIncrement64
#undef InterlockedDecrement64

