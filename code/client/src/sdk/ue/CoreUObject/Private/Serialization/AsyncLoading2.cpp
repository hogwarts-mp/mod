// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	UnAsyncLoading.cpp: Unreal async loading code.
=============================================================================*/

#include "Serialization/AsyncLoading2.h"
#include "Serialization/AsyncPackageLoader.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/FileManager.h"
#include "HAL/Event.h"
#include "HAL/RunnableThread.h"
#include "HAL/PlatformMisc.h"
#include "Misc/ScopeLock.h"
#include "Stats/StatsMisc.h"
#include "Misc/CoreStats.h"
#include "HAL/IConsoleManager.h"
#include "Internationalization/Culture.h"
#include "Internationalization/Internationalization.h"
#include "Misc/CoreDelegates.h"
#include "Misc/CommandLine.h"
#include "Misc/App.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Misc/StringBuilder.h"
#include "UObject/ObjectResource.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/NameBatchSerialization.h"
#include "Serialization/DeferredMessageLog.h"
#include "UObject/UObjectThreadContext.h"
#include "Misc/Paths.h"
#include "Misc/ExclusiveLoadPackageTimeTracker.h"
#include "ProfilingDebugging/LoadTimeTracker.h"
#include "HAL/ThreadHeartBeat.h"
#include "HAL/ExceptionHandling.h"
#include "UObject/UObjectHash.h"
#include "Templates/Casts.h"
#include "Templates/UniquePtr.h"
#include "Serialization/BufferReader.h"
#include "Async/TaskGraphInterfaces.h"
#include "Blueprint/BlueprintSupport.h"
#include "HAL/LowLevelMemTracker.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "UObject/UObjectArchetypeInternal.h"
#include "UObject/GarbageCollectionInternal.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Serialization/LoadTimeTracePrivate.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "Serialization/AsyncPackage.h"
#include "Serialization/UnversionedPropertySerialization.h"
#include "Serialization/Zenaphore.h"
#include "UObject/GCObject.h"
#include "UObject/ObjectRedirector.h"
#include "Serialization/BulkData.h"
#include "Serialization/LargeMemoryReader.h"
#include "Serialization/MemoryReader.h"
#include "UObject/UObjectClusters.h"
#include "UObject/LinkerInstancingContext.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "HAL/LowLevelMemStats.h"
#include "HAL/IPlatformFileOpenLogWrapper.h"

#if UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG
PRAGMA_DISABLE_OPTIMIZATION
#endif

FArchive& operator<<(FArchive& Ar, FMappedName& MappedName)
{
	Ar << MappedName.Index << MappedName.Number;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FContainerHeader& ContainerHeader)
{
	Ar << ContainerHeader.ContainerId;
	Ar << ContainerHeader.PackageCount;
	Ar << ContainerHeader.Names;
	Ar << ContainerHeader.NameHashes;
	Ar << ContainerHeader.PackageIds;
	Ar << ContainerHeader.StoreEntries;
	Ar << ContainerHeader.CulturePackageMap;
	Ar << ContainerHeader.PackageRedirects;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FExportBundleEntry& ExportBundleEntry)
{
	Ar << ExportBundleEntry.LocalExportIndex;
	Ar << ExportBundleEntry.CommandType;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FExportBundleHeader& ExportBundleHeader)
{
	Ar << ExportBundleHeader.FirstEntryIndex;
	Ar << ExportBundleHeader.EntryCount;

	return Ar;
}

FArchive& operator<<(FArchive& Ar, FScriptObjectEntry& ScriptObjectEntry)
{
	Ar << ScriptObjectEntry.ObjectName.Index << ScriptObjectEntry.ObjectName.Number;
	Ar << ScriptObjectEntry.GlobalIndex;
	Ar << ScriptObjectEntry.OuterIndex;
	Ar << ScriptObjectEntry.CDOClassIndex;
	return Ar;
}

FArchive& operator<<(FArchive& Ar, FExportMapEntry& ExportMapEntry)
{
	Ar << ExportMapEntry.CookedSerialOffset;
	Ar << ExportMapEntry.CookedSerialSize;
	Ar << ExportMapEntry.ObjectName;
	Ar << ExportMapEntry.OuterIndex;
	Ar << ExportMapEntry.ClassIndex;
	Ar << ExportMapEntry.SuperIndex;
	Ar << ExportMapEntry.TemplateIndex;
	Ar << ExportMapEntry.GlobalImportIndex;

	uint32 ObjectFlags = uint32(ExportMapEntry.ObjectFlags);
	Ar << ObjectFlags;
	
	if (Ar.IsLoading())
	{
		ExportMapEntry.ObjectFlags = EObjectFlags(ObjectFlags);
	}

	uint8 FilterFlags = uint8(ExportMapEntry.FilterFlags);
	Ar << FilterFlags;

	if (Ar.IsLoading())
	{
		ExportMapEntry.FilterFlags = EExportFilterFlags(FilterFlags);
	}

	Ar.Serialize(&ExportMapEntry.Pad, sizeof(ExportMapEntry.Pad));

	return Ar;
}

uint64 FPackageObjectIndex::GenerateImportHashFromObjectPath(const FStringView& ObjectPath)
{
	TArray<TCHAR, TInlineAllocator<FName::StringBufferSize>> FullImportPath;
	const int32 Len = ObjectPath.Len();
	FullImportPath.AddUninitialized(Len);
	for (int32 I = 0; I < Len; ++I)
	{
		if (ObjectPath[I] == TEXT('.') || ObjectPath[I] == TEXT(':'))
		{
			FullImportPath[I] = TEXT('/');
		}
		else
		{
			FullImportPath[I] = TChar<TCHAR>::ToLower(ObjectPath[I]);
		}
	}
	uint64 Hash = CityHash64(reinterpret_cast<const char*>(FullImportPath.GetData()), Len * sizeof(TCHAR));
	Hash &= ~(3ull << 62ull);
	return Hash;
}

void FindAllRuntimeScriptPackages(TArray<UPackage*>& OutPackages)
{
	OutPackages.Empty(256);
	ForEachObjectOfClass(UPackage::StaticClass(), [&OutPackages](UObject* InPackageObj)
	{
		UPackage* Package = CastChecked<UPackage>(InPackageObj);
		if (Package->HasAnyPackageFlags(PKG_CompiledIn) && !Package->HasAnyPackageFlags(PKG_EditorOnly))
		{
			TCHAR Buffer[FName::StringBufferSize];
			if (FStringView(Buffer, Package->GetFName().ToString(Buffer)).StartsWith(TEXT("/Script/"), ESearchCase::CaseSensitive))
			{
				OutPackages.Add(Package);
			}
		}
	}, /*bIncludeDerivedClasses*/false);
}

#if WITH_ASYNCLOADING2

#ifndef ALT2_VERIFY_ASYNC_FLAGS
#define ALT2_VERIFY_ASYNC_FLAGS DO_CHECK && !(WITH_IOSTORE_IN_EDITOR)
#endif

#ifndef ALT2_VERIFY_RECURSIVE_LOADS
#define ALT2_VERIFY_RECURSIVE_LOADS !(WITH_IOSTORE_IN_EDITOR) && DO_CHECK
#endif

#ifndef ALT2_LOG_VERBOSE
#define ALT2_LOG_VERBOSE DO_CHECK
#endif

static TSet<FPackageId> GAsyncLoading2_DebugPackageIds;
static FString GAsyncLoading2_DebugPackageNamesString;
static TSet<FPackageId> GAsyncLoading2_VerbosePackageIds;
static FString GAsyncLoading2_VerbosePackageNamesString;
static int32 GAsyncLoading2_VerboseLogFilter = 2; //None=0,Filter=1,All=2
#if !UE_BUILD_SHIPPING
static void ParsePackageNames(const FString& PackageNamesString, TSet<FPackageId>& PackageIds)
{
	TArray<FString> Args;
	const TCHAR* Delimiters[] = { TEXT(","), TEXT(" ") };
	PackageNamesString.ParseIntoArray(Args, Delimiters, UE_ARRAY_COUNT(Delimiters), true);
	PackageIds.Reserve(PackageIds.Num() + Args.Num());
	for (const FString& PackageName : Args)
	{
		if (PackageName.Len() > 0 && FChar::IsDigit(PackageName[0]))
		{
			uint64 Value;
			LexFromString(Value, *PackageName);
			PackageIds.Add(*(FPackageId*)(&Value));
		}
		else
		{
			PackageIds.Add(FPackageId::FromName(FName(*PackageName)));
		}
	}
}
static FAutoConsoleVariableRef CVar_DebugPackageNames(
	TEXT("s.DebugPackageNames"),
	GAsyncLoading2_DebugPackageNamesString,
	TEXT("Add debug breaks for all listed package names, also automatically added to s.VerbosePackageNames."),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* Variable)
	{
		GAsyncLoading2_DebugPackageIds.Reset();
		ParsePackageNames(Variable->GetString(), GAsyncLoading2_DebugPackageIds);
		ParsePackageNames(Variable->GetString(), GAsyncLoading2_VerbosePackageIds);
		GAsyncLoading2_VerboseLogFilter = GAsyncLoading2_VerbosePackageIds.Num() > 0 ? 1 : 2;
	}),
	ECVF_Default);
static FAutoConsoleVariableRef CVar_VerbosePackageNames(
	TEXT("s.VerbosePackageNames"),
	GAsyncLoading2_VerbosePackageNamesString,
	TEXT("Restrict verbose logging to listed package names."),
	FConsoleVariableDelegate::CreateLambda([](IConsoleVariable* Variable)
	{
		GAsyncLoading2_VerbosePackageIds.Reset();
		ParsePackageNames(Variable->GetString(), GAsyncLoading2_VerbosePackageIds);
		GAsyncLoading2_VerboseLogFilter = GAsyncLoading2_VerbosePackageIds.Num() > 0 ? 1 : 2;
	}),
	ECVF_Default);
#endif

#define UE_ASYNC_PACKAGE_DEBUG(PackageDesc) \
if (GAsyncLoading2_DebugPackageIds.Contains((PackageDesc).DiskPackageId)) \
{ \
	UE_DEBUG_BREAK(); \
}

#define UE_ASYNC_UPACKAGE_DEBUG(UPackage) \
if (GAsyncLoading2_DebugPackageIds.Contains((UPackage)->GetPackageId())) \
{ \
	UE_DEBUG_BREAK(); \
}

// The ELogVerbosity::VerbosityMask is used to silence PVS,
// using constexpr gave the same warning, and the disable comment can can't be used in a macro: //-V501 
// warning V501: There are identical sub-expressions 'ELogVerbosity::Verbose' to the left and to the right of the '<' operator.
#define UE_ASYNC_PACKAGE_LOG(Verbosity, PackageDesc, LogDesc, Format, ...) \
if ((ELogVerbosity::Type(ELogVerbosity::Verbosity & ELogVerbosity::VerbosityMask) < ELogVerbosity::Verbose) || \
	(GAsyncLoading2_VerboseLogFilter == 2) || \
	(GAsyncLoading2_VerboseLogFilter == 1 && GAsyncLoading2_VerbosePackageIds.Contains((PackageDesc).DiskPackageId))) \
{ \
	if (!(PackageDesc).CustomPackageName.IsNone()) \
	{ \
		UE_LOG(LogStreaming, Verbosity, LogDesc TEXT(": %s (0x%llX) %s (0x%llX) - ") Format, \
			*(PackageDesc).CustomPackageName.ToString(), \
			(PackageDesc).CustomPackageId.ValueForDebugging(), \
			*(PackageDesc).DiskPackageName.ToString(), \
			(PackageDesc).DiskPackageId.ValueForDebugging(), \
			##__VA_ARGS__); \
	} \
	else \
	{ \
		UE_LOG(LogStreaming, Verbosity, LogDesc TEXT(": %s (0x%llX) - ") Format, \
			*(PackageDesc).DiskPackageName.ToString(), \
			(PackageDesc).DiskPackageId.ValueForDebugging(), \
			##__VA_ARGS__); \
	} \
}

#define UE_ASYNC_PACKAGE_CLOG(Condition, Verbosity, PackageDesc, LogDesc, Format, ...) \
if ((Condition)) \
{ \
	UE_ASYNC_PACKAGE_LOG(Verbosity, PackageDesc, LogDesc, Format, ##__VA_ARGS__); \
}

#if ALT2_LOG_VERBOSE
#define UE_ASYNC_PACKAGE_LOG_VERBOSE(Verbosity, PackageDesc, LogDesc, Format, ...) \
	UE_ASYNC_PACKAGE_LOG(Verbosity, PackageDesc, LogDesc, Format, ##__VA_ARGS__)
#define UE_ASYNC_PACKAGE_CLOG_VERBOSE(Condition, Verbosity, PackageDesc, LogDesc, Format, ...) \
	UE_ASYNC_PACKAGE_CLOG(Condition, Verbosity, PackageDesc, LogDesc, Format, ##__VA_ARGS__)
#else
#define UE_ASYNC_PACKAGE_LOG_VERBOSE(Verbosity, PackageDesc, LogDesc, Format, ...)
#define UE_ASYNC_PACKAGE_CLOG_VERBOSE(Condition, Verbosity, PackageDesc, LogDesc, Format, ...)
#endif

CSV_DECLARE_CATEGORY_MODULE_EXTERN(CORE_API, Basic);
CSV_DECLARE_CATEGORY_MODULE_EXTERN(CORE_API, FileIO);

TRACE_DECLARE_INT_COUNTER(PendingBundleIoRequests, TEXT("AsyncLoading/PendingBundleIoRequests"));

struct FAsyncPackage2;
class FAsyncLoadingThread2;

class FSimpleArchive final
	: public FArchive
{
public:
	FSimpleArchive(const uint8* BufferPtr, uint64 BufferSize)
	{
#if (!DEVIRTUALIZE_FLinkerLoad_Serialize)
		ActiveFPLB = &InlineFPLB;
#endif
		ActiveFPLB->OriginalFastPathLoadBuffer = BufferPtr;
		ActiveFPLB->StartFastPathLoadBuffer = BufferPtr;
		ActiveFPLB->EndFastPathLoadBuffer = BufferPtr + BufferSize;
	}

	int64 TotalSize() override
	{
		return ActiveFPLB->EndFastPathLoadBuffer - ActiveFPLB->OriginalFastPathLoadBuffer;
	}

	int64 Tell() override
	{
		return ActiveFPLB->StartFastPathLoadBuffer - ActiveFPLB->OriginalFastPathLoadBuffer;
	}

	void Seek(int64 Position) override
	{
		ActiveFPLB->StartFastPathLoadBuffer = ActiveFPLB->OriginalFastPathLoadBuffer + Position;
		check(ActiveFPLB->StartFastPathLoadBuffer <= ActiveFPLB->EndFastPathLoadBuffer);
	}

	void Serialize(void* Data, int64 Length) override
	{
		if (!Length || IsError())
		{
			return;
		}
		check(ActiveFPLB->StartFastPathLoadBuffer + Length <= ActiveFPLB->EndFastPathLoadBuffer);
		FMemory::Memcpy(Data, ActiveFPLB->StartFastPathLoadBuffer, Length);
		ActiveFPLB->StartFastPathLoadBuffer += Length;
	}
private:
#if (!DEVIRTUALIZE_FLinkerLoad_Serialize)
	FArchive::FFastPathLoadBuffer InlineFPLB;
	FArchive::FFastPathLoadBuffer* ActiveFPLB;
#endif
};

struct FExportObject
{
	UObject* Object = nullptr;
	UObject* TemplateObject = nullptr;
	UObject* SuperObject = nullptr;
	bool bFiltered = false;
	bool bExportLoadFailed = false;
};

struct FAsyncPackageDesc2
{
	// A unique request id for each external call to LoadPackage
	int32 RequestID;
	// Package priority
	int32 Priority;
	// The package store entry with meta data about the actual disk package
	const FPackageStoreEntry* StoreEntry;
	// The disk package id corresponding to the StoreEntry
	// It is used by the loader for io chunks and to handle ref tracking of loaded packages and import objects.
	FPackageId DiskPackageId; 
	// The custom package id is only set for temp packages with a valid but "fake" CustomPackageName,
	// if set, it will be used as key when tracking active async packages in AsyncPackageLookup
	FPackageId CustomPackageId;
	// The disk package name from the LoadPackage call, or none for imported packages
	// up until the package summary has been serialized
	FName DiskPackageName;
	// The custom package name from the LoadPackage call is only used for temp packages,
	// if set, it will be used as the runtime UPackage name
	FName CustomPackageName;
	// Set from the package summary,
	FName SourcePackageName;
	/** Delegate called on completion of loading. This delegate can only be created and consumed on the game thread */
	TUniquePtr<FLoadPackageAsyncDelegate> PackageLoadedDelegate;

	FAsyncPackageDesc2(
		int32 InRequestID,
		int32 InPriority,
		FPackageId InPackageIdToLoad,
		const FPackageStoreEntry* InStoreEntry,
		FName InDiskPackageName = FName(),
		FPackageId InPackageId = FPackageId(),
		FName InCustomName = FName(),
		TUniquePtr<FLoadPackageAsyncDelegate>&& InCompletionDelegate = TUniquePtr<FLoadPackageAsyncDelegate>())
		: RequestID(InRequestID)
		, Priority(InPriority)
		, StoreEntry(InStoreEntry)
		, DiskPackageId(InPackageIdToLoad)
		, CustomPackageId(InPackageId)
		, DiskPackageName(InDiskPackageName)
		, CustomPackageName(InCustomName)
		, PackageLoadedDelegate(MoveTemp(InCompletionDelegate))
	{
	}

	/** This constructor does not modify the package loaded delegate as this is not safe outside the game thread */
	FAsyncPackageDesc2(const FAsyncPackageDesc2& OldPackage)
		: RequestID(OldPackage.RequestID)
		, Priority(OldPackage.Priority)
		, StoreEntry(OldPackage.StoreEntry)
		, DiskPackageId(OldPackage.DiskPackageId)
		, CustomPackageId(OldPackage.CustomPackageId)
		, DiskPackageName(OldPackage.DiskPackageName)
		, CustomPackageName(OldPackage.CustomPackageName)
		, SourcePackageName(OldPackage.SourcePackageName)
	{
	}

	/** This constructor will explicitly copy the package loaded delegate and invalidate the old one */
	FAsyncPackageDesc2(const FAsyncPackageDesc2& OldPackage, TUniquePtr<FLoadPackageAsyncDelegate>&& InPackageLoadedDelegate)
		: FAsyncPackageDesc2(OldPackage)
	{
		PackageLoadedDelegate = MoveTemp(InPackageLoadedDelegate);
	}

	void SetDiskPackageName(FName SerializedDiskPackageName, FName SerializedSourcePackageName = FName())
	{
		check(DiskPackageName.IsNone() || DiskPackageName == SerializedDiskPackageName);
		check(SourcePackageName.IsNone() || SourcePackageName == SerializedSourcePackageName);
		DiskPackageName = SerializedDiskPackageName;
		SourcePackageName = SerializedSourcePackageName;
	}

	bool CanBeImported() const
	{
		return CustomPackageName.IsNone();
	}

	/**
	 * The UPackage name is used by the engine and game code for in-memory and network communication.
	 */
	FName GetUPackageName() const
	{
		if (!CustomPackageName.IsNone())
		{
			// temp packages
			return CustomPackageName;
		}
		else if(!SourcePackageName.IsNone())
		{
			// localized packages
			return SourcePackageName;
		}
		// normal packages
		return DiskPackageName;
	}

	/**
	 * The AsyncPackage id is used by the loader as a key in AsyncPackageLookup to track active load requests,
	 * which in turn is used for looking up packages for setting up serialized arcs (mostly post load dependencies).
	 */
	FORCEINLINE FPackageId GetAsyncPackageId() const
	{
		return CustomPackageId.IsValid() ? CustomPackageId : DiskPackageId;
	}

#if DO_GUARD_SLOW
	~FAsyncPackageDesc2()
	{
		checkSlow(!PackageLoadedDelegate.IsValid() || IsInGameThread());
	}
#endif
};

class FNameMap
{
public:
	void LoadGlobal(FIoDispatcher& IoDispatcher)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LoadGlobalNameMap);

		check(NameEntries.Num() == 0);

		FIoChunkId NamesId = CreateIoChunkId(0, 0, EIoChunkType::LoaderGlobalNames);
		FIoChunkId HashesId = CreateIoChunkId(0, 0, EIoChunkType::LoaderGlobalNameHashes);

		FIoBatch Batch = IoDispatcher.NewBatch();
		FIoRequest NameRequest = Batch.Read(NamesId, FIoReadOptions(), IoDispatcherPriority_High);
		FIoRequest HashRequest = Batch.Read(HashesId, FIoReadOptions(), IoDispatcherPriority_High);
		FEvent* BatchCompletedEvent = FPlatformProcess::GetSynchEventFromPool();
		Batch.IssueAndTriggerEvent(BatchCompletedEvent);

		ReserveNameBatch(	IoDispatcher.GetSizeForChunk(NamesId).ValueOrDie(),
							IoDispatcher.GetSizeForChunk(HashesId).ValueOrDie());

		BatchCompletedEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(BatchCompletedEvent);

		FIoBuffer NameBuffer = NameRequest.GetResult().ConsumeValueOrDie();
		FIoBuffer HashBuffer = HashRequest.GetResult().ConsumeValueOrDie();

		Load(MakeArrayView(NameBuffer.Data(), NameBuffer.DataSize()), MakeArrayView(HashBuffer.Data(), HashBuffer.DataSize()), FMappedName::EType::Global);
	}

	int32 Num() const
	{
		return NameEntries.Num();
	}

	void Load(TArrayView<const uint8> NameBuffer, TArrayView<const uint8> HashBuffer, FMappedName::EType InNameMapType)
	{
		LoadNameBatch(NameEntries, NameBuffer, HashBuffer);
		NameMapType = InNameMapType;
	}

	FName GetName(const FMappedName& MappedName) const
	{
		check(MappedName.GetType() == NameMapType);
		check(MappedName.GetIndex() < uint32(NameEntries.Num()));
		FNameEntryId NameEntry = NameEntries[MappedName.GetIndex()];
		return FName::CreateFromDisplayId(NameEntry, MappedName.GetNumber());
	}

	bool TryGetName(const FMappedName& MappedName, FName& OutName) const
	{
		check(MappedName.GetType() == NameMapType);
		uint32 Index = MappedName.GetIndex();
		if (Index < uint32(NameEntries.Num()))
		{
			FNameEntryId NameEntry = NameEntries[MappedName.GetIndex()];
			OutName = FName::CreateFromDisplayId(NameEntry, MappedName.GetNumber());
			return true;
		}
		return false;
	}

	FMinimalName GetMinimalName(const FMappedName& MappedName) const
	{
		check(MappedName.GetType() == NameMapType);
		check(MappedName.GetIndex() < uint32(NameEntries.Num()));
		FNameEntryId NameEntry = NameEntries[MappedName.GetIndex()];
		return FMinimalName(NameEntry, MappedName.GetNumber());
	}

private:
	TArray<FNameEntryId> NameEntries;
	FMappedName::EType NameMapType = FMappedName::EType::Global;
};

struct FPublicExport
{
	UObject* Object = nullptr;
	FPackageId PackageId; // for fast clear of package load status during GC
};

// Note: RemoveUnreachableObjects could move from GT to ALT by removing the debug raw pointers here,
// the tradeoff would be increased complexity and more restricted debug and log possibilities.
using FUnreachablePackage = TPair<FName, UPackage*>;
using FUnreachablePublicExport = TPair<int32, UObject*>;
using FUnreachablePackages = TArray<FUnreachablePackage>;
using FUnreachablePublicExports = TArray<FUnreachablePublicExport>;

struct FGlobalImportStore
{
	TMap<FPackageObjectIndex, UObject*> ScriptObjects;
	TMap<FPackageObjectIndex, FPublicExport> PublicExportObjects;
	TMap<int32, FPackageObjectIndex> ObjectIndexToPublicExport;
	// Temporary initial load data
	TArray<FScriptObjectEntry> ScriptObjectEntries;
	TMap<FPackageObjectIndex, FScriptObjectEntry*> ScriptObjectEntriesMap;
	bool bHasInitializedScriptObjects = false;

	FGlobalImportStore()
	{
		PublicExportObjects.Reserve(32768);
		ObjectIndexToPublicExport.Reserve(32768);
	}

	TArray<FPackageId> RemovePublicExports(const FUnreachablePublicExports& PublicExports)
	{
		TArray<FPackageId> PackageIds;
		TArray<FPackageObjectIndex> GlobalIndices;
		GlobalIndices.Reserve(PublicExports.Num());
		PackageIds.Reserve(PublicExports.Num());

		for (const FUnreachablePublicExport& Item : PublicExports)
		{
			int32 ObjectIndex = Item.Key;
			FPackageObjectIndex GlobalIndex;
			if (ObjectIndexToPublicExport.RemoveAndCopyValue(ObjectIndex, GlobalIndex))
			{
				GlobalIndices.Emplace(GlobalIndex);
#if DO_CHECK
				FPublicExport* PublicExport = PublicExportObjects.Find(GlobalIndex);
				checkf(PublicExport, TEXT("Missing entry in ImportStore for object %s with id 0x%llX"), *Item.Value->GetPathName(), GlobalIndex.Value());
				int32 ObjectIndex2 = GUObjectArray.ObjectToIndex(PublicExport->Object);
				checkf(ObjectIndex2 == ObjectIndex, TEXT("Mismatch in ImportStore for %s with id 0x%llX"), *Item.Value->GetPathName(), GlobalIndex.Value());
#endif
			}
		}

		FPackageId LastPackageId;
		for (const FPackageObjectIndex& GlobalIndex : GlobalIndices)
		{
			FPublicExport PublicExport;
			PublicExportObjects.RemoveAndCopyValue(GlobalIndex, PublicExport);
			if (!(PublicExport.PackageId == LastPackageId)) // fast approximation of Contains()
			{
				LastPackageId = PublicExport.PackageId;
				PackageIds.Emplace(LastPackageId);
			}
		}
		return PackageIds;
	}

	inline UObject* GetPublicExportObject(FPackageObjectIndex GlobalIndex)
	{
		check(GlobalIndex.IsPackageImport());
		UObject* Object = nullptr;
		FPublicExport* PublicExport = PublicExportObjects.Find(GlobalIndex);
		if (PublicExport)
		{
			Object = PublicExport->Object;
			checkf(Object && !Object->IsUnreachable(), TEXT("%s"), Object ? *Object->GetFullName() : TEXT("null"));
		}
		return Object;
	}

	UObject* FindScriptImportObjectFromIndex(FPackageObjectIndex ScriptImportIndex);

	inline UObject* FindOrGetImportObject(FPackageObjectIndex GlobalIndex)
	{
		check(GlobalIndex.IsImport());
		UObject* Object = nullptr;
		if (GlobalIndex.IsScriptImport())
		{
			if (!bHasInitializedScriptObjects)
			{
				Object = FindScriptImportObjectFromIndex(GlobalIndex);
			}
			else
			{
				Object = ScriptObjects.FindRef(GlobalIndex);
			}
		}
		else
		{
			Object = GetPublicExportObject(GlobalIndex);
		}
		return Object;
	}

	void StoreGlobalObject(FPackageId PackageId, FPackageObjectIndex GlobalIndex, UObject* Object)
	{
		check(GlobalIndex.IsPackageImport());
		int32 ObjectIndex = GUObjectArray.ObjectToIndex(Object);
		PublicExportObjects.Add(GlobalIndex, {Object, PackageId});
		ObjectIndexToPublicExport.Add(ObjectIndex, GlobalIndex);
	}

	void FindAllScriptObjects();
};

class FLoadedPackageRef
{
	UPackage* Package = nullptr;
	int32 RefCount = 0;
	bool bAreAllPublicExportsLoaded = false;
	bool bIsMissing = false;
	bool bHasFailed = false;
	bool bHasBeenLoadedDebug = false;

public:
	inline int32 GetRefCount() const
	{
		return RefCount;
	}

	inline bool AddRef()
	{
		++RefCount;
		// is this the first reference to a package that has been loaded earlier?
		return RefCount == 1 && Package;
	}

	inline bool ReleaseRef(FPackageId FromPackageId, FPackageId ToPackageId)
	{
		check(RefCount > 0);
		--RefCount;

#if DO_CHECK
		ensureMsgf(!bHasBeenLoadedDebug || bAreAllPublicExportsLoaded || bIsMissing || bHasFailed,
			TEXT("LoadedPackageRef from None (0x%llX) to %s (0x%llX) should not have been released when the package is not complete.")
			TEXT("RefCount=%d, AreAllExportsLoaded=%d, IsMissing=%d, HasFailed=%d, HasBeenLoaded=%d"),
			FromPackageId.Value(),
			Package ? *Package->GetName() : TEXT("None"),
			ToPackageId.Value(),
			RefCount,
			bAreAllPublicExportsLoaded,
			bIsMissing,
			bHasFailed,
			bHasBeenLoadedDebug);

		if (bAreAllPublicExportsLoaded)
		{
			check(!bIsMissing);
		}
		if (bIsMissing)
		{
			check(!bAreAllPublicExportsLoaded);
		}
#endif
		// is this the last reference to a loaded package?
		return RefCount == 0 && Package;
	}

	inline UPackage* GetPackage() const
	{
#if DO_CHECK
		if (Package)
		{
			check(!bIsMissing);
			check(!Package->IsUnreachable());
		}
		else
		{
			check(!bAreAllPublicExportsLoaded);
		}
#endif
		return Package;
	}

	inline void SetPackage(UPackage* InPackage)
	{
		check(!bAreAllPublicExportsLoaded);
		check(!bIsMissing);
		check(!bHasFailed);
		check(!Package);
		Package = InPackage;
	}

	inline bool AreAllPublicExportsLoaded() const
	{
		return bAreAllPublicExportsLoaded;
	}

	inline void SetAllPublicExportsLoaded()
	{
		check(!bIsMissing);
		check(!bHasFailed);
		check(Package);
		bIsMissing = false;
		bAreAllPublicExportsLoaded = true;
		bHasBeenLoadedDebug = true;
	}

	inline void ClearAllPublicExportsLoaded()
	{
		check(!bIsMissing);
		check(Package);
		bIsMissing = false;
		bAreAllPublicExportsLoaded = false;
	}

	inline void SetIsMissingPackage()
	{
		check(!bAreAllPublicExportsLoaded);
		check(!Package);
		bIsMissing = true;
		bAreAllPublicExportsLoaded = false;
	}

	inline void ClearErrorFlags()
	{
		bIsMissing = false;
		bHasFailed = false;
	}

	inline void SetHasFailed()
	{
		bHasFailed = true;
	}
};

class FLoadedPackageStore
{
private:
	// Packages in active loading or completely loaded packages, with Desc.DiskPackageName as key.
	// Does not track temp packages with custom UPackage names, since they are never imorted by other packages.
	TMap<FPackageId, FLoadedPackageRef> Packages;

public:
	FLoadedPackageStore()
	{
		Packages.Reserve(32768);
	}

	int32 NumTracked() const
	{
		return Packages.Num();
	}

	inline FLoadedPackageRef* FindPackageRef(FPackageId PackageId)
	{
		return Packages.Find(PackageId);
	}

	inline FLoadedPackageRef& GetPackageRef(FPackageId PackageId)
	{
		return Packages.FindOrAdd(PackageId);
	}

	inline int32 RemovePackage(FPackageId PackageId)
	{
		FLoadedPackageRef Ref;
		bool bRemoved = Packages.RemoveAndCopyValue(PackageId, Ref);
		return bRemoved ? Ref.GetRefCount() : -1;
	}

#if ALT2_VERIFY_ASYNC_FLAGS
	void VerifyLoadedPackages()
	{
		for (TPair<FPackageId, FLoadedPackageRef>& Pair : Packages)
		{
			FPackageId& PackageId = Pair.Key;
			FLoadedPackageRef& Ref = Pair.Value;
			ensureMsgf(Ref.GetRefCount() == 0,
				TEXT("PackageId '0x%llX' with ref count %d should not have a ref count now")
				TEXT(", or this check is incorrectly reached during active loading."),
				PackageId.Value(),
				Ref.GetRefCount());
		}
	}
#endif
};

class FPackageStore
{
public:
	FPackageStore(FIoDispatcher& InIoDispatcher, FNameMap& InGlobalNameMap)
		: IoDispatcher(InIoDispatcher)
		, GlobalNameMap(InGlobalNameMap) { }
	
	struct FLoadedContainer
	{
		TUniquePtr<FNameMap> ContainerNameMap;
		TArray<uint8> StoreEntries; //FPackageStoreEntry[PackageCount];
		uint32 PackageCount;
		int32 Order = 0;
		bool bValid = false;
	};

	FIoDispatcher& IoDispatcher;
	FNameMap& GlobalNameMap;
	TMap<FIoContainerId, TUniquePtr<FLoadedContainer>> LoadedContainers;

	TArray<FString> CurrentCultureNames;

	mutable FCriticalSection PackageNameMapsCritical;

	TMap<FPackageId, FPackageStoreEntry*> StoreEntriesMap;
	TMap<FPackageId, FPackageId> RedirectsPackageMap;
	TSet<FPackageId> TargetRedirectIds;
	int32 NextCustomPackageIndex = 0;

	FGlobalImportStore ImportStore;
	FLoadedPackageStore LoadedPackageStore;
	int32 ScriptArcsCount = 0;

public:
	bool DoesPackageExist(FName InPackageName) const
	{
		FPackageId PackageId = FPackageId::FromName(InPackageName);
		FScopeLock Lock(&PackageNameMapsCritical);
		return StoreEntriesMap.Contains(PackageId);
	}

	void SetupCulture()
	{
		FInternationalization& Internationalization = FInternationalization::Get();
		FString CurrentCulture = Internationalization.GetCurrentCulture()->GetName();
		FParse::Value(FCommandLine::Get(), TEXT("CULTURE="), CurrentCulture);
		CurrentCultureNames = Internationalization.GetPrioritizedCultureNames(CurrentCulture);
	}

	void SetupInitialLoadData()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SetupInitialLoadData);

		FEvent* InitialLoadEvent = FPlatformProcess::GetSynchEventFromPool();

		FIoBatch IoBatch = IoDispatcher.NewBatch();
		FIoRequest IoRequest = IoBatch.Read(CreateIoChunkId(0, 0, EIoChunkType::LoaderInitialLoadMeta), FIoReadOptions(), IoDispatcherPriority_High);
		IoBatch.IssueAndTriggerEvent(InitialLoadEvent);

		InitialLoadEvent->Wait();
		FPlatformProcess::ReturnSynchEventToPool(InitialLoadEvent);

		FIoBuffer InitialLoadIoBuffer = IoRequest.GetResult().ConsumeValueOrDie();
		FLargeMemoryReader InitialLoadArchive(InitialLoadIoBuffer.Data(), InitialLoadIoBuffer.DataSize());
		int32 NumScriptObjects = 0;
		InitialLoadArchive << NumScriptObjects;
		ImportStore.ScriptObjectEntries = MakeArrayView(reinterpret_cast<const FScriptObjectEntry*>(InitialLoadIoBuffer.Data() + InitialLoadArchive.Tell()), NumScriptObjects);

		ImportStore.ScriptObjectEntriesMap.Reserve(ImportStore.ScriptObjectEntries.Num());
		for (FScriptObjectEntry& ScriptObjectEntry : ImportStore.ScriptObjectEntries)
		{
			const FMappedName& MappedName = FMappedName::FromMinimalName(ScriptObjectEntry.ObjectName);
			check(MappedName.IsGlobal());
			ScriptObjectEntry.ObjectName = GlobalNameMap.GetMinimalName(MappedName);

			ImportStore.ScriptObjectEntriesMap.Add(ScriptObjectEntry.GlobalIndex, &ScriptObjectEntry);
		}
	}

	void LoadContainers(TArrayView<const FIoDispatcherMountedContainer> Containers)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LoadContainers);

		int32 ContainersToLoad = 0;

		for (const FIoDispatcherMountedContainer& Container : Containers)
		{
			if (Container.ContainerId.IsValid())
			{
				++ContainersToLoad;
			}
		}

		if (!ContainersToLoad)
		{
			return;
		}

		TAtomic<int32> Remaining(ContainersToLoad);

		FEvent* Event = FPlatformProcess::GetSynchEventFromPool();
		FIoBatch IoBatch = IoDispatcher.NewBatch();

		for (const FIoDispatcherMountedContainer& Container : Containers)
		{
			const FIoContainerId& ContainerId = Container.ContainerId;
			if (!ContainerId.IsValid())
			{
				continue;
			}

			TUniquePtr<FLoadedContainer>& LoadedContainerPtr = LoadedContainers.FindOrAdd(ContainerId);
			if (!LoadedContainerPtr)
			{
				LoadedContainerPtr.Reset(new FLoadedContainer);
			}
			FLoadedContainer& LoadedContainer = *LoadedContainerPtr;
			if (LoadedContainer.bValid && LoadedContainer.Order >= Container.Environment.GetOrder())
			{
				UE_LOG(LogStreaming, Log, TEXT("Skipping loading mounted container ID '0x%llX', already loaded with higher order"), ContainerId.Value());
				if (--Remaining == 0)
				{
					Event->Trigger();
				}
				continue;
			}

			UE_LOG(LogStreaming, Log, TEXT("Loading mounted container ID '0x%llX'"), ContainerId.Value());
			LoadedContainer.bValid = true;
			LoadedContainer.Order = Container.Environment.GetOrder();

			FIoChunkId HeaderChunkId = CreateIoChunkId(ContainerId.Value(), 0, EIoChunkType::ContainerHeader);
			IoBatch.ReadWithCallback(HeaderChunkId, FIoReadOptions(), IoDispatcherPriority_High, [this, &Remaining, Event, &LoadedContainer, ContainerId](TIoStatusOr<FIoBuffer> Result)
			{
				// Execution method Thread will run the async block synchronously when multithreading is NOT supported
				const EAsyncExecution ExecutionMethod = FPlatformProcess::SupportsMultithreading() ? EAsyncExecution::TaskGraph : EAsyncExecution::Thread;

				if (!Result.IsOk())
				{
					if (EIoErrorCode::NotFound == Result.Status().GetErrorCode())
					{
						UE_LOG(LogStreaming, Warning, TEXT("Header for container '0x%llX' not found."), ContainerId.Value());
					}
					else
					{
						UE_LOG(LogStreaming, Warning, TEXT("Failed reading header for container '0x%llX' (%s)"), ContainerId.Value(), *Result.Status().ToString());
					}

					if (--Remaining == 0)
					{
						Event->Trigger();
					}
					return;
				}

				Async(ExecutionMethod, [this, &Remaining, Event, IoBuffer = Result.ConsumeValueOrDie(), &LoadedContainer]()
				{
					LLM_SCOPE(ELLMTag::AsyncLoading);

					FMemoryReaderView Ar(MakeArrayView(IoBuffer.Data(), IoBuffer.DataSize()));

					FContainerHeader ContainerHeader;
					Ar << ContainerHeader;

					const bool bHasContainerLocalNameMap = ContainerHeader.Names.Num() > 0;
					if (bHasContainerLocalNameMap)
					{
						TRACE_CPUPROFILER_EVENT_SCOPE(LoadContainerNameMap);
						LoadedContainer.ContainerNameMap.Reset(new FNameMap());
						LoadedContainer.ContainerNameMap->Load(ContainerHeader.Names, ContainerHeader.NameHashes, FMappedName::EType::Container);
					}

					LoadedContainer.PackageCount = ContainerHeader.PackageCount;
					LoadedContainer.StoreEntries = MoveTemp(ContainerHeader.StoreEntries);
					{
						TRACE_CPUPROFILER_EVENT_SCOPE(AddPackages);
						FScopeLock Lock(&PackageNameMapsCritical);

						TArrayView<FPackageStoreEntry> StoreEntries(reinterpret_cast<FPackageStoreEntry*>(LoadedContainer.StoreEntries.GetData()), LoadedContainer.PackageCount);

						int32 Index = 0;
						StoreEntriesMap.Reserve(StoreEntriesMap.Num() + LoadedContainer.PackageCount);
						for (FPackageStoreEntry& ContainerEntry : StoreEntries)
						{
							const FPackageId& PackageId = ContainerHeader.PackageIds[Index];

							FPackageStoreEntry*& GlobalEntry = StoreEntriesMap.FindOrAdd(PackageId);
							if (!GlobalEntry)
							{
								GlobalEntry = &ContainerEntry;
							}
							++Index;
						}

						{
							TRACE_CPUPROFILER_EVENT_SCOPE(LoadPackageStoreLocalization);
							const FSourceToLocalizedPackageIdMap* LocalizedPackages = nullptr;
							for (const FString& CultureName : CurrentCultureNames)
							{
								LocalizedPackages = ContainerHeader.CulturePackageMap.Find(CultureName);
								if (LocalizedPackages)
								{
									break;
								}
							}

							if (LocalizedPackages)
							{
								for (auto& Pair : *LocalizedPackages)
								{
									const FPackageId& SourceId = Pair.Key;
									const FPackageId& LocalizedId = Pair.Value;
									RedirectsPackageMap.Emplace(SourceId, LocalizedId);
									TargetRedirectIds.Add(LocalizedId);
								}
							}
						}

						{
							TRACE_CPUPROFILER_EVENT_SCOPE(LoadPackageStoreRedirects);
							for (const TPair<FPackageId, FPackageId>& Redirect : ContainerHeader.PackageRedirects)
							{
								const FPackageId& SourceId = Redirect.Key;
								const FPackageId& RedirectedId = Redirect.Value;
								RedirectsPackageMap.Emplace(SourceId, RedirectedId);
								TargetRedirectIds.Add(RedirectedId);
							}
						}
					}

					if (--Remaining == 0)
					{
						Event->Trigger();
					}
				});
			});
		}

		IoBatch.Issue();
		Event->Wait();
		FPlatformProcess::ReturnSynchEventToPool(Event);

		ApplyRedirects(RedirectsPackageMap);
	}

	void OnContainerMounted(const FIoDispatcherMountedContainer& Container)
	{
		LLM_SCOPE(ELLMTag::AsyncLoading);
		LoadContainers(MakeArrayView(&Container, 1));
	}

	void ApplyRedirects(const TMap<FPackageId, FPackageId>& Redirects)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ApplyRedirects);

		FScopeLock Lock(&PackageNameMapsCritical);

		if (Redirects.Num() == 0)
		{
			return;
		}

		for (auto It = Redirects.CreateConstIterator(); It; ++It)
		{
			const FPackageId& SourceId = It.Key();
			const FPackageId& RedirectId = It.Value();
			check(RedirectId.IsValid());
			FPackageStoreEntry* RedirectEntry = StoreEntriesMap.FindRef(RedirectId);
			check(RedirectEntry);
			FPackageStoreEntry*& PackageEntry = StoreEntriesMap.FindOrAdd(SourceId);
			if (RedirectEntry)
			{
				PackageEntry = RedirectEntry;
			}
		}

		for (auto It = StoreEntriesMap.CreateIterator(); It; ++It)
		{
			FPackageStoreEntry* StoreEntry = It.Value();

			for (FPackageId& ImportedPackageId : StoreEntry->ImportedPackages)
			{
				if (const FPackageId* RedirectId = Redirects.Find(ImportedPackageId))
				{
					ImportedPackageId = *RedirectId;
				}
			}
		}
	}

	void FinalizeInitialLoad()
	{
		ImportStore.FindAllScriptObjects();

		UE_LOG(LogStreaming, Display, TEXT("AsyncLoading2 - InitialLoad Finalized: %d script object entries in %.2f KB"),
			ImportStore.ScriptObjects.Num(), (float)ImportStore.ScriptObjects.GetAllocatedSize() / 1024.f);
	}

	inline FGlobalImportStore& GetGlobalImportStore()
	{
		return ImportStore;
	}

	void RemovePackage(FPackageId PackageId, UPackage* Package)
	{
		UE_ASYNC_UPACKAGE_DEBUG(Package);
		check(IsGarbageCollecting());

		if (!Package->CanBeImported())
		{
			return;
		}

		int32 RefCount = LoadedPackageStore.RemovePackage(PackageId);
		if (RefCount < 0) // not found
		{
			FPackageId* RedirectedId = RedirectsPackageMap.Find(PackageId);
			if (RedirectedId)
			{
				RefCount = LoadedPackageStore.RemovePackage(*RedirectedId);
			}
		}
		if (RefCount > 0)
		{
			UE_LOG(LogStreaming, Error,
				TEXT("RemovePackage: %s (0x%llX) %s (0x%llX) - with (ObjectFlags=%x, InternalObjectFlags=%x) - ")
				TEXT("Package destroyed while still being referenced, RefCount %d > 0."),
				*Package->GetName(), PackageId.Value(),
				*Package->FileName.ToString(), Package->GetPackageId().Value(),
				Package->GetFlags(), Package->GetInternalFlags(), RefCount);
			checkf(false, TEXT("Package %s destroyed with RefCount"), *Package->GetName());
		}
		else if (RefCount < 0)
		{
			UE_LOG(LogStreaming, Error,
				TEXT("RemovePackage: %s (0x%llX) %s (0x%llX) - with (ObjectFlags=%x, InternalObjectFlags=%x) - ")
				TEXT("Package not found!"),
				*Package->GetName(), PackageId.Value(),
				*Package->FileName.ToString(), Package->GetPackageId().Value(),
				Package->GetFlags(), Package->GetInternalFlags());
			checkf(false, TEXT("Package %s not found"), *Package->GetName());
		}
	}

	void RemovePackages(const FUnreachablePackages& Packages)
	{
		int32 PackageCount = Packages.Num();
		TArray<FPackageId> PackageIds;
		PackageIds.AddUninitialized(PackageCount);
		bool bForceSingleThreaded = PackageCount < 64;
		{
			// TRACE_CPUPROFILER_EVENT_SCOPE(FPackageId::FromName);
			ParallelFor(PackageCount, [&Packages, &PackageIds](int32 Index)
			{
				PackageIds[Index] = FPackageId::FromName(Packages[Index].Key);
			}, bForceSingleThreaded);
		}
		for (int32 Index = 0; Index < PackageCount; ++Index)
		{
			RemovePackage(PackageIds[Index], Packages[Index].Value);
		}
	}

	void ClearAllPublicExportsLoaded(const TArray<FPackageId>& PackageIds)
	{
		int32 PackageCount = PackageIds.Num();
		bool bForceSingleThreaded = PackageCount < 1024;
		ParallelFor(PackageCount, [this, &PackageIds](int32 Index)
		{
			if (FLoadedPackageRef* PackageRef = LoadedPackageStore.FindPackageRef(PackageIds[Index]))
			{
				PackageRef->ClearAllPublicExportsLoaded();
			}
		}, bForceSingleThreaded);
	}

	inline const FPackageStoreEntry* FindStoreEntry(FPackageId PackageId)
	{
		FScopeLock Lock(&PackageNameMapsCritical);
		FPackageStoreEntry* Entry = StoreEntriesMap.FindRef(PackageId);
		return Entry;
	}

	inline FPackageId GetRedirectedPackageId(FPackageId PackageId)
	{
		FScopeLock Lock(&PackageNameMapsCritical);
		FPackageId RedirectedId = RedirectsPackageMap.FindRef(PackageId);
		return RedirectedId;
	}

	bool IsRedirect(FPackageId PackageId) const
	{
		return TargetRedirectIds.Contains(PackageId);
	}
};

struct FPackageImportStore
{
	FPackageStore& GlobalPackageStore;
	FGlobalImportStore& GlobalImportStore;
	const FAsyncPackageDesc2& Desc;
	TArrayView<const FPackageObjectIndex> ImportMap;

	FPackageImportStore(FPackageStore& InGlobalPackageStore, const FAsyncPackageDesc2& InDesc)
		: GlobalPackageStore(InGlobalPackageStore)
		, GlobalImportStore(GlobalPackageStore.ImportStore)
		, Desc(InDesc)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(NewPackageImportStore);
		AddPackageReferences();
	}

	~FPackageImportStore()
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DeletePackageImportStore);
		check(ImportMap.Num() == 0);
		ReleasePackageReferences();
	}

	inline bool IsValidLocalImportIndex(FPackageIndex LocalIndex)
	{
		check(ImportMap.Num() > 0);
		return LocalIndex.IsImport() && LocalIndex.ToImport() < ImportMap.Num();
	}

	inline UObject* FindOrGetImportObjectFromLocalIndex(FPackageIndex LocalIndex)
	{
		check(LocalIndex.IsImport());
		check(ImportMap.Num() > 0);
		const int32 LocalImportIndex = LocalIndex.ToImport();
		check(LocalImportIndex < ImportMap.Num());
		const FPackageObjectIndex GlobalIndex = ImportMap[LocalIndex.ToImport()];
		UObject* Object = nullptr;
		if (GlobalIndex.IsImport())
		{
			Object = GlobalImportStore.FindOrGetImportObject(GlobalIndex);
		}
		else
		{
			check(GlobalIndex.IsNull());
		}
		return Object;
	}

	inline UObject* FindOrGetImportObject(FPackageObjectIndex GlobalIndex)
	{
		check(GlobalIndex.IsImport());
		return GlobalImportStore.FindOrGetImportObject(GlobalIndex);
	}

	bool GetUnresolvedCDOs(TArray<UClass*, TInlineAllocator<8>>& Classes)
	{
		for (const FPackageObjectIndex& Index : ImportMap)
		{
			if (!Index.IsScriptImport())
			{
				continue;
			}

			UObject* Object = GlobalImportStore.FindScriptImportObjectFromIndex(Index);
			if (Object)
			{
				continue;
			}

			const FScriptObjectEntry* Entry = GlobalImportStore.ScriptObjectEntriesMap.FindRef(Index);
			check(Entry);
			const FPackageObjectIndex& CDOClassIndex = Entry->CDOClassIndex;
			if (CDOClassIndex.IsScriptImport())
			{
				UObject* CDOClassObject = GlobalImportStore.FindScriptImportObjectFromIndex(CDOClassIndex);
				if (CDOClassObject)
				{
					UClass* CDOClass = static_cast<UClass*>(CDOClassObject);
					Classes.AddUnique(CDOClass);
				}
			}
		}
		return Classes.Num() > 0;
	}

	inline void StoreGlobalObject(FPackageId PackageId, FPackageObjectIndex GlobalIndex, UObject* Object)
	{
		GlobalImportStore.StoreGlobalObject(PackageId, GlobalIndex, Object);
	}

private:
	void AddAsyncFlags(UPackage* ImportedPackage)
	{
		UE_ASYNC_UPACKAGE_DEBUG(ImportedPackage);
		
		if (GUObjectArray.IsDisregardForGC(ImportedPackage))
		{
			return;
		}
		ForEachObjectWithOuter(ImportedPackage, [](UObject* Object)
		{
			if (Object->HasAllFlags(RF_Public | RF_WasLoaded))
			{
				checkf(!Object->HasAnyInternalFlags(EInternalObjectFlags::Async), TEXT("%s"), *Object->GetFullName());
				Object->SetInternalFlags(EInternalObjectFlags::Async);
			}
		}, /* bIncludeNestedObjects*/ true);
		checkf(!ImportedPackage->HasAnyInternalFlags(EInternalObjectFlags::Async), TEXT("%s"), *ImportedPackage->GetFullName());
		ImportedPackage->SetInternalFlags(EInternalObjectFlags::Async);
	}

	void ClearAsyncFlags(UPackage* ImportedPackage)
	{
		UE_ASYNC_UPACKAGE_DEBUG(ImportedPackage);

		if (GUObjectArray.IsDisregardForGC(ImportedPackage))
		{
			return;
		}
		ForEachObjectWithOuter(ImportedPackage, [](UObject* Object)
		{
			if (Object->HasAllFlags(RF_Public | RF_WasLoaded))
			{
				checkf(Object->HasAnyInternalFlags(EInternalObjectFlags::Async), TEXT("%s"), *Object->GetFullName());
				Object->AtomicallyClearInternalFlags(EInternalObjectFlags::Async);
			}
		}, /* bIncludeNestedObjects*/ true);
		checkf(ImportedPackage->HasAnyInternalFlags(EInternalObjectFlags::Async), TEXT("%s"), *ImportedPackage->GetFullName());
		ImportedPackage->AtomicallyClearInternalFlags(EInternalObjectFlags::Async);
	}

	void AddPackageReferences()
	{
		for (const FPackageId& ImportedPackageId : Desc.StoreEntry->ImportedPackages)
		{
			FLoadedPackageRef& PackageRef = GlobalPackageStore.LoadedPackageStore.GetPackageRef(ImportedPackageId);
			if (PackageRef.AddRef())
			{
				AddAsyncFlags(PackageRef.GetPackage());
			}
		}
		if (Desc.CanBeImported())
		{
			FLoadedPackageRef& PackageRef = GlobalPackageStore.LoadedPackageStore.GetPackageRef(Desc.DiskPackageId);
			PackageRef.ClearErrorFlags();
			if (PackageRef.AddRef())
			{
				AddAsyncFlags(PackageRef.GetPackage());
			}
		}
	}

	void ReleasePackageReferences()
	{
		for (const FPackageId& ImportedPackageId : Desc.StoreEntry->ImportedPackages)
		{
			FLoadedPackageRef& PackageRef = GlobalPackageStore.LoadedPackageStore.GetPackageRef(ImportedPackageId);
			if (PackageRef.ReleaseRef(Desc.DiskPackageId, ImportedPackageId))
			{
				ClearAsyncFlags(PackageRef.GetPackage());
			}
		}
		if (Desc.CanBeImported())
		{
			// clear own reference, and possible all async flags if no remaining ref count
			FLoadedPackageRef& PackageRef =	GlobalPackageStore.LoadedPackageStore.GetPackageRef(Desc.DiskPackageId);
			if (PackageRef.ReleaseRef(Desc.DiskPackageId, Desc.DiskPackageId))
			{
				ClearAsyncFlags(PackageRef.GetPackage());
			}
		}
	}
};
	
class FExportArchive final : public FArchive
{
public:
	FExportArchive(const uint8* AllExportDataPtr, const uint8* CurrentExportPtr, uint64 AllExportDataSize)
	{
#if (!DEVIRTUALIZE_FLinkerLoad_Serialize)
		ActiveFPLB = &InlineFPLB;
#endif
		ActiveFPLB->OriginalFastPathLoadBuffer = AllExportDataPtr;
		ActiveFPLB->StartFastPathLoadBuffer = CurrentExportPtr;
		ActiveFPLB->EndFastPathLoadBuffer = AllExportDataPtr + AllExportDataSize;
	}

	void ExportBufferBegin(UObject* Object, uint64 InExportCookedFileSerialOffset, uint64 InExportSerialSize)
	{
		CurrentExport = Object;
		CookedSerialOffset = InExportCookedFileSerialOffset;
		BufferSerialOffset = (ActiveFPLB->StartFastPathLoadBuffer - ActiveFPLB->OriginalFastPathLoadBuffer);
		CookedSerialSize = InExportSerialSize;
	}

	void ExportBufferEnd()
	{
		CurrentExport = nullptr;
		CookedSerialOffset = 0;
		BufferSerialOffset = 0;
		CookedSerialSize = 0;
	}

	void CheckBufferPosition(const TCHAR* Text, uint64 Offset = 0)
	{
#if DO_CHECK
		const uint64 BufferPosition = (ActiveFPLB->StartFastPathLoadBuffer - ActiveFPLB->OriginalFastPathLoadBuffer) + Offset;
		const bool bIsInsideExportBuffer =
			(BufferSerialOffset <= BufferPosition) && (BufferPosition <= BufferSerialOffset + CookedSerialSize);

		UE_ASYNC_PACKAGE_CLOG(
			!bIsInsideExportBuffer,
			Error, *PackageDesc, TEXT("FExportArchive::InvalidPosition"),
			TEXT("%s: Position %llu is outside of the current export buffer (%lld,%lld)."),
			Text,
			BufferPosition,
			BufferSerialOffset, BufferSerialOffset + CookedSerialSize);
#endif
	}

	void Skip(int64 InBytes)
	{
		CheckBufferPosition(TEXT("InvalidSkip"), InBytes);
		ActiveFPLB->StartFastPathLoadBuffer += InBytes;
	}

	virtual int64 TotalSize() override
	{
		return CookedHeaderSize + (ActiveFPLB->EndFastPathLoadBuffer - ActiveFPLB->OriginalFastPathLoadBuffer);
	}

	virtual int64 Tell() override
	{
		int64 CookedFilePosition = (ActiveFPLB->StartFastPathLoadBuffer - ActiveFPLB->OriginalFastPathLoadBuffer);
		CookedFilePosition -= BufferSerialOffset;
		CookedFilePosition += CookedSerialOffset;
		return CookedFilePosition;
	}

	virtual void Seek(int64 Position) override
	{
		uint64 BufferPosition = (uint64)Position;
		BufferPosition -= CookedSerialOffset;
		BufferPosition += BufferSerialOffset;
		ActiveFPLB->StartFastPathLoadBuffer = ActiveFPLB->OriginalFastPathLoadBuffer + BufferPosition;
		CheckBufferPosition(TEXT("InvalidSeek"));
	}

	virtual void Serialize(void* Data, int64 Length) override
	{
		if (!Length || ArIsError)
		{
			return;
		}
		CheckBufferPosition(TEXT("InvalidSerialize"), (uint64)Length);
		FMemory::Memcpy(Data, ActiveFPLB->StartFastPathLoadBuffer, Length);
		ActiveFPLB->StartFastPathLoadBuffer += Length;
	}

	void UsingCustomVersion(const FGuid& Key) override {};
	using FArchive::operator<<; // For visibility of the overloads we don't override

	virtual bool IsUsingEventDrivenLoader() const override
	{
		return true;
	}

	//~ Begin FArchive::FArchiveUObject Interface
	virtual FArchive& operator<<(FSoftObjectPath& Value) override { return FArchiveUObject::SerializeSoftObjectPath(*this, Value); }
	virtual FArchive& operator<<(FWeakObjectPtr& Value) override { return FArchiveUObject::SerializeWeakObjectPtr(*this, Value); }
	//~ End FArchive::FArchiveUObject Interface

	//~ Begin FArchive::FLinkerLoad Interface
	UObject* GetArchetypeFromLoader(const UObject* Obj) { return TemplateForGetArchetypeFromLoader; }

	virtual bool AttachExternalReadDependency(FExternalReadCallback& ReadCallback) override
	{
		ExternalReadDependencies->Add(ReadCallback);
		return true;
	}

	FORCENOINLINE void HandleBadExportIndex(int32 ExportIndex, UObject*& Object)
	{
		UE_ASYNC_PACKAGE_LOG(Fatal, *PackageDesc, TEXT("ObjectSerializationError"),
			TEXT("%s: Bad export index %d/%d."),
			CurrentExport ? *CurrentExport->GetFullName() : TEXT("null"), ExportIndex, Exports.Num());

		Object = nullptr;
	}

	FORCENOINLINE void HandleBadImportIndex(int32 ImportIndex, UObject*& Object)
	{
		UE_ASYNC_PACKAGE_LOG(Fatal, *PackageDesc, TEXT("ObjectSerializationError"),
			TEXT("%s: Bad import index %d/%d."),
			CurrentExport ? *CurrentExport->GetFullName() : TEXT("null"), ImportIndex, ImportStore->ImportMap.Num());
		Object = nullptr;
	}

	virtual FArchive& operator<<( UObject*& Object ) override
	{
		FPackageIndex Index;
		FArchive& Ar = *this;
		Ar << Index;

		if (Index.IsNull())
		{
			Object = nullptr;
		}
		else if (Index.IsExport())
		{
			const int32 ExportIndex = Index.ToExport();
			if (ExportIndex < Exports.Num())
			{
				Object = Exports[ExportIndex].Object;

#if ALT2_LOG_VERBOSE
				const FExportMapEntry& Export = ExportMap[ExportIndex];
				FName ObjectName = NameMap->GetName(Export.ObjectName);
				UE_ASYNC_PACKAGE_CLOG_VERBOSE(!Object, VeryVerbose, *PackageDesc,
					TEXT("FExportArchive: Object"), TEXT("Export %s at index %d is null."),
					*ObjectName.ToString(), 
					ExportIndex);
#endif
			}
			else
			{
				HandleBadExportIndex(ExportIndex, Object);
			}
		}
		else
		{
			if (ImportStore->IsValidLocalImportIndex(Index))
			{
				Object = ImportStore->FindOrGetImportObjectFromLocalIndex(Index);

				UE_ASYNC_PACKAGE_CLOG_VERBOSE(!Object, Log, *PackageDesc,
					TEXT("FExportArchive: Object"), TEXT("Import index %d is null"),
					Index.ToImport());
			}
			else
			{
				HandleBadImportIndex(Index.ToImport(), Object);
			}
		}
		return *this;
	}

	inline virtual FArchive& operator<<(FLazyObjectPtr& LazyObjectPtr) override
	{
		FArchive& Ar = *this;
		FUniqueObjectGuid ID;
		Ar << ID;
		LazyObjectPtr = ID;
		return Ar;
	}

	inline virtual FArchive& operator<<(FSoftObjectPtr& Value) override
	{
		FArchive& Ar = *this;
		FSoftObjectPath ID;
		ID.Serialize(Ar);
		Value = ID;
		return Ar;
	}

	FORCENOINLINE void HandleBadNameIndex(int32 NameIndex, FName& Name)
	{
		UE_ASYNC_PACKAGE_LOG(Fatal, *PackageDesc, TEXT("ObjectSerializationError"),
			TEXT("%s: Bad name index %d/%d."),
			CurrentExport ? *CurrentExport->GetFullName() : TEXT("null"), NameIndex, NameMap->Num());
		Name = FName();
		SetCriticalError();
	}

	inline virtual FArchive& operator<<(FName& Name) override
	{
		FArchive& Ar = *this;
		uint32 NameIndex;
		Ar << NameIndex;
		uint32 Number = 0;
		Ar << Number;

		FMappedName MappedName = FMappedName::Create(NameIndex, Number, FMappedName::EType::Package);
		if (!NameMap->TryGetName(MappedName, Name))
		{
			HandleBadNameIndex(NameIndex, Name);
		}
		return *this;
	}
	//~ End FArchive::FLinkerLoad Interface

private:
	friend FAsyncPackage2;
#if (!DEVIRTUALIZE_FLinkerLoad_Serialize)
	FArchive::FFastPathLoadBuffer InlineFPLB;
	FArchive::FFastPathLoadBuffer* ActiveFPLB;
#endif

	UObject* TemplateForGetArchetypeFromLoader = nullptr;

	FAsyncPackageDesc2* PackageDesc = nullptr;
	FPackageImportStore* ImportStore = nullptr;
	TArray<FExternalReadCallback>* ExternalReadDependencies;
	const FNameMap* NameMap = nullptr;
	TArrayView<const FExportObject> Exports;
	const FExportMapEntry* ExportMap = nullptr;
	UObject* CurrentExport = nullptr;
	uint32 CookedHeaderSize = 0;
	uint64 CookedSerialOffset = 0;
	uint64 CookedSerialSize = 0;
	uint64 BufferSerialOffset = 0;
};

enum class EAsyncPackageLoadingState2 : uint8
{
	NewPackage,
	ImportPackages,
	ImportPackagesDone,
	WaitingForIo,
	ProcessPackageSummary,
	ProcessExportBundles,
	WaitingForExternalReads,
	ExportsDone,
	PostLoad,
	DeferredPostLoad,
	DeferredPostLoadDone,
	Finalize,
	CreateClusters,
	Complete,
	DeferredDelete,
};

class FEventLoadGraphAllocator;
struct FAsyncLoadEventSpec;
struct FAsyncLoadingThreadState2;

/** [EDL] Event Load Node */
class FEventLoadNode2
{
	enum class ENodeState : uint8
	{
		Waiting = 0,
		Executing,
		Timeout,
		Completed
	};
public:
	FEventLoadNode2(const FAsyncLoadEventSpec* InSpec, FAsyncPackage2* InPackage, int32 InImportOrExportIndex, int32 InBarrierCount);
	void DependsOn(FEventLoadNode2* Other);
	void AddBarrier();
	void AddBarrier(int32 Count);
	void ReleaseBarrier(FAsyncLoadingThreadState2* ThreadState = nullptr);
	void Execute(FAsyncLoadingThreadState2& ThreadState);

	int32 GetBarrierCount()
	{
		return BarrierCount.Load();
	}

	inline bool IsDone()
	{
		return ENodeState::Completed == static_cast<ENodeState>(NodeState.Load());
	}

	inline bool IsExecuting() const
	{
		return ENodeState::Executing == static_cast<ENodeState>(NodeState.Load());
	}

	inline void SetState(ENodeState InNodeState)
	{
		NodeState.Store(static_cast<uint8>(InNodeState));
	}

private:
	void ProcessDependencies(FAsyncLoadingThreadState2& ThreadState);
	void Fire(FAsyncLoadingThreadState2* ThreadState = nullptr);

	union
	{
		FEventLoadNode2* SingleDependent;
		FEventLoadNode2** MultipleDependents;
	};
	uint32 DependenciesCount = 0;
	uint32 DependenciesCapacity = 0;
	TAtomic<int32> BarrierCount { 0 };
	TAtomic<uint8> DependencyWriterCount { 0 };
	TAtomic<uint8> NodeState { static_cast<uint8>(ENodeState::Waiting) };
#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	TAtomic<uint8> bFired { 0 };
#endif

	const FAsyncLoadEventSpec* Spec = nullptr;
	FAsyncPackage2* Package = nullptr;
	int32 ImportOrExportIndex = -1;
};

class FAsyncLoadEventGraphAllocator
{
public:
	FEventLoadNode2** AllocArcs(uint32 Count)
	{
		//TRACE_CPUPROFILER_EVENT_SCOPE(AllocArcs);
		SIZE_T Size = Count * sizeof(FEventLoadNode2*);
		TotalArcCount += Count;
		TotalAllocated += Size;
		return reinterpret_cast<FEventLoadNode2**>(FMemory::Malloc(Size));
	}

	void FreeArcs(FEventLoadNode2** Arcs, uint32 Count)
	{
		//TRACE_CPUPROFILER_EVENT_SCOPE(FreeArcs);
		FMemory::Free(Arcs);
		SIZE_T Size = Count * sizeof(FEventLoadNode2*);
		TotalAllocated -= Size;
		TotalArcCount -= Count;
	}

	TAtomic<int64> TotalArcCount { 0 };
	TAtomic<int64> TotalAllocated { 0 };
};

class FAsyncLoadEventQueue2
{
public:
	FAsyncLoadEventQueue2();
	~FAsyncLoadEventQueue2();

	void SetZenaphore(FZenaphore* InZenaphore)
	{
		Zenaphore = InZenaphore;
	}

	bool PopAndExecute(FAsyncLoadingThreadState2& ThreadState);
	void Push(FEventLoadNode2* Node);

private:
	FZenaphore* Zenaphore = nullptr;
	TAtomic<uint64> Head { 0 };
	TAtomic<uint64> Tail { 0 };
	TAtomic<FEventLoadNode2*> Entries[524288];
};

struct FAsyncLoadEventSpec
{
	typedef EAsyncPackageState::Type(*FAsyncLoadEventFunc)(FAsyncLoadingThreadState2&, FAsyncPackage2*, int32);
	FAsyncLoadEventFunc Func = nullptr;
	FAsyncLoadEventQueue2* EventQueue = nullptr;
	bool bExecuteImmediately = false;
};

struct FAsyncLoadingThreadState2
	: public FTlsAutoCleanup
{
	static FAsyncLoadingThreadState2* Create(FAsyncLoadEventGraphAllocator& GraphAllocator, FIoDispatcher& IoDispatcher)
	{
		check(TlsSlot != 0);
		check(!FPlatformTLS::GetTlsValue(TlsSlot));
		FAsyncLoadingThreadState2* State = new FAsyncLoadingThreadState2(GraphAllocator, IoDispatcher);
		State->Register();
		FPlatformTLS::SetTlsValue(TlsSlot, State);
		return State;
	}

	static FAsyncLoadingThreadState2* Get()
	{
		check(TlsSlot != 0);
		return static_cast<FAsyncLoadingThreadState2*>(FPlatformTLS::GetTlsValue(TlsSlot));
	}

	FAsyncLoadingThreadState2(FAsyncLoadEventGraphAllocator& InGraphAllocator, FIoDispatcher& InIoDispatcher)
		: GraphAllocator(InGraphAllocator)
	{

	}

	bool HasDeferredFrees() const
	{
		return DeferredFreeArcs.Num() > 0;
	}

	void ProcessDeferredFrees()
	{
		if (DeferredFreeArcs.Num() > 0)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ProcessDeferredFrees);
			for (TTuple<FEventLoadNode2**, uint32>& DeferredFreeArc : DeferredFreeArcs)
			{
				GraphAllocator.FreeArcs(DeferredFreeArc.Get<0>(), DeferredFreeArc.Get<1>());
			}
			DeferredFreeArcs.Reset();
		}
	}

	void SetTimeLimit(bool bInUseTimeLimit, double InTimeLimit)
	{
		bUseTimeLimit = bInUseTimeLimit;
		TimeLimit = InTimeLimit;
		StartTime = FPlatformTime::Seconds();
	}

	bool IsTimeLimitExceeded(const TCHAR* InLastTypeOfWorkPerformed = nullptr, UObject* InLastObjectWorkWasPerformedOn = nullptr)
	{
		bool bTimeLimitExceeded = false;

		if (bUseTimeLimit)
		{
			double CurrentTime = FPlatformTime::Seconds();
			bTimeLimitExceeded = CurrentTime - StartTime > TimeLimit;

			if (bTimeLimitExceeded && GWarnIfTimeLimitExceeded)
			{
				IsTimeLimitExceededPrint(StartTime, CurrentTime, LastTestTime, TimeLimit, InLastTypeOfWorkPerformed, InLastObjectWorkWasPerformedOn);
			}

			LastTestTime = CurrentTime;
		}

		if (!bTimeLimitExceeded)
		{
			bTimeLimitExceeded = IsGarbageCollectionWaiting();
			UE_CLOG(bTimeLimitExceeded, LogStreaming, Verbose, TEXT("Timing out async loading due to Garbage Collection request"));
		}

		return bTimeLimitExceeded;
	}

	bool UseTimeLimit()
	{
		return bUseTimeLimit;
	}

	FAsyncLoadEventGraphAllocator& GraphAllocator;
	TArray<TTuple<FEventLoadNode2**, uint32>> DeferredFreeArcs;
	TArray<FEventLoadNode2*> NodesToFire;
	FEventLoadNode2* CurrentEventNode = nullptr;
	bool bShouldFireNodes = true;
	bool bUseTimeLimit = false;
	double TimeLimit = 0.0;
	double StartTime = 0.0;
	double LastTestTime = -1.0;
	static uint32 TlsSlot;
};

uint32 FAsyncLoadingThreadState2::TlsSlot;

/**
 * Event node.
 */
enum EEventLoadNode2 : uint8
{
	Package_ProcessSummary,
	Package_ExportsSerialized,
	Package_NumPhases,

	ExportBundle_Process = 0,
	ExportBundle_PostLoad,
	ExportBundle_DeferredPostLoad,
	ExportBundle_NumPhases,
};

struct FAsyncPackageData
{
	int32 ExportCount = 0;
	int32 ExportBundleCount = 0;
	uint64 ExportBundlesMetaSize = 0;
	uint8* ExportBundlesMetaMemory = nullptr;
	const FExportBundleHeader* ExportBundleHeaders = nullptr;
	const FExportBundleEntry* ExportBundleEntries = nullptr;
	TArrayView<FExportObject> Exports;
	TArrayView<FAsyncPackage2*> ImportedAsyncPackages;
	TArrayView<FEventLoadNode2> PackageNodes;
	TArrayView<FEventLoadNode2> ExportBundleNodes;
};

/**
* Structure containing intermediate data required for async loading of all exports of a package.
*/

struct FAsyncPackage2
{
	friend struct FScopedAsyncPackageEvent2;
	friend struct FAsyncPackageScope2;
	friend class FAsyncLoadingThread2;

	FAsyncPackage2(const FAsyncPackageDesc2& InDesc,
		const FAsyncPackageData& InData,
		FAsyncLoadingThread2& InAsyncLoadingThread,
		FAsyncLoadEventGraphAllocator& InGraphAllocator,
		const FAsyncLoadEventSpec* EventSpecs);
	virtual ~FAsyncPackage2();


	void AddRef()
	{
		++RefCount;
	}

	void ReleaseRef();

	void ClearImportedPackages();

	/** Marks a specific request as complete */
	void MarkRequestIDsAsComplete();

	/**
	 * @return Time load begun. This is NOT the time the load was requested in the case of other pending requests.
	 */
	double GetLoadStartTime() const;

	void AddCompletionCallback(TUniquePtr<FLoadPackageAsyncDelegate>&& Callback);

	FORCEINLINE UPackage* GetLinkerRoot() const
	{
		return LinkerRoot;
	}

	/** Returns true if loading has failed */
	FORCEINLINE bool HasLoadFailed() const
	{
		return bLoadHasFailed;
	}

	/** Adds new request ID to the existing package */
	void AddRequestID(int32 Id);

	/**
	* Cancel loading this package.
	*/
	void Cancel();

	void AddConstructedObject(UObject* Object, bool bSubObjectThatAlreadyExists)
	{
		if (bSubObjectThatAlreadyExists)
		{
			ConstructedObjects.AddUnique(Object);
		}
		else
		{
			checkf(!ConstructedObjects.Contains(Object), TEXT("%s"), *Object->GetFullName());
			ConstructedObjects.Add(Object);
		}
	}

	void PinObjectForGC(UObject* Object, bool bIsNewObject)
	{
		if (bIsNewObject && !IsInGameThread())
		{
			checkf(Object->HasAnyInternalFlags(EInternalObjectFlags::Async), TEXT("%s"), *Object->GetFullName());
		}
		else
		{
			Object->SetInternalFlags(EInternalObjectFlags::Async);
		}
	}

	void ClearConstructedObjects();

	/** Returns the UPackage wrapped by this, if it is valid */
	UPackage* GetLoadedPackage();

	/** Checks if all dependencies (imported packages) of this package have been fully loaded */
	bool AreAllDependenciesFullyLoaded(TSet<FPackageId>& VisitedPackages);

	/** Creates GC clusters from loaded objects */
	EAsyncPackageState::Type CreateClusters(FAsyncLoadingThreadState2& ThreadState);

	void ImportPackagesRecursive();
	void StartLoading();

#if WITH_IOSTORE_IN_EDITOR
	void GetLoadedAssets(TArray<FWeakObjectPtr>& AssetList);
#endif

private:

	/** Checks if all dependencies (imported packages) of this package have been fully loaded */
	bool AreAllDependenciesFullyLoadedInternal(FAsyncPackage2* Package, TSet<FPackageId>& VisitedPackages, FPackageId& OutPackageId);

	/** Basic information associated with this package */
	FAsyncPackageDesc2 Desc;
	FAsyncPackageData Data;
	/** Cached async loading thread object this package was created by */
	FAsyncLoadingThread2& AsyncLoadingThread;
	FAsyncLoadEventGraphAllocator& GraphAllocator;
	/** Package which is going to have its exports and imports loaded */
	UPackage* LinkerRoot = nullptr;
	/** Time load begun. This is NOT the time the load was requested in the case of pending requests.	*/
	double						LoadStartTime = 0.0;
	TAtomic<int32> RefCount{ 0 };
	/** Current bundle entry index in the current export bundle */
	int32						ExportBundleEntryIndex = 0;
	/** Current index into ExternalReadDependencies array used to spread wating for external reads over several frames			*/
	int32						ExternalReadIndex = 0;
	/** Current index into DeferredClusterObjects array used to spread routing CreateClusters over several frames			*/
	int32						DeferredClusterIndex = 0;
	EAsyncPackageLoadingState2	AsyncPackageLoadingState = EAsyncPackageLoadingState2::NewPackage;
	/** True if our load has failed */
	bool						bLoadHasFailed = false;
	/** True if this package was created by this async package */
	bool						bCreatedLinkerRoot = false;

	/** List of all request handles */
	TArray<int32, TInlineAllocator<2>> RequestIDs;
	/** List of ConstructedObjects = Exports + UPackage + ObjectsCreatedFromExports */
	TArray<UObject*> ConstructedObjects;
	TArray<FExternalReadCallback> ExternalReadDependencies;
	/** Call backs called when we finished loading this package											*/
	using FCompletionCallback = TUniquePtr<FLoadPackageAsyncDelegate>;
	TArray<FCompletionCallback, TInlineAllocator<2>> CompletionCallbacks;

	FIoRequest IoRequest;
	FIoBuffer IoBuffer;
	const uint8* CurrentExportDataPtr = nullptr;
	const uint8* AllExportDataPtr = nullptr;
	uint64 ExportBundlesSize = 0;
	uint32 CookedHeaderSize = 0;
	uint32 LoadOrder = 0;

	const FExportMapEntry* ExportMap = nullptr;
	FPackageImportStore ImportStore;
	FNameMap NameMap;

public:

	FAsyncLoadingThread2& GetAsyncLoadingThread()
	{
		return AsyncLoadingThread;
	}

	FAsyncLoadEventGraphAllocator& GetGraphAllocator()
	{
		return GraphAllocator;
	}

	/** [EDL] Begin Event driven loader specific stuff */

	static EAsyncPackageState::Type Event_ProcessExportBundle(FAsyncLoadingThreadState2& ThreadState, FAsyncPackage2* Package, int32 ExportBundleIndex);
	static EAsyncPackageState::Type Event_ProcessPackageSummary(FAsyncLoadingThreadState2& ThreadState, FAsyncPackage2* Package, int32);
	static EAsyncPackageState::Type Event_ExportsDone(FAsyncLoadingThreadState2& ThreadState, FAsyncPackage2* Package, int32);
	static EAsyncPackageState::Type Event_PostLoadExportBundle(FAsyncLoadingThreadState2& ThreadState, FAsyncPackage2* Package, int32 ExportBundleIndex);
	static EAsyncPackageState::Type Event_DeferredPostLoadExportBundle(FAsyncLoadingThreadState2& ThreadState, FAsyncPackage2* Package, int32 ExportBundleIndex);
	
	void EventDrivenCreateExport(int32 LocalExportIndex);
	bool EventDrivenSerializeExport(int32 LocalExportIndex, FExportArchive& Ar);

	UObject* EventDrivenIndexToObject(FPackageObjectIndex Index, bool bCheckSerialized);
	template<class T>
	T* CastEventDrivenIndexToObject(FPackageObjectIndex Index, bool bCheckSerialized)
	{
		UObject* Result = EventDrivenIndexToObject(Index, bCheckSerialized);
		if (!Result)
		{
			return nullptr;
		}
		return CastChecked<T>(Result);
	}

	FEventLoadNode2& GetPackageNode(EEventLoadNode2 Phase);
	FEventLoadNode2& GetExportBundleNode(EEventLoadNode2 Phase, uint32 ExportBundleIndex);

	/** [EDL] End Event driven loader specific stuff */

	void CallCompletionCallbacks(EAsyncLoadingResult::Type LoadingResult);

private:
	void CreateNodes(const FAsyncLoadEventSpec* EventSpecs);
	void SetupSerializedArcs(const uint8* GraphData, uint64 GraphDataSize);
	void SetupScriptDependencies();

	/**
	 * Begin async loading process. Simulates parts of BeginLoad.
	 *
	 * Objects created during BeginAsyncLoad and EndAsyncLoad will have EInternalObjectFlags::AsyncLoading set
	 */
	void BeginAsyncLoad();
	/**
	 * End async loading process. Simulates parts of EndLoad().
	 */
	void EndAsyncLoad();
	/**
	 * Create UPackage
	 *
	 * @return true
	 */
	void CreateUPackage(const FPackageSummary* PackageSummary);

	/**
	 * Finish up UPackage
	 *
	 * @return true
	 */
	void FinishUPackage();

	/**
	 * Finalizes external dependencies till time limit is exceeded
	 *
	 * @return Complete if all dependencies are finished, TimeOut otherwise
	 */
	enum EExternalReadAction { ExternalReadAction_Poll, ExternalReadAction_Wait };
	EAsyncPackageState::Type ProcessExternalReads(EExternalReadAction Action);

	/**
	* Updates load percentage stat
	*/
	void UpdateLoadPercentage();

public:

	/** Serialization context for this package */
	FUObjectSerializeContext* GetSerializeContext();
};

struct FScopedAsyncPackageEvent2
{
	/** Current scope package */
	FAsyncPackage2* Package;
	/** Outer scope package */
	FAsyncPackage2* PreviousPackage;
#if WITH_IOSTORE_IN_EDITOR
	IAsyncPackageLoader* PreviousAsyncPackageLoader;
#endif

	FScopedAsyncPackageEvent2(FAsyncPackage2* InPackage);
	~FScopedAsyncPackageEvent2();
};

class FAsyncLoadingThreadWorker : private FRunnable
{
public:
	FAsyncLoadingThreadWorker(FAsyncLoadEventGraphAllocator& InGraphAllocator, FAsyncLoadEventQueue2& InEventQueue, FIoDispatcher& InIoDispatcher, FZenaphore& InZenaphore, TAtomic<int32>& InActiveWorkersCount)
		: Zenaphore(InZenaphore)
		, EventQueue(InEventQueue)
		, GraphAllocator(InGraphAllocator)
		, IoDispatcher(InIoDispatcher)
		, ActiveWorkersCount(InActiveWorkersCount)
	{
	}

	void StartThread();
	
	void StopThread()
	{
		bStopRequested = true;
		bSuspendRequested = true;
		Zenaphore.NotifyAll();
	}
	
	void SuspendThread()
	{
		bSuspendRequested = true;
		Zenaphore.NotifyAll();
	}
	
	void ResumeThread()
	{
		bSuspendRequested = false;
	}
	
	int32 GetThreadId() const
	{
		return ThreadId;
	}

private:
	virtual bool Init() override { return true; }
	virtual uint32 Run() override;
	virtual void Stop() override {};

	FZenaphore& Zenaphore;
	FAsyncLoadEventQueue2& EventQueue;
	FAsyncLoadEventGraphAllocator& GraphAllocator;
	FIoDispatcher& IoDispatcher;
	TAtomic<int32>& ActiveWorkersCount;
	FRunnableThread* Thread = nullptr;
	TAtomic<bool> bStopRequested { false };
	TAtomic<bool> bSuspendRequested { false };
	int32 ThreadId = 0;
};

class FAsyncLoadingThread2 final
	: public FRunnable
	, public IAsyncPackageLoader
{
	friend struct FAsyncPackage2;
public:
	FAsyncLoadingThread2(FIoDispatcher& IoDispatcher);
	virtual ~FAsyncLoadingThread2();

private:
	/** Thread to run the worker FRunnable on */
	FRunnableThread* Thread;
	TAtomic<bool> bStopRequested { false };
	TAtomic<bool> bSuspendRequested { false };
	TArray<FAsyncLoadingThreadWorker> Workers;
	TAtomic<int32> ActiveWorkersCount { 0 };
	bool bWorkersSuspended = false;

	/** [ASYNC/GAME THREAD] true if the async thread is actually started. We don't start it until after we boot because the boot process on the game thread can create objects that are also being created by the loader */
	bool bThreadStarted = false;

	mutable bool bLazyInitializedFromLoadPackage = false;

#if ALT2_VERIFY_RECURSIVE_LOADS
	int32 LoadRecursionLevel = 0;
#endif

#if !UE_BUILD_SHIPPING
	FPlatformFileOpenLog* FileOpenLogWrapper = nullptr;
#endif

	/** [ASYNC/GAME THREAD] Event used to signal loading should be cancelled */
	FEvent* CancelLoadingEvent;
	/** [ASYNC/GAME THREAD] Event used to signal that the async loading thread should be suspended */
	FEvent* ThreadSuspendedEvent;
	/** [ASYNC/GAME THREAD] Event used to signal that the async loading thread has resumed */
	FEvent* ThreadResumedEvent;
	/** [ASYNC/GAME THREAD] List of queued packages to stream */
	TArray<FAsyncPackageDesc2*> QueuedPackages;
	/** [ASYNC/GAME THREAD] Package queue critical section */
	FCriticalSection QueueCritical;
	TArray<FAsyncPackage2*> LoadedPackagesToProcess;
	/** [GAME THREAD] Game thread CompletedPackages list */
	TArray<FAsyncPackage2*> CompletedPackages;
	/** [ASYNC/GAME THREAD] Packages to be deleted from async thread */
	TQueue<FAsyncPackage2*, EQueueMode::Spsc> DeferredDeletePackages;
	
	struct FQueuedFailedPackageCallback
	{
		FName PackageName;
		TUniquePtr<FLoadPackageAsyncDelegate> Callback;
	};
	TArray<FQueuedFailedPackageCallback> QueuedFailedPackageCallbacks;

	FCriticalSection AsyncPackagesCritical;
	/** Packages in active loading with GetAsyncPackageId() as key */
	TMap<FPackageId, FAsyncPackage2*> AsyncPackageLookup;

	TQueue<FAsyncPackage2*, EQueueMode::Mpsc> ExternalReadQueue;
	FThreadSafeCounter WaitingForIoBundleCounter;
	
	/** List of all pending package requests */
	TSet<int32> PendingRequests;
	/** Synchronization object for PendingRequests list */
	FCriticalSection PendingRequestsCritical;

	/** [ASYNC/GAME THREAD] Number of package load requests in the async loading queue */
	TAtomic<uint32> QueuedPackagesCounter { 0 };
	/** [ASYNC/GAME THREAD] Number of packages being loaded on the async thread and post loaded on the game thread */
	FThreadSafeCounter ExistingAsyncPackagesCounter;
	/** [ASYNC/GAME THREAD] Number of packages being loaded on the async thread and post loaded on the game thread. Excludes packages in the deferred delete queue*/
	FThreadSafeCounter ActiveAsyncPackagesCounter;

	FThreadSafeCounter AsyncThreadReady;

	/** When cancelling async loading: list of package requests to cancel */
	TArray<FAsyncPackageDesc2*> QueuedPackagesToCancel;
	/** When cancelling async loading: list of packages to cancel */
	TSet<FAsyncPackage2*> PackagesToCancel;

	/** Async loading thread ID */
	uint32 AsyncLoadingThreadID;

	/** I/O Dispatcher */
	FIoDispatcher& IoDispatcher;

	FNameMap GlobalNameMap;
	FPackageStore GlobalPackageStore;

	/** Initial load pending CDOs */
	TMap<UClass*, TArray<FEventLoadNode2*>> PendingCDOs;

	struct FBundleIoRequest
	{
		bool operator<(const FBundleIoRequest& Other) const
		{
			return Package->LoadOrder < Other.Package->LoadOrder;
		}

		FAsyncPackage2* Package;
	};
	TArray<FBundleIoRequest> WaitingIoRequests;
	uint64 PendingBundleIoRequestsTotalSize = 0;

public:

	//~ Begin FRunnable Interface.
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	//~ End FRunnable Interface

	/** Start the async loading thread */
	virtual void StartThread() override;

	/** [EDL] Event queue */
	FZenaphore AltZenaphore;
	TArray<FZenaphore> WorkerZenaphores;
	FAsyncLoadEventGraphAllocator GraphAllocator;
	FAsyncLoadEventQueue2 EventQueue;
	FAsyncLoadEventQueue2 MainThreadEventQueue;
	TArray<FAsyncLoadEventQueue2*> AltEventQueues;
	TArray<FAsyncLoadEventSpec> EventSpecs;

	/** True if multithreaded async loading is currently being used. */
	inline virtual bool IsMultithreaded() override
	{
		return bThreadStarted;
	}

	/** Sets the current state of async loading */
	void EnterAsyncLoadingTick()
	{
		AsyncLoadingTickCounter++;
	}

	void LeaveAsyncLoadingTick()
	{
		AsyncLoadingTickCounter--;
		check(AsyncLoadingTickCounter >= 0);
	}

	/** Gets the current state of async loading */
	bool GetIsInAsyncLoadingTick() const
	{
		return !!AsyncLoadingTickCounter;
	}

	/** Returns true if packages are currently being loaded on the async thread */
	inline virtual bool IsAsyncLoadingPackages() override
	{
		return QueuedPackagesCounter != 0 || ExistingAsyncPackagesCounter.GetValue() != 0;
	}

	/** Returns true this codes runs on the async loading thread */
	virtual bool IsInAsyncLoadThread() override
	{
		if (IsMultithreaded())
		{
			// We still need to report we're in async loading thread even if 
			// we're on game thread but inside of async loading code (PostLoad mostly)
			// to make it behave exactly like the non-threaded version
			uint32 CurrentThreadId = FPlatformTLS::GetCurrentThreadId();
			if (CurrentThreadId == AsyncLoadingThreadID ||
				(IsInGameThread() && GetIsInAsyncLoadingTick()))
			{
				return true;
			}
			else
			{
				for (const FAsyncLoadingThreadWorker& Worker : Workers)
				{
					if (CurrentThreadId == Worker.GetThreadId())
					{
						return true;
					}
				}
			}
			return false;
		}
		else
		{
			return IsInGameThread() && GetIsInAsyncLoadingTick();
		}
	}

	/** Returns true if async loading is suspended */
	inline virtual bool IsAsyncLoadingSuspended() override
	{
		return bSuspendRequested;
	}

	virtual void NotifyConstructedDuringAsyncLoading(UObject* Object, bool bSubObjectThatAlreadyExists) override;

	virtual void NotifyUnreachableObjects(const TArrayView<FUObjectItem*>& UnreachableObjects) override;

	virtual void FireCompletedCompiledInImport(void* AsyncPackage, FPackageIndex Import) override {}

	/**
	* [ASYNC THREAD] Finds an existing async package in the AsyncPackages by its name.
	*
	* @param PackageName async package name.
	* @return Pointer to the package or nullptr if not found
	*/
	FORCEINLINE FAsyncPackage2* FindAsyncPackage(const FName& PackageName)
	{
		// TRACE_CPUPROFILER_EVENT_SCOPE(FindAsyncPackage);
		FPackageId PackageId = FPackageId::FromName(PackageName);
		if (PackageId.IsValid())
		{
			FScopeLock LockAsyncPackages(&AsyncPackagesCritical);
			//checkSlow(IsInAsyncLoadThread());
			return AsyncPackageLookup.FindRef(PackageId);
		}
		return nullptr;
	}

	FORCEINLINE FAsyncPackage2* GetAsyncPackage(const FPackageId& PackageId)
	{
		// TRACE_CPUPROFILER_EVENT_SCOPE(GetAsyncPackage);
		FScopeLock LockAsyncPackages(&AsyncPackagesCritical);
		return AsyncPackageLookup.FindRef(PackageId);
	}

	void UpdatePackagePriority(FAsyncPackage2* Package, int32 NewPriority);
	
	FAsyncPackage2* FindOrInsertPackage(FAsyncPackageDesc2* InDesc, bool& bInserted);

	/**
	* [ASYNC/GAME THREAD] Queues a package for streaming.
	*
	* @param Package package descriptor.
	*/
	void QueuePackage(FAsyncPackageDesc2& Package);

	/**
	* [ASYNC* THREAD] Loads all packages
	*
	* @param OutPackagesProcessed Number of packages processed in this call.
	* @return The current state of async loading
	*/
	EAsyncPackageState::Type ProcessAsyncLoadingFromGameThread(FAsyncLoadingThreadState2& ThreadState, int32& OutPackagesProcessed);

	/**
	* [GAME THREAD] Ticks game thread side of async loading.
	*
	* @param bUseTimeLimit True if time limit should be used [time-slicing].
	* @param bUseFullTimeLimit True if full time limit should be used [time-slicing].
	* @param TimeLimit Maximum amount of time that can be spent in this call [time-slicing].
	* @param FlushTree Package dependency tree to be flushed
	* @return The current state of async loading
	*/
	EAsyncPackageState::Type TickAsyncLoadingFromGameThread(FAsyncLoadingThreadState2& ThreadState, bool bUseTimeLimit, bool bUseFullTimeLimit, float TimeLimit, int32 FlushRequestID = INDEX_NONE);

	/**
	* [ASYNC THREAD] Main thread loop
	*
	* @param bUseTimeLimit True if time limit should be used [time-slicing].
	* @param bUseFullTimeLimit True if full time limit should be used [time-slicing].
	* @param TimeLimit Maximum amount of time that can be spent in this call [time-slicing].
	* @param FlushTree Package dependency tree to be flushed
	*/
	EAsyncPackageState::Type TickAsyncThreadFromGameThread(FAsyncLoadingThreadState2& ThreadState, bool& bDidSomething);

	/** Initializes async loading thread */
	virtual void InitializeLoading() override;

	virtual void ShutdownLoading() override;

	virtual int32 LoadPackage(
		const FString& InPackageName,
		const FGuid* InGuid,
		const TCHAR* InPackageToLoadFrom,
		FLoadPackageAsyncDelegate InCompletionDelegate,
		EPackageFlags InPackageFlags,
		int32 InPIEInstanceID,
		int32 InPackagePriority,
		const FLinkerInstancingContext* InstancingContext = nullptr) override;

	EAsyncPackageState::Type ProcessLoadingFromGameThread(FAsyncLoadingThreadState2& ThreadState, bool bUseTimeLimit, bool bUseFullTimeLimit, float TimeLimit);

	inline virtual EAsyncPackageState::Type ProcessLoading(bool bUseTimeLimit, bool bUseFullTimeLimit, float TimeLimit) override
	{
		FAsyncLoadingThreadState2& ThreadState = *FAsyncLoadingThreadState2::Get();
		return ProcessLoadingFromGameThread(ThreadState, bUseTimeLimit, bUseFullTimeLimit, TimeLimit);
	}

	EAsyncPackageState::Type ProcessLoadingUntilCompleteFromGameThread(FAsyncLoadingThreadState2& ThreadState, TFunctionRef<bool()> CompletionPredicate, float TimeLimit);

	inline virtual EAsyncPackageState::Type ProcessLoadingUntilComplete(TFunctionRef<bool()> CompletionPredicate, float TimeLimit) override
	{
		FAsyncLoadingThreadState2& ThreadState = *FAsyncLoadingThreadState2::Get();
		return ProcessLoadingUntilCompleteFromGameThread(ThreadState, CompletionPredicate, TimeLimit);
	}

	virtual void CancelLoading() override;

	virtual void SuspendLoading() override;

	virtual void ResumeLoading() override;

	virtual void FlushLoading(int32 PackageId) override;

	virtual int32 GetNumQueuedPackages() override
	{
		return QueuedPackagesCounter;
	}

	virtual int32 GetNumAsyncPackages() override
	{
		return ActiveAsyncPackagesCounter.GetValue();
	}

	/**
	 * [GAME THREAD] Gets the load percentage of the specified package
	 * @param PackageName Name of the package to return async load percentage for
	 * @return Percentage (0-100) of the async package load or -1 of package has not been found
	 */
	virtual float GetAsyncLoadPercentage(const FName& PackageName) override;

	/**
	 * [ASYNC/GAME THREAD] Checks if a request ID already is added to the loading queue
	 */
	bool ContainsRequestID(int32 RequestID)
	{
		FScopeLock Lock(&PendingRequestsCritical);
		return PendingRequests.Contains(RequestID);
	}

	/**
	 * [ASYNC/GAME THREAD] Adds a request ID to the list of pending requests
	 */
	void AddPendingRequest(int32 RequestID)
	{
		FScopeLock Lock(&PendingRequestsCritical);
		PendingRequests.Add(RequestID);
	}

	/**
	 * [ASYNC/GAME THREAD] Removes a request ID from the list of pending requests
	 */
	void RemovePendingRequests(TArray<int32, TInlineAllocator<2>>& RequestIDs)
	{
		FScopeLock Lock(&PendingRequestsCritical);
		for (int32 ID : RequestIDs)
		{
			PendingRequests.Remove(ID);
			TRACE_LOADTIME_END_REQUEST(ID);
		}
	}

	void AddPendingCDOs(FAsyncPackage2* Package, TArray<UClass*, TInlineAllocator<8>>& Classes)
	{
		FEventLoadNode2& FirstBundleNode = Package->GetExportBundleNode(ExportBundle_Process, 0);
		FirstBundleNode.AddBarrier(Classes.Num());
		for (UClass* Class : Classes)
		{
			PendingCDOs.FindOrAdd(Class).Add(&FirstBundleNode);
		}
	}

private:

	void SuspendWorkers();
	void ResumeWorkers();

	void LazyInitializeFromLoadPackage();
	void FinalizeInitialLoad();

	void RemoveUnreachableObjects(const FUnreachablePublicExports& PublicExports, const FUnreachablePackages& Packages);

	bool ProcessPendingCDOs()
	{
		if (PendingCDOs.Num() > 0)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ProcessPendingCDOs);

			auto It = PendingCDOs.CreateIterator();
			UClass* Class = It.Key();
			TArray<FEventLoadNode2*> Nodes = MoveTemp(It.Value());
			It.RemoveCurrent();

			UE_LOG(LogStreaming, Verbose, TEXT("ProcessPendingCDOs: Creating CDO for %s. %d entries remaining."), *Class->GetFullName(), PendingCDOs.Num());
			UObject* CDO = Class->GetDefaultObject();

			ensureMsgf(CDO, TEXT("Failed to create CDO for %s"), *Class->GetFullName());
			UE_LOG(LogStreaming, Verbose, TEXT("ProcessPendingCDOs: Created CDO for %s."), *Class->GetFullName());

			for (FEventLoadNode2* Node : Nodes)
			{
				Node->ReleaseBarrier();
			}
			return true;
		}
		return false;
	}

	/**
	* [GAME THREAD] Performs game-thread specific operations on loaded packages (not-thread-safe PostLoad, callbacks)
	*
	* @param bUseTimeLimit True if time limit should be used [time-slicing].
	* @param bUseFullTimeLimit True if full time limit should be used [time-slicing].
	* @param TimeLimit Maximum amount of time that can be spent in this call [time-slicing].
	* @param FlushTree Package dependency tree to be flushed
	* @return The current state of async loading
	*/
	EAsyncPackageState::Type ProcessLoadedPackagesFromGameThread(FAsyncLoadingThreadState2& ThreadState, bool& bDidSomething, int32 FlushRequestID = INDEX_NONE);

	bool CreateAsyncPackagesFromQueue(FAsyncLoadingThreadState2& ThreadState);
	void AddBundleIoRequest(FAsyncPackage2* Package);
	void BundleIoRequestCompleted(FAsyncPackage2* Package);
	void StartBundleIoRequests();

	FAsyncPackage2* CreateAsyncPackage(const FAsyncPackageDesc2& Desc)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CreateAsyncPackage);
		UE_ASYNC_PACKAGE_DEBUG(Desc);
		checkf(Desc.StoreEntry, TEXT("No package store entry for package %s"), *Desc.DiskPackageName.ToString());

		FAsyncPackageData Data;
		Data.ExportCount = Desc.StoreEntry->ExportCount;
		Data.ExportBundleCount = Desc.StoreEntry->ExportBundleCount;

		const int32 ExportBundleNodeCount = Data.ExportBundleCount * EEventLoadNode2::ExportBundle_NumPhases;
		const int32 ImportedPackageCount = Desc.StoreEntry->ImportedPackages.Num();
		const int32 NodeCount = EEventLoadNode2::Package_NumPhases + ExportBundleNodeCount;

		const uint64 ExportBundleHeadersSize = sizeof(FExportBundleHeader) * Data.ExportBundleCount;
		const uint64 ExportBundleEntriesSize = sizeof(FExportBundleEntry) * Data.ExportCount * FExportBundleEntry::ExportCommandType_Count;
		Data.ExportBundlesMetaSize = ExportBundleHeadersSize + ExportBundleEntriesSize;

		const uint64 AsyncPackageMemSize = Align(sizeof(FAsyncPackage2), 8);
		const uint64 ExportBundlesMetaMemSize = Align(Data.ExportBundlesMetaSize, 8);
		const uint64 ExportsMemSize = Align(sizeof(FExportObject) * Data.ExportCount, 8);
		const uint64 ImportedPackagesMemSize = Align(sizeof(FAsyncPackage2*) * ImportedPackageCount, 8);
		const uint64 PackageNodesMemSize = Align(sizeof(FEventLoadNode2) * NodeCount, 8);
		const uint64 MemoryBufferSize =
			AsyncPackageMemSize +
			ExportBundlesMetaMemSize +
			ExportsMemSize +
			ImportedPackagesMemSize +
			PackageNodesMemSize;

		uint8* MemoryBuffer = reinterpret_cast<uint8*>(FMemory::Malloc(MemoryBufferSize));

		Data.ExportBundlesMetaMemory = MemoryBuffer + AsyncPackageMemSize;
		Data.ExportBundleHeaders = reinterpret_cast<const FExportBundleHeader*>(Data.ExportBundlesMetaMemory);
		Data.ExportBundleEntries = reinterpret_cast<const FExportBundleEntry*>(Data.ExportBundleHeaders + Data.ExportBundleCount);

		Data.Exports = MakeArrayView(reinterpret_cast<FExportObject*>(MemoryBuffer + AsyncPackageMemSize + ExportBundlesMetaMemSize), Data.ExportCount);
		Data.ImportedAsyncPackages = MakeArrayView(reinterpret_cast<FAsyncPackage2**>(MemoryBuffer + AsyncPackageMemSize + ExportBundlesMetaMemSize + ExportsMemSize), 0);
		Data.PackageNodes = MakeArrayView(reinterpret_cast<FEventLoadNode2*>(MemoryBuffer + AsyncPackageMemSize + ExportBundlesMetaMemSize + ExportsMemSize + ImportedPackagesMemSize), NodeCount);
		Data.ExportBundleNodes = MakeArrayView(&Data.PackageNodes[EEventLoadNode2::Package_NumPhases], ExportBundleNodeCount);

		ExistingAsyncPackagesCounter.Increment();
		return new (MemoryBuffer) FAsyncPackage2(Desc, Data, *this, GraphAllocator, EventSpecs.GetData());
	}

	void DeleteAsyncPackage(FAsyncPackage2* Package)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DeleteAsyncPackage);
		UE_ASYNC_PACKAGE_DEBUG(Package->Desc);
		Package->~FAsyncPackage2();
		FMemory::Free(Package);
		ExistingAsyncPackagesCounter.Decrement();
	}

	/** Number of times we re-entered the async loading tick, mostly used by singlethreaded ticking. Debug purposes only. */
	int32 AsyncLoadingTickCounter;
};

/**
 * Updates FUObjectThreadContext with the current package when processing it.
 * FUObjectThreadContext::AsyncPackage is used by NotifyConstructedDuringAsyncLoading.
 */
struct FAsyncPackageScope2
{
	/** Outer scope package */
	void* PreviousPackage;
#if WITH_IOSTORE_IN_EDITOR
	IAsyncPackageLoader* PreviousAsyncPackageLoader;
#endif
	/** Cached ThreadContext so we don't have to access it again */
	FUObjectThreadContext& ThreadContext;

	FAsyncPackageScope2(FAsyncPackage2* InPackage)
		: ThreadContext(FUObjectThreadContext::Get())
	{
		PreviousPackage = ThreadContext.AsyncPackage;
		ThreadContext.AsyncPackage = InPackage;
#if WITH_IOSTORE_IN_EDITOR
		PreviousAsyncPackageLoader = ThreadContext.AsyncPackageLoader;
		ThreadContext.AsyncPackageLoader = &InPackage->AsyncLoadingThread;
#endif
	}
	~FAsyncPackageScope2()
	{
		ThreadContext.AsyncPackage = PreviousPackage;
#if WITH_IOSTORE_IN_EDITOR
		ThreadContext.AsyncPackageLoader = PreviousAsyncPackageLoader;
#endif
	}
};

/** Just like TGuardValue for FAsyncLoadingThread::AsyncLoadingTickCounter but only works for the game thread */
struct FAsyncLoadingTickScope2
{
	FAsyncLoadingThread2& AsyncLoadingThread;
	bool bNeedsToLeaveAsyncTick;

	FAsyncLoadingTickScope2(FAsyncLoadingThread2& InAsyncLoadingThread)
		: AsyncLoadingThread(InAsyncLoadingThread)
		, bNeedsToLeaveAsyncTick(false)
	{
		if (IsInGameThread())
		{
			AsyncLoadingThread.EnterAsyncLoadingTick();
			bNeedsToLeaveAsyncTick = true;
		}
	}
	~FAsyncLoadingTickScope2()
	{
		if (bNeedsToLeaveAsyncTick)
		{
			AsyncLoadingThread.LeaveAsyncLoadingTick();
		}
	}
};

void FAsyncLoadingThread2::InitializeLoading()
{
#if !UE_BUILD_SHIPPING
	{
		FString DebugPackageNamesString;
		FParse::Value(FCommandLine::Get(), TEXT("-s.DebugPackageNames="), DebugPackageNamesString);
		ParsePackageNames(DebugPackageNamesString, GAsyncLoading2_DebugPackageIds);
		FString VerbosePackageNamesString;
		FParse::Value(FCommandLine::Get(), TEXT("-s.VerbosePackageNames="), VerbosePackageNamesString);
		ParsePackageNames(VerbosePackageNamesString, GAsyncLoading2_VerbosePackageIds);
		ParsePackageNames(DebugPackageNamesString, GAsyncLoading2_VerbosePackageIds);
		GAsyncLoading2_VerboseLogFilter = GAsyncLoading2_VerbosePackageIds.Num() > 0 ? 1 : 2;
	}

	FileOpenLogWrapper = (FPlatformFileOpenLog*)(FPlatformFileManager::Get().FindPlatformFile(FPlatformFileOpenLog::GetTypeName()));
#endif

#if USE_NEW_BULKDATA || WITH_IOSTORE_IN_EDITOR
	FBulkDataBase::SetIoDispatcher(&IoDispatcher);
#endif

	FPackageName::DoesPackageExistOverride().BindLambda([this](FName PackageName)
	{
		LazyInitializeFromLoadPackage();
		return GlobalPackageStore.DoesPackageExist(PackageName);
	});

	AsyncThreadReady.Increment();

	UE_LOG(LogStreaming, Display, TEXT("AsyncLoading2 - Initialized"));
}

void FAsyncLoadingThread2::QueuePackage(FAsyncPackageDesc2& Package)
{
	//TRACE_CPUPROFILER_EVENT_SCOPE(QueuePackage);
	UE_ASYNC_PACKAGE_DEBUG(Package);
	checkf(Package.StoreEntry, TEXT("No package store entry for package %s"), *Package.DiskPackageName.ToString());
	{
		FScopeLock QueueLock(&QueueCritical);
		++QueuedPackagesCounter;
		QueuedPackages.Add(new FAsyncPackageDesc2(Package, MoveTemp(Package.PackageLoadedDelegate)));
	}
	AltZenaphore.NotifyOne();
}

void FAsyncLoadingThread2::UpdatePackagePriority(FAsyncPackage2* Package, int32 NewPriority)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UpdatePackagePriority);
	Package->Desc.Priority = NewPriority;
	Package->IoRequest.UpdatePriority(NewPriority);
}

FAsyncPackage2* FAsyncLoadingThread2::FindOrInsertPackage(FAsyncPackageDesc2* Desc, bool& bInserted)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FindOrInsertPackage);
	FAsyncPackage2* Package = nullptr;
	bInserted = false;
	{
		FScopeLock LockAsyncPackages(&AsyncPackagesCritical);
		Package = AsyncPackageLookup.FindRef(Desc->GetAsyncPackageId());
		if (!Package)
		{
			Package = CreateAsyncPackage(*Desc);
			checkf(Package, TEXT("Failed to create async package %s"), *Desc->DiskPackageName.ToString());
			Package->AddRef();
			ActiveAsyncPackagesCounter.Increment();
			AsyncPackageLookup.Add(Desc->GetAsyncPackageId(), Package);
			bInserted = true;
		}
		else
		{
			if (Desc->RequestID > 0)
			{
				Package->AddRequestID(Desc->RequestID);
			}
			if (Desc->Priority > Package->Desc.Priority)
			{
				UpdatePackagePriority(Package, Desc->Priority);
			}
		}
		if (Desc->PackageLoadedDelegate.IsValid())
		{
			Package->AddCompletionCallback(MoveTemp(Desc->PackageLoadedDelegate));
		}
	}
	return Package;
}

bool FAsyncLoadingThread2::CreateAsyncPackagesFromQueue(FAsyncLoadingThreadState2& ThreadState)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(CreateAsyncPackagesFromQueue);
	
	bool bPackagesCreated = false;
	const int32 TimeSliceGranularity = ThreadState.UseTimeLimit() ? 4 : MAX_int32;
	TArray<FAsyncPackageDesc2*> QueueCopy;

	do
	{
		{
			QueueCopy.Reset();
			FScopeLock QueueLock(&QueueCritical);

			const int32 NumPackagesToCopy = FMath::Min(TimeSliceGranularity, QueuedPackages.Num());
			if (NumPackagesToCopy > 0)
			{
				QueueCopy.Append(QueuedPackages.GetData(), NumPackagesToCopy);
				QueuedPackages.RemoveAt(0, NumPackagesToCopy, false);
			}
			else
			{
				break;
			}
		}

		for (FAsyncPackageDesc2* PackageDesc : QueueCopy)
		{
			bool bInserted;
			FAsyncPackage2* Package = FindOrInsertPackage(PackageDesc, bInserted);
			checkf(Package, TEXT("Failed to find or insert imported package %s"), *PackageDesc->DiskPackageName.ToString());

			if (bInserted)
			{
				UE_ASYNC_PACKAGE_LOG(Verbose, *PackageDesc, TEXT("CreateAsyncPackages: AddPackage"),
					TEXT("Start loading package."));
			}
			else
			{
				UE_ASYNC_PACKAGE_LOG_VERBOSE(Verbose, *PackageDesc, TEXT("CreateAsyncPackages: UpdatePackage"),
					TEXT("Package is alreay being loaded."));
			}

			--QueuedPackagesCounter;
			if (Package)
			{
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(ImportPackages);
					Package->ImportPackagesRecursive();
				}

				if (bInserted)
				{
					Package->StartLoading();
				}

				StartBundleIoRequests();
			}
			delete PackageDesc;
		}

		bPackagesCreated |= QueueCopy.Num() > 0;
	} while (!ThreadState.IsTimeLimitExceeded(TEXT("CreateAsyncPackagesFromQueue")));

	return bPackagesCreated;
}

void FAsyncLoadingThread2::AddBundleIoRequest(FAsyncPackage2* Package)
{
	WaitingForIoBundleCounter.Increment();
	WaitingIoRequests.HeapPush({ Package });
}

void FAsyncLoadingThread2::BundleIoRequestCompleted(FAsyncPackage2* Package)
{
	check(PendingBundleIoRequestsTotalSize >= Package->ExportBundlesSize)
	PendingBundleIoRequestsTotalSize -= Package->ExportBundlesSize;
	if (WaitingIoRequests.Num())
	{
		StartBundleIoRequests();
	}
}

void FAsyncLoadingThread2::StartBundleIoRequests()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(StartBundleIoRequests);
	constexpr uint64 MaxPendingRequestsSize = 256 << 20;
	FIoBatch IoBatch = IoDispatcher.NewBatch();
	while (WaitingIoRequests.Num())
	{
		FBundleIoRequest& BundleIoRequest = WaitingIoRequests.HeapTop();
		FAsyncPackage2* Package = BundleIoRequest.Package;
		if (PendingBundleIoRequestsTotalSize > 0 && PendingBundleIoRequestsTotalSize + Package->ExportBundlesSize > MaxPendingRequestsSize)
		{
			break;
		}
		PendingBundleIoRequestsTotalSize += Package->ExportBundlesSize;
		WaitingIoRequests.HeapPop(BundleIoRequest, false);

		FIoReadOptions ReadOptions;
		Package->IoRequest = IoBatch.ReadWithCallback(CreateIoChunkId(Package->Desc.DiskPackageId.Value(), 0, EIoChunkType::ExportBundleData),
			ReadOptions,
			Package->Desc.Priority,
			[Package](TIoStatusOr<FIoBuffer> Result)
		{
			if (Result.IsOk())
			{
				Package->IoBuffer = Result.ConsumeValueOrDie();
			}
			else
			{
				UE_ASYNC_PACKAGE_LOG(Warning, Package->Desc, TEXT("StartBundleIoRequests: FailedRead"),
					TEXT("Failed reading chunk for package: %s"), *Result.Status().ToString());
				Package->bLoadHasFailed = true;
			}
			Package->AsyncLoadingThread.WaitingForIoBundleCounter.Decrement();
			Package->GetPackageNode(EEventLoadNode2::Package_ProcessSummary).ReleaseBarrier();
		});
		TRACE_COUNTER_DECREMENT(PendingBundleIoRequests);
	}
	IoBatch.Issue();
}

FEventLoadNode2::FEventLoadNode2(const FAsyncLoadEventSpec* InSpec, FAsyncPackage2* InPackage, int32 InImportOrExportIndex, int32 InBarrierCount)
	: BarrierCount(InBarrierCount)
	, Spec(InSpec)
	, Package(InPackage)
	, ImportOrExportIndex(InImportOrExportIndex)
{
	check(Spec);
	check(Package);
}

void FEventLoadNode2::DependsOn(FEventLoadNode2* Other)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(DependsOn);
#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	check(!IsDone());
	check(!bFired);
#endif
	uint8 Expected = 0;
	while (!Other->DependencyWriterCount.CompareExchange(Expected, 1))
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(DependsOnContested);
		check(Expected == 1);
		Expected = 0;
	}
	if (!Other->IsDone())
	{
		++BarrierCount;
		if (Other->DependenciesCount == 0)
		{
			Other->SingleDependent = this;
			Other->DependenciesCount = 1;
		}
		else
		{
			if (Other->DependenciesCount == 1)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(DependsOnAlloc);
				FEventLoadNode2* FirstDependency = Other->SingleDependent;
				uint32 NewDependenciesCapacity = 4;
				Other->DependenciesCapacity = NewDependenciesCapacity;
				Other->MultipleDependents = Package->GetGraphAllocator().AllocArcs(NewDependenciesCapacity);
				Other->MultipleDependents[0] = FirstDependency;
			}
			else if (Other->DependenciesCount == Other->DependenciesCapacity)
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(DependsOnRealloc);
				FEventLoadNode2** OriginalDependents = Other->MultipleDependents;
				uint32 OldDependenciesCapcity = Other->DependenciesCapacity;
				SIZE_T OldDependenciesSize = OldDependenciesCapcity * sizeof(FEventLoadNode2*);
				uint32 NewDependenciesCapacity = OldDependenciesCapcity * 2;
				Other->DependenciesCapacity = NewDependenciesCapacity;
				Other->MultipleDependents = Package->GetGraphAllocator().AllocArcs(NewDependenciesCapacity);
				FMemory::Memcpy(Other->MultipleDependents, OriginalDependents, OldDependenciesSize);
				Package->GetGraphAllocator().FreeArcs(OriginalDependents, OldDependenciesCapcity);
			}
			Other->MultipleDependents[Other->DependenciesCount++] = this;
		}
	}
	Other->DependencyWriterCount.Store(0);
}

void FEventLoadNode2::AddBarrier()
{
#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	check(!IsDone());
	check(!bFired);
#endif
	++BarrierCount;
}

void FEventLoadNode2::AddBarrier(int32 Count)
{
#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	check(!IsDone());
	check(!bFired);
#endif
	BarrierCount += Count;
}

void FEventLoadNode2::ReleaseBarrier(FAsyncLoadingThreadState2* ThreadState)
{
	check(BarrierCount > 0);
	if (--BarrierCount == 0)
	{
		Fire(ThreadState);
	}
}

void FEventLoadNode2::Fire(FAsyncLoadingThreadState2* ThreadState)
{
	//TRACE_CPUPROFILER_EVENT_SCOPE(Fire);

#if UE_BUILD_DEBUG || UE_BUILD_DEVELOPMENT
	bFired.Store(1);
#endif

	if (Spec->bExecuteImmediately && ThreadState && !ThreadState->CurrentEventNode)
	{
		Execute(*ThreadState);
	}
	else
	{
		Spec->EventQueue->Push(this);
	}
}

void FEventLoadNode2::Execute(FAsyncLoadingThreadState2& ThreadState)
{
	//TRACE_CPUPROFILER_EVENT_SCOPE(ExecuteEvent);
	check(BarrierCount.Load() == 0);
	check(WITH_IOSTORE_IN_EDITOR || !ThreadState.CurrentEventNode || ThreadState.CurrentEventNode == this);

#if WITH_IOSTORE_IN_EDITOR
	// Allow recursive execution of event nodes in editor builds
	FEventLoadNode2* PrevNode = ThreadState.CurrentEventNode != this ? ThreadState.CurrentEventNode : nullptr;
	SetState(ENodeState::Executing);
#endif
	ThreadState.CurrentEventNode = this;
	EAsyncPackageState::Type State = Spec->Func(ThreadState, Package, ImportOrExportIndex);
	if (State == EAsyncPackageState::Complete)
	{
		SetState(ENodeState::Completed);
		ThreadState.CurrentEventNode = nullptr;
		ProcessDependencies(ThreadState);
#if WITH_IOSTORE_IN_EDITOR
		ThreadState.CurrentEventNode = PrevNode;
#endif
	}
#if WITH_IOSTORE_IN_EDITOR
	else
	{
		check(PrevNode == nullptr);
		SetState(ENodeState::Timeout);
	}
#endif
}

void FEventLoadNode2::ProcessDependencies(FAsyncLoadingThreadState2& ThreadState)
{
	//TRACE_CPUPROFILER_EVENT_SCOPE(ProcessDependencies);
	if (DependencyWriterCount.Load() != 0)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ConcurrentWriter);
		do
		{
			FPlatformProcess::Sleep(0);
		} while (DependencyWriterCount.Load() != 0);
	}

	if (DependenciesCount == 1)
	{
		check(SingleDependent->BarrierCount > 0);
		if (--SingleDependent->BarrierCount == 0)
		{
			ThreadState.NodesToFire.Push(SingleDependent);
		}
	}
	else if (DependenciesCount != 0)
	{
		FEventLoadNode2** Current = MultipleDependents;
		FEventLoadNode2** End = MultipleDependents + DependenciesCount;
		for (; Current < End; ++Current)
		{
			FEventLoadNode2* Dependent = *Current;
			check(Dependent->BarrierCount > 0);
			if (--Dependent->BarrierCount == 0)
			{
				ThreadState.NodesToFire.Push(Dependent);
			}
		}
		ThreadState.DeferredFreeArcs.Add(MakeTuple(MultipleDependents, DependenciesCapacity));
	}
	if (ThreadState.bShouldFireNodes)
	{
		ThreadState.bShouldFireNodes = false;
		while (ThreadState.NodesToFire.Num())
		{
			ThreadState.NodesToFire.Pop(false)->Fire(&ThreadState);
		}
		ThreadState.bShouldFireNodes = true;
	}
}

FAsyncLoadEventQueue2::FAsyncLoadEventQueue2()
{
	FMemory::Memzero(Entries, sizeof(Entries));
}

FAsyncLoadEventQueue2::~FAsyncLoadEventQueue2()
{
}

void FAsyncLoadEventQueue2::Push(FEventLoadNode2* Node)
{
	uint64 LocalHead = Head.IncrementExchange();
	FEventLoadNode2* Expected = nullptr;
	if (!Entries[LocalHead % UE_ARRAY_COUNT(Entries)].CompareExchange(Expected, Node))
	{
		*(volatile int*)0 = 0; // queue is full: TODO
	}
	if (Zenaphore)
	{
		Zenaphore->NotifyOne();
	}
}

bool FAsyncLoadEventQueue2::PopAndExecute(FAsyncLoadingThreadState2& ThreadState)
{
	if (ThreadState.CurrentEventNode
#if WITH_IOSTORE_IN_EDITOR
		&& !ThreadState.CurrentEventNode->IsExecuting()
#endif
		)
	{
		check(!ThreadState.CurrentEventNode->IsDone());
		ThreadState.CurrentEventNode->Execute(ThreadState);
		return true;
	}

	FEventLoadNode2* Node = nullptr;
	{
		uint64 LocalHead = Head.Load();
		uint64 LocalTail = Tail.Load();
		for (;;)
		{
			if (LocalTail >= LocalHead)
			{
				break;
			}
			if (Tail.CompareExchange(LocalTail, LocalTail + 1))
			{
				while (!Node)
				{
					Node = Entries[LocalTail % UE_ARRAY_COUNT(Entries)].Exchange(nullptr);
				}
				break;
			}
		}
	}

	if (Node)
	{
		//TRACE_CPUPROFILER_EVENT_SCOPE(Execute);
		Node->Execute(ThreadState);
		return true;
	}
	else
	{
		return false;
	}
}

FScopedAsyncPackageEvent2::FScopedAsyncPackageEvent2(FAsyncPackage2* InPackage)
	:Package(InPackage)
{
	check(Package);

	// Update the thread context with the current package. This is used by NotifyConstructedDuringAsyncLoading.
	FUObjectThreadContext& ThreadContext = FUObjectThreadContext::Get();
	PreviousPackage = static_cast<FAsyncPackage2*>(ThreadContext.AsyncPackage);
	ThreadContext.AsyncPackage = Package;
#if WITH_IOSTORE_IN_EDITOR
	PreviousAsyncPackageLoader = ThreadContext.AsyncPackageLoader;
	ThreadContext.AsyncPackageLoader = &InPackage->AsyncLoadingThread;
#endif
	Package->BeginAsyncLoad();
}

FScopedAsyncPackageEvent2::~FScopedAsyncPackageEvent2()
{
	Package->EndAsyncLoad();

	// Restore the package from the outer scope
	FUObjectThreadContext& ThreadContext = FUObjectThreadContext::Get();
	ThreadContext.AsyncPackage = PreviousPackage;
#if WITH_IOSTORE_IN_EDITOR
	ThreadContext.AsyncPackageLoader = PreviousAsyncPackageLoader;
#endif
}

void FAsyncLoadingThreadWorker::StartThread()
{
	LLM_SCOPE(ELLMTag::AsyncLoading);
	Trace::ThreadGroupBegin(TEXT("AsyncLoading"));
	Thread = FRunnableThread::Create(this, TEXT("FAsyncLoadingThreadWorker"), 0, TPri_Normal);
	ThreadId = Thread->GetThreadID();
	Trace::ThreadGroupEnd();
}

uint32 FAsyncLoadingThreadWorker::Run()
{
	LLM_SCOPE(ELLMTag::AsyncLoading);

	FPlatformProcess::SetThreadAffinityMask(FPlatformAffinity::GetAsyncLoadingThreadMask());
	FMemory::SetupTLSCachesOnCurrentThread();

	FAsyncLoadingThreadState2::Create(GraphAllocator, IoDispatcher);

	FZenaphoreWaiter Waiter(Zenaphore, TEXT("WaitForEvents"));

	FAsyncLoadingThreadState2& ThreadState = *FAsyncLoadingThreadState2::Get();

	bool bSuspended = false;
	while (!bStopRequested)
	{
		if (bSuspended)
		{
			if (!bSuspendRequested.Load(EMemoryOrder::SequentiallyConsistent))
			{
				bSuspended = false;
			}
			else
			{
				FPlatformProcess::Sleep(0.001f);
			}
		}
		else
		{
			bool bDidSomething = false;
			{
				FGCScopeGuard GCGuard;
				TRACE_CPUPROFILER_EVENT_SCOPE(AsyncLoadingTime);
				++ActiveWorkersCount;
				do
				{
					bDidSomething = EventQueue.PopAndExecute(ThreadState);
					
					if (bSuspendRequested.Load(EMemoryOrder::Relaxed))
					{
						bSuspended = true;
						bDidSomething = true;
						break;
					}
				} while (bDidSomething);
				--ActiveWorkersCount;
			}
			if (!bDidSomething)
			{
				ThreadState.ProcessDeferredFrees();
				Waiter.Wait();
			}
		}
	}
	return 0;
}

FUObjectSerializeContext* FAsyncPackage2::GetSerializeContext()
{
	return FUObjectThreadContext::Get().GetSerializeContext();
}

void FAsyncPackage2::SetupSerializedArcs(const uint8* GraphData, uint64 GraphDataSize)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SetupSerializedArcs);

	FSimpleArchive GraphArchive(GraphData, GraphDataSize);
	int32 ImportedPackagesCount;
	GraphArchive << ImportedPackagesCount;
	for (int32 ImportedPackageIndex = 0; ImportedPackageIndex < ImportedPackagesCount; ++ImportedPackageIndex)
	{
		FPackageId ImportedPackageId;
		int32 ExternalArcCount;
		GraphArchive << ImportedPackageId;
		GraphArchive << ExternalArcCount;

		FAsyncPackage2* ImportedPackage = AsyncLoadingThread.GetAsyncPackage(ImportedPackageId);
		for (int32 ExternalArcIndex = 0; ExternalArcIndex < ExternalArcCount; ++ExternalArcIndex)
		{
			int32 FromExportBundleIndex;
			int32 ToExportBundleIndex;
			GraphArchive << FromExportBundleIndex;
			GraphArchive << ToExportBundleIndex;
			if (ImportedPackage)
			{
				FromExportBundleIndex = FromExportBundleIndex == MAX_uint32
					? ImportedPackage->Data.ExportBundleCount - 1
					: FromExportBundleIndex;

				check(FromExportBundleIndex < ImportedPackage->Data.ExportBundleCount);
				check(ToExportBundleIndex < Data.ExportBundleCount);
				uint32 FromNodeIndexBase = FromExportBundleIndex * EEventLoadNode2::ExportBundle_NumPhases;
				uint32 ToNodeIndexBase = ToExportBundleIndex * EEventLoadNode2::ExportBundle_NumPhases;
				for (int32 Phase = 0; Phase < EEventLoadNode2::ExportBundle_NumPhases; ++Phase)
				{
					uint32 ToNodeIndex = ToNodeIndexBase + Phase;
					uint32 FromNodeIndex = FromNodeIndexBase + Phase;
					Data.ExportBundleNodes[ToNodeIndex].DependsOn(&ImportedPackage->Data.ExportBundleNodes[FromNodeIndex]);
				}
			}
		}
	}
}

void FAsyncPackage2::SetupScriptDependencies()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(SetupScriptDependencies);

	// UObjectLoadAllCompiledInDefaultProperties is creating CDOs from a flat list.
	// During initial laod, if a CDO called LoadObject for this package it may depend on other CDOs later in the list.
	// Then collect them here, and wait for them to be created before allowing this package to proceed.
	TArray<UClass*, TInlineAllocator<8>> UnresolvedCDOs;
	if (ImportStore.GetUnresolvedCDOs(UnresolvedCDOs))
	{
		AsyncLoadingThread.AddPendingCDOs(this, UnresolvedCDOs);
	}
}

static UObject* GFindExistingScriptImport(FPackageObjectIndex GlobalImportIndex,
	TMap<FPackageObjectIndex, UObject*>& ScriptObjects,
	const TMap<FPackageObjectIndex, FScriptObjectEntry*>& ScriptObjectEntriesMap)
{
	UObject** Object = &ScriptObjects.FindOrAdd(GlobalImportIndex);
	if (!*Object)
	{
		const FScriptObjectEntry* Entry = ScriptObjectEntriesMap.FindRef(GlobalImportIndex);
		check(Entry);
		if (Entry->OuterIndex.IsNull())
		{
			*Object = StaticFindObjectFast(UPackage::StaticClass(), nullptr, MinimalNameToName(Entry->ObjectName), true);
		}
		else
		{
			UObject* Outer = GFindExistingScriptImport(Entry->OuterIndex, ScriptObjects, ScriptObjectEntriesMap);
			Object = &ScriptObjects.FindChecked(GlobalImportIndex);
			if (Outer)
			{
				*Object = StaticFindObjectFast(UObject::StaticClass(), Outer, MinimalNameToName(Entry->ObjectName), false, true);
			}
		}
	}
	return *Object;
}

UObject* FGlobalImportStore::FindScriptImportObjectFromIndex(FPackageObjectIndex GlobalImportIndex)
{
	check(ScriptObjectEntries.Num() > 0);
	return GFindExistingScriptImport(GlobalImportIndex, ScriptObjects, ScriptObjectEntriesMap);
}

void FGlobalImportStore::FindAllScriptObjects()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FindAllScriptObjects);
	TStringBuilder<FName::StringBufferSize> Name;
	TArray<UPackage*> ScriptPackages;
	TArray<UObject*> Objects;
	FindAllRuntimeScriptPackages(ScriptPackages);

	for (UPackage* Package : ScriptPackages)
	{
		Objects.Reset();
		GetObjectsWithOuter(Package, Objects, /*bIncludeNestedObjects*/true);
		for (UObject* Object : Objects)
		{
			if (Object->HasAnyFlags(RF_Public))
			{
				Name.Reset();
				Object->GetPathName(nullptr, Name);
				FPackageObjectIndex GlobalImportIndex = FPackageObjectIndex::FromScriptPath(Name);
				ScriptObjects.Add(GlobalImportIndex, Object);
			}
		}
	}

	ScriptObjectEntriesMap.Empty();
	ScriptObjectEntries.Empty();
	ScriptObjects.Shrink();
	bHasInitializedScriptObjects = true;
}

void FAsyncPackage2::ImportPackagesRecursive()
{
	if (AsyncPackageLoadingState >= EAsyncPackageLoadingState2::ImportPackages)
	{
		return;
	}
	check(AsyncPackageLoadingState == EAsyncPackageLoadingState2::NewPackage);

	const int32 ImportedPackageCount = Desc.StoreEntry->ImportedPackages.Num();
	if (!ImportedPackageCount)
	{
		AsyncPackageLoadingState = EAsyncPackageLoadingState2::ImportPackagesDone;
		return;
	}
	else
	{
		AsyncPackageLoadingState = EAsyncPackageLoadingState2::ImportPackages;
	}

	int32 ImportedPackageIndex = 0;

	FPackageStore& GlobalPackageStore = AsyncLoadingThread.GlobalPackageStore;
	for (const FPackageId& ImportedPackageId : Desc.StoreEntry->ImportedPackages)
	{
		FLoadedPackageRef& PackageRef = GlobalPackageStore.LoadedPackageStore.GetPackageRef(ImportedPackageId);
		if (PackageRef.AreAllPublicExportsLoaded())
		{
			continue;
		}

		const FPackageStoreEntry* ImportedPackageEntry = GlobalPackageStore.FindStoreEntry(ImportedPackageId);

		if (!ImportedPackageEntry)
		{
			UE_ASYNC_PACKAGE_LOG(Warning, Desc, TEXT("ImportPackages: SkipPackage"),
				TEXT("Skipping non mounted imported package with id '0x%llX'"), ImportedPackageId.Value());
			PackageRef.SetIsMissingPackage();
			continue;
		}

		FAsyncPackageDesc2 PackageDesc(INDEX_NONE, Desc.Priority, ImportedPackageId, ImportedPackageEntry);
		bool bInserted;
		FAsyncPackage2* ImportedPackage = AsyncLoadingThread.FindOrInsertPackage(&PackageDesc, bInserted);

		checkf(ImportedPackage, TEXT("Failed to find or insert imported package with id '0x%llX'"), ImportedPackageId.Value());
		TRACE_LOADTIME_ASYNC_PACKAGE_IMPORT_DEPENDENCY(this, ImportedPackage);

		if (bInserted)
		{
			UE_ASYNC_PACKAGE_LOG(Verbose, PackageDesc, TEXT("ImportPackages: AddPackage"),
				TEXT("Start loading imported package."));
		}
		else
		{
			UE_ASYNC_PACKAGE_LOG_VERBOSE(VeryVerbose, PackageDesc, TEXT("ImportPackages: UpdatePackage"),
				TEXT("Imported package is already being loaded."));
		}
		ImportedPackage->AddRef();
		check(ImportedPackageIndex == Data.ImportedAsyncPackages.Num());
		Data.ImportedAsyncPackages = MakeArrayView(Data.ImportedAsyncPackages.GetData(), ImportedPackageIndex + 1);
		Data.ImportedAsyncPackages[ImportedPackageIndex++] = ImportedPackage;
		if (bInserted)
		{
			ImportedPackage->ImportPackagesRecursive();
			ImportedPackage->StartLoading();
		}
	}
	UE_ASYNC_PACKAGE_LOG_VERBOSE(VeryVerbose, Desc, TEXT("ImportPackages: ImportsDone"),
		TEXT("All imported packages are now being loaded."));

	check(AsyncPackageLoadingState == EAsyncPackageLoadingState2::ImportPackages);
	AsyncPackageLoadingState = EAsyncPackageLoadingState2::ImportPackagesDone;
}

void FAsyncPackage2::StartLoading()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(StartLoading);
	TRACE_LOADTIME_BEGIN_LOAD_ASYNC_PACKAGE(this);
	check(AsyncPackageLoadingState == EAsyncPackageLoadingState2::ImportPackagesDone);

	LoadStartTime = FPlatformTime::Seconds();

	AsyncLoadingThread.AddBundleIoRequest(this);
	AsyncPackageLoadingState = EAsyncPackageLoadingState2::WaitingForIo;
}

EAsyncPackageState::Type FAsyncPackage2::Event_ProcessPackageSummary(FAsyncLoadingThreadState2& ThreadState, FAsyncPackage2* Package, int32)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Event_ProcessPackageSummary);
	UE_ASYNC_PACKAGE_DEBUG(Package->Desc);
	check(Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::WaitingForIo);
	Package->AsyncPackageLoadingState = EAsyncPackageLoadingState2::ProcessPackageSummary;

	FScopedAsyncPackageEvent2 Scope(Package);

	if (Package->bLoadHasFailed)
	{
		if (Package->Desc.CanBeImported())
		{
			FLoadedPackageRef* PackageRef = Package->ImportStore.GlobalPackageStore.LoadedPackageStore.FindPackageRef(Package->Desc.DiskPackageId);
			check(PackageRef);
			PackageRef->SetHasFailed();
		}
	}
	else
	{
		check(Package->ExportBundleEntryIndex == 0);

		const uint8* PackageSummaryData = Package->IoBuffer.Data();
		const FPackageSummary* PackageSummary = reinterpret_cast<const FPackageSummary*>(PackageSummaryData);
		const uint8* GraphData = PackageSummaryData + PackageSummary->GraphDataOffset;
		const uint64 PackageSummarySize = GraphData + PackageSummary->GraphDataSize - PackageSummaryData;

		if (PackageSummary->NameMapNamesSize)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(LoadPackageNameMap);
			const uint8* NameMapNamesData = PackageSummaryData + PackageSummary->NameMapNamesOffset;
			const uint8* NameMapHashesData = PackageSummaryData + PackageSummary->NameMapHashesOffset;
			Package->NameMap.Load(
				TArrayView<const uint8>(NameMapNamesData, PackageSummary->NameMapNamesSize),
				TArrayView<const uint8>(NameMapHashesData, PackageSummary->NameMapHashesSize),
				FMappedName::EType::Package);
		}

		{
			FName PackageName = Package->NameMap.GetName(PackageSummary->Name);
			// Don't apply any redirects in editor builds
#if !WITH_IOSTORE_IN_EDITOR
			if (PackageSummary->SourceName != PackageSummary->Name)
			{
				FName SourcePackageName = Package->NameMap.GetName(PackageSummary->SourceName);
				Package->Desc.SetDiskPackageName(PackageName, SourcePackageName);
			}
			else
#endif
			{
				Package->Desc.SetDiskPackageName(PackageName);
			}
		}

		Package->CookedHeaderSize = PackageSummary->CookedHeaderSize;
		Package->ImportStore.ImportMap = TArrayView<const FPackageObjectIndex>(
				reinterpret_cast<const FPackageObjectIndex*>(PackageSummaryData + PackageSummary->ImportMapOffset),
				(PackageSummary->ExportMapOffset - PackageSummary->ImportMapOffset) / sizeof(FPackageObjectIndex));
		Package->ExportMap = reinterpret_cast<const FExportMapEntry*>(PackageSummaryData + PackageSummary->ExportMapOffset);
		
		FMemory::Memcpy(Package->Data.ExportBundlesMetaMemory, PackageSummaryData + PackageSummary->ExportBundlesOffset, Package->Data.ExportBundlesMetaSize);

		Package->CreateUPackage(PackageSummary);
		Package->SetupSerializedArcs(GraphData, PackageSummary->GraphDataSize);

		Package->AllExportDataPtr = PackageSummaryData + PackageSummarySize;
		Package->CurrentExportDataPtr = Package->AllExportDataPtr;

		TRACE_LOADTIME_PACKAGE_SUMMARY(Package, PackageSummarySize, Package->ImportStore.ImportMap.Num(), Package->Data.ExportCount);
	}

	if (GIsInitialLoad)
	{
		Package->SetupScriptDependencies();
	}
	Package->GetExportBundleNode(ExportBundle_Process, 0).ReleaseBarrier();

	check(Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::ProcessPackageSummary);
	Package->AsyncPackageLoadingState = EAsyncPackageLoadingState2::ProcessExportBundles;
	return EAsyncPackageState::Complete;
}

EAsyncPackageState::Type FAsyncPackage2::Event_ProcessExportBundle(FAsyncLoadingThreadState2& ThreadState, FAsyncPackage2* Package, int32 ExportBundleIndex)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Event_ProcessExportBundle);
	UE_ASYNC_PACKAGE_DEBUG(Package->Desc);
	check(Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::ProcessExportBundles);

	FScopedAsyncPackageEvent2 Scope(Package);

	auto FilterExport = [](const EExportFilterFlags FilterFlags) -> bool
	{
#if UE_SERVER
		return !!(static_cast<uint32>(FilterFlags) & static_cast<uint32>(EExportFilterFlags::NotForServer));
#elif !WITH_SERVER_CODE
		return !!(static_cast<uint32>(FilterFlags) & static_cast<uint32>(EExportFilterFlags::NotForClient));
#else
		static const bool bIsDedicatedServer = !GIsClient && GIsServer;
		static const bool bIsClientOnly = GIsClient && !GIsServer;

		if (bIsDedicatedServer && static_cast<uint32>(FilterFlags) & static_cast<uint32>(EExportFilterFlags::NotForServer))
		{
			return true;
		}

		if (bIsClientOnly && static_cast<uint32>(FilterFlags) & static_cast<uint32>(EExportFilterFlags::NotForClient))
		{
			return true;
		}

		return false;
#endif
	};

	check(ExportBundleIndex < Package->Data.ExportBundleCount);
	
	if (!Package->bLoadHasFailed)
	{
		const uint64 AllExportDataSize = Package->IoBuffer.DataSize() - (Package->AllExportDataPtr - Package->IoBuffer.Data());
		FExportArchive Ar(Package->AllExportDataPtr, Package->CurrentExportDataPtr, AllExportDataSize);
		{
			Ar.SetUE4Ver(Package->LinkerRoot->LinkerPackageVersion);
			Ar.SetLicenseeUE4Ver(Package->LinkerRoot->LinkerLicenseeVersion);
			// Ar.SetEngineVer(Summary.SavedByEngineVersion); // very old versioning scheme
			// Ar.SetCustomVersions(LinkerRoot->LinkerCustomVersion); // only if not cooking with -unversioned
			Ar.SetUseUnversionedPropertySerialization((Package->LinkerRoot->GetPackageFlags() & PKG_UnversionedProperties) != 0);
			Ar.SetIsLoading(true);
			Ar.SetIsPersistent(true);
			if (Package->LinkerRoot->GetPackageFlags() & PKG_FilterEditorOnly)
			{
				Ar.SetFilterEditorOnly(true);
			}
			Ar.ArAllowLazyLoading = true;

			// FExportArchive special fields
			Ar.CookedHeaderSize = Package->CookedHeaderSize;
			Ar.PackageDesc = &Package->Desc;
			Ar.NameMap = &Package->NameMap;
			Ar.ImportStore = &Package->ImportStore;
			Ar.Exports = Package->Data.Exports;
			Ar.ExportMap = Package->ExportMap;
			Ar.ExternalReadDependencies = &Package->ExternalReadDependencies;
		}
		const FExportBundleHeader* ExportBundle = Package->Data.ExportBundleHeaders + ExportBundleIndex;
		const FExportBundleEntry* BundleEntries = Package->Data.ExportBundleEntries + ExportBundle->FirstEntryIndex;
		const FExportBundleEntry* BundleEntry = BundleEntries + Package->ExportBundleEntryIndex;
		const FExportBundleEntry* BundleEntryEnd = BundleEntries + ExportBundle->EntryCount;
		check(BundleEntry <= BundleEntryEnd);
		while (BundleEntry < BundleEntryEnd)
		{
			if (ThreadState.IsTimeLimitExceeded(TEXT("Event_ProcessExportBundle")))
			{
				return EAsyncPackageState::TimeOut;
			}
			const FExportMapEntry& ExportMapEntry = Package->ExportMap[BundleEntry->LocalExportIndex];
			FExportObject& Export = Package->Data.Exports[BundleEntry->LocalExportIndex];
			Export.bFiltered = FilterExport(ExportMapEntry.FilterFlags);

			if (BundleEntry->CommandType == FExportBundleEntry::ExportCommandType_Create)
			{
				Package->EventDrivenCreateExport(BundleEntry->LocalExportIndex);
			}
			else
			{
				check(BundleEntry->CommandType == FExportBundleEntry::ExportCommandType_Serialize);

				const uint64 CookedSerialSize = ExportMapEntry.CookedSerialSize;
				UObject* Object = Export.Object;

				check(Package->CurrentExportDataPtr + CookedSerialSize <= Package->IoBuffer.Data() + Package->IoBuffer.DataSize());
				check(Object || Export.bFiltered || Export.bExportLoadFailed);

				Ar.ExportBufferBegin(Object, ExportMapEntry.CookedSerialOffset, ExportMapEntry.CookedSerialSize);

				const int64 Pos = Ar.Tell();
				UE_ASYNC_PACKAGE_CLOG(
					CookedSerialSize > uint64(Ar.TotalSize() - Pos), Fatal, Package->Desc, TEXT("ObjectSerializationError"),
					TEXT("%s: Serial size mismatch: Expected read size %d, Remaining archive size: %d"),
					Object ? *Object->GetFullName() : TEXT("null"), CookedSerialSize, uint64(Ar.TotalSize() - Pos));

				const bool bSerialized = Package->EventDrivenSerializeExport(BundleEntry->LocalExportIndex, Ar);
				if (!bSerialized)
				{
					Ar.Skip(CookedSerialSize);
				}
				UE_ASYNC_PACKAGE_CLOG(
					CookedSerialSize != uint64(Ar.Tell() - Pos), Fatal, Package->Desc, TEXT("ObjectSerializationError"),
					TEXT("%s: Serial size mismatch: Expected read size %d, Actual read size %d"),
					Object ? *Object->GetFullName() : TEXT("null"), CookedSerialSize, uint64(Ar.Tell() - Pos));

				Ar.ExportBufferEnd();

				check((Object && !Object->HasAnyFlags(RF_NeedLoad)) || Export.bFiltered || Export.bExportLoadFailed);

				Package->CurrentExportDataPtr += CookedSerialSize;
			}
			++BundleEntry;
			++Package->ExportBundleEntryIndex;
		}
	}
	
	Package->ExportBundleEntryIndex = 0;

	if (ExportBundleIndex + 1 < Package->Data.ExportBundleCount)
	{
		Package->GetExportBundleNode(ExportBundle_Process, ExportBundleIndex + 1).ReleaseBarrier();
	}
	else
	{
		Package->ImportStore.ImportMap = TArrayView<const FPackageObjectIndex>();
		Package->IoBuffer = FIoBuffer();

		if (Package->ExternalReadDependencies.Num() == 0)
		{
			check(Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::ProcessExportBundles);
			Package->AsyncPackageLoadingState = EAsyncPackageLoadingState2::ExportsDone;
			Package->GetPackageNode(Package_ExportsSerialized).ReleaseBarrier(&ThreadState);
		}
		else
		{
			check(Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::ProcessExportBundles);
			Package->AsyncPackageLoadingState = EAsyncPackageLoadingState2::WaitingForExternalReads;
			Package->AsyncLoadingThread.ExternalReadQueue.Enqueue(Package);
		}
	}

	if (ExportBundleIndex == 0)
	{
		Package->AsyncLoadingThread.BundleIoRequestCompleted(Package);
	}

	return EAsyncPackageState::Complete;
}

UObject* FAsyncPackage2::EventDrivenIndexToObject(FPackageObjectIndex Index, bool bCheckSerialized)
{
	UObject* Result = nullptr;
	if (Index.IsNull())
	{
		return Result;
	}
	if (Index.IsExport())
	{
		Result = Data.Exports[Index.ToExport()].Object;
	}
	else if (Index.IsImport())
	{
		Result = ImportStore.FindOrGetImportObject(Index);
		UE_CLOG(!Result, LogStreaming, Warning, TEXT("Missing %s import 0x%llX for package %s"),
			Index.IsScriptImport() ? TEXT("script") : TEXT("package"),
			Index.Value(),
			*Desc.DiskPackageName.ToString());
	}
#if DO_CHECK
	if (bCheckSerialized && !IsFullyLoadedObj(Result))
	{
		/*FEventLoadNode2* MyDependentNode = GetExportNode(EEventLoadNode2::Export_Serialize, Index.ToExport());
		if (!Result)
		{
			UE_LOG(LogStreaming, Error, TEXT("Missing Dependency, request for %s but it hasn't been created yet."), *Linker->GetPathName(Index));
		}
		else if (!MyDependentNode || MyDependentNode->GetBarrierCount() > 0)
		{
			UE_LOG(LogStreaming, Fatal, TEXT("Missing Dependency, request for %s but it was still waiting for serialization."), *Linker->GetPathName(Index));
		}
		else
		{
			UE_LOG(LogStreaming, Fatal, TEXT("Missing Dependency, request for %s but it was still has RF_NeedLoad."), *Linker->GetPathName(Index));
		}*/
		UE_LOG(LogStreaming, Warning, TEXT("Missing Dependency"));
	}
	if (Result)
	{
		UE_CLOG(Result->HasAnyInternalFlags(EInternalObjectFlags::Unreachable), LogStreaming, Fatal, TEXT("Returning an object  (%s) from EventDrivenIndexToObject that is unreachable."), *Result->GetFullName());
	}
#endif
	return Result;
}


void FAsyncPackage2::EventDrivenCreateExport(int32 LocalExportIndex)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(CreateExport);

	const FExportMapEntry& Export = ExportMap[LocalExportIndex];
	FExportObject& ExportObject = Data.Exports[LocalExportIndex];
	UObject*& Object = ExportObject.Object;
	check(!Object);

	TRACE_LOADTIME_CREATE_EXPORT_SCOPE(this, &Object);

	FName ObjectName;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ObjectNameFixup);
		ObjectName = NameMap.GetName(Export.ObjectName);
	}

	if (ExportObject.bFiltered | ExportObject.bExportLoadFailed)
	{
		if (ExportObject.bExportLoadFailed)
		{
			UE_ASYNC_PACKAGE_LOG(Warning, Desc, TEXT("CreateExport"), TEXT("Skipped failed export %s"), *ObjectName.ToString());
		}
		else
		{
			UE_ASYNC_PACKAGE_LOG_VERBOSE(Verbose, Desc, TEXT("CreateExport"), TEXT("Skipped filtered export %s"), *ObjectName.ToString());
		}
		return;
	}

	LLM_SCOPED_TAG_WITH_OBJECT_IN_SET(GetLinkerRoot(), ELLMTagSet::Assets);
	// LLM_SCOPED_TAG_WITH_OBJECT_IN_SET((Export.DynamicType == FObjectExport::EDynamicType::DynamicType) ? UDynamicClass::StaticClass() : 
	// 	CastEventDrivenIndexToObject<UClass>(Export.ClassIndex, false), ELLMTagSet::AssetClasses);

	bool bIsCompleteyLoaded = false;
	UClass* LoadClass = Export.ClassIndex.IsNull() ? UClass::StaticClass() : CastEventDrivenIndexToObject<UClass>(Export.ClassIndex, true);
	UObject* ThisParent = Export.OuterIndex.IsNull() ? LinkerRoot : EventDrivenIndexToObject(Export.OuterIndex, false);

	if (!LoadClass)
	{
		UE_ASYNC_PACKAGE_LOG(Error, Desc, TEXT("CreateExport"), TEXT("Could not find class object for %s"), *ObjectName.ToString());
		ExportObject.bExportLoadFailed = true;
		return;
	}
	if (!ThisParent)
	{
		UE_ASYNC_PACKAGE_LOG(Error, Desc, TEXT("CreateExport"), TEXT("Could not find outer object for %s"), *ObjectName.ToString());
		ExportObject.bExportLoadFailed = true;
		return;
	}
	check(!dynamic_cast<UObjectRedirector*>(ThisParent));
	if (!Export.SuperIndex.IsNull())
	{
		ExportObject.SuperObject = EventDrivenIndexToObject(Export.SuperIndex, false);
		if (!ExportObject.SuperObject)
		{
			UE_ASYNC_PACKAGE_LOG(Error, Desc, TEXT("CreateExport"), TEXT("Could not find SuperStruct object for %s"), *ObjectName.ToString());
			ExportObject.bExportLoadFailed = true;
			return;
		}
	}
	// Find the Archetype object for the one we are loading.
	check(!Export.TemplateIndex.IsNull());
	ExportObject.TemplateObject = EventDrivenIndexToObject(Export.TemplateIndex, true);
	if (!ExportObject.TemplateObject)
	{
		UE_ASYNC_PACKAGE_LOG(Error, Desc, TEXT("CreateExport"), TEXT("Could not find template object for %s"), *ObjectName.ToString());
		ExportObject.bExportLoadFailed = true;
		return;
	}

	// Try to find existing object first as we cannot in-place replace objects, could have been created by other export in this package
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(FindExport);
		Object = StaticFindObjectFastInternal(NULL, ThisParent, ObjectName, true);
	}

	const bool bIsNewObject = !Object;

	// Object is found in memory.
	if (Object)
	{
		// If this object was allocated but never loaded (components created by a constructor, CDOs, etc) make sure it gets loaded
		// Do this for all subobjects created in the native constructor.
		const EObjectFlags ObjectFlags = Object->GetFlags();
		bIsCompleteyLoaded = !!(ObjectFlags & RF_LoadCompleted);
		if (!bIsCompleteyLoaded)
		{
			check(!(ObjectFlags & (RF_NeedLoad | RF_WasLoaded))); // If export exist but is not completed, it is expected to have been created from a native constructor and not from EventDrivenCreateExport, but who knows...?
			if (ObjectFlags & RF_ClassDefaultObject)
			{
				// never call PostLoadSubobjects on class default objects, this matches the behavior of the old linker where
				// StaticAllocateObject prevents setting of RF_NeedPostLoad and RF_NeedPostLoadSubobjects, but FLinkerLoad::Preload
				// assigns RF_NeedPostLoad for blueprint CDOs:
				Object->SetFlags(RF_NeedLoad | RF_NeedPostLoad | RF_WasLoaded);
			}
			else
			{
				Object->SetFlags(RF_NeedLoad | RF_NeedPostLoad | RF_NeedPostLoadSubobjects | RF_WasLoaded);
			}
		}
	}
	else
	{
		// we also need to ensure that the template has set up any instances
		ExportObject.TemplateObject->ConditionalPostLoadSubobjects();

		check(!GVerifyObjectReferencesOnly); // not supported with the event driven loader
		// Create the export object, marking it with the appropriate flags to
		// indicate that the object's data still needs to be loaded.
		EObjectFlags ObjectLoadFlags = Export.ObjectFlags;
		ObjectLoadFlags = EObjectFlags(ObjectLoadFlags | RF_NeedLoad | RF_NeedPostLoad | RF_NeedPostLoadSubobjects | RF_WasLoaded);

		// If we are about to create a CDO, we need to ensure that all parent sub-objects are loaded
		// to get default value initialization to work.
#if DO_CHECK
		if ((ObjectLoadFlags & RF_ClassDefaultObject) != 0)
		{
			UClass* SuperClass = LoadClass->GetSuperClass();
			UObject* SuperCDO = SuperClass ? SuperClass->GetDefaultObject() : nullptr;
			check(!SuperCDO || ExportObject.TemplateObject == SuperCDO); // the template for a CDO is the CDO of the super
			if (SuperClass && !SuperClass->IsNative())
			{
				check(SuperCDO);
				if (SuperClass->HasAnyFlags(RF_NeedLoad))
				{
					UE_LOG(LogStreaming, Fatal, TEXT("Super %s had RF_NeedLoad while creating %s"), *SuperClass->GetFullName(), *ObjectName.ToString());
					return;
				}
				if (SuperCDO->HasAnyFlags(RF_NeedLoad))
				{
					UE_LOG(LogStreaming, Fatal, TEXT("Super CDO %s had RF_NeedLoad while creating %s"), *SuperCDO->GetFullName(), *ObjectName.ToString());
					return;
				}
				TArray<UObject*> SuperSubObjects;
				GetObjectsWithOuter(SuperCDO, SuperSubObjects, /*bIncludeNestedObjects=*/ false, /*ExclusionFlags=*/ RF_NoFlags, /*InternalExclusionFlags=*/ EInternalObjectFlags::Native);

				for (UObject* SubObject : SuperSubObjects)
				{
					if (SubObject->HasAnyFlags(RF_NeedLoad))
					{
						UE_LOG(LogStreaming, Fatal, TEXT("Super CDO subobject %s had RF_NeedLoad while creating %s"), *SubObject->GetFullName(), *ObjectName.ToString());
						return;
					}
				}
			}
			else
			{
				check(ExportObject.TemplateObject->IsA(LoadClass));
			}
		}
#endif
		checkf(!LoadClass->HasAnyFlags(RF_NeedLoad),
			TEXT("LoadClass %s had RF_NeedLoad while creating %s"), *LoadClass->GetFullName(), *ObjectName.ToString());
		checkf(!(LoadClass->GetDefaultObject() && LoadClass->GetDefaultObject()->HasAnyFlags(RF_NeedLoad)), 
			TEXT("Class CDO %s had RF_NeedLoad while creating %s"), *LoadClass->GetDefaultObject()->GetFullName(), *ObjectName.ToString());
		checkf(!ExportObject.TemplateObject->HasAnyFlags(RF_NeedLoad),
			TEXT("Template %s had RF_NeedLoad while creating %s"), *ExportObject.TemplateObject->GetFullName(), *ObjectName.ToString());

		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ConstructObject);
			FStaticConstructObjectParameters Params(LoadClass);
			Params.Outer = ThisParent;
			Params.Name = ObjectName;
			Params.SetFlags = ObjectLoadFlags;
			Params.Template = ExportObject.TemplateObject;
			Params.bAssumeTemplateIsArchetype = true;
			Object = StaticConstructObject_Internal(Params);
		}

		if (GIsInitialLoad || GUObjectArray.IsOpenForDisregardForGC())
		{
			Object->AddToRoot();
		}

		check(Object->GetClass() == LoadClass);
		check(Object->GetFName() == ObjectName);
	}

	check(Object);
	PinObjectForGC(Object, bIsNewObject);

	if (Desc.CanBeImported() && !Export.GlobalImportIndex.IsNull())
	{
		check(Object->HasAnyFlags(RF_Public));
		FPackageObjectIndex GlobalImportIndex = Export.GlobalImportIndex;
#if WITH_IOSTORE_IN_EDITOR
		// Always compute the global import index when loading cooked packages in editor builds
		// to prevent localized packages to overwrite the redirected package name
		GlobalImportIndex = FPackageObjectIndex::FromPackagePath(Object->GetPathName());
#endif
		ImportStore.StoreGlobalObject(Desc.DiskPackageId, GlobalImportIndex, Object);

		UE_ASYNC_PACKAGE_LOG_VERBOSE(VeryVerbose, Desc, TEXT("CreateExport"),
			TEXT("Created public export %s. Tracked as 0x%llX"), *Object->GetPathName(), Export.GlobalImportIndex.Value());
	}
	else
	{
		UE_ASYNC_PACKAGE_LOG_VERBOSE(VeryVerbose, Desc, TEXT("CreateExport"), TEXT("Created %s export %s. Not tracked."),
			Object->HasAnyFlags(RF_Public) ? TEXT("public") : TEXT("private"), *Object->GetPathName());
	}
}

bool FAsyncPackage2::EventDrivenSerializeExport(int32 LocalExportIndex, FExportArchive& Ar)
{
	LLM_SCOPE(ELLMTag::UObject);
	TRACE_CPUPROFILER_EVENT_SCOPE(SerializeExport);

	const FExportMapEntry& Export = ExportMap[LocalExportIndex];
	FExportObject& ExportObject = Data.Exports[LocalExportIndex];
	UObject* Object = ExportObject.Object;
	check(Object || (ExportObject.bFiltered | ExportObject.bExportLoadFailed));

	TRACE_LOADTIME_SERIALIZE_EXPORT_SCOPE(Object, Export.CookedSerialSize);

	if ((ExportObject.bFiltered | ExportObject.bExportLoadFailed) || !(Object && Object->HasAnyFlags(RF_NeedLoad)))
	{
		if (ExportObject.bExportLoadFailed)
		{
			UE_ASYNC_PACKAGE_LOG(Warning, Desc, TEXT("SerializeExport"),
				TEXT("Skipped failed export %s"), *NameMap.GetName(Export.ObjectName).ToString());
		}
		else if (ExportObject.bFiltered)
		{
			UE_ASYNC_PACKAGE_LOG_VERBOSE(Verbose, Desc, TEXT("SerializeExport"),
				TEXT("Skipped filtered export %s"), *NameMap.GetName(Export.ObjectName).ToString());
		}
		else
		{
			UE_ASYNC_PACKAGE_LOG_VERBOSE(VeryVerbose, Desc, TEXT("SerializeExport"),
				TEXT("Skipped already serialized export %s"), *NameMap.GetName(Export.ObjectName).ToString());
		}
		return false;
	}

	// If this is a struct, make sure that its parent struct is completely loaded
	if (UStruct* Struct = dynamic_cast<UStruct*>(Object))
	{
		if (UStruct* SuperStruct = dynamic_cast<UStruct*>(ExportObject.SuperObject))
		{
			Struct->SetSuperStruct(SuperStruct);
			if (UClass* ClassObject = dynamic_cast<UClass*>(Object))
			{
				ClassObject->Bind();
			}
		}
	}

	LLM_SCOPED_TAG_WITH_OBJECT_IN_SET(GetLinkerRoot(), ELLMTagSet::Assets);
	// LLM_SCOPED_TAG_WITH_OBJECT_IN_SET((Export.DynamicType == FObjectExport::EDynamicType::DynamicType) ? UDynamicClass::StaticClass() :
	// 	CastEventDrivenIndexToObject<UClass>(Export.ClassIndex, false), ELLMTagSet::AssetClasses);

	// cache archetype
	// prevents GetArchetype from hitting the expensive GetArchetypeFromRequiredInfoImpl
	check(ExportObject.TemplateObject);
	CacheArchetypeForObject(Object, ExportObject.TemplateObject);

	Object->ClearFlags(RF_NeedLoad);

	FUObjectSerializeContext* LoadContext = GetSerializeContext();
	UObject* PrevSerializedObject = LoadContext->SerializedObject;
	LoadContext->SerializedObject = Object;

	Ar.TemplateForGetArchetypeFromLoader = ExportObject.TemplateObject;

	if (Object->HasAnyFlags(RF_ClassDefaultObject))
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SerializeDefaultObject);
		Object->GetClass()->SerializeDefaultObject(Object, Ar);
	}
	else
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(SerializeObject);
		Object->Serialize(Ar);
	}
	Ar.TemplateForGetArchetypeFromLoader = nullptr;

	Object->SetFlags(RF_LoadCompleted);
	LoadContext->SerializedObject = PrevSerializedObject;

#if DO_CHECK
	if (Object->HasAnyFlags(RF_ClassDefaultObject) && Object->GetClass()->HasAnyClassFlags(CLASS_CompiledFromBlueprint))
	{
		check(Object->HasAllFlags(RF_NeedPostLoad | RF_WasLoaded));
		//Object->SetFlags(RF_NeedPostLoad | RF_WasLoaded);
	}
#endif

	UE_ASYNC_PACKAGE_LOG_VERBOSE(VeryVerbose, Desc, TEXT("SerializeExport"), TEXT("Serialized export %s"), *Object->GetPathName());

	// push stats so that we don't overflow number of tags per thread during blocking loading
	LLM_PUSH_STATS_FOR_ASSET_TAGS();

	return true;
}

EAsyncPackageState::Type FAsyncPackage2::Event_ExportsDone(FAsyncLoadingThreadState2& ThreadState, FAsyncPackage2* Package, int32)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Event_ExportsDone);
	UE_ASYNC_PACKAGE_DEBUG(Package->Desc);
	check(Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::ExportsDone);

	if (!Package->bLoadHasFailed && Package->Desc.CanBeImported())
	{
		FLoadedPackageRef& PackageRef =
			Package->AsyncLoadingThread.GlobalPackageStore.LoadedPackageStore.GetPackageRef((Package->Desc.DiskPackageId));
		PackageRef.SetAllPublicExportsLoaded();
	}

	Package->AsyncPackageLoadingState = EAsyncPackageLoadingState2::PostLoad;
	Package->GetExportBundleNode(EEventLoadNode2::ExportBundle_PostLoad, 0).ReleaseBarrier();
	return EAsyncPackageState::Complete;
}

EAsyncPackageState::Type FAsyncPackage2::Event_PostLoadExportBundle(FAsyncLoadingThreadState2& ThreadState, FAsyncPackage2* Package, int32 ExportBundleIndex)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Event_PostLoad);
	UE_ASYNC_PACKAGE_DEBUG(Package->Desc);
	check(Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::PostLoad);
	check(Package->ExternalReadDependencies.Num() == 0);
	
	FAsyncPackageScope2 PackageScope(Package);

	/*TSet<FAsyncPackage2*> Visited;
	TArray<FAsyncPackage2*> ProcessQueue;
	ProcessQueue.Push(Package);
	while (ProcessQueue.Num() > 0)
	{
		FAsyncPackage2* CurrentPackage = ProcessQueue.Pop();
		Visited.Add(CurrentPackage);
		if (CurrentPackage->AsyncPackageLoadingState < EAsyncPackageLoadingState2::ExportsDone)
		{
			UE_DEBUG_BREAK();
		}
		for (const FPackageId& ImportedPackageId : CurrentPackage->StoreEntry.ImportedPackages)
		{
			FAsyncPackage2* ImportedPackage = CurrentPackage->AsyncLoadingThread.GetAsyncPackage(ImportedPackageId);
			if (ImportedPackage && !Visited.Contains(ImportedPackage))
			{
				ProcessQueue.Push(ImportedPackage);
			}
		}
	}*/
	
	check(ExportBundleIndex < Package->Data.ExportBundleCount);

	EAsyncPackageState::Type LoadingState = EAsyncPackageState::Complete;

	if (!Package->bLoadHasFailed)
	{
		// Begin async loading, simulates BeginLoad
		Package->BeginAsyncLoad();

		SCOPED_LOADTIMER(PostLoadObjectsTime);

		FUObjectThreadContext& ThreadContext = FUObjectThreadContext::Get();
		TGuardValue<bool> GuardIsRoutingPostLoad(ThreadContext.IsRoutingPostLoad, true);

		const bool bAsyncPostLoadEnabled = FAsyncLoadingThreadSettings::Get().bAsyncPostLoadEnabled;
		const bool bIsMultithreaded = Package->AsyncLoadingThread.IsMultithreaded();

		const FExportBundleHeader* ExportBundle = Package->Data.ExportBundleHeaders + ExportBundleIndex;
		const FExportBundleEntry* BundleEntries = Package->Data.ExportBundleEntries + ExportBundle->FirstEntryIndex;
		const FExportBundleEntry* BundleEntry = BundleEntries + Package->ExportBundleEntryIndex;
		const FExportBundleEntry* BundleEntryEnd = BundleEntries + ExportBundle->EntryCount;
		check(BundleEntry <= BundleEntryEnd);
		while (BundleEntry < BundleEntryEnd)
		{
			if (ThreadState.IsTimeLimitExceeded(TEXT("Event_PostLoadExportBundle")))
			{
				LoadingState = EAsyncPackageState::TimeOut;
				break;
			}
			
			if (BundleEntry->CommandType == FExportBundleEntry::ExportCommandType_Serialize)
			{
				do
				{
					FExportObject& Export = Package->Data.Exports[BundleEntry->LocalExportIndex];
					if (Export.bFiltered | Export.bExportLoadFailed)
					{
						break;
					}

					UObject* Object = Export.Object;
					check(Object);
					check(!Object->HasAnyFlags(RF_NeedLoad));
					if (!Object->HasAnyFlags(RF_NeedPostLoad))
					{
						break;
					}

					check(Object->IsReadyForAsyncPostLoad());
					if (!bIsMultithreaded || (bAsyncPostLoadEnabled && CanPostLoadOnAsyncLoadingThread(Object)))
					{
						ThreadContext.CurrentlyPostLoadedObjectByALT = Object;
						{
							TRACE_LOADTIME_POSTLOAD_EXPORT_SCOPE(Object);
							Object->ConditionalPostLoad();
						}
						ThreadContext.CurrentlyPostLoadedObjectByALT = nullptr;
					}
				} while (false);
			}
			++BundleEntry;
			++Package->ExportBundleEntryIndex;
		}

		// End async loading, simulates EndLoad
		Package->EndAsyncLoad();
	}
	
	if (LoadingState == EAsyncPackageState::TimeOut)
	{
		return LoadingState;
	}

	Package->ExportBundleEntryIndex = 0;

	if (ExportBundleIndex + 1 < Package->Data.ExportBundleCount)
	{
		Package->GetExportBundleNode(ExportBundle_PostLoad, ExportBundleIndex + 1).ReleaseBarrier();
	}
	else
	{
		if (Package->LinkerRoot && !Package->bLoadHasFailed)
		{
			UE_ASYNC_PACKAGE_LOG(Verbose, Package->Desc, TEXT("AsyncThread: FullyLoaded"),
				TEXT("Async loading of package is done, and UPackage is marked as fully loaded."));
			// mimic old loader behavior for now, but this is more correctly also done in FinishUPackage
			// called from ProcessLoadedPackagesFromGameThread just before complection callbacks
			Package->LinkerRoot->MarkAsFullyLoaded();
		}

		check(Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::PostLoad);
		Package->AsyncPackageLoadingState = EAsyncPackageLoadingState2::DeferredPostLoad;
		Package->GetExportBundleNode(ExportBundle_DeferredPostLoad, 0).ReleaseBarrier();
	}

	return EAsyncPackageState::Complete;
}

EAsyncPackageState::Type FAsyncPackage2::Event_DeferredPostLoadExportBundle(FAsyncLoadingThreadState2& ThreadState, FAsyncPackage2* Package, int32 ExportBundleIndex)
{
	SCOPE_CYCLE_COUNTER(STAT_FAsyncPackage_PostLoadObjectsGameThread);
	TRACE_CPUPROFILER_EVENT_SCOPE(Event_DeferredPostLoad);
	UE_ASYNC_PACKAGE_DEBUG(Package->Desc);
	check(Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::DeferredPostLoad);

	FAsyncPackageScope2 PackageScope(Package);

	check(ExportBundleIndex < Package->Data.ExportBundleCount);
	EAsyncPackageState::Type LoadingState = EAsyncPackageState::Complete;

	if (Package->bLoadHasFailed)
	{
		FSoftObjectPath::InvalidateTag();
		FUniqueObjectGuid::InvalidateTag();
	}
	else
	{
		TGuardValue<bool> GuardIsRoutingPostLoad(PackageScope.ThreadContext.IsRoutingPostLoad, true);
		FAsyncLoadingTickScope2 InAsyncLoadingTick(Package->AsyncLoadingThread);

		const FExportBundleHeader* ExportBundle = Package->Data.ExportBundleHeaders + ExportBundleIndex;
		const FExportBundleEntry* BundleEntries = Package->Data.ExportBundleEntries + ExportBundle->FirstEntryIndex;
		const FExportBundleEntry* BundleEntry = BundleEntries + Package->ExportBundleEntryIndex;
		const FExportBundleEntry* BundleEntryEnd = BundleEntries + ExportBundle->EntryCount;
		check(BundleEntry <= BundleEntryEnd);
		while (BundleEntry < BundleEntryEnd)
		{
			if (ThreadState.IsTimeLimitExceeded(TEXT("Event_DeferredPostLoadExportBundle")))
			{
				LoadingState = EAsyncPackageState::TimeOut;
				break;
			}

			if (BundleEntry->CommandType == FExportBundleEntry::ExportCommandType_Serialize)
			{
				do
				{
					FExportObject& Export = Package->Data.Exports[BundleEntry->LocalExportIndex];
					if (Export.bFiltered | Export.bExportLoadFailed)
					{
						break;
					}

					UObject* Object = Export.Object;
					check(Object);
					check(!Object->HasAnyFlags(RF_NeedLoad));
					if (Object->HasAnyFlags(RF_NeedPostLoad))
					{
						PackageScope.ThreadContext.CurrentlyPostLoadedObjectByALT = Object;
						{
							TRACE_LOADTIME_POSTLOAD_EXPORT_SCOPE(Object);
							FScopeCycleCounterUObject ConstructorScope(Object, GET_STATID(STAT_FAsyncPackage_PostLoadObjectsGameThread));
							Object->ConditionalPostLoad();
						}
						PackageScope.ThreadContext.CurrentlyPostLoadedObjectByALT = nullptr;
					}
				} while (false);
			}
			++BundleEntry;
			++Package->ExportBundleEntryIndex;
		}
	}

	if (LoadingState == EAsyncPackageState::TimeOut)
	{
		return LoadingState;
	}

	Package->ExportBundleEntryIndex = 0;

	if (ExportBundleIndex + 1 < Package->Data.ExportBundleCount)
	{
		Package->GetExportBundleNode(ExportBundle_DeferredPostLoad, ExportBundleIndex + 1).ReleaseBarrier();
	}
	else
	{
		check(Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::DeferredPostLoad);
		Package->AsyncPackageLoadingState = EAsyncPackageLoadingState2::DeferredPostLoadDone;
		Package->AsyncLoadingThread.LoadedPackagesToProcess.Add(Package);
	}

	return EAsyncPackageState::Complete;
}

FEventLoadNode2& FAsyncPackage2::GetPackageNode(EEventLoadNode2 Phase)
{
	check(Phase < EEventLoadNode2::Package_NumPhases);
	return Data.PackageNodes[Phase];
}

FEventLoadNode2& FAsyncPackage2::GetExportBundleNode(EEventLoadNode2 Phase, uint32 ExportBundleIndex)
{
	check(ExportBundleIndex < uint32(Data.ExportBundleCount));
	uint32 ExportBundleNodeIndex = ExportBundleIndex * EEventLoadNode2::ExportBundle_NumPhases + Phase;
	return Data.ExportBundleNodes[ExportBundleNodeIndex];
}

#if ALT2_VERIFY_RECURSIVE_LOADS 
struct FScopedLoadRecursionVerifier
{
	int32& Level;
	FScopedLoadRecursionVerifier(int32& InLevel) : Level(InLevel)
	{
		UE_CLOG(Level > 0, LogStreaming, Error, TEXT("Entering recursive load level: %d"), Level);
		++Level;
		check(Level == 1);
	}
	~FScopedLoadRecursionVerifier()
	{
		--Level;
		UE_CLOG(Level > 0, LogStreaming, Error, TEXT("Leaving recursive load level: %d"), Level);
		check(Level == 0);
	}
};
#endif

EAsyncPackageState::Type FAsyncLoadingThread2::ProcessAsyncLoadingFromGameThread(FAsyncLoadingThreadState2& ThreadState, int32& OutPackagesProcessed)
{
	SCOPED_LOADTIMER(AsyncLoadingTime);

	check(IsInGameThread());

	OutPackagesProcessed = 0;

#if ALT2_VERIFY_RECURSIVE_LOADS 
	FScopedLoadRecursionVerifier LoadRecursionVerifier(this->LoadRecursionLevel);
#endif
	FAsyncLoadingTickScope2 InAsyncLoadingTick(*this);
	uint32 LoopIterations = 0;

	while (true)
	{
		do 
		{
			if ((++LoopIterations) % 32 == 31)
			{
				// We're not multithreaded and flushing async loading
				// Update heartbeat after 32 events
				FThreadHeartBeat::Get().HeartBeat();
				FCoreDelegates::OnAsyncLoadingFlushUpdate.Broadcast();
			}

			if (ThreadState.IsTimeLimitExceeded(TEXT("ProcessAsyncLoadingFromGameThread")))
			{
				return EAsyncPackageState::TimeOut;
			}

			if (IsAsyncLoadingSuspended())
			{
				return EAsyncPackageState::TimeOut;
			}

			if (QueuedPackagesCounter)
			{
				CreateAsyncPackagesFromQueue(ThreadState);
				OutPackagesProcessed++;
				break;
			}

			bool bPopped = false;
			for (FAsyncLoadEventQueue2* Queue : AltEventQueues)
			{
				if (Queue->PopAndExecute(ThreadState))
				{
					bPopped = true;
					break;
				}
			}
			if (bPopped)
			{
				OutPackagesProcessed++;
				break;
			}

			if (!ExternalReadQueue.IsEmpty())
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(ProcessExternalReads);

				FAsyncPackage2* Package = nullptr;
				ExternalReadQueue.Dequeue(Package);

				EAsyncPackageState::Type Result = Package->ProcessExternalReads(FAsyncPackage2::ExternalReadAction_Wait);
				check(Result == EAsyncPackageState::Complete);

				OutPackagesProcessed++;
				break;
			}

			ThreadState.ProcessDeferredFrees();

			if (!DeferredDeletePackages.IsEmpty())
			{
				FAsyncPackage2* Package = nullptr;
				DeferredDeletePackages.Dequeue(Package);
				DeleteAsyncPackage(Package);
				OutPackagesProcessed++;
				break;
			}

			return EAsyncPackageState::Complete;
		} while (false);
	}
	check(false);
	return EAsyncPackageState::Complete;
}

bool FAsyncPackage2::AreAllDependenciesFullyLoadedInternal(FAsyncPackage2* Package, TSet<FPackageId>& VisitedPackages, FPackageId& OutPackageId)
{
	for (const FPackageId& ImportedPackageId : Package->Desc.StoreEntry->ImportedPackages)
	{
		if (VisitedPackages.Contains(ImportedPackageId))
		{
			continue;
		}
		VisitedPackages.Add(ImportedPackageId);

		FAsyncPackage2* AsyncRoot = AsyncLoadingThread.GetAsyncPackage(ImportedPackageId);
		if (AsyncRoot)
		{
			if (AsyncRoot->AsyncPackageLoadingState < EAsyncPackageLoadingState2::DeferredPostLoadDone)
			{
				OutPackageId = ImportedPackageId;
				return false;
			}

			if (!AreAllDependenciesFullyLoadedInternal(AsyncRoot, VisitedPackages, OutPackageId))
			{
				return false;
			}
		}
	}
	return true;
}

bool FAsyncPackage2::AreAllDependenciesFullyLoaded(TSet<FPackageId>& VisitedPackages)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(AreAllDependenciesFullyLoaded);
	VisitedPackages.Reset();
	FPackageId PackageId;
	const bool bLoaded = AreAllDependenciesFullyLoadedInternal(this, VisitedPackages, PackageId);
	if (!bLoaded)
	{
		FAsyncPackage2* AsyncRoot = AsyncLoadingThread.GetAsyncPackage(PackageId);
		UE_LOG(LogStreaming, Verbose, TEXT("AreAllDependenciesFullyLoaded: '%s' doesn't have all exports processed by DeferredPostLoad"),
			*AsyncRoot->Desc.DiskPackageName.ToString());
	}
	return bLoaded;
}

EAsyncPackageState::Type FAsyncLoadingThread2::ProcessLoadedPackagesFromGameThread(FAsyncLoadingThreadState2& ThreadState, bool& bDidSomething, int32 FlushRequestID)
{
	EAsyncPackageState::Type Result = EAsyncPackageState::Complete;

	// This is for debugging purposes only. @todo remove
	volatile int32 CurrentAsyncLoadingCounter = AsyncLoadingTickCounter;

	if (IsMultithreaded() &&
		ENamedThreads::GetRenderThread() == ENamedThreads::GameThread &&
		!FTaskGraphInterface::Get().IsThreadProcessingTasks(ENamedThreads::GameThread)) // render thread tasks are actually being sent to the game thread.
	{
		// The async loading thread might have queued some render thread tasks (we don't have a render thread yet, so these are actually sent to the game thread)
		// We need to process them now before we do any postloads.
		FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
		if (ThreadState.IsTimeLimitExceeded(TEXT("ProcessLoadedPackagesFromGameThread")))
		{
			return EAsyncPackageState::TimeOut;
		}
	}

	// For performance reasons this set is created here and reset inside of AreAllDependenciesFullyLoaded
	TSet<FPackageId> VisistedPackages;

	for (;;)
	{
		FPlatformMisc::PumpEssentialAppMessages();

		if (ThreadState.IsTimeLimitExceeded(TEXT("ProcessAsyncLoadingFromGameThread")))
		{
			Result = EAsyncPackageState::TimeOut;
			break;
		}

		bool bLocalDidSomething = false;
		bLocalDidSomething |= MainThreadEventQueue.PopAndExecute(ThreadState);

		bLocalDidSomething |= LoadedPackagesToProcess.Num() > 0;
		for (int32 PackageIndex = 0; PackageIndex < LoadedPackagesToProcess.Num(); ++PackageIndex)
		{
			SCOPED_LOADTIMER(ProcessLoadedPackagesTime);
			FAsyncPackage2* Package = LoadedPackagesToProcess[PackageIndex];
			UE_ASYNC_PACKAGE_DEBUG(Package->Desc);
			check(Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::DeferredPostLoadDone);
			Package->AsyncPackageLoadingState = EAsyncPackageLoadingState2::Finalize;

			bool bHasClusterObjects = false;
			TArray<UObject*> CDODefaultSubobjects;
			// Clear async loading flags (we still want RF_Async, but EInternalObjectFlags::AsyncLoading can be cleared)
			for (int32 FinalizeIndex = 0; FinalizeIndex < Package->Data.ExportCount; ++FinalizeIndex)
			{
				const FExportObject& Export = Package->Data.Exports[FinalizeIndex];
				if (Export.bFiltered | Export.bExportLoadFailed)
				{
					continue;
				}

				UObject* Object = Export.Object;

				// CDO need special handling, no matter if it's listed in DeferredFinalizeObjects or created here for DynamicClass
				UObject* CDOToHandle = nullptr;

				// Dynamic Class doesn't require/use pre-loading (or post-loading). 
				// The CDO is created at this point, because now it's safe to solve cyclic dependencies.
				if (UDynamicClass* DynamicClass = Cast<UDynamicClass>(Object))
				{
					check((DynamicClass->ClassFlags & CLASS_Constructed) != 0);

					//native blueprint 

					check(DynamicClass->HasAnyClassFlags(CLASS_TokenStreamAssembled));
					// this block should be removed entirely when and if we add the CDO to the fake export table
					CDOToHandle = DynamicClass->GetDefaultObject(false);
					UE_CLOG(!CDOToHandle, LogStreaming, Fatal, TEXT("EDL did not create the CDO for %s before it finished loading."), *DynamicClass->GetFullName());
					CDOToHandle->AtomicallyClearInternalFlags(EInternalObjectFlags::AsyncLoading);
				}
				else
				{
					CDOToHandle = ((Object != nullptr) && Object->HasAnyFlags(RF_ClassDefaultObject)) ? Object : nullptr;
				}

				// Clear AsyncLoading in CDO's subobjects.
				if (CDOToHandle != nullptr)
				{
					CDOToHandle->GetDefaultSubobjects(CDODefaultSubobjects);
					for (UObject* SubObject : CDODefaultSubobjects)
					{
						if (SubObject && SubObject->HasAnyInternalFlags(EInternalObjectFlags::AsyncLoading))
						{
							SubObject->AtomicallyClearInternalFlags(EInternalObjectFlags::AsyncLoading);
						}
					}
					CDODefaultSubobjects.Reset();
				}
			}

			Package->FinishUPackage();

			if (!Package->bLoadHasFailed && CanCreateObjectClusters())
			{
				for (const FExportObject& Export : Package->Data.Exports)
				{
					if (!(Export.bFiltered | Export.bExportLoadFailed) && Export.Object->CanBeClusterRoot())
					{
						bHasClusterObjects = true;
						break;
					}
				}
			}

			FSoftObjectPath::InvalidateTag();
			FUniqueObjectGuid::InvalidateTag();

			{
				FScopeLock LockAsyncPackages(&AsyncPackagesCritical);
				AsyncPackageLookup.Remove(Package->Desc.GetAsyncPackageId());
				if (!Package->bLoadHasFailed)
				{
					Package->ClearConstructedObjects();
				}
			}

			// Remove the package from the list before we trigger the callbacks, 
			// this is to ensure we can re-enter FlushAsyncLoading from any of the callbacks
			LoadedPackagesToProcess.RemoveAt(PackageIndex--);

			// Incremented on the Async Thread, now decrement as we're done with this package				
			ActiveAsyncPackagesCounter.Decrement();

			TRACE_LOADTIME_END_LOAD_ASYNC_PACKAGE(Package);

			// Call external callbacks
			const EAsyncLoadingResult::Type LoadingResult = Package->HasLoadFailed() ? EAsyncLoadingResult::Failed : EAsyncLoadingResult::Succeeded;
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(PackageCompletionCallbacks);
				Package->CallCompletionCallbacks(LoadingResult);
			}

			// We don't need the package anymore
			check(Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::Finalize);
			if (bHasClusterObjects)
			{
				Package->AsyncPackageLoadingState = EAsyncPackageLoadingState2::CreateClusters;
			}
			else
			{
				Package->AsyncPackageLoadingState = EAsyncPackageLoadingState2::Complete;
			}
			check(!CompletedPackages.Contains(Package));
			CompletedPackages.Add(Package);
			Package->MarkRequestIDsAsComplete();

			UE_ASYNC_PACKAGE_LOG(Verbose, Package->Desc, TEXT("GameThread: LoadCompleted"),
				TEXT("All loading of package is done, and the async package and load request will be deleted."));
		}

		bLocalDidSomething |= QueuedFailedPackageCallbacks.Num() > 0;
		for (FQueuedFailedPackageCallback& QueuedFailedPackageCallback : QueuedFailedPackageCallbacks)
		{
			QueuedFailedPackageCallback.Callback->ExecuteIfBound(QueuedFailedPackageCallback.PackageName, nullptr, EAsyncLoadingResult::Failed);
		}
		QueuedFailedPackageCallbacks.Empty();

		bLocalDidSomething |= CompletedPackages.Num() > 0;
		for (int32 PackageIndex = 0; PackageIndex < CompletedPackages.Num(); ++PackageIndex)
		{
			FAsyncPackage2* Package = CompletedPackages[PackageIndex];
			{
				bool bSafeToDelete = false;
				if (Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::CreateClusters)
				{
					SCOPE_CYCLE_COUNTER(STAT_FAsyncPackage_CreateClustersGameThread);
					// This package will create GC clusters but first check if all dependencies of this package have been fully loaded
					if (Package->AreAllDependenciesFullyLoaded(VisistedPackages))
					{
						if (Package->CreateClusters(ThreadState) == EAsyncPackageState::Complete)
						{
							// All clusters created, it's safe to delete the package
							bSafeToDelete = true;
							Package->AsyncPackageLoadingState = EAsyncPackageLoadingState2::Complete;
						}
						else
						{
							// Cluster creation timed out
							Result = EAsyncPackageState::TimeOut;
							break;
						}
					}
				}
				else
				{
					// No clusters to create so it's safe to delete
					bSafeToDelete = true;
				}

				if (bSafeToDelete)
				{
					UE_ASYNC_PACKAGE_DEBUG(Package->Desc);
					check(Package->AsyncPackageLoadingState == EAsyncPackageLoadingState2::Complete);
					Package->AsyncPackageLoadingState = EAsyncPackageLoadingState2::DeferredDelete;

					CompletedPackages.RemoveAtSwap(PackageIndex--);
					Package->ClearImportedPackages();
					Package->ReleaseRef();
				}
			}

			// push stats so that we don't overflow number of tags per thread during blocking loading
			LLM_PUSH_STATS_FOR_ASSET_TAGS();
		}
		
		if (!bLocalDidSomething)
		{
			break;
		}

		bDidSomething = true;
		
		if (FlushRequestID != INDEX_NONE && !ContainsRequestID(FlushRequestID))
		{
			// The only package we care about has finished loading, so we're good to exit
			break;
		}
	}

	if (Result == EAsyncPackageState::Complete)
	{
		// We're not done until all packages have been deleted
		Result = CompletedPackages.Num() ? EAsyncPackageState::PendingImports  : EAsyncPackageState::Complete;
		if (Result == EAsyncPackageState::Complete && ThreadState.HasDeferredFrees())
		{
			ThreadState.ProcessDeferredFrees();
		}
	}

	return Result;
}

EAsyncPackageState::Type FAsyncLoadingThread2::TickAsyncLoadingFromGameThread(FAsyncLoadingThreadState2& ThreadState, bool bUseTimeLimit, bool bUseFullTimeLimit, float TimeLimit, int32 FlushRequestID)
{
	SCOPE_CYCLE_COUNTER(STAT_FAsyncPackage_TickAsyncLoadingGameThread);
	//TRACE_INT_VALUE(QueuedPackagesCounter, QueuedPackagesCounter);
	//TRACE_INT_VALUE(GraphNodeCount, GraphAllocator.TotalNodeCount);
	//TRACE_INT_VALUE(GraphArcCount, GraphAllocator.TotalArcCount);
	//TRACE_MEMORY_VALUE(GraphMemory, GraphAllocator.TotalAllocated);


	check(IsInGameThread());
	check(!IsGarbageCollecting());

	const bool bLoadingSuspended = IsAsyncLoadingSuspended();
	EAsyncPackageState::Type Result = bLoadingSuspended ? EAsyncPackageState::PendingImports : EAsyncPackageState::Complete;

	if (!bLoadingSuspended)
	{
		ThreadState.SetTimeLimit(bUseTimeLimit, TimeLimit);

		const bool bIsMultithreaded = FAsyncLoadingThread2::IsMultithreaded();
		double TickStartTime = FPlatformTime::Seconds();

		bool bDidSomething = false;
		{
			Result = ProcessLoadedPackagesFromGameThread(ThreadState, bDidSomething, FlushRequestID);
			double TimeLimitUsedForProcessLoaded = FPlatformTime::Seconds() - TickStartTime;
			UE_CLOG(!GIsEditor && bUseTimeLimit && TimeLimitUsedForProcessLoaded > .1f, LogStreaming, Warning, TEXT("Took %6.2fms to ProcessLoadedPackages"), float(TimeLimitUsedForProcessLoaded) * 1000.0f);
		}

		if (!bIsMultithreaded && Result != EAsyncPackageState::TimeOut)
		{
			Result = TickAsyncThreadFromGameThread(ThreadState, bDidSomething);
		}

		if (Result != EAsyncPackageState::TimeOut)
		{
			// Flush deferred messages
			if (ExistingAsyncPackagesCounter.GetValue() == 0)
			{
				bDidSomething = true;
				FDeferredMessageLog::Flush();
			}

			if (GIsInitialLoad && !bDidSomething)
			{
				bDidSomething = ProcessPendingCDOs();
			}
		}

		// Call update callback once per tick on the game thread
		FCoreDelegates::OnAsyncLoadingFlushUpdate.Broadcast();
	}

	return Result;
}

FAsyncLoadingThread2::FAsyncLoadingThread2(FIoDispatcher& InIoDispatcher)
	: Thread(nullptr)
	, IoDispatcher(InIoDispatcher)
	, GlobalPackageStore(InIoDispatcher, GlobalNameMap)
{
#if !WITH_IOSTORE_IN_EDITOR
	GEventDrivenLoaderEnabled = true;
#endif

#if LOADTIMEPROFILERTRACE_ENABLED
	FLoadTimeProfilerTracePrivate::Init();
#endif

	AltEventQueues.Add(&EventQueue);
	for (FAsyncLoadEventQueue2* Queue : AltEventQueues)
	{
		Queue->SetZenaphore(&AltZenaphore);
	}

	EventSpecs.AddDefaulted(EEventLoadNode2::Package_NumPhases + EEventLoadNode2::ExportBundle_NumPhases);
	EventSpecs[EEventLoadNode2::Package_ProcessSummary] = { &FAsyncPackage2::Event_ProcessPackageSummary, &EventQueue, false };
	EventSpecs[EEventLoadNode2::Package_ExportsSerialized] = { &FAsyncPackage2::Event_ExportsDone, &EventQueue, true };

	EventSpecs[EEventLoadNode2::Package_NumPhases + EEventLoadNode2::ExportBundle_Process] = { &FAsyncPackage2::Event_ProcessExportBundle, &EventQueue, false };
	EventSpecs[EEventLoadNode2::Package_NumPhases + EEventLoadNode2::ExportBundle_PostLoad] = { &FAsyncPackage2::Event_PostLoadExportBundle, &EventQueue, false };
	EventSpecs[EEventLoadNode2::Package_NumPhases + EEventLoadNode2::ExportBundle_DeferredPostLoad] = { &FAsyncPackage2::Event_DeferredPostLoadExportBundle, &MainThreadEventQueue, false };

	CancelLoadingEvent = FPlatformProcess::GetSynchEventFromPool();
	ThreadSuspendedEvent = FPlatformProcess::GetSynchEventFromPool();
	ThreadResumedEvent = FPlatformProcess::GetSynchEventFromPool();
	AsyncLoadingTickCounter = 0;

	FAsyncLoadingThreadState2::TlsSlot = FPlatformTLS::AllocTlsSlot();
	FAsyncLoadingThreadState2::Create(GraphAllocator, IoDispatcher);

	UE_LOG(LogStreaming, Display, TEXT("AsyncLoading2 - Created: Event Driven Loader: %s, Async Loading Thread: %s, Async Post Load: %s"),
		GEventDrivenLoaderEnabled ? TEXT("true") : TEXT("false"),
		FAsyncLoadingThreadSettings::Get().bAsyncLoadingThreadEnabled ? TEXT("true") : TEXT("false"),
		FAsyncLoadingThreadSettings::Get().bAsyncPostLoadEnabled ? TEXT("true") : TEXT("false"));
}

FAsyncLoadingThread2::~FAsyncLoadingThread2()
{
	if (Thread)
	{
		ShutdownLoading();
	}

#if USE_NEW_BULKDATA
	FBulkDataBase::SetIoDispatcher(nullptr);
#endif
}

void FAsyncLoadingThread2::ShutdownLoading()
{
	FCoreUObjectDelegates::GetPreGarbageCollectDelegate().RemoveAll(this);
	FCoreUObjectDelegates::GetPostGarbageCollect().RemoveAll(this);

	delete Thread;
	Thread = nullptr;
	FPlatformProcess::ReturnSynchEventToPool(CancelLoadingEvent);
	CancelLoadingEvent = nullptr;
	FPlatformProcess::ReturnSynchEventToPool(ThreadSuspendedEvent);
	ThreadSuspendedEvent = nullptr;
	FPlatformProcess::ReturnSynchEventToPool(ThreadResumedEvent);
	ThreadResumedEvent = nullptr;
}

void FAsyncLoadingThread2::StartThread()
{
	// Make sure the GC sync object is created before we start the thread (apparently this can happen before we call InitUObject())
	FGCCSyncObject::Create();

	if (!FAsyncLoadingThreadSettings::Get().bAsyncLoadingThreadEnabled)
	{
		FinalizeInitialLoad();
	}
	else if (!Thread)
	{
		UE_LOG(LogStreaming, Log, TEXT("Starting Async Loading Thread."));
		bThreadStarted = true;
		FPlatformMisc::MemoryBarrier();
		Trace::ThreadGroupBegin(TEXT("AsyncLoading"));
		Thread = FRunnableThread::Create(this, TEXT("FAsyncLoadingThread"), 0, TPri_Normal);
		Trace::ThreadGroupEnd();
	}

	UE_LOG(LogStreaming, Display, TEXT("AsyncLoading2 - Thread Started: %s, IsInitialLoad: %s"),
		FAsyncLoadingThreadSettings::Get().bAsyncLoadingThreadEnabled ? TEXT("true") : TEXT("false"),
		GIsInitialLoad ? TEXT("true") : TEXT("false"));
}

bool FAsyncLoadingThread2::Init()
{
	return true;
}

void FAsyncLoadingThread2::SuspendWorkers()
{
	if (bWorkersSuspended)
	{
		return;
	}
	TRACE_CPUPROFILER_EVENT_SCOPE(SuspendWorkers);
	for (FAsyncLoadingThreadWorker& Worker : Workers)
	{
		Worker.SuspendThread();
	}
	while (ActiveWorkersCount > 0)
	{
		FPlatformProcess::SleepNoStats(0);
	}
	bWorkersSuspended = true;
}

void FAsyncLoadingThread2::ResumeWorkers()
{
	if (!bWorkersSuspended)
	{
		return;
	}
	TRACE_CPUPROFILER_EVENT_SCOPE(ResumeWorkers);
	for (FAsyncLoadingThreadWorker& Worker : Workers)
	{
		Worker.ResumeThread();
	}
	bWorkersSuspended = false;
}

void FAsyncLoadingThread2::LazyInitializeFromLoadPackage()
{
	if (bLazyInitializedFromLoadPackage)
	{
		return;	
	}
	bLazyInitializedFromLoadPackage = true;

	TRACE_CPUPROFILER_EVENT_SCOPE(LazyInitializeFromLoadPackage);
	GlobalNameMap.LoadGlobal(IoDispatcher);
	if (GIsInitialLoad)
	{
		GlobalPackageStore.SetupInitialLoadData();
	}
	GlobalPackageStore.SetupCulture();
	GlobalPackageStore.LoadContainers(IoDispatcher.GetMountedContainers());
	IoDispatcher.OnContainerMounted().AddRaw(&GlobalPackageStore, &FPackageStore::OnContainerMounted);
}


void FAsyncLoadingThread2::FinalizeInitialLoad()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FinalizeInitialLoad);
	GlobalPackageStore.FinalizeInitialLoad();
	check(PendingCDOs.Num() == 0);
	PendingCDOs.Empty();
}

uint32 FAsyncLoadingThread2::Run()
{
	LLM_SCOPE(ELLMTag::AsyncLoading);

	AsyncLoadingThreadID = FPlatformTLS::GetCurrentThreadId();

	FAsyncLoadingThreadState2::Create(GraphAllocator, IoDispatcher);

	TRACE_LOADTIME_START_ASYNC_LOADING();

	FPlatformProcess::SetThreadAffinityMask(FPlatformAffinity::GetAsyncLoadingThreadMask());
	FMemory::SetupTLSCachesOnCurrentThread();

	FAsyncLoadingThreadState2& ThreadState = *FAsyncLoadingThreadState2::Get();
	
	FinalizeInitialLoad();

	FZenaphoreWaiter Waiter(AltZenaphore, TEXT("WaitForEvents"));
	bool bIsSuspended = false;
	while (!bStopRequested)
	{
		if (bIsSuspended)
		{
			if (!bSuspendRequested.Load(EMemoryOrder::SequentiallyConsistent) && !IsGarbageCollectionWaiting())
			{
				ThreadResumedEvent->Trigger();
				bIsSuspended = false;
				ResumeWorkers();
			}
			else
			{
				FPlatformProcess::Sleep(0.001f);
			}
		}
		else
		{
			bool bDidSomething = false;
			{
				FGCScopeGuard GCGuard;
				TRACE_CPUPROFILER_EVENT_SCOPE(AsyncLoadingTime);
				do
				{
					bDidSomething = false;

					if (QueuedPackagesCounter)
					{
						if (CreateAsyncPackagesFromQueue(ThreadState))
						{
							bDidSomething = true;
						}
					}

					bool bShouldSuspend = false;
					bool bPopped = false;
					do 
					{
						bPopped = false;
						for (FAsyncLoadEventQueue2* Queue : AltEventQueues)
						{
							if (Queue->PopAndExecute(ThreadState))
							{
								bPopped = true;
								bDidSomething = true;
							}

							if (bSuspendRequested.Load(EMemoryOrder::Relaxed) || IsGarbageCollectionWaiting())
							{
								bShouldSuspend = true;
								bPopped = false;
								break;
							}
						}
					} while (bPopped);

					if (bShouldSuspend || bSuspendRequested.Load(EMemoryOrder::Relaxed) || IsGarbageCollectionWaiting())
					{
						SuspendWorkers();
						ThreadSuspendedEvent->Trigger();
						bIsSuspended = true;
						bDidSomething = true;
						break;
					}

					{
						bool bDidExternalRead = false;
						do
						{
							bDidExternalRead = false;
							FAsyncPackage2* Package = nullptr;
							if (ExternalReadQueue.Peek(Package))
							{
								TRACE_CPUPROFILER_EVENT_SCOPE(ProcessExternalReads);

								FAsyncPackage2::EExternalReadAction Action = FAsyncPackage2::ExternalReadAction_Poll;

								EAsyncPackageState::Type Result = Package->ProcessExternalReads(Action);
								if (Result == EAsyncPackageState::Complete)
								{
									ExternalReadQueue.Pop();
									bDidExternalRead = true;
									bDidSomething = true;
								}
							}
						} while (bDidExternalRead);
					}

				} while (bDidSomething);
			}

			if (!bDidSomething)
			{
				if (ThreadState.HasDeferredFrees())
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(AsyncLoadingTime);
					ThreadState.ProcessDeferredFrees();
					bDidSomething = true;
				}

				if (!DeferredDeletePackages.IsEmpty())
				{
					FGCScopeGuard GCGuard;
					TRACE_CPUPROFILER_EVENT_SCOPE(AsyncLoadingTime);
					FAsyncPackage2* Package = nullptr;
					int32 Count = 0;
					while (++Count <= 100 && DeferredDeletePackages.Dequeue(Package))
					{
						DeleteAsyncPackage(Package);
					}
					bDidSomething = true;
				}
			}

			if (!bDidSomething)
			{
				FAsyncPackage2* Package = nullptr;
				if (WaitingForIoBundleCounter.GetValue() > 0)
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(AsyncLoadingTime);
					TRACE_CPUPROFILER_EVENT_SCOPE(WaitingForIo);
					Waiter.Wait();
				}
				else if (ExternalReadQueue.Peek(Package))
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(AsyncLoadingTime);
					TRACE_CPUPROFILER_EVENT_SCOPE(ProcessExternalReads);

					EAsyncPackageState::Type Result = Package->ProcessExternalReads(FAsyncPackage2::ExternalReadAction_Wait);
					check(Result == EAsyncPackageState::Complete);
					ExternalReadQueue.Pop();
				}
				else
				{
					Waiter.Wait();
				}
			}
		}
	}
	return 0;
}

EAsyncPackageState::Type FAsyncLoadingThread2::TickAsyncThreadFromGameThread(FAsyncLoadingThreadState2& ThreadState, bool& bDidSomething)
{
	check(IsInGameThread());
	EAsyncPackageState::Type Result = EAsyncPackageState::Complete;
	
	int32 ProcessedRequests = 0;
	if (AsyncThreadReady.GetValue())
	{
		if (ThreadState.IsTimeLimitExceeded(TEXT("TickAsyncThreadFromGameThread")))
		{
			Result = EAsyncPackageState::TimeOut;
		}
		else
		{
			FGCScopeGuard GCGuard;
			Result = ProcessAsyncLoadingFromGameThread(ThreadState, ProcessedRequests);
			bDidSomething = bDidSomething || ProcessedRequests > 0;
		}
	}

	return Result;
}

void FAsyncLoadingThread2::Stop()
{
	for (FAsyncLoadingThreadWorker& Worker : Workers)
	{
		Worker.StopThread();
	}
	bSuspendRequested = true;
	bStopRequested = true;
	AltZenaphore.NotifyAll();
}

void FAsyncLoadingThread2::CancelLoading()
{
	check(false);
	// TODO
}

void FAsyncLoadingThread2::SuspendLoading()
{
	UE_CLOG(!IsInGameThread() || IsInSlateThread(), LogStreaming, Fatal, TEXT("Async loading can only be suspended from the main thread"));
	if (!bSuspendRequested)
	{
		bSuspendRequested = true;
		if (IsMultithreaded())
		{
			TRACE_LOADTIME_SUSPEND_ASYNC_LOADING();
			AltZenaphore.NotifyAll();
			ThreadSuspendedEvent->Wait();
		}
	}
}

void FAsyncLoadingThread2::ResumeLoading()
{
	check(IsInGameThread() && !IsInSlateThread());
	if (bSuspendRequested)
	{
		bSuspendRequested = false;
		if (IsMultithreaded())
		{
			ThreadResumedEvent->Wait();
			TRACE_LOADTIME_RESUME_ASYNC_LOADING();
		}
	}
}

float FAsyncLoadingThread2::GetAsyncLoadPercentage(const FName& PackageName)
{
	float LoadPercentage = -1.0f;
	/*
	FAsyncPackage2* Package = FindAsyncPackage(PackageName);
	if (Package)
	{
		LoadPercentage = Package->GetLoadPercentage();
	}
	*/
	return LoadPercentage;
}

#if ALT2_VERIFY_ASYNC_FLAGS
static void VerifyLoadFlagsWhenFinishedLoading()
{
	const EInternalObjectFlags AsyncFlags =
		EInternalObjectFlags::Async | EInternalObjectFlags::AsyncLoading;

	const EObjectFlags LoadIntermediateFlags = 
		EObjectFlags::RF_NeedLoad | EObjectFlags::RF_WillBeLoaded |
		EObjectFlags::RF_NeedPostLoad | RF_NeedPostLoadSubobjects;

	for (int32 ObjectIndex = 0; ObjectIndex < GUObjectArray.GetObjectArrayNum(); ++ObjectIndex)
	{
		FUObjectItem* ObjectItem = &GUObjectArray.GetObjectItemArrayUnsafe()[ObjectIndex];
		if (UObject* Obj = static_cast<UObject*>(ObjectItem->Object))
		{
			const EInternalObjectFlags InternalFlags = Obj->GetInternalFlags();
			const EObjectFlags Flags = Obj->GetFlags();
			const bool bHasAnyAsyncFlags = !!(InternalFlags & AsyncFlags);
			const bool bHasAnyLoadIntermediateFlags = !!(Flags & LoadIntermediateFlags);
			const bool bWasLoaded = !!(Flags & RF_WasLoaded);
			const bool bLoadCompleted = !!(Flags & RF_LoadCompleted);

			ensureMsgf(!bHasAnyLoadIntermediateFlags,
				TEXT("Object '%s' (ObjectFlags=%X, InternalObjectFlags=%x) should not have any load flags now")
				TEXT(", or this check is incorrectly reached during active loading."),
				*Obj->GetFullName(),
				Flags,
				InternalFlags);

			if (bWasLoaded)
			{
				const bool bIsPackage = Obj->IsA(UPackage::StaticClass());

				ensureMsgf(bIsPackage || bLoadCompleted,
					TEXT("Object '%s' (ObjectFlags=%x, InternalObjectFlags=%x) is a serialized object and should be completely loaded now")
					TEXT(", or this check is incorrectly reached during active loading."),
					*Obj->GetFullName(),
					Flags,
					InternalFlags);

				ensureMsgf(!bHasAnyAsyncFlags,
					TEXT("Object '%s' (ObjectFlags=%x, InternalObjectFlags=%x) is a serialized object and should not have any async flags now")
					TEXT(", or this check is incorrectly reached during active loading."),
					*Obj->GetFullName(),
					Flags,
					InternalFlags);
			}
		}
	}
	UE_LOG(LogStreaming, Log, TEXT("Verified load flags when finished active loading."));
}
#endif

FORCENOINLINE static void FilterUnreachableObjects(
	const TArrayView<FUObjectItem*>& UnreachableObjects,
	FUnreachablePublicExports& PublicExports,
	FUnreachablePackages& Packages)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FilterUnreachableObjects);

	PublicExports.Reserve(UnreachableObjects.Num());
	Packages.Reserve(UnreachableObjects.Num());

	for (FUObjectItem* ObjectItem : UnreachableObjects)
	{
		UObject* Object = static_cast<UObject*>(ObjectItem->Object);
		if (Object->HasAllFlags(RF_WasLoaded | RF_Public))
		{
			if (Object->GetOuter())
			{
				PublicExports.Emplace(GUObjectArray.ObjectToIndex(Object), Object);
			}
			else
			{
				UPackage* Package = static_cast<UPackage*>(Object);
#if WITH_IOSTORE_IN_EDITOR
				if (Package->HasAnyPackageFlags(PKG_Cooked))
#endif
				{
					Packages.Emplace(Package->FileName, Package);
				}
			}
		}
	}
}

void FAsyncLoadingThread2::RemoveUnreachableObjects(const FUnreachablePublicExports& PublicExports, const FUnreachablePackages& Packages)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RemoveUnreachableObjects);

	TArray<FPackageId> PublicExportPackages;
	if (PublicExports.Num() > 0)
	{
		// TRACE_CPUPROFILER_EVENT_SCOPE(RemovePublicExports);
		PublicExportPackages = GlobalPackageStore.ImportStore.RemovePublicExports(PublicExports);
	}
	if (Packages.Num() > 0)
	{
		// TRACE_CPUPROFILER_EVENT_SCOPE(RemovePackages);
		GlobalPackageStore.RemovePackages(Packages);
	}
	if (PublicExportPackages.Num() > 0)
	{
		// TRACE_CPUPROFILER_EVENT_SCOPE(ClearAllPublicExportsLoaded);
		GlobalPackageStore.ClearAllPublicExportsLoaded(PublicExportPackages);
	}
}

void FAsyncLoadingThread2::NotifyUnreachableObjects(const TArrayView<FUObjectItem*>& UnreachableObjects)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(NotifyUnreachableObjects);

	if (GExitPurge)
	{
		return;
	}

	const double StartTime = FPlatformTime::Seconds();

	FUnreachablePackages Packages;
	FUnreachablePublicExports PublicExports;
	FilterUnreachableObjects(UnreachableObjects, PublicExports, Packages);

	const int32 PackageCount = Packages.Num();
	const int32 PublicExportCount = PublicExports.Num();
	if (PackageCount > 0 || PublicExportCount > 0)
	{
		const int32 OldLoadedPackageCount = GlobalPackageStore.LoadedPackageStore.NumTracked();
		const int32 OldPublicExportCount = GlobalPackageStore.GetGlobalImportStore().PublicExportObjects.Num();

		const double RemoveStartTime = FPlatformTime::Seconds();
		RemoveUnreachableObjects(PublicExports, Packages);

		const int32 NewLoadedPackageCount = GlobalPackageStore.LoadedPackageStore.NumTracked();
		const int32 NewPublicExportCount = GlobalPackageStore.GetGlobalImportStore().PublicExportObjects.Num();
		const int32 RemovedLoadedPackageCount = OldLoadedPackageCount - NewLoadedPackageCount;
		const int32 RemovedPublicExportCount = OldPublicExportCount - NewPublicExportCount;

		const double StopTime = FPlatformTime::Seconds();
		UE_LOG(LogStreaming, Display,
			TEXT("%.3f ms (%.3f+%.3f) ms for processing %d/%d objects in NotifyUnreachableObjects( Queued=%d, Async=%d). ")
			TEXT("Removed %d/%d (%d->%d tracked) packages and %d/%d (%d->%d tracked) public exports."),
			(StopTime - StartTime) * 1000,
			(RemoveStartTime - StartTime) * 1000,
			(StopTime - RemoveStartTime) * 1000,
			PublicExportCount + PackageCount, UnreachableObjects.Num(),
			GetNumQueuedPackages(), GetNumAsyncPackages(),
			RemovedLoadedPackageCount, PackageCount, OldLoadedPackageCount, NewLoadedPackageCount,
			RemovedPublicExportCount, PublicExportCount, OldPublicExportCount, NewPublicExportCount);
	}
	else
	{
		UE_LOG(LogStreaming, Display, TEXT("%.3f ms for skipping %d objects in NotifyUnreachableObjects (Queued=%d, Async=%d)."),
			(FPlatformTime::Seconds() - StartTime) * 1000,
			UnreachableObjects.Num(),
			GetNumQueuedPackages(), GetNumAsyncPackages());
	}

#if ALT2_VERIFY_ASYNC_FLAGS
	if (!IsAsyncLoadingPackages())
	{
		GlobalPackageStore.LoadedPackageStore.VerifyLoadedPackages();
		VerifyLoadFlagsWhenFinishedLoading();
	}
#endif
}

/**
 * Call back into the async loading code to inform of the creation of a new object
 * @param Object		Object created
 * @param bSubObjectThatAlreadyExists	Object created as a sub-object of a loaded object
 */
void FAsyncLoadingThread2::NotifyConstructedDuringAsyncLoading(UObject* Object, bool bSubObjectThatAlreadyExists)
{
	FUObjectThreadContext& ThreadContext = FUObjectThreadContext::Get();
	if (!ThreadContext.AsyncPackage)
	{
		// Something is creating objects on the async loading thread outside of the actual async loading code
		// e.g. ShaderCodeLibrary::OnExternalReadCallback doing FTaskGraphInterface::Get().WaitUntilTaskCompletes(Event);
		return;
	}

	// Mark objects created during async loading process (e.g. from within PostLoad or CreateExport) as async loaded so they 
	// cannot be found. This requires also keeping track of them so we can remove the async loading flag later one when we 
	// finished routing PostLoad to all objects.
	if (!bSubObjectThatAlreadyExists)
	{
		Object->SetInternalFlags(EInternalObjectFlags::AsyncLoading);
	}
	FAsyncPackage2* AsyncPackage2 = (FAsyncPackage2*)ThreadContext.AsyncPackage;
	AsyncPackage2->AddConstructedObject(Object, bSubObjectThatAlreadyExists);
}

/*-----------------------------------------------------------------------------
	FAsyncPackage implementation.
-----------------------------------------------------------------------------*/

/**
* Constructor
*/
FAsyncPackage2::FAsyncPackage2(
	const FAsyncPackageDesc2& InDesc,
	const FAsyncPackageData& InData,
	FAsyncLoadingThread2& InAsyncLoadingThread,
	FAsyncLoadEventGraphAllocator& InGraphAllocator,
	const FAsyncLoadEventSpec* EventSpecs)
: Desc(InDesc)
, Data(InData)
, AsyncLoadingThread(InAsyncLoadingThread)
, GraphAllocator(InGraphAllocator)
, ImportStore(AsyncLoadingThread.GlobalPackageStore, Desc)
{
	TRACE_LOADTIME_NEW_ASYNC_PACKAGE(this, Desc.DiskPackageName);
	AddRequestID(Desc.RequestID);

	ExportBundlesSize = Desc.StoreEntry->ExportBundlesSize;
	LoadOrder = Desc.StoreEntry->LoadOrder;

	ConstructedObjects.Reserve(Data.ExportCount + 1); // +1 for UPackage, may grow dynamically beoynd that

	for (FExportObject& Export : Data.Exports)
	{
		Export = FExportObject();
	}

	CreateNodes(EventSpecs);
}

void FAsyncPackage2::CreateNodes(const FAsyncLoadEventSpec* EventSpecs)
{
	const int32 BarrierCount = 1;
	for (int32 Phase = 0; Phase < EEventLoadNode2::Package_NumPhases; ++Phase)
	{
		new (&Data.PackageNodes[Phase]) FEventLoadNode2(EventSpecs + Phase, this, -1, BarrierCount);
	}

	for (int32 ExportBundleIndex = 0; ExportBundleIndex < Data.ExportBundleCount; ++ExportBundleIndex)
	{
		uint32 NodeIndex = EEventLoadNode2::ExportBundle_NumPhases * ExportBundleIndex;
		for (int32 Phase = 0; Phase < EEventLoadNode2::ExportBundle_NumPhases; ++Phase)
		{
			new (&Data.ExportBundleNodes[NodeIndex + Phase]) FEventLoadNode2(EventSpecs + EEventLoadNode2::Package_NumPhases + Phase, this, ExportBundleIndex, BarrierCount);
		}
	}
}

FAsyncPackage2::~FAsyncPackage2()
{
	TRACE_LOADTIME_DESTROY_ASYNC_PACKAGE(this);
	UE_ASYNC_PACKAGE_LOG(Verbose, Desc, TEXT("AsyncThread: Deleted"), TEXT("Package deleted."));

	checkf(RefCount == 0, TEXT("RefCount is not 0 when deleting package %s"),
		*Desc.DiskPackageName.ToString());

	checkf(RequestIDs.Num() == 0, TEXT("MarkRequestIDsAsComplete() has not been called for package %s"),
		*Desc.DiskPackageName.ToString());
	
	checkf(ConstructedObjects.Num() == 0, TEXT("ClearConstructedObjects() has not been called for package %s"),
		*Desc.DiskPackageName.ToString());
}

void FAsyncPackage2::ReleaseRef()
{
	check(RefCount > 0);
	if (--RefCount == 0)
	{
		FAsyncLoadingThread2& AsyncLoadingThreadLocal = AsyncLoadingThread;
		AsyncLoadingThreadLocal.DeferredDeletePackages.Enqueue(this);
		AsyncLoadingThreadLocal.AltZenaphore.NotifyOne();
	}
}

void FAsyncPackage2::ClearImportedPackages()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ClearImportedPackages);
	for (FAsyncPackage2* ImportedAsyncPackage : Data.ImportedAsyncPackages)
	{
		ImportedAsyncPackage->ReleaseRef();
	}
	Data.ImportedAsyncPackages = MakeArrayView(Data.ImportedAsyncPackages.GetData(), 0);
}

void FAsyncPackage2::ClearConstructedObjects()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(ClearConstructedObjects);

	for (UObject* Object : ConstructedObjects)
	{
		if (Object->HasAnyFlags(RF_WasLoaded))
		{
			// exports and the upackage itself are are handled below
			continue;
		}
		Object->AtomicallyClearInternalFlags(EInternalObjectFlags::AsyncLoading | EInternalObjectFlags::Async);
	}
	ConstructedObjects.Empty();

	// the async flag of all GC'able public export objects in non-temp packages are handled by FGlobalImportStore::ClearAsyncFlags
	const bool bShouldClearAsyncFlagForPublicExports = GUObjectArray.IsDisregardForGC(LinkerRoot) || !Desc.CanBeImported();

	for (FExportObject& Export : Data.Exports)
	{
		if (Export.bFiltered | Export.bExportLoadFailed)
		{
			continue;
		}

		UObject* Object = Export.Object;
		check(Object);
		checkf(Object->HasAnyFlags(RF_WasLoaded), TEXT("%s"), *Object->GetFullName());
		checkf(Object->HasAnyInternalFlags(EInternalObjectFlags::Async), TEXT("%s"), *Object->GetFullName());
		if (bShouldClearAsyncFlagForPublicExports || !Object->HasAnyFlags(RF_Public))
		{
			Object->AtomicallyClearInternalFlags(EInternalObjectFlags::AsyncLoading | EInternalObjectFlags::Async);
		}
		else
		{
			Object->AtomicallyClearInternalFlags(EInternalObjectFlags::AsyncLoading);
		}
	}

	if (LinkerRoot)
	{
		if (bShouldClearAsyncFlagForPublicExports)
		{
			LinkerRoot->AtomicallyClearInternalFlags(EInternalObjectFlags::AsyncLoading | EInternalObjectFlags::Async);
		}
		else
		{
			LinkerRoot->AtomicallyClearInternalFlags(EInternalObjectFlags::AsyncLoading);
		}
	}
}

void FAsyncPackage2::AddRequestID(int32 Id)
{
	if (Id > 0)
	{
		if (Desc.RequestID == INDEX_NONE)
		{
			// For debug readability
			Desc.RequestID = Id;
		}
		RequestIDs.Add(Id);
		AsyncLoadingThread.AddPendingRequest(Id);
		TRACE_LOADTIME_ASYNC_PACKAGE_REQUEST_ASSOCIATION(this, Id);
	}
}

void FAsyncPackage2::MarkRequestIDsAsComplete()
{
	AsyncLoadingThread.RemovePendingRequests(RequestIDs);
	RequestIDs.Reset();
}

/**
 * @return Time load begun. This is NOT the time the load was requested in the case of other pending requests.
 */
double FAsyncPackage2::GetLoadStartTime() const
{
	return LoadStartTime;
}

#if WITH_IOSTORE_IN_EDITOR
void FAsyncPackage2::GetLoadedAssets(TArray<FWeakObjectPtr>& AssetList)
{
}
#endif

/**
 * Begin async loading process. Simulates parts of BeginLoad.
 *
 * Objects created during BeginAsyncLoad and EndAsyncLoad will have EInternalObjectFlags::AsyncLoading set
 */
void FAsyncPackage2::BeginAsyncLoad()
{
	if (IsInGameThread())
	{
		AsyncLoadingThread.EnterAsyncLoadingTick();
	}

	// this won't do much during async loading except increase the load count which causes IsLoading to return true
	FUObjectSerializeContext* LoadContext = GetSerializeContext();
	BeginLoad(LoadContext);
}

/**
 * End async loading process. Simulates parts of EndLoad(). 
 */
void FAsyncPackage2::EndAsyncLoad()
{
	check(AsyncLoadingThread.IsAsyncLoadingPackages());

	// this won't do much during async loading except decrease the load count which causes IsLoading to return false
	FUObjectSerializeContext* LoadContext = GetSerializeContext();
	EndLoad(LoadContext);

	if (IsInGameThread())
	{
		AsyncLoadingThread.LeaveAsyncLoadingTick();
	}
}

void FAsyncPackage2::CreateUPackage(const FPackageSummary* PackageSummary)
{
	check(!LinkerRoot);

	// temp packages are never stored or found in loaded package store
	FLoadedPackageRef* PackageRef = nullptr;

	// Try to find existing package or create it if not already present.
	UPackage* ExistingPackage = nullptr;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPackageFind);
		if (Desc.CanBeImported())
		{
			PackageRef = ImportStore.GlobalPackageStore.LoadedPackageStore.FindPackageRef(Desc.DiskPackageId);
			UE_ASYNC_PACKAGE_CLOG(!PackageRef, Fatal, Desc, TEXT("CreateUPackage"), TEXT("Package has been destroyed by GC."));
			LinkerRoot = PackageRef->GetPackage();
#if DO_CHECK
			if (LinkerRoot)
			{
				UPackage* FoundPackage = FindObjectFast<UPackage>(nullptr, Desc.GetUPackageName());
				checkf(LinkerRoot == FoundPackage,
					TEXT("LinkerRoot '%s' (%p) is different from FoundPackage '%s' (%p)"),
					*LinkerRoot->GetName(), LinkerRoot, *FoundPackage->GetName(), FoundPackage);
			}
#endif
		}
		if (!LinkerRoot)
		{
			// Packages can be created outside the loader, i.e from ResolveName via StaticLoadObject
			ExistingPackage = FindObjectFast<UPackage>(nullptr, Desc.GetUPackageName());
		}
	}
	if (!LinkerRoot)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPackageCreate);
		if (ExistingPackage)
		{
			LinkerRoot = ExistingPackage;
		}
		else 
		{
			LinkerRoot = NewObject<UPackage>(/*Outer*/nullptr, Desc.GetUPackageName());
			bCreatedLinkerRoot = true;
		}
		LinkerRoot->SetFlags(RF_Public | RF_WasLoaded);
		LinkerRoot->FileName = Desc.DiskPackageName;
		LinkerRoot->SetCanBeImportedFlag(Desc.CanBeImported());
		LinkerRoot->SetPackageId(Desc.DiskPackageId);
		LinkerRoot->SetPackageFlagsTo(PackageSummary->PackageFlags | PKG_Cooked);
		LinkerRoot->LinkerPackageVersion = GPackageFileUE4Version;
		LinkerRoot->LinkerLicenseeVersion = GPackageFileLicenseeUE4Version;
		// LinkerRoot->LinkerCustomVersion = PackageSummaryVersions; // only if (!bCustomVersionIsLatest)
#if WITH_IOSTORE_IN_EDITOR
		LinkerRoot->bIsCookedForEditor = !!(PackageSummary->PackageFlags & PKG_FilterEditorOnly);
#endif
		if (PackageRef)
		{
			PackageRef->SetPackage(LinkerRoot);
		}
	}
	else
	{
		check(LinkerRoot->CanBeImported() == Desc.CanBeImported());
		check(LinkerRoot->GetPackageId() == Desc.DiskPackageId);
		check(LinkerRoot->GetPackageFlags() == (PackageSummary->PackageFlags | PKG_Cooked));
		check(LinkerRoot->LinkerPackageVersion == GPackageFileUE4Version);
		check(LinkerRoot->LinkerLicenseeVersion == GPackageFileLicenseeUE4Version);
		check(LinkerRoot->HasAnyFlags(RF_WasLoaded));
	}

	PinObjectForGC(LinkerRoot, bCreatedLinkerRoot);

	if (bCreatedLinkerRoot)
	{
		UE_ASYNC_PACKAGE_LOG_VERBOSE(VeryVerbose, Desc, TEXT("CreateUPackage: AddPackage"),
			TEXT("New UPackage created."));
	}
	else
	{
		UE_ASYNC_PACKAGE_LOG_VERBOSE(VeryVerbose, Desc, TEXT("CreateUPackage: UpdatePackage"),
			TEXT("Existing UPackage updated."));
	}
}

EAsyncPackageState::Type FAsyncPackage2::ProcessExternalReads(EExternalReadAction Action)
{
	check(AsyncPackageLoadingState == EAsyncPackageLoadingState2::WaitingForExternalReads);
	double WaitTime;
	if (Action == ExternalReadAction_Poll)
	{
		WaitTime = -1.f;
	}
	else// if (Action == ExternalReadAction_Wait)
	{
		WaitTime = 0.f;
	}

	while (ExternalReadIndex < ExternalReadDependencies.Num())
	{
		FExternalReadCallback& ReadCallback = ExternalReadDependencies[ExternalReadIndex];
		if (!ReadCallback(WaitTime))
		{
			return EAsyncPackageState::TimeOut;
		}
		++ExternalReadIndex;
	}

	ExternalReadDependencies.Empty();
	AsyncPackageLoadingState = EAsyncPackageLoadingState2::ExportsDone;
	GetPackageNode(Package_ExportsSerialized).ReleaseBarrier();
	return EAsyncPackageState::Complete;
}

EAsyncPackageState::Type FAsyncPackage2::CreateClusters(FAsyncLoadingThreadState2& ThreadState)
{
	while (DeferredClusterIndex < Data.ExportCount && !ThreadState.IsTimeLimitExceeded(TEXT("CreateClusters")))
	{
		const FExportObject& Export = Data.Exports[DeferredClusterIndex++];

		if (!(Export.bFiltered | Export.bExportLoadFailed) && Export.Object->CanBeClusterRoot())
		{
			Export.Object->CreateCluster();
		}
	}

	return DeferredClusterIndex == Data.ExportCount ? EAsyncPackageState::Complete : EAsyncPackageState::TimeOut;
}

void FAsyncPackage2::FinishUPackage()
{
	if (LinkerRoot)
	{
		if (!bLoadHasFailed)
		{
			// Mark package as having been fully loaded and update load time.
			LinkerRoot->MarkAsFullyLoaded();
			LinkerRoot->SetLoadTime(FPlatformTime::Seconds() - LoadStartTime);
		}
		else
		{
			// Clean up UPackage so it can't be found later
			if (bCreatedLinkerRoot && !LinkerRoot->IsRooted())
			{
				LinkerRoot->ClearFlags(RF_NeedPostLoad | RF_NeedLoad | RF_NeedPostLoadSubobjects);
				LinkerRoot->MarkPendingKill();
				LinkerRoot->Rename(*MakeUniqueObjectName(GetTransientPackage(), UPackage::StaticClass()).ToString(), nullptr, REN_DontCreateRedirectors | REN_DoNotDirty | REN_ForceNoResetLoaders | REN_NonTransactional);
			}
		}
	}
}

void FAsyncPackage2::CallCompletionCallbacks(EAsyncLoadingResult::Type LoadingResult)
{
	checkSlow(!IsInAsyncLoadingThread());

	UPackage* LoadedPackage = (!bLoadHasFailed) ? LinkerRoot : nullptr;
	for (FCompletionCallback& CompletionCallback : CompletionCallbacks)
	{
		CompletionCallback->ExecuteIfBound(Desc.GetUPackageName(), LoadedPackage, LoadingResult);
	}
	CompletionCallbacks.Empty();
}

UPackage* FAsyncPackage2::GetLoadedPackage()
{
	UPackage* LoadedPackage = (!bLoadHasFailed) ? LinkerRoot : nullptr;
	return LoadedPackage;
}

void FAsyncPackage2::Cancel()
{
	// Call any completion callbacks specified.
	bLoadHasFailed = true;
	const EAsyncLoadingResult::Type Result = EAsyncLoadingResult::Canceled;
	CallCompletionCallbacks(Result);

	if (LinkerRoot)
	{
		if (bCreatedLinkerRoot)
		{
			LinkerRoot->ClearFlags(RF_WasLoaded);
			LinkerRoot->bHasBeenFullyLoaded = false;
			LinkerRoot->Rename(*MakeUniqueObjectName(GetTransientPackage(), UPackage::StaticClass()).ToString(), nullptr, REN_DontCreateRedirectors | REN_DoNotDirty | REN_ForceNoResetLoaders | REN_NonTransactional);
		}
	}
}

void FAsyncPackage2::AddCompletionCallback(TUniquePtr<FLoadPackageAsyncDelegate>&& Callback)
{
	// This is to ensure that there is no one trying to subscribe to a already loaded package
	//check(!bLoadHasFinished && !bLoadHasFailed);
	CompletionCallbacks.Emplace(MoveTemp(Callback));
}

int32 FAsyncLoadingThread2::LoadPackage(const FString& InName, const FGuid* InGuid, const TCHAR* InPackageToLoadFrom, FLoadPackageAsyncDelegate InCompletionDelegate, EPackageFlags InPackageFlags, int32 InPIEInstanceID, int32 InPackagePriority, const FLinkerInstancingContext*)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(LoadPackage);

	LazyInitializeFromLoadPackage();

	int32 RequestID = INDEX_NONE;

	// happy path where all inputs are actual package names
	const FName Name = FName(*InName);
	FName DiskPackageName = InPackageToLoadFrom ? FName(InPackageToLoadFrom) : Name;
	bool bHasCustomPackageName = Name != DiskPackageName;

	// Verify PackageToLoadName, or fixup to handle any input string that can be converted to a long package name.
	FPackageId DiskPackageId = FPackageId::FromName(DiskPackageName);
	const FPackageStoreEntry* StoreEntry = GlobalPackageStore.FindStoreEntry(DiskPackageId);
	if (!StoreEntry)
	{
		FString PackageNameStr = DiskPackageName.ToString();
		if (!FPackageName::IsValidLongPackageName(PackageNameStr))
		{
			FString NewPackageNameStr;
			if (FPackageName::TryConvertFilenameToLongPackageName(PackageNameStr, NewPackageNameStr))
			{
				DiskPackageName = *NewPackageNameStr;
				DiskPackageId = FPackageId::FromName(DiskPackageName);
				StoreEntry = GlobalPackageStore.FindStoreEntry(DiskPackageId);
				bHasCustomPackageName &= Name != DiskPackageName;
			}
		}
	}

	// Verify CustomPackageName, or fixup to handle any input string that can be converted to a long package name.
	// CustomPackageName must not be an existing disk package name,
	// that could cause missing or incorrect import objects for other packages.
	FName CustomPackageName;
	FPackageId CustomPackageId;
	if (bHasCustomPackageName)
	{
		FPackageId PackageId = FPackageId::FromName(Name);
		if (!GlobalPackageStore.FindStoreEntry(PackageId))
		{
			FString PackageNameStr = Name.ToString();
			if (FPackageName::IsValidLongPackageName(PackageNameStr))
			{
				CustomPackageName = Name;
				CustomPackageId = PackageId;
			}
			else
			{
				FString NewPackageNameStr;
				if (FPackageName::TryConvertFilenameToLongPackageName(PackageNameStr, NewPackageNameStr))
				{
					PackageId = FPackageId::FromName(FName(*NewPackageNameStr));
					if (!GlobalPackageStore.FindStoreEntry(PackageId))
					{
						CustomPackageName = *NewPackageNameStr;
						CustomPackageId = PackageId;
					}
				}
			}
		}
	}
	// When explicitly requesting a redirected package then set CustomName to
	// the redirected name, otherwise the UPackage name will be set to the base game name.
	else if (GlobalPackageStore.IsRedirect(DiskPackageId))
	{
		bHasCustomPackageName = true;
		CustomPackageName = DiskPackageName;
		CustomPackageId = DiskPackageId;
	}

	check(CustomPackageId.IsValid() == !CustomPackageName.IsNone());

	bool bCustomNameIsValid = (!bHasCustomPackageName && CustomPackageName.IsNone()) || (bHasCustomPackageName && !CustomPackageName.IsNone());
	bool bDiskPackageIdIsValid = !!StoreEntry;
	if (!bDiskPackageIdIsValid)
	{
		// While there is an active load request for (InName=/Temp/PackageABC_abc, InPackageToLoadFrom=/Game/PackageABC), then allow these requests too:
		// (InName=/Temp/PackageA_abc, InPackageToLoadFrom=/Temp/PackageABC_abc) and (InName=/Temp/PackageABC_xyz, InPackageToLoadFrom=/Temp/PackageABC_abc)
		FAsyncPackage2* Package = GetAsyncPackage(DiskPackageId);
		if (Package)
		{
			if (CustomPackageName.IsNone())
			{
				CustomPackageName = Package->Desc.CustomPackageName;
				CustomPackageId = Package->Desc.CustomPackageId;
				bHasCustomPackageName = bCustomNameIsValid = true;
			}
			DiskPackageName = Package->Desc.DiskPackageName;
			DiskPackageId = Package->Desc.DiskPackageId;
			StoreEntry = Package->Desc.StoreEntry;
			bDiskPackageIdIsValid = true;
		}
	}

	if (bDiskPackageIdIsValid && bCustomNameIsValid)
	{
		if (FCoreDelegates::OnAsyncLoadPackage.IsBound())
		{
			FCoreDelegates::OnAsyncLoadPackage.Broadcast(InName);
		}

		// Generate new request ID and add it immediately to the global request list (it needs to be there before we exit
		// this function, otherwise it would be added when the packages are being processed on the async thread).
		RequestID = IAsyncPackageLoader::GetNextRequestId();
		TRACE_LOADTIME_BEGIN_REQUEST(RequestID);
		AddPendingRequest(RequestID);

		// Allocate delegate on Game Thread, it is not safe to copy delegates by value on other threads
		TUniquePtr<FLoadPackageAsyncDelegate> CompletionDelegatePtr;
		if (InCompletionDelegate.IsBound())
		{
			CompletionDelegatePtr.Reset(new FLoadPackageAsyncDelegate(InCompletionDelegate));
		}

#if !UE_BUILD_SHIPPING
		if (FileOpenLogWrapper)
		{
			FileOpenLogWrapper->AddPackageToOpenLog(*DiskPackageName.ToString());
		}
#endif

		// Add new package request
		FAsyncPackageDesc2 PackageDesc(RequestID, InPackagePriority, DiskPackageId, StoreEntry, DiskPackageName, CustomPackageId, CustomPackageName, MoveTemp(CompletionDelegatePtr));

		// Fixup for redirected packages since the slim StoreEntry itself has been stripped from both package names and package ids
		FPackageId RedirectedDiskPackageId = GlobalPackageStore.GetRedirectedPackageId(DiskPackageId);
		if (RedirectedDiskPackageId.IsValid())
		{
			PackageDesc.DiskPackageId = RedirectedDiskPackageId;
			PackageDesc.SourcePackageName = PackageDesc.DiskPackageName;
			PackageDesc.DiskPackageName = FName();
		}

		QueuePackage(PackageDesc);

		UE_ASYNC_PACKAGE_LOG(Verbose, PackageDesc, TEXT("LoadPackage: QueuePackage"), TEXT("Package added to pending queue."));
	}
	else
	{
		static TSet<FName> SkippedPackages;
		bool bIsAlreadySkipped = false;
		if (!StoreEntry)
		{
			SkippedPackages.Add(DiskPackageName, &bIsAlreadySkipped);
			if (!bIsAlreadySkipped)
			{
				UE_LOG(LogStreaming, Warning,
					TEXT("LoadPackage: SkipPackage: %s (0x%llX) - The package to load does not exist on disk or in the loader"),
					*DiskPackageName.ToString(), FPackageId::FromName(DiskPackageName).ValueForDebugging());
			}
		}
		else
		{
			SkippedPackages.Add(Name, &bIsAlreadySkipped);
			if (!bIsAlreadySkipped)
			{
				UE_LOG(LogStreaming, Warning,
					TEXT("LoadPackage: SkipPackage: %s (0x%llX) - The package name is invalid"),
					*Name.ToString(), FPackageId::FromName(Name).ValueForDebugging());
			}
		}

		if (InCompletionDelegate.IsBound())
		{
			// Queue completion callback and execute at next process loaded packages call to maintain behavior compatibility with old loader
			FQueuedFailedPackageCallback& QueuedFailedPackageCallback = QueuedFailedPackageCallbacks.AddDefaulted_GetRef();
			QueuedFailedPackageCallback.PackageName = Name;
			QueuedFailedPackageCallback.Callback.Reset(new FLoadPackageAsyncDelegate(InCompletionDelegate));
		}
	}

	return RequestID;
}

EAsyncPackageState::Type FAsyncLoadingThread2::ProcessLoadingFromGameThread(FAsyncLoadingThreadState2& ThreadState, bool bUseTimeLimit, bool bUseFullTimeLimit, float TimeLimit)
{
	SCOPE_CYCLE_COUNTER(STAT_AsyncLoadingTime);
	CSV_SCOPED_TIMING_STAT_EXCLUSIVE(AsyncLoading);

	// CSV_CUSTOM_STAT(FileIO, EDLEventQueueDepth, (int32)GraphAllocator.TotalNodeCount, ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(FileIO, QueuedPackagesQueueDepth, GetNumQueuedPackages(), ECsvCustomStatOp::Set);
	CSV_CUSTOM_STAT(FileIO, ExistingQueuedPackagesQueueDepth, GetNumAsyncPackages(), ECsvCustomStatOp::Set);

	TickAsyncLoadingFromGameThread(ThreadState, bUseTimeLimit, bUseFullTimeLimit, TimeLimit);
	return IsAsyncLoading() ? EAsyncPackageState::TimeOut : EAsyncPackageState::Complete;
}

void FAsyncLoadingThread2::FlushLoading(int32 RequestId)
{
	if (IsAsyncLoadingPackages())
	{
		// Flushing async loading while loading is suspend will result in infinite stall
		UE_CLOG(bSuspendRequested, LogStreaming, Fatal, TEXT("Cannot Flush Async Loading while async loading is suspended"));

		SCOPE_CYCLE_COUNTER(STAT_FAsyncPackage_FlushAsyncLoadingGameThread);

		if (RequestId != INDEX_NONE && !ContainsRequestID(RequestId))
		{
			return;
		}

		FCoreDelegates::OnAsyncLoadingFlush.Broadcast();

		double StartTime = FPlatformTime::Seconds();

		// Flush async loaders by not using a time limit. Needed for e.g. garbage collection.
		{
			FAsyncLoadingThreadState2& ThreadState = *FAsyncLoadingThreadState2::Get();
			while (IsAsyncLoadingPackages())
			{
				EAsyncPackageState::Type Result = TickAsyncLoadingFromGameThread(ThreadState, false, false, 0, RequestId);
				if (RequestId != INDEX_NONE && !ContainsRequestID(RequestId))
				{
					break;
				}

				if (IsMultithreaded())
				{
					// Update the heartbeat and sleep. If we're not multithreading, the heartbeat is updated after each package has been processed
					FThreadHeartBeat::Get().HeartBeat();
					FPlatformProcess::SleepNoStats(0.0001f);
				}

				// push stats so that we don't overflow number of tags per thread during blocking loading
				LLM_PUSH_STATS_FOR_ASSET_TAGS();
			}
		}

		double EndTime = FPlatformTime::Seconds();
		double ElapsedTime = EndTime - StartTime;

		check(RequestId != INDEX_NONE || !IsAsyncLoading());
	}
}

EAsyncPackageState::Type FAsyncLoadingThread2::ProcessLoadingUntilCompleteFromGameThread(FAsyncLoadingThreadState2& ThreadState, TFunctionRef<bool()> CompletionPredicate, float TimeLimit)
{
	if (!IsAsyncLoadingPackages())
	{
		return EAsyncPackageState::Complete;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(ProcessLoadingUntilComplete);
	SCOPE_CYCLE_COUNTER(STAT_FAsyncPackage_FlushAsyncLoadingGameThread);

	// Flushing async loading while loading is suspend will result in infinite stall
	UE_CLOG(bSuspendRequested, LogStreaming, Fatal, TEXT("Cannot Flush Async Loading while async loading is suspended"));

	if (TimeLimit <= 0.0f)
	{
		// Set to one hour if no time limit
		TimeLimit = 60 * 60;
	}

	double TimeLoadingPackage = 0.0f;

	while (IsAsyncLoadingPackages() && TimeLimit > 0 && !CompletionPredicate())
	{
		double TickStartTime = FPlatformTime::Seconds();
		if (ProcessLoadingFromGameThread(ThreadState, true, true, TimeLimit) == EAsyncPackageState::Complete)
		{
			return EAsyncPackageState::Complete;
		}

		if (IsMultithreaded())
		{
			// Update the heartbeat and sleep. If we're not multithreading, the heartbeat is updated after each package has been processed
			// only update the heartbeat up to the limit of the hang detector to ensure if we get stuck in this loop that the hang detector gets a chance to trigger
			if (TimeLoadingPackage < FThreadHeartBeat::Get().GetHangDuration())
			{
				FThreadHeartBeat::Get().HeartBeat();
			}
			FPlatformProcess::SleepNoStats(0.0001f);
		}

		double TimeDelta = (FPlatformTime::Seconds() - TickStartTime);
		TimeLimit -= TimeDelta;
		TimeLoadingPackage += TimeDelta;
	}

	return TimeLimit <= 0 ? EAsyncPackageState::TimeOut : EAsyncPackageState::Complete;
}

IAsyncPackageLoader* MakeAsyncPackageLoader2(FIoDispatcher& InIoDispatcher)
{
	return new FAsyncLoadingThread2(InIoDispatcher);
}

#endif //WITH_ASYNCLOADING2

#if UE_BUILD_DEVELOPMENT || UE_BUILD_DEBUG
PRAGMA_ENABLE_OPTIMIZATION
#endif
