// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/BulkData2.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/PathViews.h"
#include "ProfilingDebugging/LoadTimeTracker.h"
#include "Serialization/BulkData.h"
#include "UObject/LinkerLoad.h"
#include "UObject/Object.h"
#include "UObject/Package.h"
#include "IO/IoDispatcher.h"
#include "Async/Async.h"
#include "Async/MappedFileHandle.h"

DEFINE_LOG_CATEGORY_STATIC(LogBulkDataRuntime, Log, All);

IMPLEMENT_TYPE_LAYOUT(FBulkDataBase);

// If set to 0 then we will pretend that optional data does not exist, useful for testing.
#define ALLOW_OPTIONAL_DATA 1

// Handy macro to validate FIoStatus return values
#define CHECK_IOSTATUS( InIoStatus, InMethodName ) checkf(InIoStatus.IsOk(), TEXT("%s failed: %s"), InMethodName, *InIoStatus.ToString());

namespace BulkDataExt
{
	// TODO: Maybe expose this and start using everywhere?
	const FString Export		= TEXT(".uexp");	// Stored in the export data
	const FString Default		= TEXT(".ubulk");	// Stored in a separate file
	const FString MemoryMapped	= TEXT(".m.ubulk");	// Stored in a separate file aligned for memory mapping
	const FString Optional		= TEXT(".uptnl");	// Stored in a separate file that is optional
}

namespace
{
	const uint16 InvalidBulkDataIndex = ~uint16(0);

	bool ShouldAllowBulkDataInIoStore()
	{
		static struct FAllowBulkDataInIoStore
		{
			bool bEnabled = true;

			FAllowBulkDataInIoStore()
			{
				FConfigFile PlatformEngineIni;
				FConfigCacheIni::LoadLocalIniFile(PlatformEngineIni, TEXT("Engine"), true, ANSI_TO_TCHAR(FPlatformProperties::IniPlatformName()));

				PlatformEngineIni.GetBool(TEXT("Core.System"), TEXT("AllowBulkDataInIoStore"), bEnabled);

				UE_LOG(LogSerialization, Display, TEXT("AllowBulkDataInIoStore: '%s'"), bEnabled ?  TEXT("true") :  TEXT("false"));
			}
		} AllowBulkDataInIoStore;

		return AllowBulkDataInIoStore.bEnabled;
	}

	FORCEINLINE bool IsIoDispatcherEnabled()
	{
		if (ShouldAllowBulkDataInIoStore())
		{
			return FIoDispatcher::IsInitialized();
		}
		else
		{
			return false;
		}
	}

	const FIoFilenameHash FALLBACK_IO_FILENAME_HASH = INVALID_IO_FILENAME_HASH - 1;
}

FIoFilenameHash MakeIoFilenameHash(const FString& Filename)
{
	if (!Filename.IsEmpty())
	{
		FString BaseFileName = FPaths::GetBaseFilename(Filename).ToLower();
		const FIoFilenameHash Hash = FCrc::StrCrc32<TCHAR>(*BaseFileName);
		return Hash != INVALID_IO_FILENAME_HASH ? Hash : FALLBACK_IO_FILENAME_HASH;
	}
	else
	{
		return INVALID_IO_FILENAME_HASH;
	}
}

FIoFilenameHash MakeIoFilenameHash(const FIoChunkId& ChunkID)
{
	if (ChunkID.IsValid())
	{
		const FIoFilenameHash Hash = GetTypeHash(ChunkID);
		return Hash != INVALID_IO_FILENAME_HASH ? Hash : FALLBACK_IO_FILENAME_HASH;
	}
	else
	{
		return INVALID_IO_FILENAME_HASH;
	}
}

// TODO: The code in the FileTokenSystem namespace is a temporary system so that FBulkDataBase can hold
// all of it's info about where the data is on disk in a single 8byte value. This can all be removed when
// we switch this over to the new packing system.
namespace FileTokenSystem
{
	struct Data
	{
		FString PackageHeaderFilename;
	};

	// Internal to the FileTokenSystem namespace
	namespace
	{
		struct StringData
		{
			FString Filename;
			uint16 RefCount;
		};

		/**
		* Provides a ref counted PackageName->Filename look up table.
		*/
		class FStringTable
		{
		public:
			using KeyType = uint64;

			void Add(const KeyType& Key, const FString& Filename)
			{
				if (StringData* ExistingEntry = Table.Find(Key))
				{
					ExistingEntry->RefCount++;
					checkf(ExistingEntry->Filename == Filename, TEXT("Filename mismatch!"));
				}
				else
				{
					StringData& NewEntry = Table.Emplace(Key);
					NewEntry.Filename = Filename;
					NewEntry.RefCount = 1;
				}
			}

			bool Remove(const KeyType& Key)
			{
				if (StringData* ExistingEntry = Table.Find(Key))
				{
					if (--ExistingEntry->RefCount == 0)
					{
						Table.Remove(Key);
						return true;
					}
				}

				return false;
			}

			void IncRef(const KeyType& Key)
			{
				if (StringData* ExistingEntry = Table.Find(Key))
				{
					ExistingEntry->RefCount++;
				}
			}

			const FString& Resolve(const KeyType& Key)
			{
				return Table.Find(Key)->Filename;
			}

			int32 Num() const
			{
				return Table.Num();
			}

		private:
			TMap<KeyType, StringData> Table;
		};
	}

	FStringTable StringTable;

	FRWLock TokenLock;

	FBulkDataOrId::FileToken RegisterFileToken( const FName& PackageName, const FString& Filename)
	{
		const uint64 Token = (uint64(PackageName.GetComparisonIndex().ToUnstableInt()) << 32) | uint64(PackageName.GetNumber());

		FWriteScopeLock LockForScope(TokenLock);
		StringTable.Add(Token, Filename);

		return Token;
	}

	void UnregisterFileToken(FBulkDataOrId::FileToken ID)
	{
		if (ID != FBulkDataBase::InvalidToken)
		{
			FWriteScopeLock LockForScope(TokenLock);

			StringTable.Remove(ID);
		}
	}

	FBulkDataOrId::FileToken CopyFileToken(FBulkDataOrId::FileToken ID)
	{
		if (ID != FBulkDataBase::InvalidToken)
		{
			FWriteScopeLock LockForScope(TokenLock);

			StringTable.IncRef(ID);

			return ID;
		}
		else
		{
			return FBulkDataBase::InvalidToken;
		}
	}

	FString GetFilename(FBulkDataOrId::FileToken ID)
	{
		if (ID == FBulkDataBase::InvalidToken)
		{
			return FString();
		}

		FReadScopeLock LockForScope(TokenLock);
		return StringTable.Resolve(ID);
	}
}

FIoDispatcher* FBulkDataBase::IoDispatcher = nullptr;

class FSizeChunkIdRequest : public IAsyncReadRequest
{
public:
	FSizeChunkIdRequest(const FIoChunkId& ChunkId, FAsyncFileCallBack* Callback)
		: IAsyncReadRequest(Callback, true, nullptr)
	{
		TIoStatusOr<uint64> Result = FBulkDataBase::GetIoDispatcher()->GetSizeForChunk(ChunkId);
		if (Result.IsOk())
		{
			Size = Result.ValueOrDie();
		}

		SetComplete();
	}

	virtual ~FSizeChunkIdRequest() = default;

private:

	virtual void WaitCompletionImpl(float TimeLimitSeconds) override
	{
		// Even though SetComplete called in the constructor and sets bCompleteAndCallbackCalled=true, we still need to implement WaitComplete as
		// the CompleteCallback can end up starting async tasks that can overtake the constructor execution and need to wait for the constructor to finish.
		while (!*(volatile bool*)&bCompleteAndCallbackCalled);
	}

	virtual void CancelImpl() override
	{
		// No point canceling as the work is done in the constructor
	}
};

// TODO: Currently shared between all FReadChunkIdRequest as the PS4/Pak implementation do but it would be
// worth profiling on some different platforms to see if we lose more perf from the potential increase in 
// locks vs the gain we get from not creating so many CriticalSections.
static FCriticalSection FReadChunkIdRequestEvent;

class FReadChunkIdRequest : public IAsyncReadRequest
{
public:
	FReadChunkIdRequest(const FIoChunkId& InChunkId, FAsyncFileCallBack* InCallback, uint8* InUserSuppliedMemory, int64 InOffset, int64 InBytesToRead)
		: IAsyncReadRequest(InCallback, false, InUserSuppliedMemory)
	{
		// Because IAsyncReadRequest can return ownership of the target memory buffer in the form
		// of a raw pointer we must pass our own memory buffer to the FIoDispatcher otherwise the 
		// buffer that will be returned cannot have it's lifetime managed correctly.
		if (InUserSuppliedMemory == nullptr)
		{
			Memory = (uint8*)FMemory::Malloc(InBytesToRead);
		}

		FIoReadOptions Options(InOffset, InBytesToRead);
		Options.SetTargetVa(Memory);

		auto OnRequestLoaded = [this](TIoStatusOr<FIoBuffer> Result)
		{
			if (!Result.Status().IsOk())
			{
				//TODO: Enable logging when we can give some more useful message
				//UE_LOG(LogSerialization, Error, TEXT("FReadChunkIdRequest failed: %s"), *Result.Status().ToString());
				// If there was an IO error then we need to count the request as canceled
				bCanceled = true;
			}

			SetDataComplete();

			{
				FScopeLock Lock(&FReadChunkIdRequestEvent);
				bRequestOutstanding = false;

				if (DoneEvent != nullptr)
				{
					DoneEvent->Trigger();
				}

				SetAllComplete();
			}
		};

		FIoBatch IoBatch = FBulkDataBase::GetIoDispatcher()->NewBatch();
		IoRequest = IoBatch.ReadWithCallback(InChunkId, Options, IoDispatcherPriority_Low, OnRequestLoaded);
		IoBatch.Issue();
	}

	virtual ~FReadChunkIdRequest()
	{
		// Make sure no other thread is waiting on this request
		checkf(DoneEvent == nullptr, TEXT("A thread is still waiting on a FReadChunkIdRequest that is being destroyed!")); 

		// Free memory if the request allocated it (although if the user accessed the memory after
		// reading then they will have taken ownership of it anyway, and if they didn't access the
		// memory then why did we read it in the first place?)
		if (Memory != nullptr && !bUserSuppliedMemory)
		{
			FMemory::Free(Memory);
		}

		// ~IAsyncReadRequest expects Memory to be nullptr, even if the memory was user supplied
		Memory = nullptr;
	}

protected:

	virtual void WaitCompletionImpl(float TimeLimitSeconds) override
	{
		// Make sure no other thread is waiting on this request
		checkf(DoneEvent == nullptr, TEXT("Multiple threads attempting to wait on the same FReadChunkIdRequest"));

		{
			FScopeLock Lock(&FReadChunkIdRequestEvent);
			if (bRequestOutstanding)
			{
				checkf(DoneEvent == nullptr, TEXT("Multiple threads attempting to wait on the same FReadChunkIdRequest"));
				DoneEvent = FPlatformProcess::GetSynchEventFromPool(true);
			}
		}

		if (DoneEvent != nullptr)
		{
			uint32 TimeLimitMilliseconds = TimeLimitSeconds <= 0.0f ? MAX_uint32 : (uint32)(TimeLimitSeconds * 1000.0f);
			DoneEvent->Wait(TimeLimitMilliseconds);

			FScopeLock Lock(&FReadChunkIdRequestEvent);
			FPlatformProcess::ReturnSynchEventToPool(DoneEvent);
			DoneEvent = nullptr;
		}

		// Make sure everything has completed
		checkf(bRequestOutstanding == false, TEXT("Request has not completed by the end of WaitCompletionImpl"));
		checkf(PollCompletion() == true, TEXT("Request and callback has not completed by the end of WaitCompletionImpl"));
	}

	virtual void CancelImpl() override
	{
		bCanceled = true;

		IoRequest.Cancel();
	}

	/** The ChunkId that is being read. */
	FIoChunkId ChunkId;
	/** Pending io request */
	FIoRequest IoRequest;
	/** Only actually gets created if WaitCompletion is called. */
	FEvent* DoneEvent = nullptr;
	/** True while the request is pending, true once it has either been completed or canceled. */
	bool bRequestOutstanding = true;
};

class FAsyncReadChunkIdHandle : public IAsyncReadFileHandle
{
public:
	FAsyncReadChunkIdHandle(const FIoChunkId& InChunkID) 
		: ChunkID(InChunkID)
	{

	}

	virtual ~FAsyncReadChunkIdHandle() = default;

	virtual IAsyncReadRequest* SizeRequest(FAsyncFileCallBack* CompleteCallback = nullptr) override
	{
		return new FSizeChunkIdRequest(ChunkID, CompleteCallback);
	}

	virtual IAsyncReadRequest* ReadRequest(int64 Offset, int64 BytesToRead, EAsyncIOPriorityAndFlags PriorityAndFlags = AIOP_Normal, FAsyncFileCallBack* CompleteCallback = nullptr, uint8* UserSuppliedMemory = nullptr) override
	{
		return new FReadChunkIdRequest(ChunkID, CompleteCallback, UserSuppliedMemory, Offset, BytesToRead);
	}
private:
	FIoChunkId ChunkID;
};
static FCriticalSection FBulkDataIoDispatcherRequestEvent;

class FBulkDataIoDispatcherRequest : public IBulkDataIORequest
{
public:
	FBulkDataIoDispatcherRequest(const FIoChunkId& InChunkID, int64 InOffsetInBulkData, int64 InBytesToRead, FBulkDataIORequestCallBack* InCompleteCallback, uint8* InUserSuppliedMemory)
		: UserSuppliedMemory(InUserSuppliedMemory)
	{
		RequestArray.Push({ InChunkID, (uint64)InOffsetInBulkData , (uint64)InBytesToRead });

		if (InCompleteCallback != nullptr)
		{
			CompleteCallback = *InCompleteCallback;
		}
	}

	FBulkDataIoDispatcherRequest(const FIoChunkId& InChunkID, FBulkDataIORequestCallBack* InCompleteCallback)
		: UserSuppliedMemory(nullptr)
	{
		const uint64 Size = FBulkDataBase::GetIoDispatcher()->GetSizeForChunk(InChunkID).ConsumeValueOrDie();
		RequestArray.Push({ InChunkID, 0, Size });

		if (InCompleteCallback != nullptr)
		{
			CompleteCallback = *InCompleteCallback;
		}
	}

	virtual ~FBulkDataIoDispatcherRequest()
	{
		WaitCompletion(0.0f); // Wait for ever as we cannot leave outstanding requests

		// Free the data is no caller has taken ownership of it and it was allocated by FBulkDataIoDispatcherRequest
		if (UserSuppliedMemory == nullptr)
		{
			FMemory::Free(DataResult);
			DataResult = nullptr;
		}

		// Make sure no other thread is waiting on this request
		checkf(DoneEvent == nullptr, TEXT("A thread is still waiting on a FBulkDataIoDispatcherRequest that is being destroyed!"));
	}

	void StartAsyncWork()
	{		
		checkf(RequestArray.Num() > 0, TEXT("RequestArray cannot be empty"));

		auto Callback = [this]()
		{
			bool bIsOk = true;
			for (Request& Request : RequestArray)
			{
				TIoStatusOr<FIoBuffer> RequestResult = Request.IoRequest.GetResult();
				bIsOk &= RequestResult.IsOk();
			}
			if (bIsOk)
			{
				SizeResult = IoBuffer.DataSize();

				if (IoBuffer.IsMemoryOwned())
				{
					DataResult = IoBuffer.Release().ConsumeValueOrDie();
				}
				else
				{
					DataResult = IoBuffer.Data();
				}
			}
			else
			{
				//TODO: Enable logging when we can give some more useful message
				//UE_LOG(LogSerialization, Error, TEXT("FBulkDataIoDispatcherRequest failed: %s"), *Result.Status().ToString());
				// If there was an IO error then we need to count the request as canceled
				bIsCanceled = true;
			}

			bDataIsReady = true;

			if (CompleteCallback)
			{
				CompleteCallback(bIsCanceled, this);
			}

			{
				FScopeLock Lock(&FReadChunkIdRequestEvent);
				bIsCompleted = true;

				if (DoneEvent != nullptr)
				{
					DoneEvent->Trigger();
				}
			}
		};

		FIoBatch IoBatch = FBulkDataBase::GetIoDispatcher()->NewBatch();

		uint64 TotalSize = 0;
		for (const Request& Request : RequestArray)
		{
			//checkf(!Request.IoRequest.IsValid(), TEXT("FBulkDataIoDispatcherRequest::StartAsyncWork was called twice"));
			TotalSize += Request.BytesToRead;
		}
		if (UserSuppliedMemory != nullptr)
		{
			IoBuffer = FIoBuffer(FIoBuffer::Wrap, UserSuppliedMemory, TotalSize);
		}
		else
		{
			IoBuffer = FIoBuffer(FIoBuffer::AssumeOwnership, FMemory::Malloc(TotalSize), TotalSize);
		}
		uint8* Dst = IoBuffer.Data();
		for (Request& Request : RequestArray)
		{
			FIoReadOptions ReadOptions(Request.OffsetInBulkData, Request.BytesToRead);
			ReadOptions.SetTargetVa(Dst);
			Request.IoRequest = IoBatch.Read(Request.ChunkId, ReadOptions, IoDispatcherPriority_Low);
			Dst += Request.BytesToRead;
		}

		IoBatch.IssueWithCallback(Callback);
	}

	virtual bool PollCompletion() const override
	{
		return bIsCompleted;
	}

	virtual bool WaitCompletion(float TimeLimitSeconds) override
	{
		// Make sure no other thread is waiting on this request
		checkf(DoneEvent == nullptr, TEXT("Multiple threads attempting to wait on the same FBulkDataIoDispatcherRequest"));

		{
			FScopeLock Lock(&FReadChunkIdRequestEvent);
			if (!bIsCompleted)
			{
				checkf(DoneEvent == nullptr, TEXT("Multiple threads attempting to wait on the same FBulkDataIoDispatcherRequest"));
				DoneEvent = FPlatformProcess::GetSynchEventFromPool(true);
			}
		}

		if (DoneEvent != nullptr)
		{
			uint32 TimeLimitMilliseconds = TimeLimitSeconds <= 0.0f ? MAX_uint32 : (uint32)(TimeLimitSeconds * 1000.0f);
			DoneEvent->Wait(TimeLimitMilliseconds);

			FScopeLock Lock(&FReadChunkIdRequestEvent);
			FPlatformProcess::ReturnSynchEventToPool(DoneEvent);
			DoneEvent = nullptr;
		}

		return bIsCompleted;
	}

	virtual uint8* GetReadResults() override
	{
		if (bDataIsReady && !bIsCanceled)
		{
			uint8* Result = DataResult;
			DataResult = nullptr;

			return Result;
		}
		else
		{
			return nullptr;
		}	
	}

	virtual int64 GetSize() const override
	{
		if (bDataIsReady && !bIsCanceled)
		{
			return SizeResult;
		}
		else
		{
			return INDEX_NONE;
		}
	}

	virtual void Cancel() override
	{
		if (!bIsCanceled)
		{
			bIsCanceled = true;
			FPlatformMisc::MemoryBarrier();
			for (Request& Request : RequestArray)
			{
				Request.IoRequest.Cancel();
			}
		}
	}

private:
	struct Request
	{
		FIoChunkId ChunkId;
		uint64 OffsetInBulkData;
		uint64 BytesToRead;
		FIoRequest IoRequest;
	};

	TArray<Request, TInlineAllocator<8>> RequestArray;

	FBulkDataIORequestCallBack CompleteCallback;
	uint8* UserSuppliedMemory = nullptr;

	uint8* DataResult = nullptr;
	int64 SizeResult = 0;

	bool bDataIsReady = false;

	bool bIsCompleted = false;
	bool bIsCanceled = false;

	/** Only actually gets created if WaitCompletion is called. */
	FEvent* DoneEvent = nullptr;

	FIoBuffer IoBuffer;
};

TUniquePtr<IBulkDataIORequest> CreateBulkDataIoDispatcherRequest(
	const FIoChunkId& InChunkID,
	int64 InOffsetInBulkData,
	int64 InBytesToRead,
	FBulkDataIORequestCallBack* InCompleteCallback,
	uint8* InUserSuppliedMemory)
{
	TUniquePtr<FBulkDataIoDispatcherRequest> Request;

	if (InBytesToRead > 0)
	{
		Request.Reset(new FBulkDataIoDispatcherRequest(InChunkID, InOffsetInBulkData, InBytesToRead, InCompleteCallback, InUserSuppliedMemory));
	}
	else
	{
		Request.Reset(new FBulkDataIoDispatcherRequest(InChunkID, InCompleteCallback));
	}

	Request->StartAsyncWork();

	return Request;
}

FBulkDataBase::FBulkDataBase(FBulkDataBase&& Other)
	: Data(Other.Data) // Copies the entire union
	, DataAllocation(Other.DataAllocation)
	, BulkDataSize(Other.BulkDataSize)
	, BulkDataOffset(Other.BulkDataOffset)
	, BulkDataFlags(Other.BulkDataFlags)

{
	checkf(Other.LockStatus != LOCKSTATUS_ReadWriteLock, TEXT("Attempting to read from a BulkData object that is locked for write")); 

	if (!Other.IsUsingIODispatcher())
	{
		Other.Data.Token = InvalidToken; // Prevent the other object from unregistering the token
	}	
}

FBulkDataBase& FBulkDataBase::operator=(const FBulkDataBase& Other)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FBulkDataBase::operator="), STAT_UBD_Constructor, STATGROUP_Memory);

	checkf(LockStatus == LOCKSTATUS_Unlocked, TEXT("Attempting to modify a BulkData object that is locked"));
	checkf(Other.LockStatus != LOCKSTATUS_ReadWriteLock, TEXT("Attempting to read from a BulkData object that is locked for write"));

	RemoveBulkData();

	if (Other.IsUsingIODispatcher())
	{
		Data.PackageID = Other.Data.PackageID;
	}
	else
	{
		Data.Token = FileTokenSystem::CopyFileToken(Other.Data.Token);
	}

	BulkDataSize = Other.BulkDataSize;
	BulkDataOffset = Other.BulkDataOffset;
	BulkDataFlags = Other.BulkDataFlags;

	if( !Other.IsDataMemoryMapped())
	{
		if (Other.GetDataBufferReadOnly())
		{
			void* Dst = AllocateData(BulkDataSize);
			FMemory::Memcpy(Dst, Other.GetDataBufferReadOnly(), BulkDataSize);
		}
	}
	else
	{
		// Note we don't need a fallback since the original already managed the load, if we fail now then it
		// is an actual error.
		if (Other.IsUsingIODispatcher())
		{
			TIoStatusOr<FIoMappedRegion> Status = IoDispatcher->OpenMapped(CreateChunkId(), FIoReadOptions());
			FIoMappedRegion MappedRegion = Status.ConsumeValueOrDie();
			DataAllocation.SetMemoryMappedData(this, MappedRegion.MappedFileHandle, MappedRegion.MappedFileRegion);		
		}
		else
		{
			const FString AssetFilename = FileTokenSystem::GetFilename(Data.Token);
			const FString MemoryMappedFilename = ConvertFilenameFromFlags(AssetFilename);
			MemoryMapBulkData(MemoryMappedFilename, BulkDataOffset, BulkDataSize);
		}
	}

	return *this;
}

FBulkDataBase::~FBulkDataBase()
{
	FlushAsyncLoading();

	checkf(LockStatus == LOCKSTATUS_Unlocked, TEXT("Attempting to modify a BulkData object that is locked"));

	FreeData();
	if (!IsUsingIODispatcher())
	{
		FileTokenSystem::UnregisterFileToken(Data.Token);
	}	
}

void FBulkDataBase::Serialize(FArchive& Ar, UObject* Owner, int32 /*Index*/, bool bAttemptFileMapping, int32 ElementSize)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FBulkDataBase::Serialize"), STAT_UBD_Serialize, STATGROUP_Memory);

	SCOPED_LOADTIMER(BulkData_Serialize);

#if WITH_EDITOR == 0 && WITH_EDITORONLY_DATA == 0
	if (Ar.IsPersistent() && !Ar.IsObjectReferenceCollector() && !Ar.ShouldSkipBulkData())
	{		
		checkf(Ar.IsLoading(), TEXT("FBulkDataBase only works with loading")); // Only support loading from cooked data!
		checkf(!GIsEditor, TEXT("FBulkDataBase does not work in the editor"));
		checkf(LockStatus == LOCKSTATUS_Unlocked, TEXT("Attempting to modify a BulkData object that is locked"));

		Ar << BulkDataFlags;

		int64 ElementCount = 0;
		int64 BulkDataSizeOnDisk = 0;

		BulkDataOffset = 0;

		if (BulkDataFlags & BULKDATA_Size64Bit)
		{
			Ar << ElementCount;
			Ar << BulkDataSizeOnDisk;
		}
		else
		{
			int32 Temp32ByteValue;

			Ar << Temp32ByteValue;
			ElementCount = Temp32ByteValue;

			Ar << Temp32ByteValue;
			BulkDataSizeOnDisk = Temp32ByteValue;
		}

		BulkDataSize = ElementCount * ElementSize;

		Ar << BulkDataOffset;

		const bool bUseIoDispatcher = IsIoDispatcherEnabled();

		if ((BulkDataFlags & BULKDATA_BadDataVersion) != 0)
		{
			uint16 DummyValue;
			Ar << DummyValue;
		}

		// Assuming that Owner/Package/Linker are all valid, the old BulkData system would
		// generally fail if any of these were nullptr but had plenty of inconsistent checks
		// scattered throughout.
		checkf(Owner != nullptr, TEXT("FBulkDataBase::Serialize requires a valid Owner"));
		const UPackage* Package = Owner->GetOutermost();
		checkf(Package != nullptr, TEXT("FBulkDataBase::Serialize requires n Owner that returns a valid UPackage"));

		if (!IsInlined() && bUseIoDispatcher)
		{
			Data.PackageID = Package->GetPackageId().Value();
			SetRuntimeBulkDataFlags(BULKDATA_UsesIoDispatcher); // Indicates that this BulkData should use the FIoChunkId rather than a filename
		}
		else
		{
			// Reset the token even though it should already be invalid (it will be set later when registered)
			Data.Token = InvalidToken;
		}

		const FString* Filename = nullptr;
		const FLinkerLoad* Linker = nullptr;

		FString FallbackFilename;

		if (bUseIoDispatcher == false)
		{
			Linker = FLinkerLoad::FindExistingLinkerForPackage(Package);

			if (Linker != nullptr)
			{
				Filename = &Linker->Filename;
			}
			else if (!::ShouldAllowBulkDataInIoStore() && !IsInlined())
			{
				const TCHAR* PackageExtension = Package->ContainsMap() ? TEXT(".umap") : TEXT(".uasset");
				if (FPackageName::TryConvertLongPackageNameToFilename(Package->FileName.ToString(), FallbackFilename, PackageExtension))
				{
					Filename = &FallbackFilename;
				}
				else
				{	
					// Note that this Bulkdata object will end up with an invalid token and will end up resolving to an empty file path!
					UE_LOG(LogSerialization, Warning, TEXT("LongPackageNameToFilename failed to convert '%s'. Path does not map to any roots!"), *Package->FileName.ToString());
				}				
			}
		}

		// Some failed paths require us to load the data before we return from ::Serialize but it is not
		// safe to do so until the end of this method. By setting this flag to true we can indicate that 
		// the load is required.
		bool bShouldForceLoad = false;

		if (IsInlined())
		{
			UE_CLOG(bAttemptFileMapping, LogSerialization, Error, TEXT("Attempt to file map inline bulk data, this will almost certainly fail due to alignment requirements. Package '%s'"), *Package->GetFName().ToString());

			// Inline data is already in the archive so serialize it immediately
			void* DataBuffer = AllocateData(BulkDataSize);
			SerializeBulkData(Ar, DataBuffer, BulkDataSize);

			// Inline data must be set to be allowed to always discard it's data if we are using the IoDispatcher
			// since we will not be able to reload it and existing code might well rely on ::GetCopy being able to discard it.
			// TODO: We need to make the old loader and new loader consistent on how inline data is treated!
			if (FIoDispatcher::IsInitialized())
			{
				SetBulkDataFlags(BULKDATA_AlwaysAllowDiscard);
			}
		}
		else
		{
			if (IsDuplicateNonOptional())
			{
				ProcessDuplicateData(Ar, Package, Filename, BulkDataOffset);
			}

			// Set during cook
			if (NeedsOffsetFixup())
			{
				checkf(Linker != nullptr, TEXT("BulkData needs it's offset fixing on load but no linker found, data cooked with 'LegacyBulkDataOffsets=true' will not work with the IoStore!"));
				BulkDataOffset += Linker->Summary.BulkDataStartOffset;
			}

			if (bAttemptFileMapping)
			{
				if (bUseIoDispatcher)
				{
					TIoStatusOr<FIoMappedRegion> Status = IoDispatcher->OpenMapped(CreateChunkId(), FIoReadOptions());
					if (Status.IsOk())
					{
						FIoMappedRegion MappedRegion = Status.ConsumeValueOrDie();
						DataAllocation.SetMemoryMappedData(this, MappedRegion.MappedFileHandle, MappedRegion.MappedFileRegion);
					}
					else
					{
						bShouldForceLoad = true; // Signal we want to force the BulkData to load
					}
				}
				else
				{
					// If we have no valid input file name then the package is broken anyway and we will not be able to find any memory mapped data!
					if (Filename != nullptr)
					{
						const FString MemoryMappedFilename = ConvertFilenameFromFlags(*Filename);
						if (!MemoryMapBulkData(MemoryMappedFilename, BulkDataOffset, BulkDataSize))
						{
							bShouldForceLoad = true; // Signal we want to force the BulkData to load
						}
					}
				}
			}
			else if (!Ar.IsAllowingLazyLoading() && !IsInSeparateFile())
			{

				// If the archive does not support lazy loading and the data is not in a different file then we have to load 
				// the data from the archive immediately as we won't get another chance.

				const int64 CurrentArchiveOffset = Ar.Tell();
				Ar.Seek(BulkDataOffset);

				void* DataBuffer = AllocateData(BulkDataSize);
				SerializeBulkData(Ar, DataBuffer, BulkDataSize);

				Ar.Seek(CurrentArchiveOffset); // Return back to the original point in the archive so future serialization can continue
			}
		}

		// If we are not using the FIoDispatcher and we have a filename then we need to make sure we can retrieve it later!
		if (bUseIoDispatcher == false && Filename != nullptr)
		{
			Data.Token = FileTokenSystem::RegisterFileToken(Package->GetFName(), *Filename);
		}

		if (bShouldForceLoad)
		{
			ForceBulkDataResident();
		}
	}
#else
	checkf(false, TEXT("FBulkDataBase does not work in the editor")); // Only implemented for cooked builds!
#endif
}

void* FBulkDataBase::Lock(uint32 LockFlags)
{
	checkf(LockStatus == LOCKSTATUS_Unlocked, TEXT("Attempting to lock a BulkData object that is already locked"));

	ForceBulkDataResident(); 	// If nothing is currently loaded then load from disk

	if (LockFlags & LOCK_READ_WRITE)
	{
		checkf(!IsDataMemoryMapped(), TEXT("Attempting to open a write lock on a memory mapped BulkData object, this will not work!"));
		LockStatus = LOCKSTATUS_ReadWriteLock;
		return GetDataBufferForWrite();
	}
	else if (LockFlags & LOCK_READ_ONLY)
	{
		LockStatus = LOCKSTATUS_ReadOnlyLock;
		return (void*)GetDataBufferReadOnly(); // Cast the const away, icky but our hands are tied by the original API at this time
	}
	else
	{
		UE_LOG(LogSerialization, Fatal, TEXT("Unknown lock flag %i"), LockFlags);
		return nullptr;
	}
}

const void* FBulkDataBase::LockReadOnly() const
{
	checkf(LockStatus == LOCKSTATUS_Unlocked, TEXT("Attempting to lock a BulkData object that is already locked"));
	LockStatus = LOCKSTATUS_ReadOnlyLock;

	return GetDataBufferReadOnly();
}

void FBulkDataBase::Unlock()
{
	checkf(LockStatus != LOCKSTATUS_Unlocked, TEXT("Attempting to unlock a BulkData object that is not locked"));
	LockStatus = LOCKSTATUS_Unlocked;

	// Free pointer if we're guaranteed to only to access the data once.
	if (IsSingleUse())
	{
		FreeData();
	}
}

bool FBulkDataBase::IsLocked() const
{ 
	return LockStatus != LOCKSTATUS_Unlocked;
}

void* FBulkDataBase::Realloc(int64 SizeInBytes)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FBulkDataBase::Realloc"), STAT_UBD_Realloc, STATGROUP_Memory);

	checkf(LockStatus == LOCKSTATUS_ReadWriteLock, TEXT("BulkData must be locked for 'write' before reallocating!"));
	checkf(!CanLoadFromDisk(), TEXT("Cannot re-allocate a FBulkDataBase object that represents a file on disk!"));

	// We might want to consider this a valid use case if anyone can come up with one?
	checkf(!IsUsingIODispatcher(), TEXT("Attempting to re-allocate data loaded from the IoDispatcher"));

	ReallocateData(SizeInBytes);

	BulkDataSize = SizeInBytes;

	return GetDataBufferForWrite();
}

void FBulkDataBase::GetCopy(void** DstBuffer, bool bDiscardInternalCopy)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FBulkDataBase::GetCopy"), STAT_UBD_GetCopy, STATGROUP_Memory);

	checkf(LockStatus == LOCKSTATUS_Unlocked, TEXT("Attempting to modify a BulkData object that is locked"));
	checkf(DstBuffer, TEXT("FBulkDataBase::GetCopy requires a valid DstBuffer"));	// Really should just use a reference to a pointer 
																					// but we are stuck trying to stick with the old bulkdata API for now

	// Wait for anything that might be currently loading
	FlushAsyncLoading();

	UE_CLOG(IsDataMemoryMapped(), LogSerialization, Warning, TEXT("FBulkDataBase::GetCopy being called on a memory mapped BulkData object, call ::StealFileMapping instead!"));

	if (*DstBuffer != nullptr)
	{
		// TODO: Might be worth changing the API so that we can validate that the buffer is large enough?
		if (IsBulkDataLoaded())
		{
			FMemory::Memcpy(*DstBuffer, GetDataBufferReadOnly(), GetBulkDataSize());

			if (bDiscardInternalCopy && CanDiscardInternalData())
			{
				UE_LOG(LogSerialization, Warning, TEXT("FBulkDataBase::GetCopy both copied and discarded it's data, passing in an empty pointer would avoid an extra allocate and memcpy!"));
				FreeData();
			}
		}
		else
		{
			LoadDataDirectly(DstBuffer);
		}
	}
	else
	{
		if (IsBulkDataLoaded())
		{
			if (bDiscardInternalCopy && CanDiscardInternalData())
			{
				// Since we were going to discard the data anyway we can just hand over ownership to the caller
				DataAllocation.Swap(this, DstBuffer);
			}
			else
			{
				*DstBuffer = FMemory::Malloc(BulkDataSize, 0);
				FMemory::Memcpy(*DstBuffer, GetDataBufferReadOnly(), BulkDataSize);
			}
		}
		else
		{
			LoadDataDirectly(DstBuffer);
		}
	}
}

FIoChunkId FBulkDataBase::CreateChunkId() const 
{ 
	checkf(IsUsingIODispatcher(), TEXT("Calling ::CreateChunkId on Bulkdata that is not using the IoDispatcher"));

	const EIoChunkType Type =	IsOptional() ? EIoChunkType::OptionalBulkData :
								IsFileMemoryMapped() ? EIoChunkType::MemoryMappedBulkData :
								EIoChunkType::BulkData;

	return CreateIoChunkId(Data.PackageID, 0, Type);
}

void FBulkDataBase::SetBulkDataFlags(uint32 BulkDataFlagsToSet)
{
	BulkDataFlags = EBulkDataFlags(BulkDataFlags | BulkDataFlagsToSet);
}

void FBulkDataBase::ResetBulkDataFlags(uint32 BulkDataFlagsToSet)
{
	BulkDataFlags = (EBulkDataFlags)BulkDataFlagsToSet;
}

void FBulkDataBase::ClearBulkDataFlags(uint32 BulkDataFlagsToClear)
{ 
	BulkDataFlags = EBulkDataFlags(BulkDataFlags & ~BulkDataFlagsToClear);
}

void FBulkDataBase::SetRuntimeBulkDataFlags(uint32 BulkDataFlagsToSet)
{
	checkf(	BulkDataFlagsToSet == BULKDATA_UsesIoDispatcher || 
			BulkDataFlagsToSet == BULKDATA_DataIsMemoryMapped || 
			BulkDataFlagsToSet == BULKDATA_HasAsyncReadPending,
			TEXT("Attempting to set an invalid runtime flag"));
			
	BulkDataFlags = EBulkDataFlags(BulkDataFlags | BulkDataFlagsToSet);
}

void FBulkDataBase::ClearRuntimeBulkDataFlags(uint32 BulkDataFlagsToClear)
{
	checkf(	BulkDataFlagsToClear == BULKDATA_UsesIoDispatcher ||
			BulkDataFlagsToClear == BULKDATA_DataIsMemoryMapped ||
			BulkDataFlagsToClear == BULKDATA_HasAsyncReadPending,
			TEXT("Attempting to clear an invalid runtime flag"));

	BulkDataFlags = EBulkDataFlags(BulkDataFlags & ~BulkDataFlagsToClear);
}

bool FBulkDataBase::NeedsOffsetFixup() const
{
	return (BulkDataFlags & BULKDATA_NoOffsetFixUp) == 0;
}

int64 FBulkDataBase::GetBulkDataSize() const
{
	return BulkDataSize;	
}

bool FBulkDataBase::CanLoadFromDisk() const 
{ 
	// If this BulkData is using the IoDispatcher then it can load from disk 
	if (IsUsingIODispatcher())
	{
		return true;
	}

	// If this BulkData has a fallback token then it can find it's filepath and load from disk
	if (Data.Token != InvalidToken)
	{
		return true;
	}

	return false;
}

bool FBulkDataBase::DoesExist() const
{
#if ALLOW_OPTIONAL_DATA
	if (!IsUsingIODispatcher())
	{
		FString Filename = FileTokenSystem::GetFilename(Data.Token);
		Filename = ConvertFilenameFromFlags(Filename);

		return IFileManager::Get().FileExists(*Filename);
	}
	else
	{
		return IoDispatcher->DoesChunkExist(CreateChunkId());
	}
#else
	return false;
#endif
}

bool FBulkDataBase::IsStoredCompressedOnDisk() const
{
	return (GetBulkDataFlags() & BULKDATA_SerializeCompressed) != 0;
}

FName FBulkDataBase::GetDecompressionFormat() const
{
	return (BulkDataFlags & BULKDATA_SerializeCompressedZLIB) ? NAME_Zlib : NAME_None;
}

bool FBulkDataBase::IsAvailableForUse() const 
{ 
	return (GetBulkDataFlags() & BULKDATA_Unused) != 0;
}

bool FBulkDataBase::IsDuplicateNonOptional() const
{
	return (GetBulkDataFlags() & BULKDATA_DuplicateNonOptionalPayload) != 0;
}

bool FBulkDataBase::IsOptional() const
{ 
	return (GetBulkDataFlags() & BULKDATA_OptionalPayload) != 0;
}

bool FBulkDataBase::IsInlined() const
{
	return	(GetBulkDataFlags() & BULKDATA_PayloadAtEndOfFile) == 0;
}

bool FBulkDataBase::IsInSeparateFile() const
{
	return	(GetBulkDataFlags() & BULKDATA_PayloadInSeperateFile) != 0;
}

bool FBulkDataBase::IsSingleUse() const
{
	return (BulkDataFlags & BULKDATA_SingleUse) != 0;
}

bool FBulkDataBase::IsFileMemoryMapped() const
{
	return (BulkDataFlags & BULKDATA_MemoryMappedPayload) != 0;
}

bool FBulkDataBase::IsDataMemoryMapped() const
{
	return (BulkDataFlags & BULKDATA_DataIsMemoryMapped) != 0;
}

bool FBulkDataBase::IsUsingIODispatcher() const
{
	return (BulkDataFlags & BULKDATA_UsesIoDispatcher) != 0;
}

IAsyncReadFileHandle* FBulkDataBase::OpenAsyncReadHandle() const
{
	if (IsUsingIODispatcher())
	{
		return new FAsyncReadChunkIdHandle(CreateChunkId());
	}
	else
	{
		return FPlatformFileManager::Get().GetPlatformFile().OpenAsyncRead(*GetFilename());
	}
}

IBulkDataIORequest* FBulkDataBase::CreateStreamingRequest(EAsyncIOPriorityAndFlags Priority, FBulkDataIORequestCallBack* CompleteCallback, uint8* UserSuppliedMemory) const
{
	const int64 DataSize = GetBulkDataSize();

	return CreateStreamingRequest(0, DataSize, Priority, CompleteCallback, UserSuppliedMemory);
}

IBulkDataIORequest* FBulkDataBase::CreateStreamingRequest(int64 OffsetInBulkData, int64 BytesToRead, EAsyncIOPriorityAndFlags Priority, FBulkDataIORequestCallBack* CompleteCallback, uint8* UserSuppliedMemory) const
{
	if (IsUsingIODispatcher())
	{
		checkf(OffsetInBulkData + BytesToRead <= BulkDataSize, TEXT("Attempting to read past the end of BulkData"));
		FBulkDataIoDispatcherRequest* BulkDataIoDispatcherRequest = new FBulkDataIoDispatcherRequest(CreateChunkId(), BulkDataOffset + OffsetInBulkData, BytesToRead, CompleteCallback, UserSuppliedMemory);
		BulkDataIoDispatcherRequest->StartAsyncWork();

		return BulkDataIoDispatcherRequest;
	}
	else
	{
		FString Filename = FileTokenSystem::GetFilename(Data.Token);
		int64 OffsetInFile = BulkDataOffset + OffsetInBulkData;

		// Fix up the Filename/Offset to work with streaming if EDL is enabled and the filename is still referencing a uasset or umap
		if (IsInlined() && GEventDrivenLoaderEnabled && (Filename.EndsWith(TEXT(".uasset")) || Filename.EndsWith(TEXT(".umap"))))
		{
			OffsetInFile -= IFileManager::Get().FileSize(*Filename);
			Filename = FPaths::GetBaseFilename(Filename, false) + BulkDataExt::Export;
		}
		else
		{
			Filename = ConvertFilenameFromFlags(Filename);
		}

		UE_CLOG(IsStoredCompressedOnDisk(), LogSerialization, Fatal, TEXT("Package level compression is no longer supported (%s)."), *Filename);
		UE_CLOG(BulkDataSize <= 0, LogSerialization, Error, TEXT("(%s) has invalid bulk data size."), *Filename);

		IAsyncReadFileHandle* IORequestHandle = FPlatformFileManager::Get().GetPlatformFile().OpenAsyncRead(*Filename);
		checkf(IORequestHandle, TEXT("OpenAsyncRead failed")); // An assert as there shouldn't be a way for this to fail

		if (IORequestHandle == nullptr)
		{
			return nullptr;
		}
		
		FBulkDataIORequest* IORequest = new FBulkDataIORequest(IORequestHandle);

		if (IORequest->MakeReadRequest(OffsetInFile, BytesToRead, Priority, CompleteCallback, UserSuppliedMemory))
		{
			return IORequest;
		}
		else
		{
			delete IORequest;
			return nullptr;
		}
	}
}

IBulkDataIORequest* FBulkDataBase::CreateStreamingRequestForRange(const BulkDataRangeArray& RangeArray, EAsyncIOPriorityAndFlags Priority, FBulkDataIORequestCallBack* CompleteCallback)
{
	checkf(RangeArray.Num() > 0, TEXT("RangeArray cannot be empty"));

	const FBulkDataBase& Start = *(RangeArray[0]);

	checkf(!Start.IsInlined(), TEXT("Cannot stream inlined BulkData"));

	if (Start.IsUsingIODispatcher())
	{
		const FBulkDataBase& End = *RangeArray[RangeArray.Num() - 1];

		checkf(Start.CreateChunkId() == End.CreateChunkId(), TEXT("BulkData range does not come from the same file (%s vs %s)"), *Start.GetFilename(), *End.GetFilename());

		const int64 ReadOffset = Start.GetBulkDataOffsetInFile();
		const int64 ReadLength = (End.GetBulkDataOffsetInFile() + End.GetBulkDataSize()) - ReadOffset;

		checkf(ReadLength > 0, TEXT("Read length is 0"));

		FBulkDataIoDispatcherRequest* IoRequest = new FBulkDataIoDispatcherRequest(Start.CreateChunkId(), ReadOffset, ReadLength, CompleteCallback, nullptr);
		IoRequest->StartAsyncWork();

		return IoRequest;
	}
	else
	{	
		const FBulkDataBase& End = *RangeArray[RangeArray.Num() - 1];

		checkf(Start.GetFilename() == End.GetFilename(), TEXT("BulkData range does not come from the same file (%s vs %s)"), *Start.GetFilename(), *End.GetFilename());

		const int64 ReadOffset = Start.GetBulkDataOffsetInFile();
		const int64 ReadLength = (End.GetBulkDataOffsetInFile() + End.GetBulkDataSize()) - ReadOffset;

		checkf(ReadLength > 0, TEXT("Read length is 0"));

		return Start.CreateStreamingRequest(0, ReadLength, Priority, CompleteCallback, nullptr);
	}
}

void FBulkDataBase::ForceBulkDataResident()
{
	// First wait for any async load requests to finish
	FlushAsyncLoading();

	// Then check if we actually need to load or not
	if (!IsBulkDataLoaded())
	{
		void* DataBuffer = nullptr;
		LoadDataDirectly(&DataBuffer);

		DataAllocation.SetData(this, DataBuffer);
	}
}

FOwnedBulkDataPtr* FBulkDataBase::StealFileMapping()
{
	checkf(LockStatus == LOCKSTATUS_Unlocked, TEXT("Attempting to modify a BulkData object that is locked"));

	return DataAllocation.StealFileMapping(this);
}

void FBulkDataBase::RemoveBulkData()
{
	checkf(LockStatus == LOCKSTATUS_Unlocked, TEXT("Attempting to modify a BulkData object that is locked"));

	FreeData();

	if (!IsUsingIODispatcher())
	{ 
		FileTokenSystem::UnregisterFileToken(Data.Token);
		Data.Token = InvalidToken;
	}

	BulkDataFlags = BULKDATA_None;

}

bool FBulkDataBase::StartAsyncLoading()
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FUntypedBulkData::StartAsyncLoading"), STAT_UBD_StartSerializingBulkData, STATGROUP_Memory);

	if (!IsAsyncLoadingComplete())
	{
		return true; // Early out if an asynchronous load is already in progress.
	}

	if (IsBulkDataLoaded())
	{
		return false; // Early out if we do not need to actually load any data
	}

	if (!CanLoadFromDisk())
	{
		return false; // Early out if we cannot load from disk
	}

	checkf(LockStatus == LOCKSTATUS_Unlocked, TEXT("Attempting to modify a BulkData object that is locked"));

	LockStatus = LOCKSTATUS_ReadWriteLock; // Bulkdata is effectively locked while streaming!

	// Indicate that we have an async read in flight
	SetRuntimeBulkDataFlags(BULKDATA_HasAsyncReadPending);
	FPlatformMisc::MemoryBarrier();

	AsyncCallback Callback = [this](TIoStatusOr<FIoBuffer> Result)
	{
		CHECK_IOSTATUS(Result.Status(), TEXT("FBulkDataBase::StartAsyncLoading"));
		FIoBuffer IoBuffer = Result.ConsumeValueOrDie();

		// It is assumed that ::LoadDataAsynchronously will allocate memory for the loaded data, so that we
		// do not need to take ownership here. This should guard against any future change in functionality.
		checkf(!IoBuffer.IsMemoryOwned(), TEXT("The loaded data is not owned by the BulkData object"));
		DataAllocation.SetData(this, IoBuffer.Data());

		FPlatformMisc::MemoryBarrier();

		ClearRuntimeBulkDataFlags(BULKDATA_HasAsyncReadPending);
		LockStatus = LOCKSTATUS_Unlocked;
	};

	LoadDataAsynchronously(MoveTemp(Callback));

	return true;
}

bool FBulkDataBase::IsAsyncLoadingComplete() const
{
	return (GetBulkDataFlags() & BULKDATA_HasAsyncReadPending) == 0;
}

int64 FBulkDataBase::GetBulkDataOffsetInFile() const
{
	return BulkDataOffset;
}

FIoFilenameHash FBulkDataBase::GetIoFilenameHash() const
{
	if (!IsUsingIODispatcher())
	{
		FString Filename = FileTokenSystem::GetFilename(Data.Token);
		return MakeIoFilenameHash(Filename);
	}
	else
	{
		return MakeIoFilenameHash(CreateChunkId());
	}
}

FString FBulkDataBase::GetFilename() const
{
	if (!IsUsingIODispatcher())
	{
		FString Filename = FileTokenSystem::GetFilename(Data.Token);
		return ConvertFilenameFromFlags(Filename);
	}
	else
	{
		UE_LOG(LogBulkDataRuntime, Warning, TEXT("Attempting to get the filename for BulkData that uses the IoDispatcher, this will return an empty string"));
		return FString("");
	}
}

bool FBulkDataBase::CanDiscardInternalData() const
{	
	// Data marked as single use should always be discarded
	if (IsSingleUse())
	{
		return true;
	}

	// If we can load from disk then we can discard it as it can be reloaded later
	if (CanLoadFromDisk())
	{
		return true;
	}

	// IF BULKDATA_AlwaysAllowDiscard has been set then we should always allow the data to 
	// be discarded even if it cannot be reloaded again.
	if ((BulkDataFlags & BULKDATA_AlwaysAllowDiscard) != 0)
	{
		return true;
	}

	return false;
}

void FBulkDataBase::LoadDataDirectly(void** DstBuffer)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FBulkDataBase::LoadDataDirectly"), STAT_UBD_LoadDataDirectly, STATGROUP_Memory);
	if (!CanLoadFromDisk())
	{
		// Only warn if the bulkdata have a valid size
		UE_CLOG(GetBulkDataSize() > 0, LogSerialization, Warning, TEXT("Attempting to load a BulkData object that cannot be loaded from disk"));

		return; // Early out if there is nothing to load anyway
	}

	if (!IsIoDispatcherEnabled())
	{
		InternalLoadFromFileSystem(DstBuffer);
	}
	else if (IsUsingIODispatcher())
	{

		InternalLoadFromIoStore(DstBuffer);
	}
	else
	{
		// Note that currently this shouldn't be reachable as we should early out due to the ::CanLoadFromDisk check at the start of the method
		UE_LOG(LogSerialization, Error, TEXT("Attempting to reload inline BulkData when the IoDispatcher is enabled, this operation is not supported! (%d)"), IsInlined());
	}
}

void FBulkDataBase::LoadDataAsynchronously(AsyncCallback&& Callback)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("FBulkDataBase::LoadDataDirectly"), STAT_UBD_LoadDataDirectly, STATGROUP_Memory);
	if (!CanLoadFromDisk())
	{
		UE_LOG(LogSerialization, Warning, TEXT("Attempting to load a BulkData object that cannot be loaded from disk"));
		return; // Early out if there is nothing to load anyway
	}

	if (!IsIoDispatcherEnabled())
	{
		Async(EAsyncExecution::ThreadPool, [this, Callback]()
			{
				void* DataPtr = nullptr;
				InternalLoadFromFileSystem(&DataPtr);

				FIoBuffer Buffer(FIoBuffer::Wrap, DataPtr, GetBulkDataSize());
				TIoStatusOr<FIoBuffer> Status(Buffer);

				Callback(Status);
			});
	}
	else if (IsUsingIODispatcher())
	{
		void* DummyPointer = nullptr;
		InternalLoadFromIoStoreAsync(&DummyPointer, MoveTemp(Callback));
	}
	else
	{
		// Note that currently this shouldn't be reachable as we should early out due to the ::CanLoadFromDisk check at the start of the method
		UE_LOG(LogSerialization, Error, TEXT("Attempting to reload inline BulkData when the IoDispatcher is enabled, this operation is not supported!"));
	}
}

void FBulkDataBase::InternalLoadFromFileSystem(void** DstBuffer)
{
	FString Filename = FileTokenSystem::GetFilename(Data.Token);

	int64 Offset = BulkDataOffset;

	// Fix up the Filename/Offset to work with streaming if EDL is enabled and the filename is still referencing a uasset or umap
	if (IsInlined() && GEventDrivenLoaderEnabled && (Filename.EndsWith(TEXT(".uasset")) || Filename.EndsWith(TEXT(".umap"))))
	{
		Offset -= IFileManager::Get().FileSize(*Filename);
		Filename = FPaths::GetBaseFilename(Filename, false) + BulkDataExt::Export;
	}
	else
	{
		Filename = ConvertFilenameFromFlags(Filename);
	}

	// If the data is inlined then we already loaded is during ::Serialize, this warning should help track cases where data is being discarded then re-requested.
	UE_CLOG(IsInlined(), LogSerialization, Warning, TEXT("Reloading inlined bulk data directly from disk, this is detrimental to loading performance. Filename: '%s'."), *Filename);

	FArchive* Ar = IFileManager::Get().CreateFileReader(*Filename, FILEREAD_Silent);
	checkf(Ar != nullptr, TEXT("Failed to open the file to load bulk data from. Filename: '%s'."), *Filename);

	// Seek to the beginning of the bulk data in the file.
	Ar->Seek(Offset);

	if (*DstBuffer == nullptr)
	{
		*DstBuffer = FMemory::Malloc(BulkDataSize, 0);
	}

	SerializeBulkData(*Ar, *DstBuffer, BulkDataSize);

	delete Ar;
}

void FBulkDataBase::InternalLoadFromIoStore(void** DstBuffer)
{
	// Allocate the buffer if needed
	if (*DstBuffer == nullptr)
	{
		*DstBuffer = FMemory::Malloc(GetBulkDataSize(), 0);
	}

	// Set up our options (we only need to set the target)
	FIoReadOptions Options(BulkDataOffset, BulkDataSize);
	Options.SetTargetVa(*DstBuffer);

	FIoBatch Batch = GetIoDispatcher()->NewBatch();
	FIoRequest Request = Batch.Read(CreateChunkId(), Options, IoDispatcherPriority_High);

	FEvent* BatchCompletedEvent = FPlatformProcess::GetSynchEventFromPool();
	Batch.IssueAndTriggerEvent(BatchCompletedEvent);
	BatchCompletedEvent->Wait(); // Blocking wait until all requests in the batch are done
	FPlatformProcess::ReturnSynchEventToPool(BatchCompletedEvent);
	CHECK_IOSTATUS(Request.GetResult().Status(), TEXT("FIoRequest"));
}

void FBulkDataBase::InternalLoadFromIoStoreAsync(void** DstBuffer, AsyncCallback&& Callback)
{
	// Allocate the buffer if needed
	if (*DstBuffer == nullptr)
	{
		*DstBuffer = FMemory::Malloc(GetBulkDataSize(), 0);
	}

	// Set up our options (we only need to set the target)
	FIoReadOptions Options;
	Options.SetTargetVa(*DstBuffer);

	FIoBatch Batch = GetIoDispatcher()->NewBatch();
	Batch.ReadWithCallback(CreateChunkId(), Options, IoDispatcherPriority_Low, MoveTemp(Callback));
	Batch.Issue();
}

void FBulkDataBase::ProcessDuplicateData(FArchive& Ar, const UPackage* Package, const FString* Filename, int64& InOutOffsetInFile)
{
	// We need to load the optional bulkdata info as we might need to create a FIoChunkId based on it!
	EBulkDataFlags NewFlags;
	int64 NewSizeOnDisk;
	int64 NewOffset;

	SerializeDuplicateData(Ar, NewFlags, NewSizeOnDisk, NewOffset);

#if ALLOW_OPTIONAL_DATA
	if (IsUsingIODispatcher())
	{
		const FIoChunkId OptionalChunkId = CreateIoChunkId(Data.PackageID, 0, EIoChunkType::OptionalBulkData);

		if (IoDispatcher->DoesChunkExist(OptionalChunkId))
		{
			BulkDataFlags = EBulkDataFlags(NewFlags | BULKDATA_UsesIoDispatcher);

			checkf(BulkDataSize == NewSizeOnDisk, TEXT("Size mistach between original data size (%lld)and duplicate data size (%lld)"), BulkDataSize, NewSizeOnDisk);
			InOutOffsetInFile = NewOffset;
		}
	}
	else
	{
		// If we have no valid input file name then the package is broken anyway and we will not be able to find the optional data!
		if (Filename != nullptr)
		{
			const FString OptionalDataFilename = FPathViews::ChangeExtension(*Filename, BulkDataExt::Optional);

			if (IFileManager::Get().FileExists(*OptionalDataFilename))
			{
				BulkDataFlags = NewFlags;
				checkf(BulkDataSize == NewSizeOnDisk, TEXT("Size mistach between original data size (%lld)and duplicate data size (%lld)"), BulkDataSize, NewSizeOnDisk);
				InOutOffsetInFile = NewOffset;
			}
		}
	}
#endif
}

void FBulkDataBase::SerializeDuplicateData(FArchive& Ar, EBulkDataFlags& OutBulkDataFlags, int64& OutBulkDataSizeOnDisk, int64& OutBulkDataOffsetInFile)
{
	Ar << OutBulkDataFlags;

	if (OutBulkDataFlags & BULKDATA_Size64Bit)
	{
		Ar << OutBulkDataSizeOnDisk;
	}
	else
	{
		int32 Temp32ByteValue;
		Ar << Temp32ByteValue;

		OutBulkDataSizeOnDisk = Temp32ByteValue;
	}

	Ar << OutBulkDataOffsetInFile;

	if ((OutBulkDataFlags & BULKDATA_BadDataVersion) != 0)
	{
		uint16 DummyBulkDataIndex = InvalidBulkDataIndex;
		Ar << DummyBulkDataIndex;
	}
}

void FBulkDataBase::SerializeBulkData(FArchive& Ar, void* DstBuffer, int64 DataLength)
{	
	checkf(Ar.IsLoading(), TEXT("BulkData2 only supports serialization for loading"));

	if (IsAvailableForUse()) // skip serializing of unused data
	{
		return;
	}

	// Skip serialization for bulk data of zero length
	if (DataLength == 0)
	{
		return;
	}

	checkf(DstBuffer != nullptr, TEXT("No destination buffer was provided for serialization"));

	if (IsStoredCompressedOnDisk())
	{
		Ar.SerializeCompressed(DstBuffer, DataLength, GetDecompressionFormat(), COMPRESS_NoFlags, false);
	}
	// Uncompressed/ regular serialization.
	else
	{
		Ar.Serialize(DstBuffer, DataLength);
	}
}

bool FBulkDataBase::MemoryMapBulkData(const FString& Filename, int64 OffsetInBulkData, int64 BytesToRead)
{
	checkf(!IsBulkDataLoaded(), TEXT("Attempting to memory map BulkData that is already loaded"));

	IMappedFileHandle* MappedHandle = nullptr;
	IMappedFileRegion* MappedRegion = nullptr;

	MappedHandle = FPlatformFileManager::Get().GetPlatformFile().OpenMapped(*Filename);

	if (MappedHandle == nullptr)
	{
		return false;
	}

	MappedRegion = MappedHandle->MapRegion(OffsetInBulkData, BytesToRead, true);
	if (MappedRegion == nullptr)
	{
		delete MappedHandle;
		MappedHandle = nullptr;
		return false;
	}

	checkf(MappedRegion->GetMappedSize() == BytesToRead, TEXT("Mapped size (%lld) is different to the requested size (%lld)!"), MappedRegion->GetMappedSize(), BytesToRead);
	checkf(IsAligned(MappedRegion->GetMappedPtr(), FPlatformProperties::GetMemoryMappingAlignment()), TEXT("Memory mapped file has the wrong alignment!"));

	DataAllocation.SetMemoryMappedData(this, MappedHandle, MappedRegion);

	return true;
}

void FBulkDataBase::FlushAsyncLoading()
{
	if (!IsAsyncLoadingComplete())
	{
		DECLARE_SCOPE_CYCLE_COUNTER(TEXT(" FBulkDataBase::FlushAsyncLoading"), STAT_UBD_WaitForAsyncLoading, STATGROUP_Memory);

#if NO_LOGGING
		while (IsAsyncLoadingComplete() == false)
		{
			FPlatformProcess::Sleep(0);
		}
#else
		uint64 StartTime = FPlatformTime::Cycles64();
		while (IsAsyncLoadingComplete() == false)
		{
			if (FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - StartTime))
			{
				UE_LOG(LogSerialization, Warning, TEXT("Waiting for %s bulk data (%lld) to be loaded longer than 1000ms"), *GetFilename(), GetBulkDataSize());
				StartTime = FPlatformTime::Cycles64(); // Reset so we spam the log every second or so that we are stalled!
			}

			FPlatformProcess::Sleep(0);
		}
#endif
	}	
}

FString FBulkDataBase::ConvertFilenameFromFlags(const FString& Filename) const
{	
	if (IsOptional())
	{
		// Optional data should be tested for first as we in theory can have data that would
		// be marked as inline, also marked as optional and in this case we should treat it as
		// optional data first.
		return FPathViews::ChangeExtension(Filename, BulkDataExt::Optional);
	}
	else if (!IsInSeparateFile())
	{
		return Filename;
	}
	else if (IsInlined())
	{
		return FPathViews::ChangeExtension(Filename, BulkDataExt::Export);
	}
	else if (IsFileMemoryMapped())
	{
		return FPathViews::ChangeExtension(Filename, BulkDataExt::MemoryMapped);
	}
	else
	{
		return FPathViews::ChangeExtension(Filename, BulkDataExt::Default);
	}
}

void FBulkDataAllocation::Free(FBulkDataBase* Owner)
{
	if (!Owner->IsDataMemoryMapped())
	{
		FMemory::Free(Allocation);
		Allocation = nullptr;
	}
	else
	{
		FOwnedBulkDataPtr* Ptr = static_cast<FOwnedBulkDataPtr*>(Allocation);
		delete Ptr;

		Allocation = nullptr;
	}
}

void* FBulkDataAllocation::AllocateData(FBulkDataBase* Owner, SIZE_T SizeInBytes)
{
	checkf(Allocation == nullptr, TEXT("Trying to allocate a BulkData object without freeing it first!"));

	Allocation = FMemory::Malloc(SizeInBytes, DEFAULT_ALIGNMENT);

	return Allocation;
}

void* FBulkDataAllocation::ReallocateData(FBulkDataBase* Owner, SIZE_T SizeInBytes)
{
	checkf(!Owner->IsDataMemoryMapped(),  TEXT("Trying to reallocate a memory mapped BulkData object without freeing it first!"));

	Allocation = FMemory::Realloc(Allocation, SizeInBytes, DEFAULT_ALIGNMENT);

	return Allocation;
}

void FBulkDataAllocation::SetData(FBulkDataBase* Owner, void* Buffer)
{
	checkf(Allocation == nullptr, TEXT("Trying to assign a BulkData object without freeing it first!"));

	Allocation = Buffer;
}

void FBulkDataAllocation::SetMemoryMappedData(FBulkDataBase* Owner, IMappedFileHandle* MappedHandle, IMappedFileRegion* MappedRegion)
{
	checkf(Allocation == nullptr, TEXT("Trying to assign a BulkData object without freeing it first!"));
	FOwnedBulkDataPtr* Ptr = new FOwnedBulkDataPtr(MappedHandle, MappedRegion);

	Owner->SetRuntimeBulkDataFlags(BULKDATA_DataIsMemoryMapped);

	Allocation = Ptr;
}

void* FBulkDataAllocation::GetAllocationForWrite(const FBulkDataBase* Owner) const
{
	if (!Owner->IsDataMemoryMapped())
	{
		return Allocation;
	}
	else
	{
		return nullptr;
	}
}

const void* FBulkDataAllocation::GetAllocationReadOnly(const FBulkDataBase* Owner) const
{
	if (!Owner->IsDataMemoryMapped())
	{
		return Allocation;
	}
	else if (Allocation != nullptr)
	{
		FOwnedBulkDataPtr* Ptr = static_cast<FOwnedBulkDataPtr*>(Allocation);
		return Ptr->GetPointer();
	}
	else
	{
		return nullptr;
	}
}

FOwnedBulkDataPtr* FBulkDataAllocation::StealFileMapping(FBulkDataBase* Owner)
{
	FOwnedBulkDataPtr* Ptr;
	if (!Owner->IsDataMemoryMapped())
	{
		Ptr = new FOwnedBulkDataPtr(Allocation);	
	}
	else
	{
		Ptr = static_cast<FOwnedBulkDataPtr*>(Allocation);
		Owner->ClearRuntimeBulkDataFlags(BULKDATA_DataIsMemoryMapped);
	}	

	Allocation = nullptr;
	return Ptr;
}

void FBulkDataAllocation::Swap(FBulkDataBase* Owner, void** DstBuffer)
{
	if (!Owner->IsDataMemoryMapped())
	{
		::Swap(*DstBuffer, Allocation);
	}
	else
	{
		FOwnedBulkDataPtr* Ptr = static_cast<FOwnedBulkDataPtr*>(Allocation);

		const int64 BulkDataSize = Owner->GetBulkDataSize();

		*DstBuffer = FMemory::Malloc(BulkDataSize, DEFAULT_ALIGNMENT);
		FMemory::Memcpy(*DstBuffer, Ptr->GetPointer(), BulkDataSize);

		delete Ptr;
		Allocation = nullptr;

		Owner->ClearRuntimeBulkDataFlags(BULKDATA_DataIsMemoryMapped);
	}	
}

#undef CHECK_IOSTATUS
