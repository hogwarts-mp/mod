// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AsyncLoading2.h: Unreal async loading #2 definitions.
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectResource.h"
#include "UObject/PackageId.h"
#include "Serialization/Archive.h"
#include "IO/IoContainerId.h"

class FArchive;
class IAsyncPackageLoader;
class FIoDispatcher;
class IEDLBootNotificationManager;

using FSourceToLocalizedPackageIdMap = TArray<TPair<FPackageId, FPackageId>>;
using FCulturePackageMap = TMap<FString, FSourceToLocalizedPackageIdMap>;

class FMappedName
{
	static constexpr uint32 InvalidIndex = ~uint32(0);
	static constexpr uint32 IndexBits = 30u;
	static constexpr uint32 IndexMask = (1u << IndexBits) - 1u;
	static constexpr uint32 TypeMask = ~IndexMask;
	static constexpr uint32 TypeShift = IndexBits;

public:
	enum class EType
	{
		Package,
		Container,
		Global
	};

	inline FMappedName() = default;

	static inline FMappedName Create(const uint32 InIndex, const uint32 InNumber, EType InType)
	{
		check(InIndex <= MAX_int32);
		return FMappedName((uint32(InType) << TypeShift) | InIndex, InNumber);
	}

	static inline FMappedName FromMinimalName(const FMinimalName& MinimalName)
	{
		return *reinterpret_cast<const FMappedName*>(&MinimalName);
	}

	static inline bool IsResolvedToMinimalName(const FMinimalName& MinimalName)
	{
		// Not completely safe, relies on that no FName will have its Index and Number equal to Max_uint32
		const FMappedName MappedName = FromMinimalName(MinimalName);
		return MappedName.IsValid();
	}

	static inline FName SafeMinimalNameToName(const FMinimalName& MinimalName)
	{
		return IsResolvedToMinimalName(MinimalName) ? MinimalNameToName(MinimalName) : NAME_None;
	}

	inline FMinimalName ToUnresolvedMinimalName() const
	{
		return *reinterpret_cast<const FMinimalName*>(this);
	}

	inline bool IsValid() const
	{
		return Index != InvalidIndex && Number != InvalidIndex;
	}

	inline EType GetType() const
	{
		return static_cast<EType>(uint32((Index & TypeMask) >> TypeShift));
	}

	inline bool IsGlobal() const
	{
		return ((Index & TypeMask) >> TypeShift) != 0;
	}

	inline uint32 GetIndex() const
	{
		return Index & IndexMask;
	}

	inline uint32 GetNumber() const
	{
		return Number;
	}

	inline bool operator!=(FMappedName Other) const
	{
		return Index != Other.Index || Number != Other.Number;
	}

	COREUOBJECT_API friend FArchive& operator<<(FArchive& Ar, FMappedName& MappedName);

private:
	inline FMappedName(const uint32 InIndex, const uint32 InNumber)
		: Index(InIndex)
		, Number(InNumber) { }

	uint32 Index = InvalidIndex;
	uint32 Number = InvalidIndex;
};

struct FContainerHeader
{
	FIoContainerId ContainerId;
	uint32 PackageCount = 0;
	TArray<uint8> Names;
	TArray<uint8> NameHashes;
	TArray<FPackageId> PackageIds;
	TArray<uint8> StoreEntries; //FPackageStoreEntry[PackageCount]
	FCulturePackageMap CulturePackageMap;
	TArray<TPair<FPackageId, FPackageId>> PackageRedirects;

	COREUOBJECT_API friend FArchive& operator<<(FArchive& Ar, FContainerHeader& ContainerHeader);
};

class FPackageObjectIndex
{
	static constexpr uint64 IndexBits = 62ull;
	static constexpr uint64 IndexMask = (1ull << IndexBits) - 1ull;
	static constexpr uint64 TypeMask = ~IndexMask;
	static constexpr uint64 TypeShift = IndexBits;
	static constexpr uint64 Invalid = ~0ull;

	uint64 TypeAndId = Invalid;

	enum EType
	{
		Export,
		ScriptImport,
		PackageImport,
		Null,
		TypeCount = Null,
	};
	static_assert((TypeCount - 1) <= (TypeMask >> TypeShift), "FPackageObjectIndex: Too many types for TypeMask");

	inline explicit FPackageObjectIndex(EType InType, uint64 InId) : TypeAndId((uint64(InType) << TypeShift) | InId) {}

	COREUOBJECT_API static uint64 GenerateImportHashFromObjectPath(const FStringView& ObjectPath);

public:
	FPackageObjectIndex() = default;

	inline static FPackageObjectIndex FromExportIndex(const int32 Index)
	{
		return FPackageObjectIndex(Export, Index);
	}

	inline static FPackageObjectIndex FromScriptPath(const FStringView& ScriptObjectPath)
	{
		return FPackageObjectIndex(ScriptImport, GenerateImportHashFromObjectPath(ScriptObjectPath));
	}

	inline static FPackageObjectIndex FromPackagePath(const FStringView& PackageObjectPath)
	{
		return FPackageObjectIndex(PackageImport, GenerateImportHashFromObjectPath(PackageObjectPath));
	}

	inline bool IsNull() const
	{
		return TypeAndId == Invalid;
	}

	inline bool IsExport() const
	{
		return (TypeAndId >> TypeShift) == Export;
	}

	inline bool IsImport() const
	{
		return IsScriptImport() || IsPackageImport();
	}

	inline bool IsScriptImport() const
	{
		return (TypeAndId >> TypeShift) == ScriptImport;
	}

	inline bool IsPackageImport() const
	{
		return (TypeAndId >> TypeShift) == PackageImport;
	}

	inline uint32 ToExport() const
	{
		check(IsExport());
		return uint32(TypeAndId);
	}

	inline uint64 Value() const
	{
		return TypeAndId & IndexMask;
	}

	inline bool operator==(FPackageObjectIndex Other) const
	{
		return TypeAndId == Other.TypeAndId;
	}

	inline bool operator!=(FPackageObjectIndex Other) const
	{
		return TypeAndId != Other.TypeAndId;
	}

	COREUOBJECT_API friend FArchive& operator<<(FArchive& Ar, FPackageObjectIndex& Value)
	{
		Ar << Value.TypeAndId;
		return Ar;
	}

	inline friend uint32 GetTypeHash(const FPackageObjectIndex& Value)
	{
		return uint32(Value.TypeAndId);
	}
};

/**
 * Export filter flags.
 */
enum class EExportFilterFlags : uint8
{
	None,
	NotForClient,
	NotForServer
};

/**
 * Package summary.
 */
struct FPackageSummary
{
	FMappedName Name;
	FMappedName SourceName;
	uint32 PackageFlags;
	uint32 CookedHeaderSize;
	int32 NameMapNamesOffset;
	int32 NameMapNamesSize;
	int32 NameMapHashesOffset;
	int32 NameMapHashesSize;
	int32 ImportMapOffset;
	int32 ExportMapOffset;
	int32 ExportBundlesOffset;
	int32 GraphDataOffset;
	int32 GraphDataSize;
	int32 Pad = 0;
};

/**
 * Export bundle entry.
 */
struct FExportBundleEntry
{
	enum EExportCommandType
	{
		ExportCommandType_Create,
		ExportCommandType_Serialize,
		ExportCommandType_Count
	};
	uint32 LocalExportIndex;
	uint32 CommandType;

	COREUOBJECT_API friend FArchive& operator<<(FArchive& Ar, FExportBundleEntry& ExportBundleEntry);
};

template<typename T>
class TPackageStoreEntryCArrayView
{
	const uint32 ArrayNum = 0;
	const uint32 OffsetToDataFromThis = 0;

public:
	inline uint32 Num() const						{ return ArrayNum; }

	inline const T* Data() const					{ return (T*)((char*)this + OffsetToDataFromThis); }
	inline T* Data()								{ return (T*)((char*)this + OffsetToDataFromThis); }

	inline const T* begin() const					{ return Data(); }
	inline T* begin()								{ return Data(); }

	inline const T* end() const						{ return Data() + ArrayNum; }
	inline T* end()									{ return Data() + ArrayNum; }

	inline const T& operator[](uint32 Index) const	{ return Data()[Index]; }
	inline T& operator[](uint32 Index)				{ return Data()[Index]; }
};

struct FPackageStoreEntry
{
	uint64 ExportBundlesSize;
	int32 ExportCount;
	int32 ExportBundleCount;
	uint32 LoadOrder;
	uint32 Pad;
	TPackageStoreEntryCArrayView<FPackageId> ImportedPackages;
};

/**
 * Export bundle header
 */
struct FExportBundleHeader
{
	uint32 FirstEntryIndex;
	uint32 EntryCount;

	COREUOBJECT_API friend FArchive& operator<<(FArchive& Ar, FExportBundleHeader& ExportBundleHeader);
};

struct FScriptObjectEntry
{
	FMinimalName ObjectName;
	FPackageObjectIndex GlobalIndex;
	FPackageObjectIndex OuterIndex;
	FPackageObjectIndex CDOClassIndex;

	COREUOBJECT_API friend FArchive& operator<<(FArchive& Ar, FScriptObjectEntry& ScriptObjectEntry);
};

/**
 * Export map entry.
 */
struct FExportMapEntry
{
	uint64 CookedSerialOffset = 0;
	uint64 CookedSerialSize = 0;
	FMappedName ObjectName;
	FPackageObjectIndex OuterIndex;
	FPackageObjectIndex ClassIndex;
	FPackageObjectIndex SuperIndex;
	FPackageObjectIndex TemplateIndex;
	FPackageObjectIndex GlobalImportIndex;
	EObjectFlags ObjectFlags = EObjectFlags::RF_NoFlags;
	EExportFilterFlags FilterFlags = EExportFilterFlags::None;
	uint8 Pad[3] = {};

	COREUOBJECT_API friend FArchive& operator<<(FArchive& Ar, FExportMapEntry& ExportMapEntry);
};

COREUOBJECT_API void FindAllRuntimeScriptPackages(TArray<UPackage*>& OutPackages);

#ifndef WITH_ASYNCLOADING2
#define WITH_ASYNCLOADING2 (WITH_IOSTORE_IN_EDITOR || !WITH_EDITORONLY_DATA)
#endif

#if WITH_ASYNCLOADING2

/**
 * Creates a new instance of the AsyncPackageLoader #2.
 *
 * @param InIoDispatcher				The I/O dispatcher.
 *
 * @return The async package loader.
 */
IAsyncPackageLoader* MakeAsyncPackageLoader2(FIoDispatcher& InIoDispatcher);

#endif
