// Copyright Epic Games, Inc. All Rights Reserved.


#include "HAL/ThreadingBase.h"
#include "UObject/NameTypes.h"
#include "Stats/Stats.h"
#include "Misc/CommandLine.h"
#include "Misc/CoreStats.h"
#include "Misc/EventPool.h"
#include "Misc/LazySingleton.h"
#include "Misc/Fork.h"
#include "Templates/Atomic.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformStackWalk.h"
#include "ProfilingDebugging/MiscTrace.h"

#ifndef DEFAULT_FORK_PROCESS_MULTITHREAD
	#define DEFAULT_FORK_PROCESS_MULTITHREAD 0
#endif

DEFINE_STAT( STAT_EventWaitWithId );
DEFINE_STAT( STAT_EventTriggerWithId );

DECLARE_DWORD_COUNTER_STAT( TEXT( "ThreadPoolDummyCounter" ), STAT_ThreadPoolDummyCounter, STATGROUP_ThreadPoolAsyncTasks );

static bool GDoPooledThreadWaitTimeouts = false;
static FAutoConsoleVariableRef CVarDoPooledThreadWaitTimeouts(
	TEXT("DoPooledThreadWaitTimeouts"),
	GDoPooledThreadWaitTimeouts,
	TEXT("If enabled, uses the old behaviour for waking up pool threads every 10ms. Otherwise, lets pooled threads sleep until data arrives."),
	ECVF_Default
);

/** The global thread pool */
FQueuedThreadPool* GThreadPool = nullptr;

FQueuedThreadPool* GIOThreadPool = nullptr;

FQueuedThreadPool* GBackgroundPriorityThreadPool = nullptr;

#if WITH_EDITOR
FQueuedThreadPool* GLargeThreadPool = nullptr;
#endif

CORE_API bool IsInSlateThread()
{
	// If this explicitly is a slate thread, not just the main thread running slate
	return GSlateLoadingThreadId != 0 && FPlatformTLS::GetCurrentThreadId() == GSlateLoadingThreadId;
}

CORE_API TAtomic<bool> GIsAudioThreadSuspended(false);

CORE_API FRunnableThread* GAudioThread = nullptr;

CORE_API bool IsAudioThreadRunning()
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return (GAudioThread != nullptr) && !GIsAudioThreadSuspended.Load(EMemoryOrder::Relaxed);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

CORE_API bool IsInAudioThread()
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	// Check if audio thread null or if audio thread is suspended 
	if (nullptr == GAudioThread || GIsAudioThreadSuspended.Load(EMemoryOrder::Relaxed))
	{
		// If the audio thread is suspended or does not exist, true if in game thread. 
		return FPlatformTLS::GetCurrentThreadId() == GGameThreadId;
	}
	else
	{
		// If the audio thread is not suspended, true if in actual audio thread. 
		return FPlatformTLS::GetCurrentThreadId() == GAudioThread->GetThreadID();
	} 
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

CORE_API TAtomic<int32> GIsRenderingThreadSuspended(0);

CORE_API FRunnableThread* GRenderingThread = nullptr;

CORE_API bool IsInActualRenderingThread()
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return FPlatformTLS::GetCurrentThreadId() == GRenderThreadId;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

CORE_API bool IsInRenderingThread()
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return !GRenderingThread || GIsRenderingThreadSuspended.Load(EMemoryOrder::Relaxed) || (FPlatformTLS::GetCurrentThreadId() == GRenderingThread->GetThreadID());
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

CORE_API bool IsInParallelRenderingThread()
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	if (!GRenderingThread || GIsRenderingThreadSuspended.Load(EMemoryOrder::Relaxed))
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	{
		return true;
	}
	else
	{
		return FPlatformTLS::GetCurrentThreadId() != GGameThreadId;
	}
}

CORE_API uint32 GRHIThreadId = 0;
CORE_API FRunnableThread* GRHIThread_InternalUseOnly = nullptr;

CORE_API bool IsRHIThreadRunning()
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return GRHIThreadId != 0;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

CORE_API bool IsInRHIThread()
{
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return GRHIThreadId && FPlatformTLS::GetCurrentThreadId() == GRHIThreadId;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

// Fake threads

// Core version of IsInAsyncLoadingThread
static bool IsInAsyncLoadingThreadCoreInternal()
{
	// No async loading in Core
	return false;
}
bool(*IsInAsyncLoadingThread)() = &IsInAsyncLoadingThreadCoreInternal;

/**
 * Fake thread created when multi-threading is disabled.
 */
class FFakeThread : public FRunnableThread
{
	/** Thread Id pool */
	static uint32 ThreadIdCounter;

protected:

	/** Thread is suspended. */
	bool bIsSuspended;

	/** Runnable object associated with this thread. */
	FSingleThreadRunnable* SingleThreadRunnable;

public:

	/** Use the MSB as a mask to prevent clashes between kernel assigned thread ids and fake thread ids */
	static constexpr uint32 FakeIdReservedBit = 1 << 31;

	/** Constructor. */
	FFakeThread()
		: bIsSuspended(false)
		, SingleThreadRunnable(nullptr)
	{
		ThreadID = ThreadIdCounter++;
		ThreadID |= FakeIdReservedBit;
		// Auto register with single thread manager.
		FThreadManager::Get().AddThread(ThreadID, this);
	}

	/** Virtual destructor. */
	virtual ~FFakeThread()
	{
		// Remove from the manager.
		FThreadManager::Get().RemoveThread(this);
	}

	/** Tick one time per frame. */
	virtual void Tick() override
	{
		if (SingleThreadRunnable && !bIsSuspended)
		{
			SingleThreadRunnable->Tick();
		}
	}

public:

	// FRunnableThread interface

	virtual void SetThreadPriority(EThreadPriority NewPriority) override
	{
		// Not relevant.
	}

	virtual void Suspend(bool bShouldPause) override
	{
		bIsSuspended = bShouldPause;
	}

	virtual bool Kill(bool bShouldWait) override
	{
		FThreadManager::Get().RemoveThread(this);
		return true;
	}

	virtual void WaitForCompletion() override
	{
		FThreadManager::Get().RemoveThread(this);
	}

	virtual bool CreateInternal(FRunnable* InRunnable, const TCHAR* InThreadName,
		uint32 InStackSize,
		EThreadPriority InThreadPri, uint64 InThreadAffinityMask,
		EThreadCreateFlags InCreateFlags = EThreadCreateFlags::None) override

	{
		ThreadName = InThreadName;
		ThreadAffinityMask = InThreadAffinityMask;

		SingleThreadRunnable = InRunnable->GetSingleThreadInterface();
		if (SingleThreadRunnable)
		{
			InRunnable->Init();

			Runnable = InRunnable;
		}
		return SingleThreadRunnable != nullptr;
	}

protected:

	virtual FRunnableThread::ThreadType GetThreadType() const override
	{
		return ThreadType::Fake;
	}
};
uint32 FFakeThread::ThreadIdCounter = 0xffff;


void FThreadManager::AddThread(uint32 ThreadId, FRunnableThread* Thread)
{
	// Convert the thread's priority into an ordered value that is suitable
	// for sorting. Note we're using higher values so as to not collide with
	// existing trace data that's using TPri directly, and leaving gaps so
	// values can be added in between should need be
	int8 PriRemap[][2] = {
		{ TPri_TimeCritical,		0x10 },
		{ TPri_Highest,				0x20 },
		{ TPri_AboveNormal,			0x30 },
		{ TPri_Normal,				0x40 },
		{ TPri_SlightlyBelowNormal,	0x50 },
		{ TPri_BelowNormal,			0x60 },
		{ TPri_Lowest,				0x70 },
	};
	static_assert(TPri_Num == UE_ARRAY_COUNT(PriRemap), "Please update PriRemap when adding/removing thread priorities. Many thanks.");
	int32 SortHint = UE_ARRAY_COUNT(PriRemap);
	for (auto Candidate : PriRemap)
	{
		if (Candidate[0] == Thread->GetThreadPriority())
		{
			SortHint = Candidate[1];
			break;
		}
	}

	// Note that this must be called from thread being registered.
	Trace::ThreadRegister(*(Thread->GetThreadName()), Thread->GetThreadID(), SortHint);

	const bool bIsSingleThreadEnvironment = FPlatformProcess::SupportsMultithreading() == false;

	if (bIsSingleThreadEnvironment && Thread->GetThreadType() == FRunnableThread::ThreadType::Real)
	{
		checkf((ThreadId & FFakeThread::FakeIdReservedBit) == 0, TEXT("The thread ID  assigned by the kernel clashes with the bit reserved for identifying fake threads. Need to revisit the fake ID assignment algo."));
	}

	FScopeLock ThreadsLock(&ThreadsCritical);	

	// Some platforms do not support TLS
	if (!Threads.Contains(ThreadId))
	{
		Threads.Add(ThreadId, Thread);
	}
}

void FThreadManager::RemoveThread(FRunnableThread* Thread)
{
	FScopeLock ThreadsLock(&ThreadsCritical);
	const uint32* ThreadId = Threads.FindKey(Thread);
	if (ThreadId)
	{
		Threads.Remove(*ThreadId);
	}
}

void FThreadManager::Tick()
{	
	const bool bIsSingleThreadEnvironment = FPlatformProcess::SupportsMultithreading() == false;
	if (bIsSingleThreadEnvironment)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FSingleThreadManager_Tick);

		FScopeLock ThreadsLock(&ThreadsCritical);

		// Tick all registered fake threads.
		for (TPair<uint32, FRunnableThread*>& ThreadPair : Threads)
		{
			// Only fake and forkable threads are ticked by the ThreadManager
			if( ThreadPair.Value->GetThreadType() != FRunnableThread::ThreadType::Real )
			{
				ThreadPair.Value->Tick();
			}
		}
	}
}

const FString& FThreadManager::GetThreadNameInternal(uint32 ThreadId)
{
	static FString NoThreadName;
	FScopeLock ThreadsLock(&ThreadsCritical);
	FRunnableThread** Thread = Threads.Find(ThreadId);
	if (Thread)
	{
		return (*Thread)->GetThreadName();
	}
	return NoThreadName;
}

#if PLATFORM_WINDOWS || PLATFORM_MAC
static void GetAllThreadStackBackTraces_ProcessSingle(
	uint32 CurThreadId,
	uint32 ThreadId,
	const TCHAR* ThreadName,
	typename FThreadManager::FThreadStackBackTrace& OutStackTrace)
{
	constexpr uint32 MaxDepth = 100;
	OutStackTrace.ThreadId = ThreadId;
	OutStackTrace.ThreadName = ThreadName;
	auto& PCs = OutStackTrace.ProgramCounters;
	PCs.AddZeroed(MaxDepth);
	uint32 Depth;
	if (CurThreadId != ThreadId)
	{
		Depth = FPlatformStackWalk::CaptureThreadStackBackTrace(ThreadId, PCs.GetData(), MaxDepth);
	}
	else
	{
		Depth = FPlatformStackWalk::CaptureStackBackTrace(PCs.GetData(), MaxDepth);
	}
	PCs.SetNum(Depth);
}

void FThreadManager::GetAllThreadStackBackTraces(TArray<FThreadStackBackTrace>& StackTraces)
{
	const uint32 CurThreadId = FPlatformTLS::GetCurrentThreadId();
	FScopeLock Lock(&ThreadsCritical);
	const int32 NumThreads = Threads.Num() + 1;

	StackTraces.Empty(NumThreads);
	GetAllThreadStackBackTraces_ProcessSingle(CurThreadId, GGameThreadId, TEXT("GameThread"), StackTraces.AddDefaulted_GetRef());

	for (const TPair<uint32, FRunnableThread*>& Pair : Threads)
	{
		const uint32 Id = Pair.Key;
		const FString& Name = Pair.Value->GetThreadName();
		GetAllThreadStackBackTraces_ProcessSingle(CurThreadId, Id, *Name, StackTraces.AddDefaulted_GetRef());
	}
}
#endif

void FThreadManager::ForEachThread(TFunction<void(uint32, class FRunnableThread*)> Func)
{
	FScopeLock Lock(&ThreadsCritical);
	for (const TPair<uint32, FRunnableThread*>& Pair : Threads)
	{
		Func(Pair.Key, Pair.Value);
	}
}

FThreadManager& FThreadManager::Get()
{
	static FThreadManager Singleton;
	FThreadManager::bIsInitialized = true;
	return Singleton;
}

TArray<FRunnableThread*> FThreadManager::GetForkableThreads()
{
	TArray<FRunnableThread*> ForkableThreads;
	FScopeLock Lock(&ThreadsCritical);
	for (const TPair<uint32, FRunnableThread*>& Pair : Threads)
	{
		if (Pair.Value->GetThreadType() == FRunnableThread::ThreadType::Forkable)
		{
			ForkableThreads.Add(Pair.Value);
		}
	}

	return ForkableThreads;
}

bool FThreadManager::bIsInitialized = false;


/*-----------------------------------------------------------------------------
	FEvent, FScopedEvent
-----------------------------------------------------------------------------*/

TAtomic<uint32> FEvent::EventUniqueId;

void FEvent::AdvanceStats()
{
#if	STATS
	EventId = EventUniqueId++;
	EventStartCycles = 0;
#endif // STATS
}

void FEvent::WaitForStats()
{
#if	STATS
	// Only start counting on the first wait, trigger will "close" the history.
	if( FThreadStats::IsCollectingData() && EventStartCycles.Load(EMemoryOrder::Relaxed) == 0 )
	{
		const uint64 PacketEventIdAndCycles = ((uint64)EventId << 32) | 0;
		STAT_ADD_CUSTOMMESSAGE_PTR( STAT_EventWaitWithId, PacketEventIdAndCycles );
		EventStartCycles = FPlatformTime::Cycles();
	}
#endif // STATS
}

void FEvent::TriggerForStats()
{
#if	STATS
	// Only add wait-trigger pairs.
	uint32 LocalEventStartCycles = EventStartCycles.Load(EMemoryOrder::Relaxed);
	if( LocalEventStartCycles > 0 && FThreadStats::IsCollectingData() )
	{
		const uint32 EndCycles = FPlatformTime::Cycles();
		const int32 DeltaCycles = int32( EndCycles - LocalEventStartCycles );
		const uint64 PacketEventIdAndCycles = ((uint64)EventId << 32) | DeltaCycles;
		STAT_ADD_CUSTOMMESSAGE_PTR( STAT_EventTriggerWithId, PacketEventIdAndCycles );

		AdvanceStats();
	}
#endif // STATS
}

void FEvent::ResetForStats()
{
#if	STATS
	AdvanceStats();
#endif // STATS
}

FScopedEvent::FScopedEvent()
	: Event(TLazySingleton<FEventPool<EEventPoolTypes::AutoReset>>::Get().GetEventFromPool())
{ }

bool FScopedEvent::IsReady()
{
	if ( Event )
	{
		if ( Event->Wait(1) )
		{
			TLazySingleton<FEventPool<EEventPoolTypes::AutoReset>>::Get().ReturnToPool(Event);
			Event = nullptr;
			return true;
		}
		return false;
	}
	return true;
}

FScopedEvent::~FScopedEvent()
{
	if ( Event )
	{
		Event->Wait();
		TLazySingleton<FEventPool<EEventPoolTypes::AutoReset>>::Get().ReturnToPool(Event);
		Event = nullptr;
	}
}


/*-----------------------------------------------------------------------------
	FEventRef
-----------------------------------------------------------------------------*/

FEventRef::FEventRef(EEventMode Mode /* = EEventMode::AutoReset */)
	: Event(FPlatformProcess::GetSynchEventFromPool(Mode == EEventMode::ManualReset))
{}

FEventRef::~FEventRef()
{
	FPlatformProcess::ReturnSynchEventToPool(Event);
}

/*-----------------------------------------------------------------------------
	FRunnableThread
-----------------------------------------------------------------------------*/

uint32 FRunnableThread::RunnableTlsSlot = FRunnableThread::GetTlsSlot();

uint32 FRunnableThread::GetTlsSlot()
{
	check( IsInGameThread() );
	uint32 TlsSlot = FPlatformTLS::AllocTlsSlot();
	check( FPlatformTLS::IsValidTlsSlot( TlsSlot ) );
	return TlsSlot;
}

FRunnableThread::FRunnableThread()
	: Runnable(nullptr)
	, ThreadInitSyncEvent(nullptr)
	, ThreadAffinityMask(FPlatformAffinity::GetNoAffinityMask())
	, ThreadPriority(TPri_Normal)
	, ThreadID(0)
{
}

FRunnableThread::~FRunnableThread()
{
	if (!IsEngineExitRequested())
	{
		FThreadManager::Get().RemoveThread(this);
	}
}

FRunnableThread* FRunnableThread::Create(
	class FRunnable* InRunnable, 
	const TCHAR* ThreadName,
	uint32 InStackSize,
	EThreadPriority InThreadPri, 
	uint64 InThreadAffinityMask,
	EThreadCreateFlags InCreateFlags)
{
	bool bCreateRealThread = FPlatformProcess::SupportsMultithreading();

	FRunnableThread* NewThread = nullptr;

	if (bCreateRealThread)
	{
		check(InRunnable);
		// Create a new thread object
		NewThread = FPlatformProcess::CreateRunnableThread();
	}
	else if (InRunnable->GetSingleThreadInterface())
	{
		// Create a fake thread when multithreading is disabled.
		NewThread = new FFakeThread();
	}

	if (NewThread)
	{
		SetupCreatedThread(NewThread, InRunnable, ThreadName, InStackSize, InThreadPri, InThreadAffinityMask, InCreateFlags);
	}

	return NewThread;
}

void FRunnableThread::SetupCreatedThread(FRunnableThread*& NewThread, class FRunnable* InRunnable,  const TCHAR* ThreadName, uint32 InStackSize, EThreadPriority InThreadPri, uint64 InThreadAffinityMask, EThreadCreateFlags InCreateFlags)
{
	// Call the thread's create method
	bool bIsValid = NewThread->CreateInternal(InRunnable, ThreadName, InStackSize, InThreadPri, InThreadAffinityMask, InCreateFlags);

	if( bIsValid )
	{
		check(NewThread->Runnable);
		NewThread->PostCreate(InThreadPri);
	}
	else
	{
		// We failed to start the thread correctly so clean up
		delete NewThread;
		NewThread = nullptr;
	}
}

void FRunnableThread::PostCreate(EThreadPriority InThreadPriority)
{
#if	STATS
	FStartupMessages::Get().AddThreadMetadata( FName( *GetThreadName() ), GetThreadID() );
#endif // STATS
}

void FRunnableThread::SetTls()
{
	// Make sure it's called from the owning thread.
	check( ThreadID == FPlatformTLS::GetCurrentThreadId() );
	check( FPlatformTLS::IsValidTlsSlot(RunnableTlsSlot) );
	FPlatformTLS::SetTlsValue( RunnableTlsSlot, this );
}

void FRunnableThread::FreeTls()
{
	// Make sure it's called from the owning thread.
	check( ThreadID == FPlatformTLS::GetCurrentThreadId() );
	check( FPlatformTLS::IsValidTlsSlot(RunnableTlsSlot) );
	FPlatformTLS::SetTlsValue( RunnableTlsSlot, nullptr );

	// Delete all FTlsAutoCleanup objects created for this thread.
	for( auto& Instance : TlsInstances )
	{
		delete Instance;
		Instance = nullptr;
	}
}

/*-----------------------------------------------------------------------------
	FQueuedThread
-----------------------------------------------------------------------------*/

/**
 * This is the interface used for all poolable threads. The usage pattern for
 * a poolable thread is different from a regular thread and this interface
 * reflects that. Queued threads spend most of their life cycle idle, waiting
 * for work to do. When signaled they perform a job and then return themselves
 * to their owning pool via a callback and go back to an idle state.
 */
class FQueuedThread
	: public FRunnable
{
protected:

	/** The event that tells the thread there is work to do. */
	FEvent* DoWorkEvent = nullptr;

	/** If true, the thread should exit. */
	TAtomic<bool> TimeToDie { false };

	/** The work this thread is doing. */
	IQueuedWork* volatile QueuedWork = nullptr;

	/** The pool this thread belongs to. */
	class FQueuedThreadPoolBase* OwningThreadPool = nullptr;

	/** My Thread  */
	FRunnableThread* Thread = nullptr;

	/**
	 * The real thread entry point. It waits for work events to be queued. Once
	 * an event is queued, it executes it and goes back to waiting.
	 */
	virtual uint32 Run() override;

public:

	/** Default constructor **/
	FQueuedThread() = default;

	/**
	 * Creates the thread with the specified stack size and creates the various
	 * events to be able to communicate with it.
	 *
	 * @param InPool The thread pool interface used to place this thread back into the pool of available threads when its work is done
	 * @param InStackSize The size of the stack to create. 0 means use the current thread's stack size
	 * @param ThreadPriority priority of new thread
	 * @return True if the thread and all of its initialization was successful, false otherwise
	 */
	virtual bool Create(class FQueuedThreadPoolBase* InPool,uint32 InStackSize = 0,EThreadPriority ThreadPriority=TPri_Normal)
	{
		static int32 PoolThreadIndex = 0;
		const FString PoolThreadName = FString::Printf( TEXT( "PoolThread %d" ), PoolThreadIndex );
		PoolThreadIndex++;

		OwningThreadPool = InPool;
		DoWorkEvent = FPlatformProcess::GetSynchEventFromPool();
		Thread = FRunnableThread::Create(this, *PoolThreadName, InStackSize, ThreadPriority, FPlatformAffinity::GetPoolThreadMask());
		check(Thread);
		return true;
	}
	
	/**
	 * Tells the thread to exit. If the caller needs to know when the thread
	 * has exited, it should use the bShouldWait value and tell it how long
	 * to wait before deciding that it is deadlocked and needs to be destroyed.
	 * NOTE: having a thread forcibly destroyed can cause leaks in TLS, etc.
	 *
	 * @return True if the thread exited graceful, false otherwise
	 */
	bool KillThread()
	{
		bool bDidExitOK = true;
		// Tell the thread it needs to die
		TimeToDie = true;
		// Trigger the thread so that it will come out of the wait state if
		// it isn't actively doing work
		DoWorkEvent->Trigger();
		// If waiting was specified, wait the amount of time. If that fails,
		// brute force kill that thread. Very bad as that might leak.
		Thread->WaitForCompletion();
		// Clean up the event
		FPlatformProcess::ReturnSynchEventToPool(DoWorkEvent);
		DoWorkEvent = nullptr;
		delete Thread;
		return bDidExitOK;
	}

	/**
	 * Tells the thread there is work to be done. Upon completion, the thread
	 * is responsible for adding itself back into the available pool.
	 *
	 * @param InQueuedWork The queued work to perform
	 */
	void DoWork(IQueuedWork* InQueuedWork)
	{
		DECLARE_SCOPE_CYCLE_COUNTER( TEXT( "FQueuedThread::DoWork" ), STAT_FQueuedThread_DoWork, STATGROUP_ThreadPoolAsyncTasks );

		check(QueuedWork == nullptr && "Can't do more than one task at a time");
		// Tell the thread the work to be done
		QueuedWork = InQueuedWork;
		FPlatformMisc::MemoryBarrier();
		// Tell the thread to wake up and do its job
		DoWorkEvent->Trigger();
	}
};


/**
 * Implementation of a queued thread pool.
 */
class FQueuedThreadPoolBase : public FQueuedThreadPool
{
protected:

	/** The work queue to pull from. */
	TArray<IQueuedWork*> QueuedWork;
	
	/** The thread pool to dole work out to. */
	TArray<FQueuedThread*> QueuedThreads;

	/** All threads in the pool. */
	TArray<FQueuedThread*> AllThreads;

	/** The synchronization object used to protect access to the queued work. */
	FCriticalSection* SynchQueue;

	/** If true, indicates the destruction process has taken place. */
	bool TimeToDie;

public:

	/** Default constructor. */
	FQueuedThreadPoolBase()
		: SynchQueue(nullptr)
		, TimeToDie(0)
	{ }

	/** Virtual destructor (cleans up the synchronization objects). */
	virtual ~FQueuedThreadPoolBase()
	{
		Destroy();
	}

	virtual bool Create(uint32 InNumQueuedThreads, uint32 StackSize, EThreadPriority ThreadPriority, const TCHAR* Name) override
	{
		Trace::ThreadGroupBegin(Name);

		// Make sure we have synch objects
		bool bWasSuccessful = true;
		check(SynchQueue == nullptr);
		SynchQueue = new FCriticalSection();
		FScopeLock Lock(SynchQueue);
		// Presize the array so there is no extra memory allocated
		check(QueuedThreads.Num() == 0);
		QueuedThreads.Empty(InNumQueuedThreads);

		// Check for stack size override.
		if( OverrideStackSize > StackSize )
		{
			StackSize = OverrideStackSize;
		}

		// Now create each thread and add it to the array
		for (uint32 Count = 0; Count < InNumQueuedThreads && bWasSuccessful == true; Count++)
		{
			// Create a new queued thread
			FQueuedThread* pThread = new FQueuedThread();
			// Now create the thread and add it if ok
			if (pThread->Create(this,StackSize,ThreadPriority) == true)
			{
				QueuedThreads.Add(pThread);
				AllThreads.Add(pThread);
			}
			else
			{
				// Failed to fully create so clean up
				bWasSuccessful = false;
				delete pThread;
			}
		}
		// Destroy any created threads if the full set was not successful
		if (bWasSuccessful == false)
		{
			Destroy();
		}

		Trace::ThreadGroupEnd();
		return bWasSuccessful;
	}

	virtual void Destroy() override
	{
		if (SynchQueue)
		{
			{
				FScopeLock Lock(SynchQueue);
				TimeToDie = 1;
				FPlatformMisc::MemoryBarrier();
				// Clean up all queued objects
				for (int32 Index = 0; Index < QueuedWork.Num(); Index++)
				{
					QueuedWork[Index]->Abandon();
				}
				// Empty out the invalid pointers
				QueuedWork.Empty();
			}
			// wait for all threads to finish up
			while (1)
			{
				{
					FScopeLock Lock(SynchQueue);
					if (AllThreads.Num() == QueuedThreads.Num())
					{
						break;
					}
				}
				FPlatformProcess::Sleep(0.0f);
			}
			// Delete all threads
			{
				FScopeLock Lock(SynchQueue);
				// Now tell each thread to die and delete those
				for (int32 Index = 0; Index < AllThreads.Num(); Index++)
				{
					AllThreads[Index]->KillThread();
					delete AllThreads[Index];
				}
				QueuedThreads.Empty();
				AllThreads.Empty();
			}
			delete SynchQueue;
			SynchQueue = nullptr;
		}
	}

	int32 GetNumQueuedJobs() const
	{
		// this is a estimate of the number of queued jobs. 
		// no need for thread safe lock as the queuedWork array isn't moved around in memory so unless this class is being destroyed then we don't need to wrory about it
		return QueuedWork.Num();
	}
	virtual int32 GetNumThreads() const 
	{
		return AllThreads.Num();
	}
	void AddQueuedWork(IQueuedWork* InQueuedWork) override
	{
		check(InQueuedWork != nullptr);

		if (TimeToDie)
		{
			InQueuedWork->Abandon();
			return;
		}

		// Check to see if a thread is available. Make sure no other threads
		// can manipulate the thread pool while we do this.
		//
		// We pick a thread from the back of the array since this will be the
		// most recently used thread and therefore the most likely to have
		// a 'hot' cache for the stack etc (similar to Windows IOCP scheduling
		// strategy). Picking from the back also happens to be cheaper since
		// no memory movement is necessary.

		check(SynchQueue);

		FQueuedThread* Thread = nullptr;

		{
			FScopeLock sl(SynchQueue);
			const int32 AvailableThreadCount = QueuedThreads.Num();
			if (AvailableThreadCount == 0)
			{
				// No thread available, queue the work to be done
				// as soon as one does become available
				QueuedWork.Add(InQueuedWork);

				return;
			}

			const int32 ThreadIndex = AvailableThreadCount - 1;

			Thread = QueuedThreads[ThreadIndex];
			// Remove it from the list so no one else grabs it
			QueuedThreads.RemoveAt(ThreadIndex, 1, /* do not allow shrinking */ false);
		}

		// Tell our chosen thread to do the work
		Thread->DoWork(InQueuedWork);
	}

	virtual bool RetractQueuedWork(IQueuedWork* InQueuedWork) override
	{
		if (TimeToDie)
		{
			return false; // no special consideration for this, refuse the retraction and let shutdown proceed
		}
		check(InQueuedWork != nullptr);
		check(SynchQueue);
		FScopeLock sl(SynchQueue);
		return !!QueuedWork.RemoveSingle(InQueuedWork);
	}

	IQueuedWork* ReturnToPoolOrGetNextJob(FQueuedThread* InQueuedThread)
	{
		check(InQueuedThread != nullptr);
		IQueuedWork* Work = nullptr;
		// Check to see if there is any work to be done
		FScopeLock sl(SynchQueue);
		if (TimeToDie)
		{
			check(!QueuedWork.Num());  // we better not have anything if we are dying
		}
		if (QueuedWork.Num() > 0)
		{
			// Grab the oldest work in the queue. This is slower than
			// getting the most recent but prevents work from being
			// queued and never done
			Work = QueuedWork[0];
			// Remove it from the list so no one else grabs it
			QueuedWork.RemoveAt(0, 1, /* do not allow shrinking */ false);
		}
		if (!Work)
		{
			// There was no work to be done, so add the thread to the pool
			QueuedThreads.Add(InQueuedThread);
		}
		return Work;
	}
};

uint32 FQueuedThreadPool::OverrideStackSize = 0;

FQueuedThreadPool* FQueuedThreadPool::Allocate()
{
	return new FQueuedThreadPoolBase;
}

FQueuedThreadPool::FQueuedThreadPool() = default;
FQueuedThreadPool::~FQueuedThreadPool() = default;

//////////////////////////////////////////////////////////////////////////

uint32
FQueuedThread::Run()
{
	while (!TimeToDie.Load(EMemoryOrder::Relaxed))
	{
		// This will force sending the stats packet from the previous frame.
		SET_DWORD_STAT(STAT_ThreadPoolDummyCounter, 0);
		// We need to wait for shorter amount of time
		bool bContinueWaiting = true;

		// Unless we're collecting stats there doesn't appear to be any reason to wake
		// up again until there's work to do (or it's time to die)

#if STATS
		if (FThreadStats::IsCollectingData())
		{
			while (bContinueWaiting)
			{
				DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FQueuedThread::Run.WaitForWork"), STAT_FQueuedThread_Run_WaitForWork, STATGROUP_ThreadPoolAsyncTasks);

				// Wait for some work to do

				bContinueWaiting = !DoWorkEvent->Wait(GDoPooledThreadWaitTimeouts ? 10 : MAX_uint32);
			}
		}
#endif

		if (bContinueWaiting)
		{
			DoWorkEvent->Wait();
		}

		IQueuedWork* LocalQueuedWork = QueuedWork;
		QueuedWork = nullptr;
		FPlatformMisc::MemoryBarrier();
		check(LocalQueuedWork || TimeToDie.Load(EMemoryOrder::Relaxed)); // well you woke me up, where is the job or termination request?
		while (LocalQueuedWork)
		{
			// Tell the object to do the work
			LocalQueuedWork->DoThreadedWork();
			// Let the object cleanup before we remove our ref to it
			LocalQueuedWork = OwningThreadPool->ReturnToPoolOrGetNextJob(this);
		}
	}
	return 0;
}

/*-----------------------------------------------------------------------------
	FThreadSingletonInitializer
-----------------------------------------------------------------------------*/

FTlsAutoCleanup* FThreadSingletonInitializer::Get( TFunctionRef<FTlsAutoCleanup*()> CreateInstance, uint32& TlsSlot )
{
	if (TlsSlot == 0xFFFFFFFF)
	{
		const uint32 ThisTlsSlot = FPlatformTLS::AllocTlsSlot();
		check(FPlatformTLS::IsValidTlsSlot(ThisTlsSlot));
		const uint32 PrevTlsSlot = FPlatformAtomics::InterlockedCompareExchange( (int32*)&TlsSlot, (int32)ThisTlsSlot, 0xFFFFFFFF );
		if (PrevTlsSlot != 0xFFFFFFFF)
		{
			FPlatformTLS::FreeTlsSlot( ThisTlsSlot );
		}
	}
	FTlsAutoCleanup* ThreadSingleton = (FTlsAutoCleanup*)FPlatformTLS::GetTlsValue( TlsSlot );
	if( !ThreadSingleton )
	{
		ThreadSingleton = CreateInstance();
		ThreadSingleton->Register();
		FPlatformTLS::SetTlsValue( TlsSlot, ThreadSingleton );
	}
	return ThreadSingleton;
}

FTlsAutoCleanup* FThreadSingletonInitializer::TryGet(uint32& TlsSlot)
{
	if (TlsSlot == 0xFFFFFFFF)
	{
		return nullptr;
	}

	FTlsAutoCleanup* ThreadSingleton = (FTlsAutoCleanup*)FPlatformTLS::GetTlsValue(TlsSlot);
	return ThreadSingleton;
}

void FTlsAutoCleanup::Register()
{
	FRunnableThread* RunnableThread = FRunnableThread::GetRunnableThread();
	if( RunnableThread )
	{
		RunnableThread->TlsInstances.Add( this );
	}
}

//-------------------------------------------------------------------------------
// FForkableThread
//-------------------------------------------------------------------------------

/**
 * This thread starts as a fake thread and gets ticked like it was in a single-threaded environment.
 * Once it receives the OnPostFork event it creates and holds a real thread that
 * will cause the RunnableObject to be executed in it's own thread.
 */
class FForkableThread : public FFakeThread
{
	typedef FFakeThread Super;

private:

	/** Real thread that gets created right after forking */
	FRunnableThread* RealThread = nullptr;

	/** Cached values to use when the real thread is created post-fork */
	EThreadPriority CachedPriority = TPri_Normal;
	uint32 CachedStackSize = 0;

public:

	virtual void Tick() override
	{
		// Tick in single-thread mode when the real thread isn't created yet
		if(RealThread == nullptr)
		{
			Super::Tick();
		}
	}

	virtual void SetThreadPriority(EThreadPriority NewPriority) override
	{
		CachedPriority = NewPriority;
		
		if (RealThread)
		{
			RealThread->SetThreadPriority(NewPriority);
		}
	}

	virtual void Suspend(bool bShouldPause) override
	{
		Super::Suspend(bShouldPause);

		if (RealThread)
		{
			RealThread->Suspend(bShouldPause);
		}
	}

	virtual bool Kill(bool bShouldWait) override
	{
		bool bExitedCorrectly = true;

		if (RealThread)
		{
			bExitedCorrectly = RealThread->Kill(bShouldWait);
		}

		Super::Kill(bShouldWait);

		return bExitedCorrectly;
	}

	virtual void WaitForCompletion() override
	{
		if (RealThread)
		{
			RealThread->WaitForCompletion();
		}

		Super::WaitForCompletion();
	}

	virtual bool CreateInternal(FRunnable* InRunnable, const TCHAR* InThreadName, uint32 InStackSize, EThreadPriority InThreadPri, uint64 InThreadAffinityMask, EThreadCreateFlags InCreateFlags) override
	{
		checkf(FForkProcessHelper::SupportsMultithreadingPostFork(), TEXT("ForkableThreads should only be created when -PostForkThreading is enabled"));
		checkf(FForkProcessHelper::IsForkedMultithreadInstance() == false, TEXT("Once forked we create a real runnable thread instead of a ForkableThread"));

		// Call the fake thread creator
		bool bCreated = Super::CreateInternal(InRunnable, InThreadName, InStackSize, InThreadPri, InThreadAffinityMask, InCreateFlags);

		// Cache the target values until we create the real thread
		CachedStackSize = InStackSize;
		CachedPriority = InThreadPri;

		return bCreated;
	}

protected:

	virtual void OnPostFork() override
	{
		check(FForkProcessHelper::IsForkedMultithreadInstance());

		check(RealThread == nullptr);
		RealThread = FPlatformProcess::CreateRunnableThread();
		bool bCreated = RealThread->CreateInternal(Runnable, *GetThreadName(), CachedStackSize, CachedPriority, ThreadAffinityMask, EThreadCreateFlags::None);

		if (bCreated)
		{
			RealThread->PostCreate(CachedPriority);

			// Suspend the thread if the fake thread was suspended too
			//TODO: this lets the thread run for a few cycles before hitting the suspend call...
			if (bIsSuspended)
			{
				RealThread->Suspend(bIsSuspended);
			}
		}
		else
		{
			delete RealThread;
			RealThread = nullptr;
		}
	}

	virtual FRunnableThread::ThreadType GetThreadType() const override
	{
		return ThreadType::Forkable;
	}
};

//-------------------------------------------------------------------------------
// ForkableThreadHelper
//-------------------------------------------------------------------------------

bool FForkProcessHelper::bIsForkedMultithreadInstance = false;
bool FForkProcessHelper::bIsForkedChildProcess = false;

bool FForkProcessHelper::IsForkedChildProcess()
{
	return bIsForkedChildProcess;
}

void FForkProcessHelper::SetIsForkedChildProcess()
{
	bIsForkedChildProcess = true;
}

void FForkProcessHelper::OnForkingOccured()
{
	if( SupportsMultithreadingPostFork() )
	{
		ensureMsgf(GMalloc->IsInternallyThreadSafe(), TEXT("The BaseAllocator %s is not threadsafe. Switch to a multithread allocator or ensure the FMallocThreadSafeProxy wraps it."), GMalloc->GetDescriptiveName());

		bIsForkedMultithreadInstance = true;

		// Use a local list of forkable threads so we don't keep a lock on the global list during thread creation
		auto ForkableThreads = FThreadManager::Get().GetForkableThreads();
		for (FRunnableThread* ForkableThread : ForkableThreads)
		{
			ForkableThread->OnPostFork();
		}
	}
}

bool FForkProcessHelper::IsForkedMultithreadInstance()
{
	return bIsForkedMultithreadInstance;
}

bool FForkProcessHelper::SupportsMultithreadingPostFork()
{
	check(FCommandLine::IsInitialized());
#if DEFAULT_FORK_PROCESS_MULTITHREAD
	// Always multi thread unless manually turned off via command line
	static bool bSupportsMT = FParse::Param(FCommandLine::Get(), TEXT("DisablePostForkThreading")) == false;
	return bSupportsMT;
#else
	// Always single thread unless manually turned on via command line
	static bool bSupportsMT = FParse::Param(FCommandLine::Get(), TEXT("PostForkThreading")) == true;
	return bSupportsMT;
#endif
}

FRunnableThread* FForkProcessHelper::CreateForkableThread(class FRunnable* InRunnable, const TCHAR* InThreadName, uint32 InStackSize, EThreadPriority InThreadPri, uint64 InThreadAffinityMask, EThreadCreateFlags InCreateFlags)
{
	bool bCreateRealThread = FPlatformProcess::SupportsMultithreading();
	bool bCreateForkableThread(false);

	// Look for conditions allowing real threads in a non-multithread environment
	if (bCreateRealThread == false)
	{
		if( SupportsMultithreadingPostFork() )
		{
			if( IsForkedMultithreadInstance() )
			{
				// Already forked, create a real thread immediately
				bCreateRealThread = true;
			}
			else
			{
				// We have yet to fork the process, create a forkable thread to handle the fork event
				bCreateForkableThread = true;
			}
		
		}
	}

	FRunnableThread* NewThread(nullptr);
	if (bCreateRealThread)
	{
		check(InRunnable);
		NewThread = FPlatformProcess::CreateRunnableThread();
	}
	else if (bCreateForkableThread)
	{
		if( InRunnable->GetSingleThreadInterface() )
		{
			NewThread = new FForkableThread();
		}
	}
	else
	{
		if (InRunnable->GetSingleThreadInterface())
		{
			NewThread = new FFakeThread();
		}
	}

	if (NewThread)
	{
		FRunnableThread::SetupCreatedThread(NewThread, InRunnable, InThreadName, InStackSize, InThreadPri, InThreadAffinityMask, InCreateFlags);
	}

	return NewThread;
}
