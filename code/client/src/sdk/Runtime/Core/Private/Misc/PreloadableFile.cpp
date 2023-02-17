// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/PreloadableFile.h"

#include "Async/Async.h"
#include "Async/AsyncFileHandle.h"
#include "GenericPlatform/GenericPlatformMisc.h"
#include "HAL/Event.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "Math/NumericLimits.h"
#include "ProfilingDebugging/CookStats.h"
#include "ProfilingDebugging/ScopedTimers.h"

#define PRELOADABLEFILE_COOK_STATS_ENABLED 0 && ENABLE_COOK_STATS

#if PRELOADABLEFILE_COOK_STATS_ENABLED
namespace FPreloadableFileImpl
{
	static int64 NumNonPreloadedPages = 0;
	static int64 NumPreloadedPages = 0;
	static double SerializeTime = 0;
	static double OpenFileTime = 0;
	FCriticalSection OpenFileTimeLock;
	static FCookStatsManager::FAutoRegisterCallback RegisterCookStats([](FCookStatsManager::AddStatFuncRef AddStat)
		{
			AddStat(TEXT("Package.Load"), FCookStatsManager::CreateKeyValueArray(TEXT("NumPreloadedPages"), NumPreloadedPages));
			AddStat(TEXT("Package.Load"), FCookStatsManager::CreateKeyValueArray(TEXT("NumNonPreloadedPages"), NumNonPreloadedPages));
			AddStat(TEXT("Package.Load"), FCookStatsManager::CreateKeyValueArray(TEXT("SerializeTime"), SerializeTime));
			AddStat(TEXT("Package.Load"), FCookStatsManager::CreateKeyValueArray(TEXT("OpenFileTime"), OpenFileTime));
		});
}
#endif

class FPreloadableFileProxy : public FArchive
{
public:
	explicit FPreloadableFileProxy(const TSharedPtr<FPreloadableFile>& InArchive)
		:Archive(InArchive)
	{
		check(Archive);
	}
	virtual void Seek(int64 InPos) final
	{
		Archive->Seek(InPos);
	}
	virtual int64 Tell() final
	{
		return Archive->Tell();
	}
	virtual int64 TotalSize() final
	{
		return Archive->TotalSize();
	}
	virtual bool Close() final
	{
		return Archive->Close();
	}
	virtual void Serialize(void* V, int64 Length) final
	{
		Archive->Serialize(V, Length);
	}
	virtual FString GetArchiveName() const final
	{
		return Archive->GetArchiveName();
	}

private:
	TSharedPtr<FPreloadableFile> Archive;
};

TMap<FString, TSharedPtr<FPreloadableFile>> FPreloadableFile::RegisteredFiles;

FPreloadableFile::FPreloadableFile(const TCHAR* InFileName)
	: FArchive()
	, FileName(InFileName)
	, bInitialized(false)
	, bIsPreloading(false)
	, bIsPreloadingPaused(false)
	, CacheEnd(0)
{
	PendingAsyncComplete = FPlatformProcess::GetSynchEventFromPool(true);
	PendingAsyncComplete->Trigger();
	FPaths::MakeStandardFilename(FileName);
	this->SetIsLoading(true);
	this->SetIsPersistent(true);
}

FPreloadableFile::~FPreloadableFile()
{
	Close();
	// It is possible we set a flag indicating that an async event is done, but we haven't yet called Trigger in the task thread; Trigger is always the last memory-accessing instruction on the task thread
	// This happens for example at the end of FPreloadableFile::InitializeInternal
	// Wait for the trigger call to be made before deleting PendingAsyncComplete.
	PendingAsyncComplete->Wait();
	FPlatformProcess::ReturnSynchEventToPool(PendingAsyncComplete);
}

void FPreloadableFile::SetPageSize(int64 InPageSize)
{
	if (!bInitialized)
	{
		PendingAsyncComplete->Wait();
	}
	if (bInitialized)
	{
		checkf(!bInitialized, TEXT("It is invalid to SetPageSize after initialization"));
		return;
	}
	PageSize = InPageSize;
}

void FPreloadableFile::InitializeAsync(uint32 InFlags, int64 PrimeSize)
{
	if (!bInitialized)
	{
		PendingAsyncComplete->Wait();
	}
	if (bInitialized)
	{
		return;
	}
	check(PendingAsyncComplete->Wait());
	PendingAsyncComplete->Reset();
	Async(EAsyncExecution::TaskGraph, [this, InFlags, PrimeSize] { InitializeInternal(InFlags, PrimeSize); });
}

bool FPreloadableFile::IsInitialized() const
{
	return bInitialized;
}

void FPreloadableFile::WaitForInitialization() const
{
	if (bInitialized)
	{
		return;
	}
	PendingAsyncComplete->Wait();
}

void FPreloadableFile::InitializeInternal(uint32 InFlags, int64 PrimeSize)
{
	check(!bInitialized);

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	uint32 Mode = InFlags & Flags::ModeBits;
	switch (Mode)
	{
	case Flags::PreloadBytes:
	{
		AsynchronousHandle.Reset(PlatformFile.OpenAsyncRead(*FileName));
		if (AsynchronousHandle)
		{
			IAsyncReadRequest* SizeRequest = AsynchronousHandle->SizeRequest();
			if (!SizeRequest)
			{
				// AsyncReadHandle is not working; fall back to the synchronous handle
				AsynchronousHandle.Reset();
			}
			else
			{
				SizeRequest->WaitCompletion();
				Size = SizeRequest->GetSizeResults();
				delete SizeRequest;
			}
		}
		break;
	}
	case Flags::PreloadHandle:
	{
		ConstructSynchronousArchive();
		if (SynchronousArchive)
		{
			Size = SynchronousArchive->TotalSize();
			if ((InFlags & (Flags::Prime)) && PrimeSize > 0)
			{
				SynchronousArchive->Precache(0, PrimeSize);
			}
		}
		break;
	}
	default:
	{
		checkf(false, TEXT("Invalid mode %u."), Mode);
		break;
	}
	}

#if FPRELOADABLEFILE_TEST_ENABLED
	if (Size != -1)
	{
		TestHandle.Reset(PlatformFile.OpenRead(*FileName, false));
		check(TestHandle);
	}
#endif

	FPlatformMisc::MemoryBarrier(); // Make sure all members written above are fully written before we set the thread-safe variable bInitialized to true
	bInitialized = true;
	FPlatformMisc::MemoryBarrier(); // Make sure bInitialized is fully written before we wake any threads waiting on PendingAsyncComplete
	PendingAsyncComplete->Trigger();
}

bool FPreloadableFile::TryRegister(const TSharedPtr<FPreloadableFile>& PreloadableFile)
{
	if (!PreloadableFile || !PreloadableFile->IsInitialized() || PreloadableFile->TotalSize() < 0)
	{
		return false;
	}

	TSharedPtr<FPreloadableFile>& ExistingFile = RegisteredFiles.FindOrAdd(PreloadableFile->FileName);
	if (ExistingFile)
	{
		return ExistingFile.Get() == PreloadableFile.Get();
	}

	ExistingFile = PreloadableFile;
	return true;
}

FArchive* FPreloadableFile::TryTakeArchive(const TCHAR* FileName)
{
	if (RegisteredFiles.Num() == 0)
	{
		return nullptr;
	}

	FString StandardFileName(FileName);
	FPaths::MakeStandardFilename(StandardFileName);
	TSharedPtr<FPreloadableFile> ExistingFile;
	if (!RegisteredFiles.RemoveAndCopyValue(*StandardFileName, ExistingFile))
	{
		return nullptr;
	}
	if (!ExistingFile->IsInitialized())
	{
		// Someone has called Close on the FPreloadableFile. Unregister it, and behave as if it had not been registered.
		return nullptr;
	}
	if (!ExistingFile->AsynchronousHandle)
	{
		// The PreloadableFile is in PreloadHandle mode; it is not preloading bytes, but instead is only providing a pre-opened (and possibly primed) sync handle
		// Detach the SynchronousHandle from the preloadable file and return that
		// The SynchronousArchive may be nullptr, which will indicate TryTakeArchive has nothing to offer for the given FileName.
		return ExistingFile->DetachLowerLevel();
	}
	else
	{
		// Return a proxy to the Preloadable file; it will use its cache to service serialize requests
		return new FPreloadableFileProxy(ExistingFile);
	}
}

bool FPreloadableFile::UnRegister(const TSharedPtr<FPreloadableFile>& PreloadableFile)
{
	if (!PreloadableFile)
	{
		return false;
	}

	TSharedPtr<FPreloadableFile> ExistingFile;
	if (!RegisteredFiles.RemoveAndCopyValue(PreloadableFile->FileName, ExistingFile))
	{
		return false;
	}

	if (ExistingFile.Get() != PreloadableFile.Get())
	{
		// Some other FPreloadableFile was registered for the same FileName. We removed it in the RemoveAndCopyValue above (which we do to optimize the common case).
		// Add it back, and notify the caller that their PreloadableFile was not registered.
		RegisteredFiles.Add(PreloadableFile->FileName, MoveTemp(ExistingFile));
		return false;
	}

	return true;
}

bool FPreloadableFile::StartPreload()
{
	if (bIsPreloading)
	{
		return true;
	}
	if (!bInitialized)
	{
		UE_LOG(LogCore, Error, TEXT("Attempted FPreloadableFile::StartPreload when uninitialized. Call will be ignored."));
		return false;
	}
	if (!AllocateCache())
	{
		return false;
	}
	// Wait for the async initialization task to complete, in case bInitialized = true was set on the InitializeAsync thread but PendingAsyncComplete->Trigger has not yet been called.
	// This might also wait for PendingAsyncComplete to finish after the last Preloading task completed, after setting bIsPreloading = false, but that's a don't-care because that
	// PendingAsyncComplete is triggered before the Preloading task thread exits the critical section that we enter below.
	PendingAsyncComplete->Wait();

	FScopeLock ScopeLock(&PreloadLock);
	bIsPreloading = true;
	check(!bIsPreloadingPaused); // IsPreloadingPaused is an internally-set value that is always reset to false before exiting from a public interface function
	ResumePreload();
	return true;
}

void FPreloadableFile::StopPreload()
{
	if (!bIsPreloading)
	{
		FScopeLock ScopeLock(&PreloadLock);
		FreeRetiredRequests();
		return;
	}
	PausePreload();
	bIsPreloading = false;
	bIsPreloadingPaused = false;
}

bool FPreloadableFile::IsPreloading() const
{
	// Note that this function is for public use only, and a true result does not indicate we have a currently pending Preload operation;
	// we may be paused even if bIsPreloading is true.
	return bIsPreloading;
}

bool FPreloadableFile::AllocateCache()
{
	if (IsCacheAllocated())
	{
		return true;
	}
	if (!bInitialized)
	{
		UE_LOG(LogCore, Error, TEXT("Attempted FPreloadableFile::AllocateCache when uninitialized. Call will be ignored."));
		return false;
	}
	if (Size < 0)
	{
		return false;
	}
	if (!AsynchronousHandle)
	{
		return false;
	}

	check(CacheBytes == nullptr); // Otherwise IsCacheAllocated would have returned true
	CacheBytes = reinterpret_cast<uint8*>(FMemory::Malloc(FMath::Max(Size, (int64)1)));
	return true;
}

void FPreloadableFile::ReleaseCache()
{
	if (!IsCacheAllocated())
	{
		return;
	}

	StopPreload();
#if PRELOADABLEFILE_COOK_STATS_ENABLED
	FPreloadableFileImpl::NumPreloadedPages += CacheEnd / PageSize;
	FPreloadableFileImpl::NumNonPreloadedPages += (Size - CacheEnd + PageSize - 1) / PageSize;
#endif
	FMemory::Free(CacheBytes);
	CacheBytes = nullptr;
	check(RetiredRequests.Num() == 0);
	RetiredRequests.Shrink();
}

bool FPreloadableFile::IsCacheAllocated() const
{
	return CacheBytes != nullptr;
}

FArchive* FPreloadableFile::DetachLowerLevel()
{
	WaitForInitialization();
	return SynchronousArchive.Release();
}

void FPreloadableFile::PausePreload()
{
	bIsPreloadingPaused = true;
	PendingAsyncComplete->Wait();

	{
		FScopeLock ScopeLock(&PreloadLock);
		FreeRetiredRequests();
	}
}

void FPreloadableFile::ResumePreload()
{
	// Contract: This function is only called when inside the PreloadLock CriticalSection
	// Contract: this function is only called when already initialized and no async reads are pending
	check(PendingAsyncComplete->Wait(0));

	bIsPreloadingPaused = false;
	PendingAsyncComplete->Reset();
	bool bComplete = ResumePreloadNonRecursive();
	if (!bReadCompleteWasCalledInline)
	{
		if (bComplete)
		{
			PendingAsyncComplete->Trigger();
		}
	}
	else
	{
		check(!bComplete);
		bool bCanceled;
		IAsyncReadRequest* ReadRequest;
		SavedReadCompleteArguments.Get(bCanceled, ReadRequest);

		// This call to OnReadComplete may result in further calls to ResumePreloadNonRecursive
		OnReadComplete(bCanceled, ReadRequest);
	}
}

bool FPreloadableFile::ResumePreloadNonRecursive()
{
	check(!PendingAsyncComplete->Wait(0)); // Caller should have set this before calling
	int64 RemainingSize = Size - CacheEnd;
	if (RemainingSize <= 0)
	{
		FPlatformMisc::MemoryBarrier(); // Make sure we have fully written any values of CacheEnd written by our caller before we set the thread-safe bIsPreloading value to false
		bIsPreloading = false;
		FPlatformMisc::MemoryBarrier(); // Our caller will call PendingAsyncComplete->Trigger; make sure that the thread-safe bIsPreloading value has been fully written before waking any threads waiting on PendingAsyncComplete
		return true;
	}
	if (bIsPreloadingPaused)
	{
		return true;
	}
	int64 ReadSize = FMath::Min(RemainingSize, PageSize);
	// If called from ResumePreload, these flags should all be false because we had no pending async call and we set them to false in the constructor or the last call to OnReadComplete
	// If called from OnReadComplete, OnReadComplete should have cleared them within the PreloadLock before calling
	check(!bIsInlineReadComplete && !bReadCompleteWasCalledInline); 
	bIsInlineReadComplete = true;
	FAsyncFileCallBack CompletionCallback = [this](bool bCanceled, IAsyncReadRequest* InRequest) { OnReadComplete(bCanceled, InRequest); };
	IAsyncReadRequest* ReadRequest = AsynchronousHandle->ReadRequest(CacheEnd, ReadSize, AIOP_Normal, &CompletionCallback, CacheBytes + CacheEnd);
	if (!ReadRequest)
	{
		// There was a bug with our request
		UE_LOG(LogCore, Warning, TEXT("ReadRequest returned null"));
		bIsInlineReadComplete = false;
		FPlatformMisc::MemoryBarrier(); // Make sure we have fully written any values of CacheEnd written by our caller before we set the thread-safe bIsPreloading variable to false
		bIsPreloading = false;
		FPlatformMisc::MemoryBarrier(); // Our caller will call PendingAsyncComplete->Trigger; make sure that the thread-safe bIsPreloading variable has been fully written before waking any threads waiting on PendingAsyncComplete
		return true;
	}
	bIsInlineReadComplete = false;
	return false;
}

void FPreloadableFile::OnReadComplete(bool bCanceled, IAsyncReadRequest* ReadRequest)
{
	TArray<IAsyncReadRequest*> LocalRetired;
	while (true)
	{
		FScopeLock ScopeLock(&PreloadLock);
		if (bIsInlineReadComplete)
		{
			SavedReadCompleteArguments.Set(bCanceled, ReadRequest);
			bReadCompleteWasCalledInline = true;
			check(LocalRetired.Num() == 0);
			return;
		}
		bReadCompleteWasCalledInline = false;
		FreeRetiredRequests();

		// We are not allowed to delete any ReadRequests until after other work that is done on the callback thread that occurs AFTER the callback has run, such as
		//  1) FAsyncTask::FinishThreadedWork which is called from FAsyncTask::DoThreadedWork, after the call to DoWork that results in our callback being called
		//  2) AsyncReadRequest::SetAllComplete which is called from AsyncReadRequest::SetComplete, after the call to SetDataComplete that results in our callback being called
		// So instead of deleting the request now, add it to the list of RetiredRequests. Both future calls to OnReadComplete and the class teardown code (Close and PausePrecache) will
		//  A) Wait for the request to complete, in case they are run simultaneously with this call thread after we have added the request to retired but before SetAllComplete is called
		//  B) Delete the request, which will then wait for FinishThreadedWork to be called if it hasn't already.
		// We need to add to a local copy of Retired until we're ready to return from OnReadComplete, so that later iterations of this loop inside of OnReadComplete do not attempt to wait on the ReadRequests
		//   we retired in earlier iterations.
		// One unfortunate side effect of this this retirement design is that we will hang on to the IAsyncReadRequest instance for the final request until Close is called.
		LocalRetired.Add(ReadRequest);

		uint8* ReadResults = ReadRequest->GetReadResults();
		if (bCanceled || !ReadResults)
		{
			UE_LOG(LogCore, Warning, TEXT("Precaching failed for %s: %s."), *FileName, (bCanceled ? TEXT("Canceled") : TEXT("GetReadResults returned null")));
			RetiredRequests.Append(MoveTemp(LocalRetired));
			FPlatformMisc::MemoryBarrier(); // Make sure we have fully written any values of CacheEnd written earlier in the loop before we set the thread-safe bIsPreloading value to false
			bIsPreloading = false;
			FPlatformMisc::MemoryBarrier(); // Make sure we have fully written bIsPreloading before we wake any threads waiting on PendingAsyncComplete
			PendingAsyncComplete->Trigger();
			return;
		}
		else
		{
			check(ReadResults == CacheBytes + CacheEnd);
			int64 ReadSize = FMath::Min(PageSize, Size - CacheEnd);
			FPlatformMisc::MemoryBarrier(); // Make sure we set have written the bytes (which our caller did at some point before calling OnReadComplete) before we increment the readonly-threadsafe variable CacheEnd
			CacheEnd += ReadSize;
			bool bComplete = ResumePreloadNonRecursive();
			if (!bReadCompleteWasCalledInline)
			{
				RetiredRequests.Append(MoveTemp(LocalRetired));
				if (bComplete)
				{
					PendingAsyncComplete->Trigger();
				}
				return;
			}
			else
			{
				check(!bComplete);
				// PrecacheNextPage's ReadRequest completed immediately, and called OnReadComplete on the current thread.
				// We made a design decision to not allow that inner OnReadComplete to recursively execute, as that could lead
				// to a stack of arbitrary depth if all the requested pages are already precached. Instead, we saved the calling values and returned.
				// Now that we have popped up to the outer OnReadComplete, use those saved values and run again.
				SavedReadCompleteArguments.Get(bCanceled, ReadRequest);
			}
		}
	}
}

void FPreloadableFile::FreeRetiredRequests()
{
	for (IAsyncReadRequest* Retired : RetiredRequests)
	{
		Retired->WaitCompletion();
		delete Retired;
	}
	RetiredRequests.Reset();
}


void FPreloadableFile::Serialize(void* V, int64 Length)
{
#if PRELOADABLEFILE_COOK_STATS_ENABLED
	FScopedDurationTimer ScopeTimer(FPreloadableFileImpl::SerializeTime);
#endif
#if FPRELOADABLEFILE_TEST_ENABLED
	if (!TestHandle)
	{
		SerializeInternal(V, Length);
		return;
	}

	int64 SavedPos = Pos;

	bool bWasPreloading = IsPreloading();
	TestHandle->Seek(Pos);
	TArray64<uint8> TestBytes;
	TestBytes.AddUninitialized(Length);
	TestHandle->Read(TestBytes.GetData(), Length);

	SerializeInternal(V, Length);

	bool bBytesMatch = FMemory::Memcmp(V, TestBytes.GetData(), Length) == 0;
	bool bPosMatch = Pos == TestHandle->Tell();
	if (!bBytesMatch || !bPosMatch)
	{
		UE_LOG(LogCore, Warning, TEXT("FPreloadableFile::Serialize Mismatch on %s. BytesMatch=%s, PosMatch=%s, WasPreloading=%s"),
			*FileName, (bBytesMatch ? TEXT("true") : TEXT("false")), (bPosMatch ? TEXT("true") : TEXT("false")),
			(bWasPreloading ? TEXT("true") : TEXT("false")));
		Seek(SavedPos);
		TestHandle->Seek(SavedPos);
		SerializeInternal(V, Length);
		TestHandle->Read(TestBytes.GetData(), Length);
	}
}

void FPreloadableFile::SerializeInternal(void* V, int64 Length)
{
#endif
	if (!bInitialized)
	{
		SetError();
		UE_LOG(LogCore, Error, TEXT("Attempted to Serialize from FPreloadableFile when not initialized."));
		return;
	}
	if (Pos + Length > Size)
	{
		SetError();
		UE_LOG(LogCore, Error, TEXT("Requested read of %d bytes when %d bytes remain (file=%s, size=%d)"), Length, Size - Pos, *FileName, Size);
		return;
	}

	if (!IsCacheAllocated())
	{
		SerializeFromSynchronousArchive(V, Length);
		return;
	}

	bool bLocalIsPreloading = bIsPreloading;
	int64 LocalCacheEnd = CacheEnd;
	int64 EndPos = Pos + Length;
	while (Pos < EndPos)
	{
		if (LocalCacheEnd > Pos)
		{
			int64 ReadLength = FMath::Min(LocalCacheEnd, EndPos) - Pos;
			FMemory::Memcpy(V, CacheBytes + Pos, ReadLength);
			V = ((uint8*)V) + ReadLength;
			Pos += ReadLength;
		}
		else
		{
			if (bLocalIsPreloading)
			{
				PausePreload();
				check(PendingAsyncComplete->Wait(0));
				LocalCacheEnd = CacheEnd;
				if (LocalCacheEnd > Pos)
				{
					// The page we just finished Preloading contains the position. Resume Preloading and continue in the loop to read from the now-available page.
					FScopeLock ScopeLock(&PreloadLock);
					ResumePreload();
					// ResumePreload may have found no further pages need to be preloaded, and it may have found some but immediately finished them and THEN found no further pages need to be preloaded.
					// So we can't assume bIsPreloading is still true after calling ResumePreload
					bLocalIsPreloading = bIsPreloading;
					continue;
				}
				else
				{
					// Turn off preloading for good, since we will be issuing synchronous IO to the same file from this point on
					bIsPreloading = false;
					bIsPreloadingPaused = false;
					bLocalIsPreloading = false;
				}
			}

			int64 ReadLength = EndPos - Pos;
			SerializeFromSynchronousArchive(V, ReadLength);
			V = ((uint8*)V) + ReadLength;
			// SerializeBuffered incremented Pos
		}
	}
}

void FPreloadableFile::SerializeFromSynchronousArchive(void* V, int64 Length)
{
	if (!SynchronousArchive)
	{
		ConstructSynchronousArchive();
		if (!SynchronousArchive)
		{
			UE_LOG(LogCore, Warning, TEXT("Failed to open file for %s"), *FileName);
			SetError();
			Pos += Length;
			return;
		}
	}
	SynchronousArchive->Seek(Pos);
	if (SynchronousArchive->IsError())
	{
		if (!IsError())
		{
			UE_LOG(LogCore, Warning, TEXT("Failed to seek to offset %ld in %s."), Pos, *FileName);
			SetError();
		}
	}
	else
	{
		SynchronousArchive->Serialize(V, Length);
		if (SynchronousArchive->IsError() && !IsError())
		{
			UE_LOG(LogCore, Warning, TEXT("Failed to read %ld bytes at offset %ld in %s."), Length, Pos, *FileName);
			SetError();
		}
	}
	Pos += Length;
	return;
}

void FPreloadableFile::ConstructSynchronousArchive()
{
	check(!SynchronousArchive);
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
#if PRELOADABLEFILE_COOK_STATS_ENABLED
	FScopeLock OpenFileTimeScopeLock(&FPreloadableFileImpl::OpenFileTimeLock);
	FScopedDurationTimer ScopedDurationTimer(FPreloadableFileImpl::OpenFileTime);
#endif

	SynchronousArchive.Reset(IFileManager::Get().CreateFileReader(*FileName));
}

void FPreloadableFile::Seek(int64 InPos)
{
	checkf(InPos >= 0, TEXT("Attempted to seek to a negative location (%lld/%lld), file: %s. The file is most likely corrupt."), InPos, Size, *FileName);
	checkf(InPos <= Size, TEXT("Attempted to seek past the end of file (%lld/%lld), file: %s. The file is most likely corrupt."), InPos, Size, *FileName);
	Pos = InPos;
}

int64 FPreloadableFile::Tell()
{
	return Pos;
}

int64 FPreloadableFile::TotalSize()
{
	return Size;
}

bool FPreloadableFile::Close()
{
	if (!bInitialized)
	{
		PendingAsyncComplete->Wait();
	}
	ReleaseCache();

	AsynchronousHandle.Reset();
	SynchronousArchive.Reset();
#if FPRELOADABLEFILE_TEST_ENABLED
	TestHandle.Reset();
#endif

	bInitialized = false;
	return !IsError();
}

FString FPreloadableFile::GetArchiveName() const
{
	return FileName;
}
