// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "Async/TaskGraphInterfaces.h"
#include "UObject/UnrealType.h"
#include "Misc/ScopeLock.h"
#include "HAL/PlatformProcess.h"
#include "UObject/FieldPath.h"
#include "UObject/UObjectArray.h"
#include "UObject/FastReferenceCollectorOptions.h"

struct FStackEntry;

/*=============================================================================
	FastReferenceCollector.h: Unreal realtime garbage collection helpers
=============================================================================*/

/**
 * Pool for reducing GC allocations
 */
class FGCArrayPool
{
private:
	// allows sharing a singleton between all compilation units while still having an inlined getter
	COREUOBJECT_API static FGCArrayPool* GetGlobalSingleton();
public:

	/**
	 * Gets the singleton instance of the FObjectArrayPool
	 * @return Pool singleton.
	 */
	FORCEINLINE static FGCArrayPool& Get()
	{
		static FGCArrayPool* Singleton = nullptr;

		if (!Singleton)
		{
			Singleton = GetGlobalSingleton();
		}
		return *Singleton;
	}

	/**
	 * Gets an event from the pool or creates one if necessary.
	 *
	 * @return The array.
	 * @see ReturnToPool
	 */
	FORCEINLINE FGCArrayStruct* GetArrayStructFromPool()
	{
		FGCArrayStruct* Result = Pool.Pop();
		if (!Result)
		{
			Result = new FGCArrayStruct();
		}
		check(Result);
#if UE_BUILD_DEBUG
		NumberOfUsedArrays.Increment();
#endif // UE_BUILD_DEBUG
		return Result;
	}

	/**
	 * Returns an array to the pool.
	 *
	 * @param Array The array to return.
	 * @see GetArrayFromPool
	 */
	FORCEINLINE void ReturnToPool(FGCArrayStruct* ArrayStruct)
	{
#if UE_BUILD_DEBUG
		const int32 CheckUsedArrays = NumberOfUsedArrays.Decrement();
		checkSlow(CheckUsedArrays >= 0);
#endif // UE_BUILD_DEBUG
		check(ArrayStruct);
		ArrayStruct->ObjectsToSerialize.Reset();
		Pool.Push(ArrayStruct);
	}

	/** 
	 * Performs manual memory cleanup. 
	 * Generally the pools will be cleaned up when ClearWeakReferences is called on a full GC purge
	 */
	void Cleanup()
	{
#if UE_BUILD_DEBUG
		const int32 CheckUsedArrays = NumberOfUsedArrays.GetValue();
		checkSlow(CheckUsedArrays == 0);
#endif // UE_BUILD_DEBUG

		uint32 FreedMemory = 0;
		TArray<FGCArrayStruct*> AllArrays;
		Pool.PopAll(AllArrays);
		for (FGCArrayStruct* ArrayStruct : AllArrays)
		{
			// If we are cleaning up with active weak references the weak references will get corrupted
			checkSlow(ArrayStruct->WeakReferences.Num() == 0);
			FreedMemory += ArrayStruct->ObjectsToSerialize.GetAllocatedSize();
			FreedMemory += ArrayStruct->WeakReferences.GetAllocatedSize();
			delete ArrayStruct;
		}
		UE_LOG(LogGarbage, Log, TEXT("Freed %ub from %d GC array pools."), FreedMemory, AllArrays.Num());
	}

	/**
	 * Writes out info about the makeup of the pool. called by 'gc.DumpPoolStats'
	 *
	 * @param Array The array to return.
	 * @see GetArrayFromPool
	 */
	static void DumpStats(FOutputDevice& OutputDevice)
	{
		FGCArrayPool& Instance = Get();

		TArray<FGCArrayStruct*> PoppedItems;

		TMap<int32, int32> Buckets;

		int32 TotalSize = 0;
		int32 MaxSize = 0;
		int32 TotalItems = 0;

		do
		{
			FGCArrayStruct* Item = Instance.Pool.Pop();

			if (Item)
			{
				PoppedItems.Push(Item);

				// Inc our bucket
				Buckets.FindOrAdd(Item->ObjectsToSerialize.Max()) += 1;

				TotalSize += Item->ObjectsToSerialize.Max();
				TotalSize += Item->WeakReferences.Max();
				TotalItems++;
			}
			else
			{
				break;
			}

		} while (true);

		// return everything to the pool
		while (PoppedItems.Num())
		{
			Instance.Pool.Push(PoppedItems.Pop());
		}

		// One of these lists is used by the main GC and is huge, so remove it so that it doesn't
		// pollute the stats and we can accurately see what the task pools are using.
		int32 TotalSizeKB = (TotalSize * sizeof(UObject*)) / 1024;

		OutputDevice.Logf(TEXT("GCPoolStats: %d Pools totaling %d KB. Avg: Objs=%d, Size=%d KB."),
			TotalItems,
			TotalSizeKB,
			TotalSize / FMath::Max(TotalItems, 1),
			TotalSizeKB / FMath::Max(TotalItems, 1));

		// long form output...
		TArray<int32> Keys;

		Buckets.GetKeys(Keys);

		Keys.Sort([&](int32 lhs, int32 rhs) {
			return lhs > rhs;
		});

		for (int Key : Keys)
		{
			const int32 Value = Buckets[Key];
			int32 ItemSize = (Key * sizeof(UObject*)) / 1024;
			OutputDevice.Logf(TEXT("\t%d\t\t(%d Items @ %d KB = %d KB)"), Key, Value, ItemSize, Value * ItemSize);
		}
	}

	/** 
	 * Clears weak references for everything in the pool. 
	 * If bClearPools is true it will clear all of the pools as well, which is used during a full purge 
	 */
	void ClearWeakReferences(bool bClearPools)
	{
		TArray<FGCArrayStruct*> AllArrays;
		Pool.PopAll(AllArrays);
		int32 Index = 0;
		for (FGCArrayStruct* ArrayStruct : AllArrays)
		{
			for (UObject** WeakReference : ArrayStruct->WeakReferences)
			{
				UObject*& ReferencedObject = *WeakReference;
				if (ReferencedObject && ReferencedObject->IsUnreachable())
				{
					ReferencedObject = nullptr;
				}
			}
			ArrayStruct->WeakReferences.Reset();
			if (bClearPools 
				|| Index % 7 == 3) // delete 1/7th of them just to keep things from growing too much between full purges
			{
				delete ArrayStruct;
			}
			else
			{
				Pool.Push(ArrayStruct);
			}
			Index++;
		}
	}

#if UE_BUILD_DEBUG
	void CheckLeaks()
	{
		// This function is called after GC has finished so at this point there should be no
		// arrays used by GC and all should be returned to the pool
		const int32 LeakedGCPoolArrays = NumberOfUsedArrays.GetValue();
		checkSlow(LeakedGCPoolArrays == 0);
	}
#endif

private:

	/** Holds the collection of recycled arrays. */
	TLockFreePointerListLIFO< FGCArrayStruct > Pool;

#if UE_BUILD_DEBUG
	/** Number of arrays currently acquired from the pool by GC */
	FThreadSafeCounter NumberOfUsedArrays;
#endif // UE_BUILD_DEBUG
};

/**
 * Helper class that looks for UObject references by traversing UClass token stream and calls AddReferencedObjects.
 * Provides a generic way of processing references that is used by Unreal Engine garbage collection.
 * Can be used for fast (does not use serialization) reference collection purposes.
 * 
 * IT IS CRITICAL THIS CLASS DOES NOT CHANGE WITHOUT CONSIDERING PERFORMANCE IMPACT OF SAID CHANGES
 *
 * This class depends on three components: ReferenceProcessor, ReferenceCollector and ArrayPool.
 * The assumptions for each of those components are as follows:
 *
   class FSampleReferenceProcessor
   {
   public:
     int32 GetMinDesiredObjectsPerSubTask() const;
		 void HandleTokenStreamObjectReference(TArray<UObject*>& ObjectsToSerialize, UObject* ReferencingObject, UObject*& Object, const int32 TokenIndex, bool bAllowReferenceElimination);
		 void UpdateDetailedStats(UObject* CurrentObject, uint32 DeltaCycles);
		 void LogDetailedStatsSummary();
	 };

	 class FSampleCollector : public FReferenceCollector 
	 {
	   // Needs to implement FReferenceCollector pure virtual functions
	 };
   
	 class FSampleArrayPool
	 {
	   static FSampleArrayPool& Get();
		 FGCArrayStruct* GetArrayStryctFromPool();
		 void ReturnToPool(FGCArrayStruct* ArrayStruct);
	 };
 */

template <typename ReferenceProcessorType, typename CollectorType, typename ArrayPoolType, EFastReferenceCollectorOptions Options = EFastReferenceCollectorOptions::None>
class TFastReferenceCollector
{
private:

	constexpr FORCEINLINE bool IsParallel() const
	{
		return !!(Options & EFastReferenceCollectorOptions::Parallel);
	}
	constexpr FORCEINLINE bool CanAutogenerateTokenStream() const
	{
		return !!(Options & EFastReferenceCollectorOptions::AutogenerateTokenStream);
	}
	constexpr FORCEINLINE bool ShouldProcessNoOpTokens() const
	{
		return !!(Options & EFastReferenceCollectorOptions::ProcessNoOpTokens);
	}
	constexpr FORCEINLINE bool ShouldProcessWeakReferences() const
	{
		return !!(Options & EFastReferenceCollectorOptions::ProcessWeakReferences);
	}
	
	class FCollectorTaskQueue
	{
		TFastReferenceCollector*	Owner;
		ArrayPoolType& ArrayPool;
		TLockFreePointerListUnordered<FGCArrayStruct, PLATFORM_CACHE_LINE_SIZE> Tasks;

		FCriticalSection WaitingThreadsLock;
		TArray<FEvent*> WaitingThreads;
		bool bDone;
		int32 NumThreadsStarted;
	public:

		FCollectorTaskQueue(TFastReferenceCollector* InOwner, ArrayPoolType& InArrayPool)
			: Owner(InOwner)
			, ArrayPool(InArrayPool)
			, bDone(false)
			, NumThreadsStarted(0)
		{
		}

		void CheckDone()
		{
			FScopeLock Lock(&WaitingThreadsLock);
			check(bDone);
			check(!Tasks.Pop());
			check(!WaitingThreads.Num());
			check(NumThreadsStarted);
		}

		FORCENOINLINE void AddTask(const TArray<UObject*>* InObjectsToSerialize, int32 StartIndex, int32 NumObjects)
		{
			FGCArrayStruct* ArrayStruct = ArrayPool.GetArrayStructFromPool();
			ArrayStruct->ObjectsToSerialize.AddUninitialized(NumObjects);
			FMemory::Memcpy(ArrayStruct->ObjectsToSerialize.GetData(), InObjectsToSerialize->GetData() + StartIndex, NumObjects * sizeof(UObject*));
			Tasks.Push(ArrayStruct);

			FEvent* WaitingThread = nullptr;
			{
				FScopeLock Lock(&WaitingThreadsLock);
				check(!bDone);
				if (WaitingThreads.Num())
				{
					WaitingThread = WaitingThreads.Pop();
				}
			}
			if (WaitingThread)
			{
				WaitingThread->Trigger();
			}
		}

		FORCENOINLINE void DoTask()
		{
			{
				FScopeLock Lock(&WaitingThreadsLock);
				if (bDone)
				{
					return;
				}
				NumThreadsStarted++;
			}
			while (true)
			{
				FGCArrayStruct* ArrayStruct = Tasks.Pop();
				while (!ArrayStruct)
				{
					if (bDone)
					{
						return;
					}
					FEvent* WaitEvent = nullptr;
					{
						FScopeLock Lock(&WaitingThreadsLock);
						if (bDone)
						{
							return;
						}
						ArrayStruct = Tasks.Pop();
						if (!ArrayStruct)
						{
							if (WaitingThreads.Num() + 1 == NumThreadsStarted)
							{
								bDone = true;
								FPlatformMisc::MemoryBarrier();
								for (FEvent* WaitingThread : WaitingThreads)
								{
									WaitingThread->Trigger();
								}
								WaitingThreads.Empty();
								return;
							}
							else
							{
								WaitEvent = FPlatformProcess::GetSynchEventFromPool(false);
								WaitingThreads.Push(WaitEvent);
							}
						}
					}
					if (ArrayStruct)
					{
						check(!WaitEvent);
					}
					else
					{
						check(WaitEvent);
						WaitEvent->Wait();
						FPlatformProcess::ReturnSynchEventToPool(WaitEvent);
						ArrayStruct = Tasks.Pop();
						check(!ArrayStruct || !bDone);
					}
				}
				Owner->ProcessObjectArray(*ArrayStruct, FGraphEventRef());
				ArrayPool.ReturnToPool(ArrayStruct);
			}
		}
	};

	/** Task graph task responsible for processing UObject array */
	class FCollectorTaskProcessorTask
	{
		FCollectorTaskQueue& TaskQueue;
		ENamedThreads::Type DesiredThread;
	public:
		FCollectorTaskProcessorTask(FCollectorTaskQueue& InTaskQueue, ENamedThreads::Type InDesiredThread)
			: TaskQueue(InTaskQueue)
			, DesiredThread(InDesiredThread)
		{
		}
		FORCEINLINE TStatId GetStatId() const
		{
			RETURN_QUICK_DECLARE_CYCLE_STAT(FCollectorTaskProcessorTask, STATGROUP_TaskGraphTasks);
		}
		ENamedThreads::Type GetDesiredThread()
		{
			return DesiredThread;
		}
		static ESubsequentsMode::Type GetSubsequentsMode()
		{
			return ESubsequentsMode::TrackSubsequents;
		}
		void DoTask(ENamedThreads::Type CurrentThread, FGraphEventRef& MyCompletionGraphEvent)
		{
			TaskQueue.DoTask();
		}
	};

	/** Task graph task responsible for processing UObject array */
	class FCollectorTask
	{
		TFastReferenceCollector*	Owner;
		FGCArrayStruct*	ArrayStruct;
		ArrayPoolType& ArrayPool;

	public:
		FCollectorTask(TFastReferenceCollector* InOwner, const TArray<UObject*>* InObjectsToSerialize, int32 StartIndex, int32 NumObjects, ArrayPoolType& InArrayPool)
			: Owner(InOwner)
			, ArrayStruct(InArrayPool.GetArrayStructFromPool())
			, ArrayPool(InArrayPool)
		{
			ArrayStruct->ObjectsToSerialize.AddUninitialized(NumObjects);
			FMemory::Memcpy(ArrayStruct->ObjectsToSerialize.GetData(), InObjectsToSerialize->GetData() + StartIndex, NumObjects * sizeof(UObject*));
		}
		~FCollectorTask()
		{
			ArrayPool.ReturnToPool(ArrayStruct);
		}
		FORCEINLINE TStatId GetStatId() const
		{
			RETURN_QUICK_DECLARE_CYCLE_STAT(FCollectorTask, STATGROUP_TaskGraphTasks);
		}
		static ENamedThreads::Type GetDesiredThread()
		{
			return FPlatformProcess::GetDesiredThreadForUObjectReferenceCollector();
		}
		static ESubsequentsMode::Type GetSubsequentsMode()
		{
			return ESubsequentsMode::TrackSubsequents;
		}
		void DoTask(ENamedThreads::Type CurrentThread, FGraphEventRef& MyCompletionGraphEvent)
		{
			Owner->ProcessObjectArray(*ArrayStruct, MyCompletionGraphEvent);
		}
	};

	/** Object that handles all UObject references */
	ReferenceProcessorType& ReferenceProcessor;
	/** Custom TArray allocator */
	ArrayPoolType& ArrayPool;

	FCollectorTaskQueue TaskQueue;

	/** Helper struct for stack based approach */
	struct FStackEntry
	{
		/** Current data pointer, incremented by stride */
		uint8* Data;
		/** Current container property for data pointer. DO NOT rely on its value being initialized. Instead check ContainerType first. */
		FProperty* ContainerProperty;
		/** Pointer to the container being processed by GC. DO NOT rely on its value being initialized. Instead check ContainerType first. */
		void* ContainerPtr;
		/** Current index within the container. DO NOT rely on its value being initialized. Instead check ContainerType first. */
		int32	ContainerIndex;
		/** Current container helper type */
		uint32	ContainerType : 5; // The number of bits needs to match FGCReferenceInfo::Type
		/** Current stride */
		uint32	Stride : 27; // This will always be bigger (8 bits more) than FGCReferenceInfo::Ofset which is the max offset GC can handle
		/** Current loop count, decremented each iteration */
		int32	Count;
		/** First token index in loop */
		int32	LoopStartIndex;
	};

public:
	/** Default constructor, initializing all members. */
	TFastReferenceCollector(ReferenceProcessorType& InReferenceProcessor, ArrayPoolType& InArrayPool)
		: ReferenceProcessor(InReferenceProcessor)
		, ArrayPool(InArrayPool)
		, TaskQueue(this, InArrayPool)
	{}

	/**
	* Performs reachability analysis.
	*
	* @param ObjectsToCollectReferencesFor List of objects which references should be collected
	* @param bForceSingleThreaded Collect references on a single thread
	*/
	void CollectReferences(FGCArrayStruct& ArrayStruct)
	{
		TArray<UObject*>& ObjectsToCollectReferencesFor = ArrayStruct.ObjectsToSerialize;
		if (ObjectsToCollectReferencesFor.Num())
		{
			if (!IsParallel())
			{
				FGraphEventRef InvalidRef;
				ProcessObjectArray(ArrayStruct, InvalidRef);
			}
			else
			{
				FGraphEventArray ChunkTasks;

				int32 NumThreads = FTaskGraphInterface::Get().GetNumWorkerThreads();
				int32 NumBackgroundThreads = ENamedThreads::bHasBackgroundThreads ? NumThreads : 0;
				ENamedThreads::Type NormalThreadName = ENamedThreads::AnyNormalThreadNormalTask;
				ENamedThreads::Type BackgroundThreadName = ENamedThreads::AnyBackgroundThreadNormalTask;

				FPlatformProcess::ModifyThreadAssignmentForUObjectReferenceCollector(NumThreads, NumBackgroundThreads, NormalThreadName, BackgroundThreadName);
				int32 NumTasks = NumThreads + NumBackgroundThreads;

				check(NumTasks > 0);
				ChunkTasks.Empty(NumTasks);
				int32 NumPerChunk = ObjectsToCollectReferencesFor.Num() / NumTasks;
				int32 StartIndex = 0;
				for (int32 Chunk = 0; Chunk < NumTasks; Chunk++)
				{
					if (Chunk + 1 == NumTasks)
					{
						NumPerChunk = ObjectsToCollectReferencesFor.Num() - StartIndex; // last chunk takes all remaining items
					}
					TaskQueue.AddTask(&ObjectsToCollectReferencesFor, StartIndex, NumPerChunk);
					StartIndex += NumPerChunk;
				}
				for (int32 Chunk = 0; Chunk < NumTasks; Chunk++)
				{
					ChunkTasks.Add(TGraphTask< FCollectorTaskProcessorTask >::CreateTask().ConstructAndDispatchWhenReady(TaskQueue, Chunk >= NumThreads ? BackgroundThreadName : NormalThreadName));
				}

				QUICK_SCOPE_CYCLE_COUNTER(STAT_GC_Subtask_Wait);
				FTaskGraphInterface::Get().WaitUntilTasksComplete(ChunkTasks, ENamedThreads::GameThread_Local);
				TaskQueue.CheckDone();
			}
		}
	}

private:

	FORCEINLINE bool MoveToNextContainerElementAndCheckIfValid(FStackEntry* StackEntry) const
	{
		switch (StackEntry->ContainerType)
		{
		case GCRT_AddTMapReferencedObjects:
		{
			FMapProperty* MapProperty = (FMapProperty*)StackEntry->ContainerProperty;
			return MapProperty->IsValidIndex(StackEntry->ContainerPtr, ++StackEntry->ContainerIndex);
		}
		case GCRT_AddTSetReferencedObjects:
		{
			FSetProperty* SetProperty = (FSetProperty*)StackEntry->ContainerProperty;
			return SetProperty->IsValidIndex(StackEntry->ContainerPtr, ++StackEntry->ContainerIndex);
		}
		default:
		{
			return true;
		}
		}
	}

	/**
	 * Handles weak object pointer references
	 * @param WeakPtr weak object pointer
	 * @param NewObjectsToSerialize List of new objects to process as a result of processing this reference
	 * @param CurrentObject current object being processed (owner of the weak object pointer)
	 * @param ReferenceTokenStreamIndex GC token stream index (for debugging)
	 */
	FORCEINLINE void HandleWeakObjectPtr(FWeakObjectPtr& WeakPtr, TArray<UObject*>& NewObjectsToSerialize, UObject* CurrentObject, int32 ReferenceTokenStreamIndex)
	{
		UObject* WeakObject = WeakPtr.Get(true);
		ReferenceProcessor.HandleTokenStreamObjectReference(NewObjectsToSerialize, CurrentObject, WeakObject, ReferenceTokenStreamIndex, true);
	}

	/**
	 * Traverses UObject token stream to find existing references
	 *
	 * @param InObjectsToSerializeArray Objects to process
	 * @param MyCompletionGraphEvent Task graph event
	 */
	void ProcessObjectArray(FGCArrayStruct& InObjectsToSerializeStruct, const FGraphEventRef& MyCompletionGraphEvent)
	{
		DECLARE_SCOPE_CYCLE_COUNTER(TEXT("TFastReferenceCollector::ProcessObjectArray"), STAT_FFastReferenceCollector_ProcessObjectArray, STATGROUP_GC);

		UObject* CurrentObject = nullptr;

		const int32 MinDesiredObjectsPerSubTask = ReferenceProcessor.GetMinDesiredObjectsPerSubTask(); // sometimes there will be less, a lot less

		/** Growing array of objects that require serialization */
		FGCArrayStruct&	NewObjectsToSerializeStruct = *ArrayPool.GetArrayStructFromPool();

		// Ping-pong between these two arrays if there's not enough objects to spawn a new task
		TArray<UObject*>& ObjectsToSerialize = InObjectsToSerializeStruct.ObjectsToSerialize;
		TArray<UObject*>& NewObjectsToSerialize = NewObjectsToSerializeStruct.ObjectsToSerialize;

		// Presized "recursion" stack for handling arrays and structs.
		TArray<FStackEntry> Stack;
		Stack.AddUninitialized(128); //@todo rtgc: need to add code handling more than 128 layers of recursion or at least assert

		// it is necessary to have at least one extra item in the array memory block for the iffy prefetch code, below
		ObjectsToSerialize.Reserve(ObjectsToSerialize.Num() + 1);

		// Keep serializing objects till we reach the end of the growing array at which point
		// we are done.
		int32 CurrentIndex = 0;
		do
		{
			CollectorType ReferenceCollector(ReferenceProcessor, NewObjectsToSerializeStruct);
			while (CurrentIndex < ObjectsToSerialize.Num())
			{
#if PERF_DETAILED_PER_CLASS_GC_STATS
				uint32 StartCycles = FPlatformTime::Cycles();
#endif
				CurrentObject = ObjectsToSerialize[CurrentIndex++];
				checkSlow(CurrentObject);

				// GetData() used to avoiding bounds checking (min and max)
				// FMath::Min used to avoid out of bounds (without branching) on last iteration. Though anything can be passed into PrefetchBlock, 
				// reading ObjectsToSerialize out of bounds is not safe since ObjectsToSerialize[Num()] may be an unallocated/unsafe address.
				const UObject * const NextObject = ObjectsToSerialize.GetData()[FMath::Min<int32>(CurrentIndex, ObjectsToSerialize.Num() - 1)];

				// Prefetch the next object assuming that the property size of the next object is the same as the current one.
				// This allows us to avoid a branch here.
				FPlatformMisc::PrefetchBlock(NextObject, CurrentObject->GetClass()->GetPropertiesSize());

				//@todo rtgc: we need to handle object references in struct defaults

				// Make sure that token stream has been assembled at this point as the below code relies on it.
				if (!IsParallel() && CanAutogenerateTokenStream())
				{
					UClass* ObjectClass = CurrentObject->GetClass();
					if (!ObjectClass->HasAnyClassFlags(CLASS_TokenStreamAssembled))
					{
						ObjectClass->AssembleReferenceTokenStream();
					}
				}
#if DO_CHECK
				if (!CurrentObject->GetClass()->HasAnyClassFlags(CLASS_TokenStreamAssembled))
				{
					UE_LOG(LogGarbage, Fatal, TEXT("%s does not yet have a token stream assembled."), *GetFullNameSafe(CurrentObject->GetClass()));
				}
#endif
				if (!IsParallel())
				{
					ReferenceProcessor.SetCurrentObject(CurrentObject);
				}

				// Get pointer to token stream and jump to the start.
				FGCReferenceTokenStream* RESTRICT TokenStream = &CurrentObject->GetClass()->ReferenceTokenStream;
				uint32 TokenStreamIndex = 0;
				// Keep track of index to reference info. Used to avoid LHSs.
				uint32 ReferenceTokenStreamIndex = 0;

				// Create stack entry and initialize sane values.
				FStackEntry* RESTRICT StackEntry = Stack.GetData();
				uint8* StackEntryData = (uint8*)CurrentObject;
				StackEntry->Data = StackEntryData;
				StackEntry->ContainerType = GCRT_None;
				StackEntry->Stride = 0;
				StackEntry->Count = -1;
				StackEntry->LoopStartIndex = -1;

				// Keep track of token return count in separate integer as arrays need to fiddle with it.
				int32 TokenReturnCount = 0;

				// Parse the token stream.
				while (true)
				{
					// Cache current token index as it is the one pointing to the reference info.
					ReferenceTokenStreamIndex = TokenStreamIndex;

					// Handle returning from an array of structs, array of structs of arrays of ... (yadda yadda)
					for (int32 ReturnCount = 0; ReturnCount<TokenReturnCount; ReturnCount++)
					{
						// Make sure there's no stack underflow.
						check(StackEntry->Count != -1);

						// We pre-decrement as we're already through the loop once at this point.
						if (--StackEntry->Count > 0)
						{
							if (StackEntry->ContainerType == GCRT_None)
							{
								// Fast path for TArrays of structs
								// Point data to next entry.
								StackEntryData = StackEntry->Data + StackEntry->Stride;
								StackEntry->Data = StackEntryData;
							}
							else
							{
								// Slower path for other containers
								// Point data to next valid entry.
								do
								{
									StackEntryData = StackEntry->Data + StackEntry->Stride;
									StackEntry->Data = StackEntryData;
								} while (!MoveToNextContainerElementAndCheckIfValid(StackEntry));
							}

							// Jump back to the beginning of the loop.
							TokenStreamIndex = StackEntry->LoopStartIndex;
							ReferenceTokenStreamIndex = StackEntry->LoopStartIndex;
							// We're not done with this token loop so we need to early out instead of backing out further.
							break;
						}
						else
						{
							StackEntry->ContainerType = GCRT_None;
							StackEntry--;
							StackEntryData = StackEntry->Data;
						}
					}

					TokenStreamIndex++;
					FGCReferenceInfo ReferenceInfo = TokenStream->AccessReferenceInfo(ReferenceTokenStreamIndex);

					switch(ReferenceInfo.Type)
					{
					case GCRT_Object:
					case GCRT_Class:
					{
						// We're dealing with an object reference (this code should be identical to GCRT_NoopClass if ShouldProcessNoOpTokens())
						UObject**	ObjectPtr = (UObject**)(StackEntryData + ReferenceInfo.Offset);
						UObject*&	Object = *ObjectPtr;
						TokenReturnCount = ReferenceInfo.ReturnCount;
						ReferenceProcessor.HandleTokenStreamObjectReference(NewObjectsToSerialize, CurrentObject, Object, ReferenceTokenStreamIndex, true);
					}
					break;
					case GCRT_ArrayObject:
					{
						// We're dealing with an array of object references.
						TArray<UObject*>& ObjectArray = *((TArray<UObject*>*)(StackEntryData + ReferenceInfo.Offset));
						TokenReturnCount = ReferenceInfo.ReturnCount;
						for (int32 ObjectIndex = 0, ObjectNum = ObjectArray.Num(); ObjectIndex < ObjectNum; ++ObjectIndex)
						{
							ReferenceProcessor.HandleTokenStreamObjectReference(NewObjectsToSerialize, CurrentObject, ObjectArray[ObjectIndex], ReferenceTokenStreamIndex, true);
						}
					}
					break;
					case GCRT_ArrayObjectFreezable:
					{
						// We're dealing with an array of object references.
						TArray<UObject*, FMemoryImageAllocator>& ObjectArray = *((TArray<UObject*, FMemoryImageAllocator>*)(StackEntryData + ReferenceInfo.Offset));
						TokenReturnCount = ReferenceInfo.ReturnCount;
						for (int32 ObjectIndex = 0, ObjectNum = ObjectArray.Num(); ObjectIndex < ObjectNum; ++ObjectIndex)
						{
							ReferenceProcessor.HandleTokenStreamObjectReference(NewObjectsToSerialize, CurrentObject, ObjectArray[ObjectIndex], ReferenceTokenStreamIndex, true);
						}
					}
					break;
					case GCRT_ArrayStruct:
					{
						// We're dealing with a dynamic array of structs.
						const FScriptArray& Array = *((FScriptArray*)(StackEntryData + ReferenceInfo.Offset));
						StackEntry++;
						StackEntryData = (uint8*)Array.GetData();
						StackEntry->Data = StackEntryData;
						StackEntry->Stride = TokenStream->ReadStride(TokenStreamIndex);
						StackEntry->Count = Array.Num();
						StackEntry->ContainerType = GCRT_None;

						const FGCSkipInfo SkipInfo = TokenStream->ReadSkipInfo(TokenStreamIndex);
						StackEntry->LoopStartIndex = TokenStreamIndex;

						if (StackEntry->Count == 0)
						{
							// Skip empty array by jumping to skip index and set return count to the one about to be read in.
							TokenStreamIndex = SkipInfo.SkipIndex;
							TokenReturnCount = TokenStream->GetSkipReturnCount(SkipInfo);
						}
						else
						{
							// Loop again.
							check(StackEntry->Data);
							TokenReturnCount = 0;
						}
					}
					break;
					case GCRT_ArrayStructFreezable:
					{
						// We're dealing with a dynamic array of structs.
						const FFreezableScriptArray& Array = *((FFreezableScriptArray*)(StackEntryData + ReferenceInfo.Offset));
						StackEntry++;
						StackEntryData = (uint8*)Array.GetData();
						StackEntry->Data = StackEntryData;
						StackEntry->Stride = TokenStream->ReadStride(TokenStreamIndex);
						StackEntry->Count = Array.Num();
						StackEntry->ContainerType = GCRT_None;

						const FGCSkipInfo SkipInfo = TokenStream->ReadSkipInfo(TokenStreamIndex);
						StackEntry->LoopStartIndex = TokenStreamIndex;

						if (StackEntry->Count == 0)
						{
							// Skip empty array by jumping to skip index and set return count to the one about to be read in.
							TokenStreamIndex = SkipInfo.SkipIndex;
							TokenReturnCount = TokenStream->GetSkipReturnCount(SkipInfo);
						}
						else
						{
							// Loop again.
							check(StackEntry->Data);
							TokenReturnCount = 0;
						}
					}
					break;
					case GCRT_PersistentObject:
					{
						// We're dealing with an object reference (this code should be identical to GCRT_NoopPersistentObject if ShouldProcessNoOpTokens())
						UObject**	ObjectPtr = (UObject**)(StackEntryData + ReferenceInfo.Offset);
						UObject*&	Object = *ObjectPtr;
						TokenReturnCount = ReferenceInfo.ReturnCount;
						ReferenceProcessor.HandleTokenStreamObjectReference(NewObjectsToSerialize, CurrentObject, Object, ReferenceTokenStreamIndex, false);
					}
					break;
					case GCRT_ExternalPackage:
					{
						// We're dealing with the external package reference.
						TokenReturnCount = ReferenceInfo.ReturnCount;
						// Test if the object isn't itself, since currently package are their own external and tracking that reference is pointless
						UObject* Object = CurrentObject->GetExternalPackageInternal();
						Object = Object != CurrentObject ? Object : nullptr;
						ReferenceProcessor.HandleTokenStreamObjectReference(NewObjectsToSerialize, CurrentObject, Object, ReferenceTokenStreamIndex, false);
					}
					break;
					case GCRT_FixedArray:
					{
						// We're dealing with a fixed size array
						uint8* PreviousData = StackEntryData;
						StackEntry++;
						StackEntryData = PreviousData;
						StackEntry->Data = PreviousData;
						StackEntry->Stride = TokenStream->ReadStride(TokenStreamIndex);
						StackEntry->Count = TokenStream->ReadCount(TokenStreamIndex);
						StackEntry->LoopStartIndex = TokenStreamIndex;
						StackEntry->ContainerType = GCRT_None;
						TokenReturnCount = 0;
					}
					break;
					case GCRT_AddStructReferencedObjects:
					{
						// We're dealing with a function call
						void* StructPtr = (void*)(StackEntryData + ReferenceInfo.Offset);
						TokenReturnCount = ReferenceInfo.ReturnCount;
						UScriptStruct::ICppStructOps::TPointerToAddStructReferencedObjects Func = (UScriptStruct::ICppStructOps::TPointerToAddStructReferencedObjects) TokenStream->ReadPointer(TokenStreamIndex);
						Func(StructPtr, ReferenceCollector);
					}
					break;
					case GCRT_AddReferencedObjects:
					{
						// Static AddReferencedObjects function call.
						void(*AddReferencedObjects)(UObject*, FReferenceCollector&) = (void(*)(UObject*, FReferenceCollector&))TokenStream->ReadPointer(TokenStreamIndex);
						TokenReturnCount = ReferenceInfo.ReturnCount;
						AddReferencedObjects(CurrentObject, ReferenceCollector);
					}
					break;
					case GCRT_AddTMapReferencedObjects:
					{
						void* MapPtr = StackEntryData + ReferenceInfo.Offset;
						FMapProperty* MapProperty = (FMapProperty*)TokenStream->ReadPointer(TokenStreamIndex);
						TokenStreamIndex++; // GCRT_EndOfPointer

						StackEntry++;
						StackEntry->ContainerType = GCRT_AddTMapReferencedObjects;
						StackEntry->ContainerIndex = 0;
						StackEntry->ContainerProperty = MapProperty;
						StackEntry->ContainerPtr = MapPtr;
						StackEntry->Stride = MapProperty->GetPairStride();
						StackEntry->Count = MapProperty->GetNum(MapPtr);

						const FGCSkipInfo SkipInfo = TokenStream->ReadSkipInfo(TokenStreamIndex);
						StackEntry->LoopStartIndex = TokenStreamIndex;

						if (StackEntry->Count == 0)
						{
							// The map is empty
							StackEntryData = nullptr;
							StackEntry->Data = StackEntryData;

							// Skip empty map by jumping to skip index and set return count to the one about to be read in.
							TokenStreamIndex = SkipInfo.SkipIndex;
							TokenReturnCount = TokenStream->GetSkipReturnCount(SkipInfo);
						}
						else
						{
							// Skip any initial invalid entries in the map. We need a valid index for MapProperty->GetPairPtr()
							int32 FirstValidIndex = 0;
							while (!MapProperty->IsValidIndex(MapPtr, FirstValidIndex))
							{
								FirstValidIndex++;
							}

							StackEntry->ContainerIndex = FirstValidIndex;
							StackEntryData = MapProperty->GetPairPtr(MapPtr, FirstValidIndex);
							StackEntry->Data = StackEntryData;

							// Loop again.
							TokenReturnCount = 0;
						}
					}
					break;
					case GCRT_AddTSetReferencedObjects:
					{
						void* SetPtr = StackEntryData + ReferenceInfo.Offset;
						FSetProperty* SetProperty = (FSetProperty*)TokenStream->ReadPointer(TokenStreamIndex);
						TokenStreamIndex++; // GCRT_EndOfPointer

						StackEntry++;
						StackEntry->ContainerProperty = SetProperty;
						StackEntry->ContainerPtr = SetPtr;
						StackEntry->ContainerType = GCRT_AddTSetReferencedObjects;
						StackEntry->ContainerIndex = 0;
						StackEntry->Stride = SetProperty->GetStride();
						StackEntry->Count = SetProperty->GetNum(SetPtr);

						const FGCSkipInfo SkipInfo = TokenStream->ReadSkipInfo(TokenStreamIndex);
						StackEntry->LoopStartIndex = TokenStreamIndex;

						if (StackEntry->Count == 0)
						{
							// The set is empty or it doesn't contain any valid elements
							StackEntryData = nullptr;
							StackEntry->Data = StackEntryData;

							// Skip empty set by jumping to skip index and set return count to the one about to be read in.
							TokenStreamIndex = SkipInfo.SkipIndex;
							TokenReturnCount = TokenStream->GetSkipReturnCount(SkipInfo);
						}
						else
						{
							// Skip any initial invalid entries in the set. We need a valid index for SetProperty->GetElementPtr()
							int32 FirstValidIndex = 0;
							while (!SetProperty->IsValidIndex(SetPtr, FirstValidIndex))
							{
								FirstValidIndex++;
							}

							StackEntry->ContainerIndex = FirstValidIndex;
							StackEntryData = SetProperty->GetElementPtr(SetPtr, FirstValidIndex);
							StackEntry->Data = StackEntryData;

							// Loop again.
							TokenReturnCount = 0;
						}
					}
					break;
					case GCRT_AddFieldPathReferencedObject:
					{
						FFieldPath*	FieldPathPtr = (FFieldPath*)(StackEntryData + ReferenceInfo.Offset);
						FUObjectItem* FieldOwnerItem = FieldPathPtr->GetResolvedOwnerItemInternal();
						TokenReturnCount = ReferenceInfo.ReturnCount;
						if (FieldOwnerItem)
						{
							UObject* OwnerObject = static_cast<UObject*>(FieldOwnerItem->Object);
							UObject* PreviousOwner = OwnerObject;
							ReferenceProcessor.HandleTokenStreamObjectReference(NewObjectsToSerialize, CurrentObject, OwnerObject, ReferenceTokenStreamIndex, true);
							// Handle reference elimination (PendingKill owner)
							if (PreviousOwner && !OwnerObject)
							{
								FieldPathPtr->ClearCachedFieldInternal();
							}
						}
					}
					break;
					case GCRT_ArrayAddFieldPathReferencedObject:
					{
						// We're dealing with an array of object references.
						TArray<FFieldPath>& FieldArray = *((TArray<FFieldPath>*)(StackEntryData + ReferenceInfo.Offset));
						TokenReturnCount = ReferenceInfo.ReturnCount;
						for (int32 FieldIndex = 0, FieldNum = FieldArray.Num(); FieldIndex < FieldNum; ++FieldIndex)
						{
							FUObjectItem* FieldOwnerItem = FieldArray[FieldIndex].GetResolvedOwnerItemInternal();
							if (FieldOwnerItem)
							{
								UObject* OwnerObject = static_cast<UObject*>(FieldOwnerItem->Object);
								UObject* PreviousOwner = OwnerObject;
								ReferenceProcessor.HandleTokenStreamObjectReference(NewObjectsToSerialize, CurrentObject, OwnerObject, ReferenceTokenStreamIndex, true);
								// Handle reference elimination (PendingKill owner)
								if (PreviousOwner && !OwnerObject)
								{
									FieldArray[FieldIndex].ClearCachedFieldInternal();
								}
							}
						}
					}
					break;
					case GCRT_Optional:
					{
						const FGCSkipInfo SkipInfo = TokenStream->ReadSkipInfo(TokenStreamIndex);
						uint32 ValueSize = TokenStream->ReadStride(TokenStreamIndex); // Size of value in bytes. This is also the offset to the bIsSet variable stored thereafter.
						const bool& bIsSet = *((bool*)(StackEntryData + ReferenceInfo.Offset + ValueSize));
						if (bIsSet)
						{
							// It's set - push a stack entry for processing the value
							// This is somewhat suboptimal since there is only ever just one value, but this approach avoids any changes to the surrounding code
							StackEntry++;
							StackEntryData += ReferenceInfo.Offset;
							StackEntry->Data = StackEntryData;
							StackEntry->Stride = ValueSize;
							StackEntry->Count = 1;
							StackEntry->LoopStartIndex = TokenStreamIndex;
						}
						else
						{
							// It's unset - keep going by jumping to skip index
							TokenStreamIndex = SkipInfo.SkipIndex;
						}
						TokenReturnCount = 0;
					}
					break;
					case GCRT_EndOfPointer:
					{
						TokenReturnCount = ReferenceInfo.ReturnCount;
					}
					break;
					case GCRT_NoopPersistentObject:
					{
						TokenReturnCount = ReferenceInfo.ReturnCount;
						if (ShouldProcessNoOpTokens())
						{
							// We're dealing with an object reference (this code should be identical to GCRT_PersistentObject)
							UObject**	ObjectPtr = (UObject**)(StackEntryData + ReferenceInfo.Offset);
							UObject*&	Object = *ObjectPtr;
							ReferenceProcessor.HandleTokenStreamObjectReference(NewObjectsToSerialize, CurrentObject, Object, ReferenceTokenStreamIndex, false);
						}
					}
					break;
					case GCRT_NoopClass:
					{
						TokenReturnCount = ReferenceInfo.ReturnCount;
						if (ShouldProcessNoOpTokens())
						{
							// We're dealing with an object reference (this code should be identical to GCRT_Object and GCRT_Class)
							UObject**	ObjectPtr = (UObject**)(StackEntryData + ReferenceInfo.Offset);
							UObject*&	Object = *ObjectPtr;
							ReferenceProcessor.HandleTokenStreamObjectReference(NewObjectsToSerialize, CurrentObject, Object, ReferenceTokenStreamIndex, true);
						}
					}
					break;
					case GCRT_WeakObject:
					{
						TokenReturnCount = ReferenceInfo.ReturnCount;
						if (ShouldProcessWeakReferences())
						{							
							FWeakObjectPtr& WeakPtr = *(FWeakObjectPtr*)(StackEntryData + ReferenceInfo.Offset);
							HandleWeakObjectPtr(WeakPtr, NewObjectsToSerialize, CurrentObject, ReferenceTokenStreamIndex);
						}
					}
					break;
					case GCRT_ArrayWeakObject:
					{
						TokenReturnCount = ReferenceInfo.ReturnCount;
						if (ShouldProcessWeakReferences())
						{							
							TArray<FWeakObjectPtr>& WeakPtrArray = *((TArray<FWeakObjectPtr>*)(StackEntryData + ReferenceInfo.Offset));
							for (FWeakObjectPtr& WeakPtr : WeakPtrArray)
							{
								HandleWeakObjectPtr(WeakPtr, NewObjectsToSerialize, CurrentObject, ReferenceTokenStreamIndex);
							}
						}
					}
					break;
					case GCRT_LazyObject:
					{
						TokenReturnCount = ReferenceInfo.ReturnCount;
						if (ShouldProcessWeakReferences())
						{							
							FLazyObjectPtr& LazyPtr = *(FLazyObjectPtr*)(StackEntryData + ReferenceInfo.Offset);
							FWeakObjectPtr& WeakPtr = LazyPtr.WeakPtr;
							HandleWeakObjectPtr(WeakPtr, NewObjectsToSerialize, CurrentObject, ReferenceTokenStreamIndex);
						}
					}
					break;
					case GCRT_ArrayLazyObject:
					{
						TokenReturnCount = ReferenceInfo.ReturnCount;
						if (ShouldProcessWeakReferences())
						{							
							TArray<FLazyObjectPtr>& LazyPtrArray = *((TArray<FLazyObjectPtr>*)(StackEntryData + ReferenceInfo.Offset));
							TokenReturnCount = ReferenceInfo.ReturnCount;
							for (FLazyObjectPtr& LazyPtr : LazyPtrArray)
							{
								FWeakObjectPtr& WeakPtr = LazyPtr.WeakPtr;
								HandleWeakObjectPtr(WeakPtr, NewObjectsToSerialize, CurrentObject, ReferenceTokenStreamIndex);
							}
						}
					}
					break;
					case GCRT_SoftObject:
					{
						TokenReturnCount = ReferenceInfo.ReturnCount;
						if (ShouldProcessWeakReferences())
						{							
							FSoftObjectPtr& SoftPtr = *(FSoftObjectPtr*)(StackEntryData + ReferenceInfo.Offset);
							FWeakObjectPtr& WeakPtr = SoftPtr.WeakPtr;
							HandleWeakObjectPtr(WeakPtr, NewObjectsToSerialize, CurrentObject, ReferenceTokenStreamIndex);
						}
					}
					break;
					case GCRT_ArraySoftObject:
					{
						TokenReturnCount = ReferenceInfo.ReturnCount;
						if (ShouldProcessWeakReferences())
						{							
							TArray<FSoftObjectPtr>& SoftPtrArray = *((TArray<FSoftObjectPtr>*)(StackEntryData + ReferenceInfo.Offset));
							for (FSoftObjectPtr& SoftPtr : SoftPtrArray)
							{
								FWeakObjectPtr& WeakPtr = SoftPtr.WeakPtr;
								HandleWeakObjectPtr(WeakPtr, NewObjectsToSerialize, CurrentObject, ReferenceTokenStreamIndex);
							}
						}
					}
					break;
					case GCRT_Delegate:
					{
						TokenReturnCount = ReferenceInfo.ReturnCount;
						if (ShouldProcessWeakReferences())
						{							
							FScriptDelegate& Delegate = *(FScriptDelegate*)(StackEntryData + ReferenceInfo.Offset);
							UObject* DelegateObject = Delegate.GetUObject();
							ReferenceProcessor.HandleTokenStreamObjectReference(NewObjectsToSerialize, CurrentObject, DelegateObject, ReferenceTokenStreamIndex, false);
						}
					}
					break;
					case GCRT_ArrayDelegate:
					{
						TokenReturnCount = ReferenceInfo.ReturnCount;
						if (ShouldProcessWeakReferences())
						{							
							TArray<FScriptDelegate>& DelegateArray = *((TArray<FScriptDelegate>*)(StackEntryData + ReferenceInfo.Offset));
							for (FScriptDelegate& Delegate : DelegateArray)
							{
								UObject* DelegateObject = Delegate.GetUObject();
								ReferenceProcessor.HandleTokenStreamObjectReference(NewObjectsToSerialize, CurrentObject, DelegateObject, ReferenceTokenStreamIndex, false);
							}
						}
					}
					break;
					case GCRT_MulticastDelegate:
					{
						TokenReturnCount = ReferenceInfo.ReturnCount;
						if (ShouldProcessWeakReferences())
						{							
							FMulticastScriptDelegate& Delegate = *(FMulticastScriptDelegate*)(StackEntryData + ReferenceInfo.Offset);
							TArray<UObject*> DelegateObjects(Delegate.GetAllObjects());
							for (UObject* DelegateObject : DelegateObjects)
							{
								ReferenceProcessor.HandleTokenStreamObjectReference(NewObjectsToSerialize, CurrentObject, DelegateObject, ReferenceTokenStreamIndex, false);
							}
						}
					}
					break;
					case GCRT_ArrayMulticastDelegate:
					{
						TokenReturnCount = ReferenceInfo.ReturnCount;
						if (ShouldProcessWeakReferences())
						{							
							TArray<FMulticastScriptDelegate>& DelegateArray = *((TArray<FMulticastScriptDelegate>*)(StackEntryData + ReferenceInfo.Offset));
							for (FMulticastScriptDelegate& Delegate : DelegateArray)
							{
								TArray<UObject*> DelegateObjects(Delegate.GetAllObjects());
								for (UObject* DelegateObject : DelegateObjects)
								{
									ReferenceProcessor.HandleTokenStreamObjectReference(NewObjectsToSerialize, CurrentObject, DelegateObject, ReferenceTokenStreamIndex, false);
								}
							}
						}
					}
					break;
					case GCRT_EndOfStream:
					{
						// Break out of loop.
						goto EndLoop;
					}
					break;
					default:
					{
						UE_LOG(LogGarbage, Fatal, TEXT("Unknown token. Type:%d ReferenceTokenStreamIndex:%d Class:%s Obj:%s"), ReferenceInfo.Type, ReferenceTokenStreamIndex, CurrentObject ? *GetNameSafe(CurrentObject->GetClass()) : TEXT("Unknown"), *GetPathNameSafe(CurrentObject));
						break;
					}
				}
				}
EndLoop:
				check(StackEntry == Stack.GetData());

				if (IsParallel() && NewObjectsToSerialize.Num() >= MinDesiredObjectsPerSubTask)
				{
					// This will start queueing task with objects from the end of array until there's less objects than worth to queue
					const int32 ObjectsPerSubTask = FMath::Max<int32>(MinDesiredObjectsPerSubTask, NewObjectsToSerialize.Num() / FTaskGraphInterface::Get().GetNumWorkerThreads());
					while (NewObjectsToSerialize.Num() >= MinDesiredObjectsPerSubTask)
					{
						const int32 StartIndex = FMath::Max(0, NewObjectsToSerialize.Num() - ObjectsPerSubTask);
						const int32 NumThisTask = NewObjectsToSerialize.Num() - StartIndex;
						if (MyCompletionGraphEvent.GetReference())
						{
							MyCompletionGraphEvent->DontCompleteUntil(TGraphTask< FCollectorTask >::CreateTask().ConstructAndDispatchWhenReady(this, &NewObjectsToSerialize, StartIndex, NumThisTask, ArrayPool));
						}
						else
						{
							TaskQueue.AddTask(&NewObjectsToSerialize, StartIndex, NumThisTask);
						}
						NewObjectsToSerialize.SetNumUnsafeInternal(StartIndex);
					}
				}

#if PERF_DETAILED_PER_CLASS_GC_STATS
				// Detailed per class stats should not be performed when parallel GC is running
				check(!IsParallel());
				ReferenceProcessor.UpdateDetailedStats(CurrentObject, FPlatformTime::Cycles() - StartCycles);
#endif
			}

			if (IsParallel() && NewObjectsToSerialize.Num() >= MinDesiredObjectsPerSubTask)
			{
				const int32 ObjectsPerSubTask = FMath::Max<int32>(MinDesiredObjectsPerSubTask, NewObjectsToSerialize.Num() / FTaskGraphInterface::Get().GetNumWorkerThreads());
				int32 StartIndex = 0;
				while (StartIndex < NewObjectsToSerialize.Num())
				{
					const int32 NumThisTask = FMath::Min<int32>(ObjectsPerSubTask, NewObjectsToSerialize.Num() - StartIndex);
					if (MyCompletionGraphEvent.GetReference())
					{
						MyCompletionGraphEvent->DontCompleteUntil(TGraphTask< FCollectorTask >::CreateTask().ConstructAndDispatchWhenReady(this, &NewObjectsToSerialize, StartIndex, NumThisTask, ArrayPool));
					}
					else
					{
						TaskQueue.AddTask(&NewObjectsToSerialize, StartIndex, NumThisTask);
					}
					StartIndex += NumThisTask;
				}
				NewObjectsToSerialize.SetNumUnsafeInternal(0);
			}
			else if (NewObjectsToSerialize.Num())
			{
				// Don't spawn a new task, continue in the current one
				// To avoid allocating and moving memory around swap ObjectsToSerialize and NewObjectsToSerialize arrays
				Exchange(ObjectsToSerialize, NewObjectsToSerialize);
				// Empty but don't free allocated memory
				NewObjectsToSerialize.SetNumUnsafeInternal(0);

				CurrentIndex = 0;
			}
		}
		while (CurrentIndex < ObjectsToSerialize.Num());

#if PERF_DETAILED_PER_CLASS_GC_STATS
		// Detailed per class stats should not be performed when parallel GC is running
		check(!IsParallel());
		ReferenceProcessor.LogDetailedStatsSummary();
#endif

		ArrayPool.ReturnToPool(&NewObjectsToSerializeStruct);
	}
};


/** Default implementation for reference collector that can be used with TFastReferenceCollector */
template <typename ReferenceProcessorType, bool bIgnoringArchetypeRef = false, bool bIgnoringTransient = false>
class TDefaultReferenceCollector : public FReferenceCollector
{
	ReferenceProcessorType& Processor;
	FGCArrayStruct& ObjectArrayStruct;

public:
	TDefaultReferenceCollector(ReferenceProcessorType& InProcessor, FGCArrayStruct& InObjectArrayStruct)
		: Processor(InProcessor)
		, ObjectArrayStruct(InObjectArrayStruct)
	{
	}
	virtual void HandleObjectReference(UObject*& Object, const UObject* ReferencingObject, const FProperty* ReferencingProperty) override
	{
		Processor.HandleTokenStreamObjectReference(ObjectArrayStruct.ObjectsToSerialize, const_cast<UObject*>(ReferencingObject), Object, INDEX_NONE, false);
	}
	virtual void HandleObjectReferences(UObject** InObjects, const int32 ObjectNum, const UObject* ReferencingObject, const FProperty* InReferencingProperty) override
	{
		for (int32 ObjectIndex = 0; ObjectIndex < ObjectNum; ++ObjectIndex)
		{
			UObject*& Object = InObjects[ObjectIndex];
			Processor.HandleTokenStreamObjectReference(ObjectArrayStruct.ObjectsToSerialize, const_cast<UObject*>(ReferencingObject), Object, INDEX_NONE, false);
		}
	}
	virtual bool IsIgnoringArchetypeRef() const override
	{
		return bIgnoringArchetypeRef;
	}
	virtual bool IsIgnoringTransient() const override
	{
		return bIgnoringTransient;
	}
};

/** Simple single-threaded base implementation for reference processor that can be used with FFastReferenceCollector */
class FSimpleReferenceProcessorBase
{
public:
	FORCEINLINE int32 GetMinDesiredObjectsPerSubTask() const
	{
		// We only support single-threaded processing at the moment.
		return 0;
	}
	FORCEINLINE volatile bool IsRunningMultithreaded() const
	{
		// We only support single-threaded processing at the moment.
		return false;
	}
	FORCEINLINE void SetIsRunningMultithreaded(bool bIsParallel)
	{
		// We only support single-threaded processing at the moment.
		check(!bIsParallel);
	}
	void UpdateDetailedStats(UObject* CurrentObject, uint32 DeltaCycles)
	{
		// Do nothing
	}
	void LogDetailedStatsSummary()
	{
		// Do nothing
	}
	void SetCurrentObject(UObject* Obj)
	{
		// Do nothing
	}
	// Implement this in your derived class, don't make this virtual as it will affect performance!
	//FORCEINLINE void HandleTokenStreamObjectReference(TArray<UObject*>& ObjectsToSerialize, UObject* ReferencingObject, UObject*& Object, const int32 TokenIndex, bool bAllowReferenceElimination);
};