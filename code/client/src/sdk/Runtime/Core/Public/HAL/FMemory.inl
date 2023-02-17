// Copyright Epic Games, Inc. All Rights Reserved.

#if !defined(FMEMORY_INLINE_FUNCTION_DECORATOR)
	#define FMEMORY_INLINE_FUNCTION_DECORATOR 
#endif

#if !defined(FMEMORY_INLINE_GMalloc)
	#define FMEMORY_INLINE_GMalloc GMalloc
#endif

#include "HAL/LowLevelMemTracker.h"

struct FMemory;
struct FScopedMallocTimer;

FMEMORY_INLINE_FUNCTION_DECORATOR void* FMemory::Malloc(SIZE_T Count, uint32 Alignment)
{
	void* Ptr;
	if (!FMEMORY_INLINE_GMalloc)
	{
		Ptr = MallocExternal(Count, Alignment);
	}
	else
	{
		DoGamethreadHook(0);
		FScopedMallocTimer Timer(0);
		Ptr = FMEMORY_INLINE_GMalloc->Malloc(Count, Alignment);
	}
	// optional tracking of every allocation
	LLM_IF_ENABLED(FLowLevelMemTracker::Get().OnLowLevelAlloc(ELLMTracker::Default, Ptr, Count, ELLMTag::Untagged, ELLMAllocType::FMalloc));
	return Ptr;
}

FMEMORY_INLINE_FUNCTION_DECORATOR void* FMemory::Realloc(void* Original, SIZE_T Count, uint32 Alignment)
{
	// optional tracking -- a realloc with an Original pointer of null is equivalent
	// to malloc() so there's nothing to free
	LLM_REALLOC_SCOPE(Original);
	LLM_IF_ENABLED(if (Original != nullptr) FLowLevelMemTracker::Get().OnLowLevelFree(ELLMTracker::Default, Original, ELLMAllocType::FMalloc));

	void* Ptr;
	if (!FMEMORY_INLINE_GMalloc)
	{
		Ptr = ReallocExternal(Original, Count, Alignment);
	}
	else
	{
		DoGamethreadHook(1);
		FScopedMallocTimer Timer(1);
		Ptr = FMEMORY_INLINE_GMalloc->Realloc(Original, Count, Alignment);
	}

	// optional tracking of every allocation - a realloc with a Count of zero is equivalent to a call 
	// to free() and will return a null pointer which does not require tracking. If realloc returns null
	// for some other reason (like failure to allocate) there's also no reason to track it
	LLM_IF_ENABLED(if (Ptr != nullptr) FLowLevelMemTracker::Get().OnLowLevelAlloc(ELLMTracker::Default, Ptr, Count, ELLMTag::Untagged, ELLMAllocType::FMalloc));

	return Ptr;
}

FMEMORY_INLINE_FUNCTION_DECORATOR void FMemory::Free(void* Original)
{
	if (!Original)
	{
		FScopedMallocTimer Timer(3);
		return;
	}

	// optional tracking of every allocation
	LLM_IF_ENABLED(FLowLevelMemTracker::Get().OnLowLevelFree(ELLMTracker::Default, Original, ELLMAllocType::FMalloc));

	if (!FMEMORY_INLINE_GMalloc)
	{
		FreeExternal(Original);
		return;
	}
	DoGamethreadHook(2);
	FScopedMallocTimer Timer(2);
	FMEMORY_INLINE_GMalloc->Free(Original);
}

FMEMORY_INLINE_FUNCTION_DECORATOR SIZE_T FMemory::GetAllocSize(void* Original)
{
	if (!FMEMORY_INLINE_GMalloc)
	{
		return GetAllocSizeExternal(Original);
	}

	SIZE_T Size = 0;
	return FMEMORY_INLINE_GMalloc->GetAllocationSize(Original, Size) ? Size : 0;
}

FMEMORY_INLINE_FUNCTION_DECORATOR SIZE_T FMemory::QuantizeSize(SIZE_T Count, uint32 Alignment)
{
	if (!FMEMORY_INLINE_GMalloc)
	{
		return Count;
	}
	return FMEMORY_INLINE_GMalloc->QuantizeSize(Count, Alignment);
}

