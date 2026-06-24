// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/OutputDeviceRedirector.h"

#include "Containers/BitArray.h"
#include "HAL/PlatformProcess.h"
#include "Misc/CoreStats.h"
#include "Misc/ScopeLock.h"
#include "Stats/Stats.h"

/*-----------------------------------------------------------------------------
	FOutputDeviceRedirector.
-----------------------------------------------------------------------------*/

class FLogAllocator
{
public:
	bool HasSpace(int32 NumChars) const
	{
		return Data[BufferIndex].Num() + NumChars <= BufferSize;
	}

	TCHAR* Alloc(int32 NumChars)
	{
		check(HasSpace(NumChars));
		int32 DataIndex = Data[BufferIndex].AddUninitialized(NumChars);
		return Data[BufferIndex].GetData() + DataIndex;
	}

	/**
	 * Swap which buffer is being used for new allocations.
	 *
	 * Buffer remains valid until BufferCount calls are made to this function.
	 */
	void SwapBuffers()
	{
		BufferIndex = (BufferIndex + 1) % BufferCount;
		// A failed check within InternalFlushThreadedLogs can cause a stack overflow. This can
		// currently fail because InternalFlushThreadedLogs can be called from multiple threads
		// simultaneously if errors cause SetCurrentThreadAsMasterThread to be called from more
		// than one thread at once, or while the game thread is in InternalFlushThreadedLogs.
		//check(!DataLocked[BufferIndex]);
		Data[BufferIndex].Empty();
	}

	struct FBufferLock 
	{ 
		int32 Index = -1;

		/** Returns true if this object represents a locked buffer */
		bool IsValid() const
		{
			return Index >= 0;
		}
	};

	FBufferLock LockBuffer()
	{
		DataLocked[BufferIndex] = true;
		return {BufferIndex};
	}

	void UnlockBuffer(FBufferLock Lock)
	{
		DataLocked[Lock.Index] = false;
	}

private:
	static constexpr int32 BufferSize = 4096;
	// BufferCount can be 2 once the check in SwapBuffers can be safely re-enabled. Using more
	// buffers in the interim minimizes the likelihood of writes to a buffer while it is still
	// being flushed by another thread.
	static constexpr int32 BufferCount = 4;
	TArray<TCHAR, TInlineAllocator<BufferSize>> Data[BufferCount];
	TBitArray<TInlineAllocator<1>> DataLocked{false, BufferCount};
	int32 BufferIndex = 0;
};

FBufferedLine::FBufferedLine(const TCHAR* InData, const FName& InCategory, ELogVerbosity::Type InVerbosity, double InTime, FLogAllocator* ExternalAllocator)
	: FBufferedLine(InData, FLazyName(InCategory), InVerbosity, InTime, ExternalAllocator)
{
}

FBufferedLine::FBufferedLine(const TCHAR* InData, const FLazyName& InCategory, ELogVerbosity::Type InVerbosity, double InTime, FLogAllocator* ExternalAllocator)
	: Category(InCategory)
	, Time(InTime)
	, Verbosity(InVerbosity)
{
	int32 NumChars = FCString::Strlen(InData) + 1;
	bExternalAllocation = ExternalAllocator && ExternalAllocator->HasSpace(NumChars);
	void* Dest = bExternalAllocation ? ExternalAllocator->Alloc(NumChars) : FMemory::Malloc(sizeof(TCHAR) * NumChars);
	Data = (TCHAR*)FMemory::Memcpy(Dest, InData, sizeof(TCHAR) * NumChars);
}

FBufferedLine::~FBufferedLine()
{
	if (!bExternalAllocation)
	{
		FMemory::Free(const_cast<TCHAR*>(Data));
	}
}

/** Initialization constructor. */
FOutputDeviceRedirector::FOutputDeviceRedirector(FLogAllocator* Allocator)
:	MasterThreadID(FPlatformTLS::GetCurrentThreadId())
,	bEnableBacklog(false)
,	BufferedLinesAllocator(Allocator)
{
}

static FLogAllocator GLogAllocator;

FOutputDeviceRedirector* FOutputDeviceRedirector::Get()
{
	static FOutputDeviceRedirector Singleton(&GLogAllocator);
	return &Singleton;
}

/**
 * Adds an output device to the chain of redirections.	
 *
 * @param OutputDevice	output device to add
 */
void FOutputDeviceRedirector::AddOutputDevice( FOutputDevice* OutputDevice )
{
	if (OutputDevice)
	{
		bool bAdded = false;
		do
		{
			{
				FScopeLock ScopeLock(&OutputDevicesMutex);
				if (OutputDevicesLockCounter.GetValue() == 0)
				{
					if (OutputDevice->CanBeUsedOnMultipleThreads())
					{
						UnbufferedOutputDevices.AddUnique(OutputDevice);
					}
					else
					{
						BufferedOutputDevices.AddUnique(OutputDevice);
					}
					bAdded = true;
				}
			}
			if (!bAdded)
			{
				FPlatformProcess::Sleep(0);
			}
		} while (!bAdded);
	}
}

/**
 * Removes an output device from the chain of redirections.	
 *
 * @param OutputDevice	output device to remove
 */
void FOutputDeviceRedirector::RemoveOutputDevice( FOutputDevice* OutputDevice )
{
	bool bRemoved = false;
	do
	{
		{
			FScopeLock ScopeLock(&OutputDevicesMutex);
			if (OutputDevicesLockCounter.GetValue() == 0)
			{
				BufferedOutputDevices.Remove( OutputDevice );
				UnbufferedOutputDevices.Remove(OutputDevice);
				bRemoved = true;
			}
		}
		if (!bRemoved)
		{
			FPlatformProcess::Sleep(0);
		}
	} while (!bRemoved);
}

/**
 * Returns whether an output device is currently in the list of redirectors.
 *
 * @param	OutputDevice	output device to check the list against
 * @return	true if messages are currently redirected to the the passed in output device, false otherwise
 */
bool FOutputDeviceRedirector::IsRedirectingTo( FOutputDevice* OutputDevice )
{
	// For performance reasons whe're not using the FOutputDevicesLock here
	FScopeLock OutputDevicesLock(&OutputDevicesMutex);
	return BufferedOutputDevices.Contains(OutputDevice) || UnbufferedOutputDevices.Contains(OutputDevice);
}

void FOutputDeviceRedirector::InternalFlushThreadedLogs(bool bUseAllDevices)
{
	TLocalOutputDevicesArray LocalBufferedDevices;
	TLocalOutputDevicesArray LocalUnbufferedDevices;
	FOutputDevicesLock OutputDevicesLock(this, LocalBufferedDevices, LocalUnbufferedDevices);

	InternalFlushThreadedLogs(LocalBufferedDevices, LocalUnbufferedDevices, bUseAllDevices);
}

/**
 * The unsynchronized version of FlushThreadedLogs.
 */
void FOutputDeviceRedirector::InternalFlushThreadedLogs(TLocalOutputDevicesArray& InBufferedDevices, TLocalOutputDevicesArray& InUnbufferedDevices, bool bUseAllDevices)
{	
	if (BufferedLines.Num())
	{
		TArray<FBufferedLine, TInlineAllocator<64>> LocalBufferedLines;
		FLogAllocator::FBufferLock BufferLock;
		{
			FScopeLock ScopeLock(&BufferSynchronizationObject);
			// Copy the buffered lines only if there's any buffered output devices
			if (InBufferedDevices.Num())
			{
				LocalBufferedLines.AddUninitialized(BufferedLines.Num());
				for (int32 LineIndex = 0; LineIndex < BufferedLines.Num(); LineIndex++)
				{
					new(&LocalBufferedLines[LineIndex]) FBufferedLine(BufferedLines[LineIndex], FBufferedLine::EMoveCtor);
				}
			}

			// If there's no output devices to redirect to (assumption is that we haven't added any yet)
			// don't clear the buffer otherwise its content will be lost (for example when calling SetCurrentThreadAsMasterThread() on init)
			if (InBufferedDevices.Num() || InUnbufferedDevices.Num())
			{
				if (BufferedLinesAllocator)
				{
					BufferLock = BufferedLinesAllocator->LockBuffer();
				}
				EmptyBufferedLines();
			}
		}

		for (FBufferedLine& Line : LocalBufferedLines)
		{
			const TCHAR* Data = Line.Data;
			const FLazyName Category = Line.Category;
			const double Time = Line.Time;
			const ELogVerbosity::Type Verbosity = Line.Verbosity;

			for (FOutputDevice* OutputDevice : InBufferedDevices)
			{
				if (OutputDevice->CanBeUsedOnAnyThread() || bUseAllDevices)
				{
					OutputDevice->Serialize(Data, Verbosity, Category, Time);
				}
			}
		}

		if (BufferLock.IsValid())
		{
			FScopeLock ScopeLock(&BufferSynchronizationObject);
			BufferedLinesAllocator->UnlockBuffer(BufferLock);
		}
	}
}

void FOutputDeviceRedirector::EmptyBufferedLines()
{
	BufferedLines.Empty();

	if (BufferedLinesAllocator)
	{
		BufferedLinesAllocator->SwapBuffers();
	}
}

/**
 * Flushes lines buffered by secondary threads.
 */
void FOutputDeviceRedirector::FlushThreadedLogs()
{
	//QUICK_SCOPE_CYCLE_COUNTER(STAT_FlushThreadedLogs);
	check(IsInGameThread());
	InternalFlushThreadedLogs(true);
}

void FOutputDeviceRedirector::PanicFlushThreadedLogs()
{
	//QUICK_SCOPE_CYCLE_COUNTER(STAT_FlushThreadedLogs);

	TLocalOutputDevicesArray LocalBufferedDevices;
	TLocalOutputDevicesArray LocalUnbufferedDevices;
	FOutputDevicesLock OutputDevicesLock(this, LocalBufferedDevices, LocalUnbufferedDevices);

	// Flush threaded logs, but use the safe version.
	InternalFlushThreadedLogs(LocalBufferedDevices, LocalUnbufferedDevices, false);

	// Flush devices.
	for (FOutputDevice* OutputDevice : LocalBufferedDevices)
	{
		if (OutputDevice->CanBeUsedOnAnyThread())
		{
			OutputDevice->Flush();
		}
	}

	for (FOutputDevice* OutputDevice : LocalUnbufferedDevices)
	{
		OutputDevice->Flush();
	}
}

/**
 * Serializes the current backlog to the specified output device.
 * @param OutputDevice	- Output device that will receive the current backlog
 */
void FOutputDeviceRedirector::SerializeBacklog( FOutputDevice* OutputDevice )
{
	FScopeLock ScopeLock( &SynchronizationObject );

	for (int32 LineIndex = 0; LineIndex < BacklogLines.Num(); LineIndex++)
	{
		const FBufferedLine& BacklogLine = BacklogLines[ LineIndex ];
		OutputDevice->Serialize( BacklogLine.Data, BacklogLine.Verbosity, BacklogLine.Category, BacklogLine.Time );
	}
}

/**
 * Enables or disables the backlog.
 * @param bEnable	- Starts saving a backlog if true, disables and discards any backlog if false
 */
void FOutputDeviceRedirector::EnableBacklog( bool bEnable )
{
	FScopeLock ScopeLock( &SynchronizationObject );

	bEnableBacklog = bEnable;
	if ( bEnableBacklog == false )
	{
		BacklogLines.Empty();
	}
}

/**
 * Sets the current thread to be the master thread that prints directly
 * (isn't queued up)
 */
void FOutputDeviceRedirector::SetCurrentThreadAsMasterThread()
{
	InternalFlushThreadedLogs( false );

	// make sure anything queued up is flushed out, this may be called from a background thread, so use the safe version.
	{
		FScopeLock ScopeLock(&SynchronizationObject);
		// set the current thread as the master thread
		MasterThreadID = FPlatformTLS::GetCurrentThreadId();
	}
}

void FOutputDeviceRedirector::LockOutputDevices(TLocalOutputDevicesArray& OutBufferedDevices, TLocalOutputDevicesArray& OutUnbufferedDevices)
{
	FScopeLock OutputDevicesLock(&OutputDevicesMutex);
	OutputDevicesLockCounter.Increment();
	OutBufferedDevices.Append(BufferedOutputDevices);
	OutUnbufferedDevices.Append(UnbufferedOutputDevices);
}

void FOutputDeviceRedirector::UnlockOutputDevices()
{
	FScopeLock OutputDevicesLock(&OutputDevicesMutex);
	int32 LockValue = OutputDevicesLockCounter.Decrement();
	check(LockValue >= 0);
}

template<class T>
void FOutputDeviceRedirector::SerializeImpl(const TCHAR* Data, ELogVerbosity::Type Verbosity, T& Category, const double Time)
{
	const double RealTime = Time == -1.0f ? FPlatformTime::Seconds() - GStartTime : Time;

	TLocalOutputDevicesArray LocalBufferedDevices;
	TLocalOutputDevicesArray LocalUnbufferedDevices;
	FOutputDevicesLock OutputDevicesLock(this, LocalBufferedDevices, LocalUnbufferedDevices);

#if PLATFORM_DESKTOP
	// this is for errors which occur after shutdown we might be able to salvage information from stdout 
	if ((LocalBufferedDevices.Num() == 0) && IsEngineExitRequested())
	{
#if PLATFORM_WINDOWS
		_tprintf(_T("%s\n"), Data);
#else
		FGenericPlatformMisc::LocalPrint(Data);
		// printf("%s\n", TCHAR_TO_ANSI(Data));
#endif
		return;
	}
#endif

	// Serialize directly to any output devices which don't require buffering
	for (FOutputDevice* OutputDevice : LocalUnbufferedDevices)
	{
		OutputDevice->Serialize(Data, Verbosity, Category, RealTime);
	}


	if (bEnableBacklog)
	{
		FScopeLock ScopeLock(&SynchronizationObject);
		new(BacklogLines)FBufferedLine(Data, Category, Verbosity, RealTime, nullptr);
	}

	if (FPlatformTLS::GetCurrentThreadId() != MasterThreadID || LocalBufferedDevices.Num() == 0)
	{
		FScopeLock ScopeLock(&BufferSynchronizationObject);
		new(BufferedLines)FBufferedLine(Data, Category, Verbosity, RealTime, BufferedLinesAllocator);
	}
	else
	{
		// Flush previously buffered lines from secondary threads.
		InternalFlushThreadedLogs(LocalBufferedDevices, LocalUnbufferedDevices, true);

		for (FOutputDevice* OutputDevice : LocalBufferedDevices)
		{
			OutputDevice->Serialize(Data, Verbosity, Category, RealTime);
		}
	}
}

void FOutputDeviceRedirector::Serialize( const TCHAR* Data, ELogVerbosity::Type Verbosity, const class FName& Category, const double Time )
{
	SerializeImpl(Data, Verbosity, Category, Time);
}

void FOutputDeviceRedirector::Serialize( const TCHAR* Data, ELogVerbosity::Type Verbosity, const class FName& Category )
{
	SerializeImpl( Data, Verbosity, Category, -1.0 );
}

void FOutputDeviceRedirector::RedirectLog(const FName& Category, ELogVerbosity::Type Verbosity, const TCHAR* Data)
{
	SerializeImpl(Data, Verbosity, Category, -1.0);
}

void FOutputDeviceRedirector::RedirectLog(const FLazyName& Category, ELogVerbosity::Type Verbosity, const TCHAR* Data)
{
	SerializeImpl(Data, Verbosity, Category, -1.0);
}

/**
 * Passes on the flush request to all current output devices.
 */
void FOutputDeviceRedirector::Flush()
{
	TLocalOutputDevicesArray LocalBufferedDevices;
	TLocalOutputDevicesArray LocalUnbufferedDevices;
	FOutputDevicesLock OutputDevicesLock(this, LocalBufferedDevices, LocalUnbufferedDevices);

	if(FPlatformTLS::GetCurrentThreadId() == MasterThreadID)
	{
		// Flush previously buffered lines from secondary threads.
		InternalFlushThreadedLogs(true);

		for (FOutputDevice* OutputDevice : LocalBufferedDevices)
		{
			OutputDevice->Flush();
		}
	}

	for (FOutputDevice* OutputDevice : LocalUnbufferedDevices)
	{
		OutputDevice->Flush();
	}
}

/**
 * Closes output device and cleans up. This can't happen in the destructor
 * as we might have to call "delete" which cannot be done for static/ global
 * objects.
 */
void FOutputDeviceRedirector::TearDown()
{
	FScopeLock SyncLock(&SynchronizationObject);
	check(FPlatformTLS::GetCurrentThreadId() == MasterThreadID);

	TLocalOutputDevicesArray LocalBufferedDevices;
	TLocalOutputDevicesArray LocalUnbufferedDevices;

	{
		// We need to lock the mutex here so that it gets unlocked after we empty the devices arrays
		FScopeLock GlobalOutputDevicesLock(&OutputDevicesMutex);
		LockOutputDevices(LocalBufferedDevices, LocalUnbufferedDevices);
		BufferedOutputDevices.Empty();
		UnbufferedOutputDevices.Empty();	
	}

	// Flush previously buffered lines from secondary threads.
	InternalFlushThreadedLogs(LocalBufferedDevices, LocalUnbufferedDevices, false);

	for (FOutputDevice* OutputDevice : LocalBufferedDevices)
	{
		if (OutputDevice->CanBeUsedOnAnyThread())
		{
			OutputDevice->Flush();
		}
		OutputDevice->TearDown();
	}

	for (FOutputDevice* OutputDevice : LocalUnbufferedDevices)
	{
		OutputDevice->Flush();
		OutputDevice->TearDown();
	}

	UnlockOutputDevices();
}

CORE_API FOutputDeviceRedirector* GetGlobalLogSingleton()
{
	return FOutputDeviceRedirector::Get();
}
