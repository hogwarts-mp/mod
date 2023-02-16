// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/Map.h"
#include "Containers/Set.h"
#include "Containers/StringFwd.h"
#include "HAL/ThreadSingleton.h"
#include "ProfilingDebugging/CookStats.h"
#include "Serialization/ArchiveObjectCrc32.h"
#include "Serialization/ArchiveStackTrace.h"
#include "Serialization/FileRegions.h"
#include "UObject/NameTypes.h"
#include "UObject/UObjectMarks.h"

// This file contains private utilities shared by UPackage::Save and UPackage::Save2 

class FMD5;
class FSavePackageContext;
template<typename StateType> class TAsyncWorkSequence;

DECLARE_LOG_CATEGORY_EXTERN(LogSavePackage, Log, All);

struct FLargeMemoryDelete
{
	void operator()(uint8* Ptr) const
	{
		if (Ptr)
		{
			FMemory::Free(Ptr);
		}
	}
};
typedef TUniquePtr<uint8, FLargeMemoryDelete> FLargeMemoryPtr;

enum class EAsyncWriteOptions
{
	None = 0,
	WriteFileToDisk = 0x01,
	ComputeHash = 0x02
};
ENUM_CLASS_FLAGS(EAsyncWriteOptions)

struct FScopedSavingFlag
{
	FScopedSavingFlag(bool InSavingConcurrent);
	~FScopedSavingFlag();

	bool bSavingConcurrent;
};

struct FSavePackageDiffSettings
{
	int32 MaxDiffsToLog;
	bool bIgnoreHeaderDiffs;
	bool bSaveForDiff;
	FSavePackageDiffSettings(bool bDiffing);
};

struct FCanSkipEditorReferencedPackagesWhenCooking
{
	bool bCanSkipEditorReferencedPackagesWhenCooking;
	FCanSkipEditorReferencedPackagesWhenCooking();
	FORCEINLINE operator bool() const { return bCanSkipEditorReferencedPackagesWhenCooking; }
};

/**
 * Helper structure to encapsulate sorting a linker's export table alphabetically, taking into account conforming to other linkers.
 * @note Save2 should not have to use this sorting long term
 */
struct FObjectExportSortHelper
{
private:
	struct FObjectFullName
	{
	public:
		FObjectFullName(const UObject* Object, const UObject* Root);
		FObjectFullName(FObjectFullName&& InFullName);

		FName ClassName;
		TArray<FName> Path;
	};

public:
	FObjectExportSortHelper() : bUseFObjectFullName(false) {}

	/**
	 * Sorts exports alphabetically.  If a package is specified to be conformed against, ensures that the order
	 * of the exports match the order in which the corresponding exports occur in the old package.
	 *
	 * @param	Linker				linker containing the exports that need to be sorted
	 * @param	LinkerToConformTo	optional linker to conform against.
	 */
	void SortExports(FLinkerSave* Linker, FLinkerLoad* LinkerToConformTo = nullptr, bool InbUseFObjectFullName = false);

private:
	/** Comparison function used by Sort */
	bool operator()(const FObjectExport& A, const FObjectExport& B) const;

	/** the linker that we're sorting exports for */
	friend struct TDereferenceWrapper<FObjectExport, FObjectExportSortHelper>;

	bool bUseFObjectFullName;

	TMap<UObject*, FObjectFullName> ObjectToObjectFullNameMap;

	/**
	 * Map of UObject => full name; optimization for sorting.
	 */
	TMap<UObject*, FString>			ObjectToFullNameMap;
};

/**
 * Helper struct used during cooking to validate EDL dependencies
 */
struct FEDLCookChecker : public TThreadSingleton<FEDLCookChecker>
{
	void SetActiveIfNeeded();

	void Reset();

	void AddImport(UObject* Import, UPackage* ImportingPackage);
	void AddExport(UObject* Export);
	void AddArc(UObject* DepObject, bool bDepIsSerialize, UObject* Export, bool bExportIsSerialize);

	static void StartSavingEDLCookInfoForVerification();
	static void Verify(bool bFullReferencesExpected);

private:
	typedef uint32 FEDLNodeID;
	static const FEDLNodeID NodeIDInvalid = static_cast<FEDLNodeID>(-1);

	struct FEDLNodeData;
public: // FEDLNodeHash is public only so that GetTypeHash can be defined
	enum class EObjectEvent : uint8
	{
		Create,
		Serialize
	};

	/**
	 * Wrapper aroundan FEDLNodeData (or around a UObject when searching for an FEDLNodeData corresponding to the UObject)
	 * that provides the hash-by-objectpath to lookup the FEDLNodeData for an objectpath.
	 */
	struct FEDLNodeHash
	{
		FEDLNodeHash(); // creates an uninitialized node; only use this to provide as an out parameter
		FEDLNodeHash(const TArray<FEDLNodeData>* InNodes, FEDLNodeID InNodeID, EObjectEvent InObjectEvent);
		FEDLNodeHash(const UObject* InObject, EObjectEvent InObjectEvent);
		bool operator==(const FEDLNodeHash& Other) const;
		friend uint32 GetTypeHash(const FEDLNodeHash& A);

		FName GetName() const;
		bool TryGetParent(FEDLNodeHash& Parent) const;
		EObjectEvent GetObjectEvent() const;
		void SetNodes(const TArray<FEDLNodeData>* InNodes);

	private:
		static FName ObjectNameFirst(const FEDLNodeHash& InNode, uint32& OutNodeID, const UObject*& OutObject);
		static FName ObjectNameNext(const FEDLNodeHash& InNode, uint32& OutNodeID, const UObject*& OutObject);
			
		union
		{
			/**
			 * The array of nodes from the FEDLCookChecker; this is how we lookup the node for the FEDLNodeData.
			 * Because the FEDLNodeData are elements in an array which can resize and therefore reallocate the nodes, we cannot store the pointer to the node.
			 * Only used if bIsNode is true.
			 */
			const TArray<FEDLNodeData>* Nodes;
			/** Pointer to the Object we are looking up, if this hash was created during lookup-by-objectpath for an object */
			const UObject* Object;
		};
		/** The identifier for the FEDLNodeData this hash is wrapping. Only used if bIsNode is true. */
		FEDLNodeID NodeID;
		/** True if this hash is wrapping an FEDLNodeData, false if it is wrapping a UObject. */
		bool bIsNode;
		EObjectEvent ObjectEvent;
	};

private:

	/**
	 * Node representing either the Create event or Serialize event of a UObject in the graph of runtime dependencies between UObjects.
	 */
	struct FEDLNodeData
	{
		// Note that order of the variables is important to reduce alignment waste in the size of FEDLNodeData.
		/** Name of the UObject represented by this node; full objectpath name is obtainable by combining parent data with the name. */
		FName Name;
		/** Index of this node in the FEDLCookChecker's Nodes array. This index is used to provide a small-memory-usage identifier for the node. */
		FEDLNodeID ID;
		/**
		 * Tracks references to this node's UObjects from other packages (which is the reverse of the references from each node that we track in NodePrereqs.)
		 * We only need this information from each package, so we track by package name instead of node id.
		 */
		TArray<FName> ImportingPackagesSorted;
		/**
		 * ID of the node representing the UObject parent of this node's UObject. NodeIDInvalid if the UObject has no parent.
		 * The ParentID always refers to the node for the Create event of the parent UObject.
		 */
		uint32 ParentID;
		/** True if this node represents the Serialize event on the UObject, false if it represents the Create event. */
		EObjectEvent ObjectEvent;
		/** True if the UObject represented by this node has been exported by a SavePackage call; used to verify that the imports requested by packages are present somewhere in the cook. */
		bool bIsExport;

		FEDLNodeData(FEDLNodeID InID, FEDLNodeID InParentID, FName InName, EObjectEvent InObjectEvent);
		FEDLNodeData(FEDLNodeID InID, FEDLNodeID InParentID, FName InName, FEDLNodeData&& Other);
		FEDLNodeHash GetNodeHash(const FEDLCookChecker& Owner) const;

		FString ToString(const FEDLCookChecker& Owner) const;
		void AppendPathName(const FEDLCookChecker& Owner, FStringBuilderBase& Result) const;
		void Merge(FEDLNodeData&& Other);
	};

	enum class EInternalConstruct
	{
		Type
	};

	FEDLCookChecker();
	FEDLCookChecker(EInternalConstruct);

	FEDLNodeID FindOrAddNode(const FEDLNodeHash& NodeLookup);
	FEDLNodeID FindOrAddNode(FEDLNodeData&& NodeData, const FEDLCookChecker& OldOwnerOfNode, FEDLNodeID ParentIDInThis, bool& bNew);
	FEDLNodeID FindNode(const FEDLNodeHash& NodeHash);
	void Merge(FEDLCookChecker&& Other);
	bool CheckForCyclesInner(TSet<FEDLNodeID>& Visited, TSet<FEDLNodeID>& Stack, const FEDLNodeID& Visit, FEDLNodeID& FailNode);
	void AddDependency(FEDLNodeID SourceID, FEDLNodeID TargetID);

	/**
	 * All the FEDLNodeDatas that have been created for this checker. These are allocated as elements of an array rather than pointers to reduce cputime and
	 * memory due to many small allocations, and to provide index-based identifiers. Nodes are not deleted during the lifetime of the checker.
	 */
	TArray<FEDLNodeData> Nodes;
	/** A map to lookup the node for a UObject or for the corresponding node in another thread's FEDLCookChecker. */
	TMap<FEDLNodeHash, FEDLNodeID> NodeHashToNodeID;
	/** The graph of dependencies between nodes. */
	TMultiMap<FEDLNodeID, FEDLNodeID> NodePrereqs;
	/** True if the EDLCookChecker should be active; it is turned off if the runtime will not be using EDL. */
	bool bIsActive;

	/** When cooking with concurrent saving, each thread has its own FEDLCookChecker, and these are merged after the cook is complete. */
	static FCriticalSection CookCheckerInstanceCritical;
	static TArray<FEDLCookChecker*> CookCheckerInstances;

	friend TThreadSingleton<FEDLCookChecker>;
};

#if WITH_EDITORONLY_DATA

/**
 * Archive to calculate a checksum on an object's serialized data stream, but only of its non-editor properties.
 */
class FArchiveObjectCrc32NonEditorProperties : public FArchiveObjectCrc32
{
	using Super = FArchiveObjectCrc32;

public:
	FArchiveObjectCrc32NonEditorProperties()
		: EditorOnlyProp(0)
	{
	}

	virtual FString GetArchiveName() const
	{
		return TEXT("FArchiveObjectCrc32NonEditorProperties");
	}

	virtual void Serialize(void* Data, int64 Length);
private:
	int32 EditorOnlyProp;
};

#else

class COREUOBJECT_API FArchiveObjectCrc32NonEditorProperties : public FArchiveObjectCrc32
{
};

#endif

// Utility functions used by both UPackage::Save and/or UPackage::Save2
namespace SavePackageUtilities
{
	extern const FName NAME_World;
	extern const FName NAME_Level;
	extern const FName NAME_PrestreamPackage;

	void GetBlueprintNativeCodeGenReplacement(UObject* InObj, UClass*& ObjClass, UObject*& ObjOuter, FName& ObjName, const ITargetPlatform* TargetPlatform);

	void IncrementOutstandingAsyncWrites();
	void DecrementOutstandingAsyncWrites();

	void SaveThumbnails(UPackage* InOuter, FLinkerSave* Linker, FStructuredArchive::FSlot Slot);
	void SaveBulkData(FLinkerSave* Linker, const UPackage* InOuter, const TCHAR* Filename, const ITargetPlatform* TargetPlatform,
		FSavePackageContext* SavePackageContext, const bool bTextFormat, const bool bDiffing, const bool bComputeHash, TAsyncWorkSequence<FMD5>& AsyncWriteAndHashSequence, int64& TotalPackageSizeUncompressed);
	void SaveWorldLevelInfo(UPackage* InOuter, FLinkerSave* Linker, FStructuredArchive::FRecord Record);
	EObjectMark GetExcludedObjectMarksForTargetPlatform(const class ITargetPlatform* TargetPlatform);
	bool HasUnsaveableOuter(UObject* InObj, UPackage* InSavingPackage);
	void CheckObjectPriorToSave(FArchiveUObject& Ar, UObject* InObj, UPackage* InSavingPackage);
	void ConditionallyExcludeObjectForTarget(UObject* Obj, EObjectMark ExcludedObjectMarks, const ITargetPlatform* TargetPlatform);
	void FindMostLikelyCulprit(TArray<UObject*> BadObjects, UObject*& MostLikelyCulprit, const FProperty*& PropertyRef);
	void AddFileToHash(FString const& Filename, FMD5& Hash);

	void WriteToFile(const FString& Filename, const uint8* InDataPtr, int64 InDataSize);
	void AsyncWriteFile(TAsyncWorkSequence<FMD5>& AsyncWriteAndHashSequence, FLargeMemoryPtr Data, const int64 DataSize, const TCHAR* Filename, EAsyncWriteOptions Options, TArrayView<const FFileRegion> InFileRegions);
	void AsyncWriteFileWithSplitExports(TAsyncWorkSequence<FMD5>& AsyncWriteAndHashSequence, FLargeMemoryPtr Data, const int64 DataSize, const int64 HeaderSize, const TCHAR* Filename, EAsyncWriteOptions Options, TArrayView<const FFileRegion> InFileRegions);

	void GetCDOSubobjects(UObject* CDO, TArray<UObject*>& Subobjects);
}

#if ENABLE_COOK_STATS
struct FSavePackageStats
{
	static int32 NumPackagesSaved;
	static double SavePackageTimeSec;
	static double TagPackageExportsPresaveTimeSec;
	static double TagPackageExportsTimeSec;
	static double FullyLoadLoadersTimeSec;
	static double ResetLoadersTimeSec;
	static double TagPackageExportsGetObjectsWithOuter;
	static double TagPackageExportsGetObjectsWithMarks;
	static double SerializeImportsTimeSec;
	static double SortExportsSeekfreeInnerTimeSec;
	static double SerializeExportsTimeSec;
	static double SerializeBulkDataTimeSec;
	static double AsyncWriteTimeSec;
	static double MBWritten;
	static TMap<FName, FArchiveDiffStats> PackageDiffStats;
	static int32 NumberOfDifferentPackages;
	static FCookStatsManager::FAutoRegisterCallback RegisterCookStats;
	static void AddSavePackageStats(FCookStatsManager::AddStatFuncRef AddStat);
	static void MergeStats(const TMap<FName, FArchiveDiffStats>& ToMerge);
};
#endif

