// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	GarbageCollection.h: Unreal realtime garbage collection helpers
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "UObject/UObjectGlobals.h"
#include "Serialization/ArchiveUObject.h"
#include "HAL/ThreadSafeBool.h"
#include "UObject/FastReferenceCollectorOptions.h"

#if !defined(UE_WITH_GC)
#	define UE_WITH_GC	1
#endif

/** Context sensitive keep flags for garbage collection */
#define GARBAGE_COLLECTION_KEEPFLAGS	(GIsEditor ? RF_Standalone : RF_NoFlags)

#define	ENABLE_GC_DEBUG_OUTPUT					1
#define PERF_DETAILED_PER_CLASS_GC_STATS				(LOOKING_FOR_PERF_ISSUES || 0) 

/** UObject pointer checks are disabled by default in shipping and test builds as they add roughly 20% overhead to GC times */
#define ENABLE_GC_OBJECT_CHECKS (!(UE_BUILD_TEST || UE_BUILD_SHIPPING) || 0)

COREUOBJECT_API DECLARE_LOG_CATEGORY_EXTERN(LogGarbage, Warning, All);
DECLARE_STATS_GROUP(TEXT("Garbage Collection"), STATGROUP_GC, STATCAT_Advanced);

/**
 * Do extra checks on GC'd function references to catch uninitialized pointers?
 */
#define DO_POINTER_CHECKS_ON_GC WITH_EDITORONLY_DATA

/*-----------------------------------------------------------------------------
	Realtime garbage collection helper classes.
-----------------------------------------------------------------------------*/

/**
 * Enum of different supported reference type tokens.
 */
enum EGCReferenceType
{
	GCRT_None			= 0,
	GCRT_Object,
	GCRT_Class,
	GCRT_PersistentObject,
	GCRT_ExternalPackage,				// Specific reference type token for UObject external package
	GCRT_ArrayObject,
	GCRT_ArrayStruct,
	GCRT_FixedArray,
	GCRT_AddStructReferencedObjects,
	GCRT_AddReferencedObjects,
	GCRT_AddTMapReferencedObjects,
	GCRT_AddTSetReferencedObjects,
	GCRT_AddFieldPathReferencedObject,
	GCRT_ArrayAddFieldPathReferencedObject,
	GCRT_EndOfPointer,
	GCRT_EndOfStream,
	GCRT_NoopPersistentObject,
	GCRT_NoopClass,
	GCRT_ArrayObjectFreezable,
	GCRT_ArrayStructFreezable,
	GCRT_Optional,
	GCRT_WeakObject,
	GCRT_ArrayWeakObject,
	GCRT_LazyObject,
	GCRT_ArrayLazyObject,
	GCRT_SoftObject,
	GCRT_ArraySoftObject,
	GCRT_Delegate,
	GCRT_ArrayDelegate,
	GCRT_MulticastDelegate,
	GCRT_ArrayMulticastDelegate,
};

/** 
 * Convenience struct containing all necessary information for a reference.
 */
struct FGCReferenceInfo
{
	/**
	 * Constructor
	 *
	 * @param InType	type of reference
	 * @param InOffset	offset into object/ struct
	 */
	FORCEINLINE FGCReferenceInfo( EGCReferenceType InType, uint32 InOffset )
	:	ReturnCount( 0 )
	,	Type( InType )
	,	Offset( InOffset )	
	{
		check( InType != GCRT_None );
		check( (InOffset & ~0x7FFFF) == 0 );
	}
	/**
	 * Constructor
	 *
	 * @param InValue	value to set union mapping to a uint32 to
	 */
	FORCEINLINE FGCReferenceInfo( uint32 InValue )
	:	Value( InValue )
	{}
	/**
	 * uint32 conversion operator
	 *
	 * @return uint32 value of struct
	 */
	FORCEINLINE operator uint32() const 
	{ 
		return Value; 
	}

	/** Mapping to exactly one uint32 */
	union
	{
		/** Mapping to exactly one uint32 */
		struct
		{
			/** Return depth, e.g. 1 for last entry in an array, 2 for last entry in an array of structs of arrays, ... */
			uint32 ReturnCount	: 8;
			/** Type of reference */
			uint32 Type			: 5; // The number of bits needs to match TFastReferenceCollector::FStackEntry::ContainerHelperType
			/** Offset into struct/ object */
			uint32 Offset		: 19;
		};
		/** uint32 value of reference info, used for easy conversion to/ from uint32 for token array */
		uint32 Value;
	};
};

/** 
 * Convenience structure containing all necessary information for skipping a dynamic array
 */
struct FGCSkipInfo
{
	/**
	 * Default constructor
	 */
	FORCEINLINE FGCSkipInfo()
	{}

	/**
	 * Constructor
	 *
	 * @param InValue	value to set union mapping to a uint32 to
	 */
	FORCEINLINE FGCSkipInfo( uint32 InValue )
	:	Value( InValue )
	{}
	/**
	 * uint32 conversion operator
	 *
	 * @return uint32 value of struct
	 */
	FORCEINLINE operator uint32() const 
	{ 
		return Value; 
	}

	/** Mapping to exactly one uint32 */
	union
	{
		/** Mapping to exactly one uint32 */
		struct
		{
			/** Return depth not taking into account embedded arrays. This is needed to return appropriately when skipping empty dynamic arrays as we never step into them */
			uint32 InnerReturnCount	: 8;
			/** Skip index */
			uint32 SkipIndex			: 24;
		};
		/** uint32 value of skip info, used for easy conversion to/ from uint32 for token array */
		uint32 Value;
	};
};

#if ENABLE_GC_OBJECT_CHECKS
/**
 * Stores debug info about the token.
 */
struct FTokenInfo
{
	/* Token offset. */
	int32 Offset;
	/* Token debug name. */
	FName Name;
};

#endif // ENABLE_GC_OBJECT_CHECKS

/**
 * Reference token stream class. Used for creating and parsing stream of object references.
 */
struct COREUOBJECT_API FGCReferenceTokenStream
{
	/** Initialization value to ensure that we have the right skip index index */
	enum EGCArrayInfoPlaceholder { E_GCSkipIndexPlaceholder = 0xDEADBABE }; 

	/** Constructor */
	FGCReferenceTokenStream()
	{
		check( sizeof(FGCReferenceInfo) == sizeof(uint32) );
	}

	/**
	 * Shrinks the token stream, removing array slack.
	 */
	void Shrink()
	{
		Tokens.Shrink();
#if ENABLE_GC_OBJECT_CHECKS
		TokenDebugInfo.Shrink();
#endif // ENABLE_GC_OBJECT_CHECKS
	}

	/** Empties the token stream entirely */
	void Empty()
	{
		Tokens.Empty();
#if ENABLE_GC_OBJECT_CHECKS
		TokenDebugInfo.Empty();
#endif // ENABLE_GC_OBJECT_CHECKS
	}

	/**
	 * Returns the size ofthe reference token stream.
	 *
	 * @returns Size of the stream.
	 */
	int32 Size() const
	{
		return Tokens.Num();
	}

	/**
	 * return true if this is empty
	 */
	bool IsEmpty() const
	{
		return Tokens.Num() == 0;
	}

	/**
	 * Prepends passed in stream to existing one.
	 *
	 * @param Other	stream to concatenate
	 */
	void PrependStream( const FGCReferenceTokenStream& Other );

	/**
	 * Emit reference info.
	 *
	 * @param ReferenceInfo	reference info to emit.
	 *
	 * @return Index of the reference info in the token stream.
	 */
	int32 EmitReferenceInfo(FGCReferenceInfo ReferenceInfo, const FName& DebugName);

	/**
	 * Emit placeholder for aray skip index, updated in UpdateSkipIndexPlaceholder
	 *
	 * @return the index of the skip index, used later in UpdateSkipIndexPlaceholder
	 */
	uint32 EmitSkipIndexPlaceholder();

	/**
	 * Updates skip index place holder stored and passed in skip index index with passed
	 * in skip index. The skip index is used to skip over tokens in the case of an emtpy 
	 * dynamic array.
	 * 
	 * @param SkipIndexIndex index where skip index is stored at.
	 * @param SkipIndex index to store at skip index index
	 */
	void UpdateSkipIndexPlaceholder( uint32 SkipIndexIndex, uint32 SkipIndex );

	/**
	 * Emit count
	 *
	 * @param Count count to emit
	 */
	int32 EmitCount( uint32 Count );

	/**
	 * Emit a pointer
	 *
	 * @param Ptr pointer to emit
	 */
	int32 EmitPointer( void const* Ptr );

	/**
	 * Emit stride
	 *
	 * @param Stride stride to emit
	 */
	int32 EmitStride( uint32 Stride );

	/**
	 * Increase return count on last token.
	 *
	 * @return index of next token
	 */
	uint32 EmitReturn();

	/**
	 * Helper function to perform post parent token stream prepend fixup
	 */
	void Fixup(void (*AddReferencedObjectsPtr)(UObject*, class FReferenceCollector&), bool bKeepOuterToken, bool bKeepClassToken);

	/**
	 * Reads count and advances stream.
	 *
	 * @return read in count
	 */
	FORCEINLINE uint32 ReadCount( uint32& CurrentIndex )
	{
		return Tokens[CurrentIndex++];
	}

	/**
	 * Reads stride and advances stream.
	 *
	 * @return read in stride
	 */
	FORCEINLINE uint32 ReadStride( uint32& CurrentIndex )
	{
		return Tokens[CurrentIndex++];
	}

	/**
	 * Reads pointer and advance stream
	 *
	 * @return read in pointer
	 */
	FORCEINLINE void* ReadPointer( uint32& CurrentIndex )
	{
		UPTRINT Result = UPTRINT(Tokens[CurrentIndex++]);
#if PLATFORM_64BITS // warning C4293: '<<' : shift count negative or too big, undefined behavior, so we needed the ifdef
		static_assert(sizeof(void*) == 8, "Pointer size mismatch.");
		Result |= UPTRINT(Tokens[CurrentIndex++]) << 32;
#else
		static_assert(sizeof(void*) == 4, "Pointer size mismatch.");
#endif
		return (void*)Result;
	}

	/**
	 * Reads in reference info and advances stream.
	 *
	 * @return read in reference info
	 */
	FORCEINLINE FGCReferenceInfo ReadReferenceInfo( uint32& CurrentIndex )
	{
		return Tokens[CurrentIndex++];
	}

	/**
	 * Access reference info at passed in index. Used as helper to eliminate LHS.
	 *
	 * @return Reference info at passed in index
	 */
	FORCEINLINE FGCReferenceInfo AccessReferenceInfo( uint32 CurrentIndex ) const
	{
		return Tokens[CurrentIndex];
	}

	/**
	 * Read in skip index and advances stream.
	 *
	 * @return read in skip index
	 */
	FORCEINLINE FGCSkipInfo ReadSkipInfo( uint32& CurrentIndex )
	{
		FGCSkipInfo SkipInfo = Tokens[CurrentIndex];
		SkipInfo.SkipIndex += CurrentIndex;
		CurrentIndex++;
		return SkipInfo;
	}

	/**
	 * Read return count stored at the index before the skip index. This is required 
	 * to correctly return the right amount of levels when skipping over an empty array.
	 *
	 * @param SkipIndex index of first token after array
	 */
	FORCEINLINE uint32 GetSkipReturnCount( FGCSkipInfo SkipInfo )
	{
		check( SkipInfo.SkipIndex > 0 && SkipInfo.SkipIndex <= (uint32)Tokens.Num() );		
		FGCReferenceInfo ReferenceInfo = Tokens[SkipInfo.SkipIndex-1];
		check( ReferenceInfo.Type != GCRT_None );
		return ReferenceInfo.ReturnCount - SkipInfo.InnerReturnCount;		
	}

	/**
	 * Queries the stream for an end of stream condition
	 *
	 * @return true if the end of the stream has been reached, false otherwise
	 */
	FORCEINLINE bool EndOfStream( uint32 CurrentIndex )
	{
		return CurrentIndex >= (uint32)Tokens.Num();
	}

#if ENABLE_GC_OBJECT_CHECKS
	FTokenInfo GetTokenInfo(int32 TokenIndex) const;
#endif

private:

	/**
	 * Helper function to store a pointer into a preallocated token stream.
	 *
	 * @param Stream Preallocated token stream.
	 * @param Ptr pointer to store
	 */
	FORCEINLINE void StorePointer( uint32* Stream, void const* Ptr )
	{
	#if PLATFORM_64BITS // warning C4293: '<<' : shift count negative or too big, undefined behavior, so we needed the ifdef
		static_assert(sizeof(void*) == 8, "Pointer size mismatch.");
		Stream[0] = UPTRINT(Ptr) & 0xffffffff;
		Stream[1] = UPTRINT(Ptr) >> 32;
	#else
		static_assert(sizeof(void*) == 4, "Pointer size mismatch.");
		Stream[0] = PTRINT(Ptr);
	#endif
	}

	/** Token array */
	TArray<uint32> Tokens;
#if ENABLE_GC_OBJECT_CHECKS
	/** 
	 * Name of the proprty that emitted the associated token or token type (pointer etc).
	 * We want to keep it in a separate array for performance reasons
	 */
	TArray<FName> TokenDebugInfo;
#endif // ENABLE_GC_OBJECT_CHECKS
};

/** Prevent GC from running in the current scope */
class COREUOBJECT_API FGCScopeGuard
{
public:
	FGCScopeGuard();
	~FGCScopeGuard();
};

template <EFastReferenceCollectorOptions Options> class FGCReferenceProcessor;

/** Struct to hold the objects to serialize array and the list of weak references. This is allocated by ArrayPool */
struct FGCArrayStruct
{
	TArray<UObject*> ObjectsToSerialize;
	TArray<UObject**> WeakReferences;
};

/**
* Specialized FReferenceCollector that uses FGCReferenceProcessor to mark objects as reachable.
*/
template <EFastReferenceCollectorOptions Options>
class FGCCollector : public FReferenceCollector
{
	FGCReferenceProcessor<Options>& ReferenceProcessor;
	FGCArrayStruct& ObjectArrayStruct;
	bool bAllowEliminatingReferences;

	constexpr FORCEINLINE bool IsParallel() const
	{
		return !!(Options & EFastReferenceCollectorOptions::Parallel);
	}
	constexpr FORCEINLINE bool IsWithClusters() const
	{
		return !!(Options & EFastReferenceCollectorOptions::WithClusters);
	}

public:

	FGCCollector(FGCReferenceProcessor<Options>& InProcessor, FGCArrayStruct& InObjectArrayStruct);

	virtual void HandleObjectReference(UObject*& InObject, const UObject* InReferencingObject, const FProperty* InReferencingProperty) override;

	virtual void HandleObjectReferences(UObject** InObjects, const int32 ObjectNum, const UObject* InReferencingObject, const FProperty* InReferencingProperty) override;

	virtual bool IsIgnoringArchetypeRef() const override
	{
		return false;
	}
	virtual bool IsIgnoringTransient() const override
	{
		return false;
	}
	virtual void AllowEliminatingReferences(bool bAllow) override
	{
		bAllowEliminatingReferences = bAllow;
	}
	virtual bool MarkWeakObjectReferenceForClearing(UObject** WeakReference) override
	{
		// Track this references for later destruction if necessary. These should be relatively rare
		ObjectArrayStruct.WeakReferences.Add(WeakReference);
		return true;
	}
private:

	FORCEINLINE void InternalHandleObjectReference(UObject*& Object, const UObject* ReferencingObject, const FProperty* ReferencingProperty);
};

/**
 * FGarbageCollectionTracer
 * Interface to allow external systems to trace additional object references, used for bridging GCs
 */
class FGarbageCollectionTracer
{
public:
	virtual ~FGarbageCollectionTracer() {}

	virtual void PerformReachabilityAnalysisOnObjects(FGCArrayStruct* ArrayStruct, bool bForceSingleThreaded, bool bWithClusters) = 0;

};

/** True if Garbage Collection is running. Use IsGarbageCollecting() functio n instead of using this variable directly */
extern COREUOBJECT_API FThreadSafeBool GIsGarbageCollecting;

/**
* Whether we are inside garbage collection
*/
FORCEINLINE bool IsGarbageCollecting()
{
	return GIsGarbageCollecting;
}