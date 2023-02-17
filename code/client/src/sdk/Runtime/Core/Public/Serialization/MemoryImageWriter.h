// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Serialization/MemoryLayout.h"

struct FMinimalName;
struct FScriptName;
class FMemoryImage;
class FMemoryImageSection;
class FPointerTableBase;

class CORE_API FMemoryImageWriter
{
public:
	explicit FMemoryImageWriter(FMemoryImage& InImage);
	explicit FMemoryImageWriter(FMemoryImageSection* InSection);
	~FMemoryImageWriter();

	FMemoryImage& GetImage() const;
	const FPlatformTypeLayoutParameters& GetHostLayoutParams() const;
	const FPlatformTypeLayoutParameters& GetTargetLayoutParams() const;
	FPointerTableBase& GetPointerTable() const;
	const FPointerTableBase* TryGetPrevPointerTable() const;

	inline bool Is32BitTarget() const { return GetTargetLayoutParams().Is32Bit(); }
	inline bool Is64BitTarget() const { return !Is32BitTarget(); }

	void AddDependency(const FTypeLayoutDesc& TypeDesc);

	void WriteObject(const void* Object, const FTypeLayoutDesc& TypeDesc);
	void WriteObjectArray(const void* Object, const FTypeLayoutDesc& TypeDesc, uint32_t NumArray);

	uint32 GetOffset() const;
	uint32 WriteAlignment(uint32 Alignment);
	void WritePaddingToSize(uint32 Offset);
	uint32 WriteBytes(const void* Data, uint32 Size);
	FMemoryImageWriter WritePointer(const FString& SectionName, uint32 Offset = 0u);
	uint32 WriteRawPointerSizedBytes(uint64 PointerValue);
	uint32 WriteMemoryImagePointerSizedBytes(uint64 PointerValue);
	uint32 WriteVTable(const FTypeLayoutDesc& TypeDesc, const FTypeLayoutDesc& DerivedTypeDesc);
	uint32 WriteFName(const FName& Name);
	uint32 WriteFMinimalName(const FMinimalName& Name);
	uint32 WriteFScriptName(const FScriptName& Name);

	template<typename T>
	void WriteObjectArray(const T* Object, uint32 NumArray)
	{
		const FTypeLayoutDesc& TypeDesc = StaticGetTypeLayoutDesc<T>();
		WriteObjectArray(Object, TypeDesc, TypeDesc, NumArray);
	}

	template<typename T>
	void WriteObject(const T& Object)
	{
		WriteObject(&Object, GetTypeLayoutDesc(TryGetPrevPointerTable(), Object));
	}

	template<typename T>
	uint32 WriteAlignment()
	{
		return WriteAlignment(alignof(T));
	}

	template<typename T>
	uint32 WriteBytes(const T& Data)
	{
		return WriteBytes(&Data, sizeof(T));
	}

//private:
	FMemoryImageSection* Section;
};

class FMemoryUnfreezeContent
{
public:
	FMemoryUnfreezeContent() : PrevPointerTable(nullptr) {}

	const FPointerTableBase* TryGetPrevPointerTable() const { return PrevPointerTable; }

	void UnfreezeObject(const void* Object, const FTypeLayoutDesc& TypeDesc, void* OutDst) const
	{
		TypeDesc.UnfrozenCopyFunc(*this, Object, TypeDesc, OutDst);
	}

	template<typename T>
	void UnfreezeObject(const T& Object, void* OutDst) const
	{
		const FTypeLayoutDesc& TypeDesc = GetTypeLayoutDesc(TryGetPrevPointerTable(), Object);
		UnfreezeObject(&Object, TypeDesc, OutDst);
	}

	const FPointerTableBase* PrevPointerTable;
};
