// Copyright Epic Games, Inc. All Rights Reserved.

#include "Containers/ContainerAllocationPolicies.h"
#include "Containers/ResourceArray.h"
#include "Misc/StringBuilder.h"
#include "Serialization/MemoryImage.h"

IMPLEMENT_ABSTRACT_TYPE_LAYOUT(FResourceArrayInterface);

FMemoryImageAllocatorBase::~FMemoryImageAllocatorBase()
{
	if (!Data.IsFrozen())
	{
		FScriptContainerElement* LocalData = Data.Get();
		if (LocalData)
		{
			FMemory::Free(LocalData);
		}
	}
}

void FMemoryImageAllocatorBase::MoveToEmpty(FMemoryImageAllocatorBase& Other)
{
	checkSlow(this != &Other);
	if (!Data.IsFrozen())
	{
		FScriptContainerElement* LocalData = Data.Get();
		if (LocalData)
		{
			FMemory::Free(LocalData);
		}
	}
	Data = Other.Data;
	Other.Data = nullptr;
}

void FMemoryImageAllocatorBase::ResizeAllocation(int32 PreviousNumElements, int32 NumElements, SIZE_T NumBytesPerElement, uint32 Alignment)
{
	FScriptContainerElement* LocalData = Data.Get();
	// Avoid calling FMemory::Realloc( nullptr, 0 ) as ANSI C mandates returning a valid pointer which is not what we want.
	if (Data.IsFrozen())
	{
		// Can't grow a frozen array
		check(NumElements <= PreviousNumElements);
	}
	else if (LocalData || NumElements > 0)
	{
		Data = (FScriptContainerElement*)FMemory::Realloc(LocalData, NumElements*NumBytesPerElement, Alignment);
	}
}

void FMemoryImageAllocatorBase::WriteMemoryImage(FMemoryImageWriter& Writer, const FTypeLayoutDesc& TypeDesc, int32 NumAllocatedElements, uint32 Alignment) const
{
	if (NumAllocatedElements > 0)
	{
		const void* RawPtr = GetAllocation();
		check(RawPtr);
		FMemoryImageWriter ArrayWriter = Writer.WritePointer(FString::Printf(TEXT("FMemoryImageAllocator<%s>"), TypeDesc.Name));
		ArrayWriter.AddDependency(TypeDesc);
		ArrayWriter.WriteAlignment(Alignment);
		ArrayWriter.WriteObjectArray(RawPtr, TypeDesc, NumAllocatedElements);
	}
	else
	{
		Writer.WriteMemoryImagePointerSizedBytes(0u);
	}
}

void FMemoryImageAllocatorBase::CopyUnfrozen(const FMemoryUnfreezeContent& Context, const FTypeLayoutDesc& TypeDesc, int32 NumAllocatedElements, void* OutDst) const
{
	if (NumAllocatedElements > 0)
	{
		const void* RawPtr = GetAllocation();
		FTypeLayoutDesc::FUnfrozenCopyFunc* Func = TypeDesc.UnfrozenCopyFunc;
		const uint32 ElementSize = TypeDesc.Size;

		for (int32 i = 0; i < NumAllocatedElements; ++i)
		{
			Func(Context,
				(uint8*)RawPtr + ElementSize * i,
				TypeDesc,
				(uint8*)OutDst + ElementSize * i);
		}
	}
}

void FMemoryImageAllocatorBase::ToString(const FTypeLayoutDesc& TypeDesc, int32 NumAllocatedElements, const FPlatformTypeLayoutParameters& LayoutParams, FMemoryToStringContext& OutContext) const
{
	OutContext.String->Appendf(TEXT("TArray<%s>, Num: %d\n"), TypeDesc.Name, NumAllocatedElements);
	++OutContext.Indent;

	const void* RawPtr = GetAllocation();
	FTypeLayoutDesc::FToStringFunc* Func = TypeDesc.ToStringFunc;
	const uint32 ElementSize = TypeDesc.Size;

	for (int32 i = 0; i < NumAllocatedElements; ++i)
	{
		OutContext.AppendIndent();
		OutContext.String->Appendf(TEXT("[%d]: "), i);
		Func((uint8*)RawPtr + ElementSize * i,
			TypeDesc,
			LayoutParams,
			OutContext);
	}

	--OutContext.Indent;
}
