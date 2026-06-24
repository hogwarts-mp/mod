// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	ParllelFor.h: TaskGraph library
=============================================================================*/

#pragma once

#include "CoreTypes.h"
#include "Misc/AssertionMacros.h"
#include "Math/UnrealMathUtility.h"
#include "Templates/Function.h"
#include "Templates/SharedPointer.h"
#include "HAL/ThreadSafeCounter.h"
#include "Stats/Stats.h"
#include "Async/TaskGraphInterfaces.h"
#include "Misc/App.h"
#include "Misc/Fork.h"

// Flags controlling the ParallelFor's behavior.
enum class EParallelForFlags
{
	// Default behavior
	None,

	//Mostly used for testing, when used, ParallelFor will run single threaded instead.
	ForceSingleThread = 1,

	//Offers better work distribution among threads at the cost of a little bit more synchronization.
	//This should be used for tasks with highly variable computational time.
	Unbalanced = 2,

	// if running on the rendering thread, make sure the ProcessThread is called when idle
	PumpRenderingThread = 4,

	// tasks should run on background priority threads
	BackgroundPriority = 8,
};

ENUM_CLASS_FLAGS(EParallelForFlags)

namespace ParallelForImpl
{
	// struct to hold the working data; this outlives the ParallelFor call; lifetime is controlled by a shared pointer
	template<typename FunctionType>
	struct TParallelForData
	{
		int32 Num;
		int32 BlockSize;
		int32 LastBlockExtraNum;
		FunctionType Body;
		FEvent* Event;
		FThreadSafeCounter IndexToDo;
		FThreadSafeCounter NumCompleted;
		bool bExited;
		bool bTriggered;
		bool bSaveLastBlockForMaster;
		TParallelForData(int32 InTotalNum, int32 InNumThreads, bool bInSaveLastBlockForMaster, FunctionType InBody, EParallelForFlags Flags)
			: Body(InBody)
			, Event(FPlatformProcess::GetSynchEventFromPool(false))
			, bExited(false)
			, bTriggered(false)
			, bSaveLastBlockForMaster(bInSaveLastBlockForMaster)
		{
			check(InTotalNum >= InNumThreads);

			if ((Flags & EParallelForFlags::Unbalanced) != EParallelForFlags::None)
			{
				BlockSize = 1;
				Num = InTotalNum;
			}
			else
			{
				BlockSize = 0;
				Num = 0;
				for (int32 Div = 6; Div; Div--)
				{
					BlockSize = InTotalNum / (InNumThreads * Div);
					if (BlockSize)
					{
						Num = InTotalNum / BlockSize;
						if (Num >= InNumThreads + !!bSaveLastBlockForMaster)
						{
							break;
						}
					}
				}
			}

			check(BlockSize && Num);
			LastBlockExtraNum = InTotalNum - Num * BlockSize;
			check(LastBlockExtraNum >= 0);
		}
		~TParallelForData()
		{
			check(IndexToDo.GetValue() >= Num);
			check(NumCompleted.GetValue() == Num);
			check(bExited);
			FPlatformProcess::ReturnSynchEventToPool(Event);
		}
		bool Process(int32 TasksToSpawn, TSharedRef<TParallelForData, ESPMode::ThreadSafe>& Data, ENamedThreads::Type InDesiredThread, bool bMaster);
	};

	template<typename FunctionType>
	class TParallelForTask
	{
		TSharedRef<TParallelForData<FunctionType>, ESPMode::ThreadSafe> Data;
		ENamedThreads::Type DesiredThread;
		int32 TasksToSpawn;
	public:
		TParallelForTask(TSharedRef<TParallelForData<FunctionType>, ESPMode::ThreadSafe>& InData, ENamedThreads::Type InDesiredThread, int32 InTasksToSpawn = 0)
			: Data(InData) 
			, DesiredThread(InDesiredThread)
			, TasksToSpawn(InTasksToSpawn)
		{
		}
		static FORCEINLINE TStatId GetStatId()
		{
			return GET_STATID(STAT_ParallelForTask);
		}
		
		FORCEINLINE ENamedThreads::Type GetDesiredThread()
		{
			return DesiredThread;
		}

		static FORCEINLINE ESubsequentsMode::Type GetSubsequentsMode() 
		{ 
			return ESubsequentsMode::FireAndForget; 
		}
		void DoTask(ENamedThreads::Type CurrentThread, const FGraphEventRef& MyCompletionGraphEvent)
		{
			FMemMark Mark(FMemStack::Get());
			if (Data->Process(TasksToSpawn, Data, DesiredThread, false))
			{
				checkSlow(!Data->bTriggered);
				Data->bTriggered = true;
				Data->Event->Trigger();
			}
		}
	};

	template<typename FunctionType>
	inline bool TParallelForData<FunctionType>::Process(int32 TasksToSpawn, TSharedRef<TParallelForData<FunctionType>, ESPMode::ThreadSafe>& Data, ENamedThreads::Type InDesiredThread, bool bMaster)
	{
		int32 MaybeTasksLeft = Num - IndexToDo.GetValue();
		if (TasksToSpawn && MaybeTasksLeft > 0)
		{
			TasksToSpawn = FMath::Min<int32>(TasksToSpawn, MaybeTasksLeft);
			TGraphTask<TParallelForTask<FunctionType>>::CreateTask().ConstructAndDispatchWhenReady(Data, InDesiredThread, TasksToSpawn - 1);
		}
		int32 LocalBlockSize = BlockSize;
		int32 LocalNum = Num;
		bool bLocalSaveLastBlockForMaster = bSaveLastBlockForMaster;
		TFunctionRef<void(int32)> LocalBody(Body);
		while (true)
		{
			int32 MyIndex = IndexToDo.Increment() - 1;
			if (bLocalSaveLastBlockForMaster)
			{
				if (!bMaster && MyIndex >= LocalNum - 1)
				{
					break; // leave the last block for the master, hoping to avoid an event
				}
				else if (bMaster && MyIndex > LocalNum - 1)
				{
					MyIndex = LocalNum - 1; // I am the master, I need to take this block, hoping to avoid an event
				}
			}
			if (MyIndex < LocalNum)
			{
				int32 ThisBlockSize = LocalBlockSize;
				if (MyIndex == LocalNum - 1)
				{
					ThisBlockSize += LastBlockExtraNum;
				}
				for (int32 LocalIndex = 0; LocalIndex < ThisBlockSize; LocalIndex++)
				{
					LocalBody(MyIndex * LocalBlockSize + LocalIndex);
				}
				checkSlow(!bExited);
				int32 LocalNumCompleted = NumCompleted.Increment();
				if (LocalNumCompleted == LocalNum)
				{
					return true;
				}
				checkSlow(LocalNumCompleted < LocalNum);
			}
			if (MyIndex >= LocalNum - 1)
			{
				break;
			}
		}
		return false;
	}

	template<typename FunctionType>
	inline void ParallelForInternal(int32 Num, FunctionType Body, EParallelForFlags Flags)
	{
		SCOPE_CYCLE_COUNTER(STAT_ParallelFor);
		check(Num >= 0);

		int32 AnyThreadTasks = 0;
		const bool bIsMultithread = FApp::ShouldUseThreadingForPerformance() || FForkProcessHelper::IsForkedMultithreadInstance();
		if (Num > 1 && (Flags & EParallelForFlags::ForceSingleThread) == EParallelForFlags::None && bIsMultithread)
		{
			AnyThreadTasks = FMath::Min<int32>(FTaskGraphInterface::Get().GetNumWorkerThreads(), Num - 1);
		}
		if (!AnyThreadTasks)
		{
			// no threads, just do it and return
			for (int32 Index = 0; Index < Num; Index++)
			{
				Body(Index);
			}
			return;
		}

		const bool bPumpRenderingThread         = (Flags & EParallelForFlags::PumpRenderingThread) != EParallelForFlags::None;
		const bool bBackgroundPriority          = (Flags & EParallelForFlags::BackgroundPriority) != EParallelForFlags::None;
		const ENamedThreads::Type DesiredThread = bBackgroundPriority ? ENamedThreads::AnyBackgroundThreadNormalTask : ENamedThreads::AnyHiPriThreadHiPriTask;

		TParallelForData<FunctionType>* DataPtr = new TParallelForData<FunctionType>(Num, AnyThreadTasks + 1, (Num > AnyThreadTasks + 1) && bPumpRenderingThread, Body, Flags);
		TSharedRef<TParallelForData<FunctionType>, ESPMode::ThreadSafe> Data = MakeShareable(DataPtr);
		TGraphTask<TParallelForTask<FunctionType>>::CreateTask().ConstructAndDispatchWhenReady(Data, DesiredThread, AnyThreadTasks - 1);
		// this thread can help too and this is important to prevent deadlock on recursion 
		if (!Data->Process(0, Data, DesiredThread, true))
		{
			if (bPumpRenderingThread && IsInActualRenderingThread())
			{
				while (!Data->Event->Wait(1))
				{
					FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GetRenderThread_Local());
				}
			}
			else
			{
				Data->Event->Wait();
			}
			check(Data->bTriggered);
		}
		else
		{
			check(!Data->bTriggered);
		}
		check(Data->NumCompleted.GetValue() == Data->Num);
		Data->bExited = true;
		// DoneEvent waits here if some other thread finishes the last item
		// Data must live on until all of the tasks are cleared which might be long after this function exits
	}
	
	/** 
		*	General purpose parallel for that uses the taskgraph
		*	@param Num; number of calls of Body; Body(0), Body(1)....Body(Num - 1)
		*	@param Body; Function to call from multiple threads
		*	@param CurrentThreadWorkToDoBeforeHelping; The work is performed on the main thread before it starts helping with the ParallelFor proper
		*	@param Flags; Used to customize the behavior of the ParallelFor if needed.
		*	Notes: Please add stats around to calls to parallel for and within your lambda as appropriate. Do not clog the task graph with long running tasks or tasks that block.
	**/
	template<typename FunctionType>
	inline void ParallelForWithPreWorkInternal(int32 Num, FunctionType Body, TFunctionRef<void()> CurrentThreadWorkToDoBeforeHelping, EParallelForFlags Flags = EParallelForFlags::None)
	{
		SCOPE_CYCLE_COUNTER(STAT_ParallelFor);

		int32 AnyThreadTasks = 0;
		const bool bIsMultithread = FApp::ShouldUseThreadingForPerformance() || FForkProcessHelper::IsForkedMultithreadInstance();
		if ((Flags & EParallelForFlags::ForceSingleThread) == EParallelForFlags::None && bIsMultithread)
		{
			AnyThreadTasks = FMath::Min<int32>(FTaskGraphInterface::Get().GetNumWorkerThreads(), Num);
		}
		if (!AnyThreadTasks)
		{
			// do the prework
			CurrentThreadWorkToDoBeforeHelping();
			// no threads, just do it and return
			for (int32 Index = 0; Index < Num; Index++)
			{
				Body(Index);
			}
			return;
		}
		check(Num);

		const bool bBackgroundPriority = (Flags & EParallelForFlags::BackgroundPriority) != EParallelForFlags::None;
		const ENamedThreads::Type DesiredThread = bBackgroundPriority ? ENamedThreads::AnyBackgroundThreadNormalTask : ENamedThreads::AnyHiPriThreadHiPriTask;

		TParallelForData<FunctionType>* DataPtr = new TParallelForData<FunctionType>(Num, AnyThreadTasks, false, Body, Flags);
		TSharedRef<TParallelForData<FunctionType>, ESPMode::ThreadSafe> Data = MakeShareable(DataPtr);
		TGraphTask<TParallelForTask<FunctionType>>::CreateTask().ConstructAndDispatchWhenReady(Data, DesiredThread, AnyThreadTasks - 1);
		// do the prework
		CurrentThreadWorkToDoBeforeHelping();
		// this thread can help too and this is important to prevent deadlock on recursion 
		if (!Data->Process(0, Data, DesiredThread, true))
		{
			if (IsInRenderingThread() && (Flags & EParallelForFlags::PumpRenderingThread) != EParallelForFlags::None)
			{
				while (!Data->Event->Wait(1))
				{
					FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GetRenderThread_Local());
				}
			}
			else
			{
				Data->Event->Wait();
			}
			check(Data->bTriggered);
		}
		else
		{
			check(!Data->bTriggered);
		}
		check(Data->NumCompleted.GetValue() == Data->Num);
		Data->bExited = true;
		// Data must live on until all of the tasks are cleared which might be long after this function exits
	}
}

/** 
	*	General purpose parallel for that uses the taskgraph
	*	@param Num; number of calls of Body; Body(0), Body(1)....Body(Num - 1)
	*	@param Body; Function to call from multiple threads
	*	@param bForceSingleThread; Mostly used for testing, if true, run single threaded instead.
	*	Notes: Please add stats around to calls to parallel for and within your lambda as appropriate. Do not clog the task graph with long running tasks or tasks that block.
**/
inline void ParallelFor(int32 Num, TFunctionRef<void(int32)> Body, bool bForceSingleThread, bool bPumpRenderingThread=false)
{
	ParallelForImpl::ParallelForInternal(Num, Body,
		(bForceSingleThread ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None) | 
		(bPumpRenderingThread ? EParallelForFlags::PumpRenderingThread : EParallelForFlags::None));
}

/**
	*	General purpose parallel for that uses the taskgraph
	*	@param Num; number of calls of Body; Body(0), Body(1)....Body(Num - 1)
	*	@param Body; Function to call from multiple threads
	*	@param bForceSingleThread; Mostly used for testing, if true, run single threaded instead.
	*	Notes: Please add stats around to calls to parallel for and within your lambda as appropriate. Do not clog the task graph with long running tasks or tasks that block.
**/
template<typename FunctionType>
inline void ParallelForTemplate(int32 Num, const FunctionType& Body, EParallelForFlags Flags = EParallelForFlags::None)
{
	ParallelForImpl::ParallelForInternal(Num, Body, Flags);
}
/** 
	*	General purpose parallel for that uses the taskgraph for unbalanced tasks
	*	Offers better work distribution among threads at the cost of a little bit more synchronization.
	*	This should be used for tasks with highly variable computational time.
	*
	*	@param Num; number of calls of Body; Body(0), Body(1)....Body(Num - 1)
	*	@param Body; Function to call from multiple threads
	*	@param Flags; Used to customize the behavior of the ParallelFor if needed.
	*	Notes: Please add stats around to calls to parallel for and within your lambda as appropriate. Do not clog the task graph with long running tasks or tasks that block.
**/
inline void ParallelFor(int32 Num, TFunctionRef<void(int32)> Body, EParallelForFlags Flags = EParallelForFlags::None)
{
	ParallelForImpl::ParallelForInternal(Num, Body, Flags);
}

/** 
	*	General purpose parallel for that uses the taskgraph
	*	@param Num; number of calls of Body; Body(0), Body(1)....Body(Num - 1)
	*	@param Body; Function to call from multiple threads
	*	@param CurrentThreadWorkToDoBeforeHelping; The work is performed on the main thread before it starts helping with the ParallelFor proper
	*	@param bForceSingleThread; Mostly used for testing, if true, run single threaded instead.
	*	Notes: Please add stats around to calls to parallel for and within your lambda as appropriate. Do not clog the task graph with long running tasks or tasks that block.
**/
inline void ParallelForWithPreWork(int32 Num, TFunctionRef<void(int32)> Body, TFunctionRef<void()> CurrentThreadWorkToDoBeforeHelping, bool bForceSingleThread, bool bPumpRenderingThread = false)
{
	ParallelForImpl::ParallelForWithPreWorkInternal(Num, Body, CurrentThreadWorkToDoBeforeHelping,
		(bForceSingleThread ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None) |
		(bPumpRenderingThread ? EParallelForFlags::PumpRenderingThread : EParallelForFlags::None));
}

/** 
	*	General purpose parallel for that uses the taskgraph
	*	@param Num; number of calls of Body; Body(0), Body(1)....Body(Num - 1)
	*	@param Body; Function to call from multiple threads
	*	@param CurrentThreadWorkToDoBeforeHelping; The work is performed on the main thread before it starts helping with the ParallelFor proper
	*	@param Flags; Used to customize the behavior of the ParallelFor if needed.
	*	Notes: Please add stats around to calls to parallel for and within your lambda as appropriate. Do not clog the task graph with long running tasks or tasks that block.
**/
inline void ParallelForWithPreWork(int32 Num, TFunctionRef<void(int32)> Body, TFunctionRef<void()> CurrentThreadWorkToDoBeforeHelping, EParallelForFlags Flags = EParallelForFlags::None)
{
	ParallelForImpl::ParallelForWithPreWorkInternal(Num, Body, CurrentThreadWorkToDoBeforeHelping, Flags);
}
