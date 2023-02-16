// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/Array.h"
#include "Containers/Set.h"
#include "Containers/UnrealString.h"
#include "Misc/SecureHash.h"
#include "Serialization/Archive.h"
#include "Serialization/MemoryLayout.h"
#include "Serialization/MemoryImageWriter.h"
#include "Templates/RefCounting.h"

#if defined(WITH_RTTI) || defined(_CPPRTTI) || defined(__GXX_RTTI) || WITH_EDITOR
#include <typeinfo>
#endif

class FMemoryImage;
class FMemoryImageString;

class FPointerTableBase
{
public:
	virtual ~FPointerTableBase() {}
	virtual int32 AddIndexedPointer(const FTypeLayoutDesc& TypeDesc, void* Ptr) = 0;
	virtual void* GetIndexedPointer(const FTypeLayoutDesc& TypeDesc, uint32 i) const = 0;
};

struct FMemoryImageVTablePointer
{
	uint64 TypeNameHash;
	uint32 VTableOffset;
	uint32 Offset;
};
inline bool operator==(const FMemoryImageVTablePointer& Lhs, const FMemoryImageVTablePointer& Rhs)
{
	return Lhs.TypeNameHash == Rhs.TypeNameHash && Lhs.VTableOffset == Rhs.VTableOffset && Lhs.Offset == Rhs.Offset;
}
inline bool operator!=(const FMemoryImageVTablePointer& Lhs, const FMemoryImageVTablePointer& Rhs)
{
	return !operator==(Lhs, Rhs);
}
inline bool operator<(const FMemoryImageVTablePointer& Lhs, const FMemoryImageVTablePointer& Rhs)
{
	if (Lhs.TypeNameHash != Rhs.TypeNameHash) return Lhs.TypeNameHash < Rhs.TypeNameHash;
	if (Lhs.VTableOffset != Rhs.VTableOffset) return Lhs.VTableOffset < Rhs.VTableOffset;
	return Lhs.Offset < Rhs.Offset;
}

struct FMemoryImageNamePointer
{
	FName Name;
	uint32 Offset;
};
inline bool operator==(const FMemoryImageNamePointer& Lhs, const FMemoryImageNamePointer& Rhs)
{
	return Lhs.Offset == Rhs.Offset && Lhs.Name == Rhs.Name;
}
inline bool operator!=(const FMemoryImageNamePointer& Lhs, const FMemoryImageNamePointer& Rhs)
{
	return !operator==(Lhs, Rhs);
}
inline bool operator<(const FMemoryImageNamePointer& Lhs, const FMemoryImageNamePointer& Rhs)
{
	if (Lhs.Name != Rhs.Name)
	{
		return Lhs.Name.LexicalLess(Rhs.Name);
	}
	return Lhs.Offset < Rhs.Offset;
}

struct FMemoryImageResult
{
	TArray<uint8> Bytes;
	TArray<FMemoryImageVTablePointer> VTables;
	TArray<FMemoryImageNamePointer> ScriptNames;
	TArray<FMemoryImageNamePointer> MinimalNames;

	CORE_API void SaveToArchive(FArchive& Ar) const;
	CORE_API void ApplyPatches(void* FrozenObject) const;
	CORE_API static void ApplyPatchesFromArchive(void* FrozenObject, FArchive& Ar);
};

class CORE_API FMemoryImageSection : public FRefCountedObject
{
public:
	struct FSectionPointer
	{
		uint32 SectionIndex;
		uint32 PointerOffset;
		uint32 Offset;
	};

	FMemoryImageSection(FMemoryImage* InImage, FString InName)
		: ParentImage(InImage)
		, DebugName(InName)
		, MaxAlignment(1u)
	{}

	uint32 GetOffset() const { return Bytes.Num(); }

	uint32 WriteAlignment(uint32 Alignment)
	{
		const uint32 PrevSize = Bytes.Num();
		const uint32 Offset = Align(PrevSize, Alignment);
		Bytes.SetNumZeroed(Offset);
		MaxAlignment = FMath::Max(MaxAlignment, Alignment);
		return Offset;
	}

	void WritePaddingToSize(uint32 Offset)
	{
		check(Offset >= (uint32)Bytes.Num());
		Bytes.SetNumZeroed(Offset);
	}

	uint32 WriteBytes(const void* Data, uint32 Size)
	{
		const uint32 Offset = GetOffset();
		Bytes.SetNumUninitialized(Offset + Size);
		FMemory::Memcpy(Bytes.GetData() + Offset, Data, Size);
		return Offset;
	}

	template<typename T>
	uint32 WriteBytes(const T& Data) { return WriteBytes(&Data, sizeof(T)); }

	FMemoryImageSection* WritePointer(const FString& SectionName, uint32 Offset = 0u);
	uint32 WriteMemoryImagePointerSizedBytes(uint64 PointerValue);
	uint32 WriteRawPointerSizedBytes(uint64 PointerValue);
	uint32 WriteVTable(const FTypeLayoutDesc& TypeDesc, const FTypeLayoutDesc& DerivedTypeDesc);
	uint32 WriteFName(const FName& Name);
	uint32 WriteFMinimalName(const FMinimalName& Name);
	uint32 WriteFScriptName(const FScriptName& Name);
	uint32 Flatten(FMemoryImageResult& OutResult) const;

	void ComputeHash();

	FMemoryImage* ParentImage;
	FString DebugName;
	TArray<uint8> Bytes;
	TArray<FSectionPointer> Pointers;
	TArray<FMemoryImageVTablePointer> VTables;
	TArray<FMemoryImageNamePointer> ScriptNames;
	TArray<FMemoryImageNamePointer> MinimalNames;
	FSHAHash Hash;
	uint32 MaxAlignment;
};

class CORE_API FMemoryImage
{
public:
	FMemoryImage()
		: PointerTable(nullptr)
		, PrevPointerTable(nullptr)
		, CurrentStruct(nullptr)
	{
		HostLayoutParameters.InitializeForCurrent();
	}

	FPointerTableBase& GetPointerTable() const { check(PointerTable); return *PointerTable; }
	const FPointerTableBase& GetPrevPointerTable() const { check(PrevPointerTable); return *PrevPointerTable; }

	FMemoryImageSection* AllocateSection(const FString& Name)
	{
		FMemoryImageSection* Section = new FMemoryImageSection(this, Name);
		Sections.Add(Section);
		return Section;
	}

	void AddDependency(const FTypeLayoutDesc& TypeDesc);

	/** Merging duplicate sections will make the resulting memory image smaller.
	 * This will only work for data that is expected to be read-only after freezing.  Merging sections will break any manual fix-ups applied to the frozen data
	 */
	void Flatten(FMemoryImageResult& OutResult, bool bMergeDuplicateSections = false);

	TArray<TRefCountPtr<FMemoryImageSection>> Sections;
	TArray<const FTypeLayoutDesc*> TypeDependencies;
	FPointerTableBase* PointerTable;
	const FPointerTableBase* PrevPointerTable;
	FPlatformTypeLayoutParameters HostLayoutParameters;
	FPlatformTypeLayoutParameters TargetLayoutParameters;
	const class UStruct* CurrentStruct;
};

template<typename T>
class TMemoryImagePtr
{
public:
	inline bool IsFrozen() const { return OffsetFromThis & IsFrozenMask; }
	inline bool IsValid() const { return Ptr != nullptr; }
	inline bool IsNull() const { return Ptr == nullptr; }

	inline TMemoryImagePtr(T* InPtr = nullptr) : Ptr(InPtr) {}
	inline TMemoryImagePtr(const TMemoryImagePtr<T>& InPtr) : Ptr(InPtr.Get()) {}
	inline TMemoryImagePtr& operator=(T* InPtr) { Ptr = InPtr; check((OffsetFromThis & AllFlags) == 0u); return *this; }
	inline TMemoryImagePtr& operator=(const TMemoryImagePtr<T>& InPtr) { Ptr = InPtr.Get(); check((OffsetFromThis & AllFlags) == 0u); return *this; }

	inline ~TMemoryImagePtr() 
	{

	}

	inline FMemoryImagePtrInt GetFrozenOffsetFromThis() const { check(IsFrozen()); return (OffsetFromThis >> OffsetShift); }

	inline T* Get() const
	{
		return IsFrozen() ? GetFrozenPtrInternal() : Ptr;
	}

	inline T* GetChecked() const { T* Value = Get(); check(Value); return Value; }
	inline T* operator->() const { return GetChecked(); }
	inline T& operator*() const { return *GetChecked(); }
	inline operator T*() const { return Get(); }

	void SafeDelete(const FPointerTableBase* PtrTable = nullptr)
	{
		T* RawPtr = Get();
		if (RawPtr)
		{
			DeleteObjectFromLayout(RawPtr, PtrTable, IsFrozen());
			Ptr = nullptr;
		}
	}

	void WriteMemoryImageWithDerivedType(FMemoryImageWriter& Writer, const FTypeLayoutDesc* DerivedTypeDesc) const
	{
		const T* RawPtr = Get();
		if (RawPtr)
		{
			check(DerivedTypeDesc);
			// Compile-time type of the thing we're pointing to
			const FTypeLayoutDesc& StaticTypeDesc = StaticGetTypeLayoutDesc<T>();
			// 'this' offset to adjust from the compile-time type to the run-time type
			const uint32 OffsetToBase = DerivedTypeDesc->GetOffsetToBase(StaticTypeDesc);

			FMemoryImageWriter PointerWriter = Writer.WritePointer(FString::Printf(TEXT("TMemoryImagePtr<%s>"), DerivedTypeDesc->Name), OffsetToBase);
			PointerWriter.WriteObject((uint8*)RawPtr - OffsetToBase, *DerivedTypeDesc);
		}
		else
		{
			Writer.WriteMemoryImagePointerSizedBytes(0u);
		}
	}

private:
	inline T* GetFrozenPtrInternal() const
	{
		return (T*)((char*)this + (OffsetFromThis >> OffsetShift));
	}

	enum
	{
		IsFrozenMask = (1 << 0),
		AllFlags = IsFrozenMask,
		OffsetShift = 1u,
	};

protected:
	union
	{
		T* Ptr;
		FMemoryImagePtrInt OffsetFromThis;
	};
};

namespace Freeze
{
	template<typename T>
	void IntrinsicWriteMemoryImage(FMemoryImageWriter& Writer, const TMemoryImagePtr<T>& Object, const FTypeLayoutDesc&)
	{
		T* RawPtr = Object.Get();
		if (RawPtr)
		{
			// Compile-time type of the thing we're pointing to
			const FTypeLayoutDesc& StaticTypeDesc = StaticGetTypeLayoutDesc<T>();
			// Actual run-time type of the thing we're pointing to
			const FTypeLayoutDesc& DerivedTypeDesc = GetTypeLayoutDesc(Writer.TryGetPrevPointerTable(), *RawPtr);
			// 'this' offset to adjust from the compile-time type to the run-time type
			const uint32 OffsetToBase = DerivedTypeDesc.GetOffsetToBase(StaticTypeDesc);

			FMemoryImageWriter PointerWriter = Writer.WritePointer(FString::Printf(TEXT("TMemoryImagePtr<%s>"), DerivedTypeDesc.Name), OffsetToBase);
			PointerWriter.WriteObject((uint8*)RawPtr - OffsetToBase, DerivedTypeDesc);
		}
		else
		{
			Writer.WriteMemoryImagePointerSizedBytes(0u);
		}
	}

	template<typename T>
	void IntrinsicUnfrozenCopy(const FMemoryUnfreezeContent& Context, const TMemoryImagePtr<T>& Object, void* OutDst)
	{
		T* RawPtr = Object.Get();
		if (RawPtr)
		{
			// Compile-time type of the thing we're pointing to
			const FTypeLayoutDesc& StaticTypeDesc = StaticGetTypeLayoutDesc<T>();
			// Actual run-time type of the thing we're pointing to
			const FTypeLayoutDesc& DerivedTypeDesc = GetTypeLayoutDesc(Context.TryGetPrevPointerTable(), *RawPtr);
			// 'this' offset to adjust from the compile-time type to the run-time type
			const uint32 OffsetToBase = DerivedTypeDesc.GetOffsetToBase(StaticTypeDesc);

			void* UnfrozenMemory = FMemory::Malloc(DerivedTypeDesc.Size, DerivedTypeDesc.Alignment);
			Context.UnfreezeObject((uint8*)RawPtr - OffsetToBase, DerivedTypeDesc, UnfrozenMemory);
			T* UnfrozenPtr = (T*)((uint8*)UnfrozenMemory + OffsetToBase);
			new(OutDst) TMemoryImagePtr<T>(UnfrozenPtr);
		}
		else
		{
			new(OutDst) TMemoryImagePtr<T>(nullptr);
		}
	}

	template<typename T>
	uint32 IntrinsicAppendHash(const TMemoryImagePtr<T>* DummyObject, const FTypeLayoutDesc& TypeDesc, const FPlatformTypeLayoutParameters& LayoutParams, FSHA1& Hasher)
	{
		return AppendHashForNameAndSize(TypeDesc.Name, LayoutParams.GetMemoryImagePointerSize(), Hasher);
	}

	template<typename T>
	inline uint32 IntrinsicGetTargetAlignment(const TMemoryImagePtr<T>* DummyObject, const FTypeLayoutDesc& TypeDesc, const FPlatformTypeLayoutParameters& LayoutParams)
	{
		return FMath::Min(LayoutParams.GetMemoryImagePointerSize(), LayoutParams.MaxFieldAlignment);
	}

	template<typename T>
	void IntrinsicToString(const TMemoryImagePtr<T>& Object, const FTypeLayoutDesc& TypeDesc, const FPlatformTypeLayoutParameters& LayoutParams, FMemoryToStringContext& OutContext)
	{
		T* RawPtr = Object.Get();
		if (RawPtr)
		{
			// Compile-time type of the thing we're pointing to
			const FTypeLayoutDesc& StaticTypeDesc = StaticGetTypeLayoutDesc<T>();
			// Actual run-time type of the thing we're pointing to
			const FTypeLayoutDesc& DerivedTypeDesc = GetTypeLayoutDesc(OutContext.TryGetPrevPointerTable(), *RawPtr);
			// 'this' offset to adjust from the compile-time type to the run-time type
			const uint32 OffsetToBase = DerivedTypeDesc.GetOffsetToBase(StaticTypeDesc);

			DerivedTypeDesc.ToStringFunc((uint8*)RawPtr - OffsetToBase, DerivedTypeDesc, LayoutParams, OutContext);
		}
		else
		{
			OutContext.AppendNullptr();
		}
	}
}

DECLARE_TEMPLATE_INTRINSIC_TYPE_LAYOUT(template<typename T>, TMemoryImagePtr<T>);

template<typename T>
class TUniqueMemoryImagePtr : public TMemoryImagePtr<T>
{
public:
	inline TUniqueMemoryImagePtr()
		: TMemoryImagePtr<T>(nullptr)
	{}
	explicit inline TUniqueMemoryImagePtr(T* InPtr)
		: TMemoryImagePtr<T>(InPtr)
	{ }

	inline TUniqueMemoryImagePtr(TUniqueMemoryImagePtr&& Other)
	{
		this->SafeDelete();
		this->Ptr = Other.Ptr;
		Other.Ptr = nullptr;
	}
	inline ~TUniqueMemoryImagePtr()
	{
		this->SafeDelete();
	}
	inline TUniqueMemoryImagePtr& operator=(T* InPtr)
	{
		this->SafeDelete();
		this->Ptr = InPtr;
	}
	inline TUniqueMemoryImagePtr& operator=(TUniqueMemoryImagePtr&& Other)
	{
		if (this != &Other)
		{
			// We _should_ delete last like TUniquePtr, but we have issues with SafeDelete, and being Frozen or not
			this->SafeDelete();
			this->Ptr = Other.Ptr;
			Other.Ptr = nullptr;
		}

		return *this;
	}

};

class CORE_API FMemoryImageAllocatorBase
{
	UE_NONCOPYABLE(FMemoryImageAllocatorBase);
public:
	FMemoryImageAllocatorBase() = default;

	/**
	* Moves the state of another allocator into this one.
	* Assumes that the allocator is currently empty, i.e. memory may be allocated but any existing elements have already been destructed (if necessary).
	* @param Other - The allocator to move the state from.  This allocator should be left in a valid empty state.
	*/
	void MoveToEmpty(FMemoryImageAllocatorBase& Other);

	/** Destructor. */
	~FMemoryImageAllocatorBase();

	// FContainerAllocatorInterface
	FORCEINLINE FScriptContainerElement* GetAllocation() const
	{
		return Data.Get();
	}

	FORCEINLINE SIZE_T GetAllocatedSize(int32 NumAllocatedElements, SIZE_T NumBytesPerElement) const
	{
		return NumAllocatedElements * NumBytesPerElement;
	}
	FORCEINLINE bool HasAllocation()
	{
		return Data.IsValid();
	}
	FORCEINLINE FMemoryImagePtrInt GetFrozenOffsetFromThis() const { return Data.GetFrozenOffsetFromThis(); }

	void ResizeAllocation(int32 PreviousNumElements, int32 NumElements, SIZE_T NumBytesPerElement, uint32 Alignment);
	void WriteMemoryImage(FMemoryImageWriter& Writer, const FTypeLayoutDesc& TypeDesc, int32 NumAllocatedElements, uint32 Alignment) const;
	void ToString(const FTypeLayoutDesc& TypeDesc, int32 NumAllocatedElements, const FPlatformTypeLayoutParameters& LayoutParams, FMemoryToStringContext& OutContext) const;
	void CopyUnfrozen(const FMemoryUnfreezeContent& Context, const FTypeLayoutDesc& TypeDesc, int32 NumAllocatedElements, void* Dst) const;

private:
	TMemoryImagePtr<FScriptContainerElement> Data;
};

template<uint32 Alignment = DEFAULT_ALIGNMENT>
class TMemoryImageAllocator
{
public:
	using SizeType = int32;

	enum { NeedsElementType = false };
	enum { RequireRangeCheck = true };
	enum { SupportsFreezeMemoryImage = true };

	class ForAnyElementType : public FMemoryImageAllocatorBase
	{
	public:
		/** Default constructor. */
		ForAnyElementType() = default;

		FORCEINLINE SizeType GetInitialCapacity() const
		{
			return 0;
		}
		FORCEINLINE int32 CalculateSlackReserve(int32 NumElements, int32 NumBytesPerElement) const
		{
			return DefaultCalculateSlackReserve(NumElements, NumBytesPerElement, true, Alignment);
		}
		FORCEINLINE int32 CalculateSlackShrink(int32 NumElements, int32 NumAllocatedElements, int32 NumBytesPerElement) const
		{
			return DefaultCalculateSlackShrink(NumElements, NumAllocatedElements, NumBytesPerElement, true, Alignment);
		}
		FORCEINLINE int32 CalculateSlackGrow(int32 NumElements, int32 NumAllocatedElements, int32 NumBytesPerElement) const
		{
			return DefaultCalculateSlackGrow(NumElements, NumAllocatedElements, NumBytesPerElement, true, Alignment);
		}
		FORCEINLINE void ResizeAllocation(int32 PreviousNumElements, int32 NumElements, SIZE_T NumBytesPerElement)
		{
			FMemoryImageAllocatorBase::ResizeAllocation(PreviousNumElements, NumElements, NumBytesPerElement, Alignment);
		}

		FORCEINLINE void WriteMemoryImage(FMemoryImageWriter& Writer, const FTypeLayoutDesc& TypeDesc, int32 NumAllocatedElements) const
		{
			FMemoryImageAllocatorBase::WriteMemoryImage(Writer, TypeDesc, NumAllocatedElements, Alignment);
		}
	};

	template<typename ElementType>
	class ForElementType : public ForAnyElementType
	{
	public:
		ForElementType() {}
		FORCEINLINE ElementType* GetAllocation() const { return (ElementType*)ForAnyElementType::GetAllocation(); }
	};
};

//@todo stever
/*static_assert(
	sizeof(TMemoryImageAllocator<>::ForAnyElementType) == sizeof(FDefaultAllocator::ForAnyElementType) && alignof(TMemoryImageAllocator<>::ForAnyElementType) == alignof(FDefaultAllocator::ForAnyElementType),
	"TMemoryImageAllocator must be the same layout as FDefaultAllocator for our FScriptArray hacks to work"
);*/

template <uint32 Alignment>
struct TAllocatorTraits<TMemoryImageAllocator<Alignment>> : TAllocatorTraitsBase<TMemoryImageAllocator<Alignment>>
{
	enum { SupportsMove = true };
	enum { IsZeroConstruct = true };
	enum { SupportsFreezeMemoryImage = true };
};

using FMemoryImageAllocator = TMemoryImageAllocator<>;

using FMemoryImageSparseArrayAllocator = TSparseArrayAllocator<FMemoryImageAllocator, FMemoryImageAllocator>;
using FMemoryImageSetAllocator = TSetAllocator<FMemoryImageSparseArrayAllocator, FMemoryImageAllocator>;

template<typename T>
using TMemoryImageArray = TArray<T, FMemoryImageAllocator>;

template<typename ElementType, typename KeyFuncs = DefaultKeyFuncs<ElementType>>
using TMemoryImageSet = TSet<ElementType, KeyFuncs, FMemoryImageSetAllocator>;

template <typename KeyType, typename ValueType, typename KeyFuncs = TDefaultMapHashableKeyFuncs<KeyType, ValueType, false>>
using TMemoryImageMap = TMap<KeyType, ValueType, FMemoryImageSetAllocator, KeyFuncs>;

template <>
struct TIsContiguousContainer<FMemoryImageString>
{
	static constexpr bool Value = true;
};

class FMemoryImageString
{
	DECLARE_EXPORTED_TYPE_LAYOUT(FMemoryImageString, CORE_API, NonVirtual);
private:
	/** Array holding the character data */
	using DataType = TMemoryImageArray<TCHAR>;
	LAYOUT_FIELD(DataType, Data);

	CORE_API void ToString(FMemoryToStringContext& OutContext) const;
	LAYOUT_TOSTRING(ToString);
public:
	using ElementType = TCHAR;

	FMemoryImageString() = default;
	FMemoryImageString(FMemoryImageString&&) = default;
	FMemoryImageString(const FMemoryImageString&) = default;
	FMemoryImageString& operator=(FMemoryImageString&&) = default;
	FMemoryImageString& operator=(const FMemoryImageString&) = default;

	FORCEINLINE FMemoryImageString(const FString& Other) : Data(Other.GetCharArray()) {}

	template <
		typename CharType,
		typename = typename TEnableIf<TIsCharType<CharType>::Value>::Type // This TEnableIf is to ensure we don't instantiate this constructor for non-char types, like id* in Obj-C
	>
		FORCEINLINE FMemoryImageString(const CharType* Src)
	{
		if (Src && *Src)
		{
			int32 SrcLen = TCString<CharType>::Strlen(Src) + 1;
			int32 DestLen = FPlatformString::ConvertedLength<TCHAR>(Src, SrcLen);
			Data.AddUninitialized(DestLen);
			FPlatformString::Convert(Data.GetData(), DestLen, Src, SrcLen);
		}
	}

	FORCEINLINE operator FString() const { return FString(Len(), Data.GetData()); }

	FORCEINLINE const TCHAR* operator*() const
	{
		return Data.Num() ? Data.GetData() : TEXT("");
	}

	FORCEINLINE bool IsEmpty() const { return Data.Num() <= 1; }
	FORCEINLINE SIZE_T GetAllocatedSize() const { return Data.GetAllocatedSize(); }

	FORCEINLINE int32 Len() const
	{
		return Data.Num() ? Data.Num() - 1 : 0;
	}

	friend inline const TCHAR* GetData(const FMemoryImageString& String)
	{
		return *String;
	}

	friend inline int32 GetNum(const FMemoryImageString& String)
	{
		return String.Len();
	}

	friend inline FArchive& operator<<(FArchive& Ar, FMemoryImageString& Ref)
	{
		Ar << Ref.Data;
		return Ar;
	}

	friend inline bool operator==(const FMemoryImageString& Lhs, const FMemoryImageString& Rhs)
	{
		return FCString::Stricmp(*Lhs, *Rhs) == 0;
	}

	friend inline bool operator!=(const FMemoryImageString& Lhs, const FMemoryImageString& Rhs)
	{
		return !operator==(Lhs, Rhs);
	}

	friend inline bool operator==(const FMemoryImageString& Lhs, const FString& Rhs)
	{
		return FCString::Stricmp(*Lhs, *Rhs) == 0;
	}

	friend inline bool operator!=(const FMemoryImageString& Lhs, const FString& Rhs)
	{
		return !operator==(Lhs, Rhs);
	}

	inline DataType::ElementAllocatorType& GetAllocatorInstance() { return Data.GetAllocatorInstance(); }
};

/** Case insensitive string hash function. */
FORCEINLINE uint32 GetTypeHash(const FMemoryImageString& S)
{
	return FCrc::Strihash_DEPRECATED(*S);
}

#if WITH_EDITORONLY_DATA
struct FHashedNameDebugString
{
	TMemoryImagePtr<const char> String;
};

namespace Freeze
{
	void IntrinsicWriteMemoryImage(FMemoryImageWriter& Writer, const FHashedNameDebugString& Object, const FTypeLayoutDesc&);
	void IntrinsicUnfrozenCopy(const FMemoryUnfreezeContent& Context, const FHashedNameDebugString& Object, void* OutDst);
}

DECLARE_INTRINSIC_TYPE_LAYOUT(FHashedNameDebugString);

#endif // WITH_EDITORONLY_DATA

class FHashedName
{
	DECLARE_EXPORTED_TYPE_LAYOUT(FHashedName, CORE_API, NonVirtual);
public:
	inline FHashedName() : Hash(0u) {}
	CORE_API explicit FHashedName(uint64 InHash);
	CORE_API FHashedName(const TCHAR* InString);
	CORE_API FHashedName(const FString& InString);
	CORE_API FHashedName(const FName& InName);

	inline uint64 GetHash() const { return Hash; }
	inline bool IsNone() const { return Hash == 0u; }

#if WITH_EDITORONLY_DATA
	const FHashedNameDebugString& GetDebugString() const { return DebugString; }
#endif

	friend inline bool operator==(const FHashedName& Lhs, const FHashedName& Rhs) { return Lhs.Hash == Rhs.Hash; }
	friend inline bool operator!=(const FHashedName& Lhs, const FHashedName& Rhs) { return Lhs.Hash != Rhs.Hash; }

	/** For sorting by name */
	friend inline bool operator<(const FHashedName& Lhs, const FHashedName& Rhs) { return Lhs.Hash < Rhs.Hash; }

	friend inline FArchive& operator<<(FArchive& Ar, FHashedName& String)
	{
		Ar << String.Hash;
		return Ar;
	}

	friend inline uint32 GetTypeHash(const FHashedName& Name)
	{
		return GetTypeHash(Name.Hash);
	}

	/*inline FString ToString() const
	{
		return FString::Printf(TEXT("0x%016X"), Hash);
	}*/

private:
	LAYOUT_FIELD(uint64, Hash);
	LAYOUT_FIELD_EDITORONLY(FHashedNameDebugString, DebugString);
};

namespace Freeze
{
	CORE_API void IntrinsicToString(const FHashedName& Object, const FTypeLayoutDesc& TypeDesc, const FPlatformTypeLayoutParameters& LayoutParams, FMemoryToStringContext& OutContext);
}


class CORE_API FPtrTableBase
{
public:
	template<typename PtrType>
	static void LoadAndApplyPatchesFromArchive(FArchive& Ar, void* FrozenBase, const PtrType& Ptr)
	{
		int32 NumOffsets = 0;
		Ar << NumOffsets;
		for (int32 OffsetIndex = 0; OffsetIndex < NumOffsets; ++OffsetIndex)
		{
			uint32 Offset = 0u;
			Ar << Offset;
			new((char*)FrozenBase + Offset) PtrType(Ptr);
		}
	}

	void SavePatchesToArchive(FArchive& Ar, uint32 PtrIndex) const;

protected:
	struct FPatchOffset
	{
		uint32 Offset;
		uint32 NextIndex;
	};

	struct FPatchOffsetList
	{
		FPatchOffsetList() : FirstIndex(~0u), NumOffsets(0u) {}
		uint32 FirstIndex;
		uint32 NumOffsets;
	};

	void AddPatchedPointerBase(uint32 PtrIndex, uint64 Offset);

	TArray<FPatchOffsetList> PatchLists;
	TArray<FPatchOffset> PatchOffsets;
};

template<typename T, typename PtrType>
class TPtrTableBase : public FPtrTableBase
{
public:
	static const FTypeLayoutDesc& StaticGetPtrTypeLayoutDesc();

	void Empty(int32 NewSize = 0)
	{
		Pointers.Reset(NewSize);
	}

	uint32 Num() const { return Pointers.Num(); }
	uint32 AddIndexedPointer(T* Ptr) { check(Ptr); return Pointers.AddUnique(Ptr); }

	bool TryAddIndexedPtr(const FTypeLayoutDesc& TypeDesc, void* Ptr, int32& OutIndex)
	{
		if (TypeDesc == StaticGetPtrTypeLayoutDesc())
		{
			OutIndex = AddIndexedPointer(static_cast<T*>(Ptr));
			return true;
		}
		return false;
	}

	void LoadIndexedPointer(T* Ptr)
	{
		if (Ptr)
		{
			checkSlow(!Pointers.Contains(Ptr));
			Pointers.Add(Ptr);
		}
		else
		{
			// allow duplicate nullptrs
			// pointers that were valid when saving may not be found when loading, need to preserve indices
			Pointers.Add(nullptr);
		}
	}

	void AddPatchedPointer(T* Ptr, uint64 Offset)
	{
		const uint32 PtrIndex = AddIndexedPointer(Ptr);
		FPtrTableBase::AddPatchedPointerBase(PtrIndex, Offset);
	}

	T* GetIndexedPointer(uint32 i) const { return Pointers[i]; }

	bool TryGetIndexedPtr(const FTypeLayoutDesc& TypeDesc, uint32 i, void*& OutPtr) const
	{
		if (TypeDesc == StaticGetPtrTypeLayoutDesc())
		{
			OutPtr = GetIndexedPointer(i);
			return true;
		}
		return false;
	}

	void ApplyPointerPatches(void* FrozenBase) const
	{
		for (int32 PtrIndex = 0; PtrIndex < PatchLists.Num(); ++PtrIndex)
		{
			uint32 PatchIndex = PatchLists[PtrIndex].FirstIndex;
			while (PatchIndex != ~0u)
			{
				const FPatchOffset& Patch = PatchOffsets[PatchIndex];
				new((char*)FrozenBase + Patch.Offset) PtrType(Pointers[PtrIndex]);
				PatchIndex = Patch.NextIndex;
			}
		}
	}

	inline typename TArray<PtrType>::RangedForIteratorType begin() { return Pointers.begin(); }
	inline typename TArray<PtrType>::RangedForIteratorType end() { return Pointers.end(); }
	inline typename TArray<PtrType>::RangedForConstIteratorType begin() const { return Pointers.begin(); }
	inline typename TArray<PtrType>::RangedForConstIteratorType end() const { return Pointers.end(); }
private:
	TArray<PtrType> Pointers;
};

template<typename T>
class TPtrTable : public TPtrTableBase<T, T*> {};

template<typename T>
class TRefCountPtrTable : public TPtrTableBase<T, TRefCountPtr<T>>
{
	using Super = TPtrTableBase<T, TRefCountPtr<T>>;
};

class FVoidPtrTable : public TPtrTableBase<void, void*> {};

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4583) // destructor is not implicitly called
#endif

/**
 * Note that IndexedPtr types don't declare a default intrinsic type layout.
 * Instead any required pointer types need to be declared/implemented using DECLARE_EXPORTED_TEMPLATE_INTRINSIC_TYPE_LAYOUT/IMPLEMENT_TEMPLATE_INTRINSIC_TYPE_LAYOUT.
 * The TypeDesc of indexed pointers are compared for equality when adding to pointer tables,
 * and it's possible for inline type layouts to generate multiple copies when referenced from multiple modules
 */
template<typename T, typename PtrType>
class TIndexedPtrBase
{
public:
	using FPtrTable = TPtrTableBase<T, PtrType>;

	inline TIndexedPtrBase(T* InPtr = nullptr) : Ptr(InPtr) {}
	inline ~TIndexedPtrBase() { if(!IsFrozen()) Ptr.~PtrType(); }

	// Copy constructor requires an unfrozen source
	inline TIndexedPtrBase(const TIndexedPtrBase<T, PtrType>& Rhs) : Ptr(Rhs.GetUnfrozen()) {}

	inline TIndexedPtrBase(const TIndexedPtrBase<T, PtrType>& Rhs, const FPtrTable& InTable) : Ptr(Rhs.Get(InTable)) {}

	inline TIndexedPtrBase& operator=(T* Rhs)
	{
		// If not currently frozen, invoke the standard assignment operator for the underlying pointer type
		// If frozen, construct a new (non-frozen) pointer over the existing frozen offset
		if (!IsFrozen()) Ptr = Rhs;
		else new(&Ptr) PtrType(Rhs);
		check(!IsFrozen());
		return *this;
	}

	inline TIndexedPtrBase& operator=(const PtrType& Rhs)
	{
		if (!IsFrozen()) Ptr = Rhs;
		else new(&Ptr) PtrType(Rhs);
		check(!IsFrozen());
		return *this;
	}

	inline TIndexedPtrBase& operator=(PtrType&& Rhs)
	{
		if (!IsFrozen()) Ptr = Rhs;
		else new(&Ptr) PtrType(Rhs);
		check(!IsFrozen());
		return *this;
	}
	
	inline bool IsFrozen() const { return PackedIndex & IsFrozenMask; }
	inline bool IsValid() const { return PackedIndex != 0u; } // works for both frozen/unfrozen cases
	inline bool IsNull() const { return PackedIndex == 0u; }

	inline void SafeRelease()
	{
		if (!IsFrozen())
		{
			SafeReleaseImpl(Ptr);
		}
	}

	inline T* Get(const FPtrTable& PtrTable) const
	{
		if (IsFrozen())
		{
			return PtrTable.GetIndexedPointer(PackedIndex >> IndexShift);
		}
		return Ptr;
	}

	inline T* Get(const FPointerTableBase* PtrTable) const
	{
		if (IsFrozen())
		{
			check(PtrTable);
			const FTypeLayoutDesc& TypeDesc = StaticGetTypeLayoutDesc<TIndexedPtrBase<T, PtrType>>();
			return static_cast<T*>(PtrTable->GetIndexedPointer(TypeDesc, PackedIndex >> IndexShift));
		}
		return Ptr;
	}

	inline T* GetUnfrozen() const { check(!IsFrozen()); return Ptr; }

private:
	enum
	{
		IsFrozenMask = (1 << 0),
		IndexShift = 1,
	};

	static void SafeReleaseImpl(T*& InPtr)
	{
		if (InPtr)
		{
			delete InPtr;
			InPtr = nullptr;
		}
	}

	static void SafeReleaseImpl(TRefCountPtr<T>& InPtr)
	{
		InPtr.SafeRelease();
	}

	static_assert(sizeof(PtrType) <= sizeof(FMemoryImageUPtrInt), "PtrType must fit within a standard pointer");
	union
	{
		PtrType Ptr;
		FMemoryImageUPtrInt PackedIndex;
	};
};

#ifdef _MSC_VER
#pragma warning(pop)
#endif

template<typename T, typename PtrType>
inline const FTypeLayoutDesc& TPtrTableBase<T, PtrType>::StaticGetPtrTypeLayoutDesc()
{
	return StaticGetTypeLayoutDesc<TIndexedPtrBase<T, PtrType>>();
}

namespace Freeze
{
	template<typename T, typename PtrType>
	void IntrinsicWriteMemoryImage(FMemoryImageWriter& Writer, const TIndexedPtrBase<T, PtrType>& Object, const FTypeLayoutDesc& TypeDesc)
	{
		T* RawPtr = Object.Get(Writer.TryGetPrevPointerTable());
		if (RawPtr)
		{
			const uint32 Index = Writer.GetPointerTable().AddIndexedPointer(TypeDesc, RawPtr);
			check(Index != (uint32)INDEX_NONE);
			const uint64 FrozenPackedIndex = ((uint64)Index << 1u) | 1u;
			Writer.WriteMemoryImagePointerSizedBytes(FrozenPackedIndex);
		}
		else
		{
			Writer.WriteMemoryImagePointerSizedBytes(0u);
		}
	}

	template<typename T, typename PtrType>
	void IntrinsicUnfrozenCopy(const FMemoryUnfreezeContent& Context, const TIndexedPtrBase<T, PtrType>& Object, void* OutDst)
	{
		new(OutDst) TIndexedPtrBase<T, PtrType>(Object.Get(Context.TryGetPrevPointerTable()));
	}

	template<typename T, typename PtrType>
	uint32 IntrinsicAppendHash(const TIndexedPtrBase<T, PtrType>* DummyObject, const FTypeLayoutDesc& TypeDesc, const FPlatformTypeLayoutParameters& LayoutParams, FSHA1& Hasher)
	{
		return AppendHashForNameAndSize(TypeDesc.Name, LayoutParams.GetMemoryImagePointerSize(), Hasher);
	}

	template<typename T, typename PtrType>
	inline uint32 IntrinsicGetTargetAlignment(const TIndexedPtrBase<T, PtrType>* DummyObject, const FTypeLayoutDesc& TypeDesc, const FPlatformTypeLayoutParameters& LayoutParams)
	{
		return FMath::Min(LayoutParams.GetMemoryImagePointerSize(), LayoutParams.MaxFieldAlignment);
	}
}

template<typename T>
using TIndexedPtr = TIndexedPtrBase<T, T*>;

template<typename T>
using TIndexedRefCountPtr = TIndexedPtrBase<T, TRefCountPtr<T>>;

template<typename T, typename PtrType>
class TPatchedPtrBase
{
public:
	using FPtrTable = TPtrTableBase<T, PtrType>;

	inline TPatchedPtrBase(T* InPtr = nullptr) : Ptr(InPtr) {}

	inline T* Get() const
	{
		return Ptr;
	}

	inline T* GetChecked() const { T* Value = Get(); check(Value); return Value; }
	inline T* operator->() const { return GetChecked(); }
	inline T& operator*() const { return *GetChecked(); }
	inline operator T*() const { return Get(); }

private:
	static_assert(sizeof(PtrType) == sizeof(void*), "PtrType must be a standard pointer");
	PtrType Ptr;
};

template<typename T>
using TPatchedPtr = TPatchedPtrBase<T, T*>;

template<typename T>
using TPatchedRefCountPtr = TPatchedPtrBase<T, TRefCountPtr<T>>;

