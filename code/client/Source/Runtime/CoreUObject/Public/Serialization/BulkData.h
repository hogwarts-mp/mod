// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/SortedMap.h"
#include "UObject/WeakObjectPtr.h"
#include "Async/Future.h"
#include "Async/AsyncFileHandle.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Serialization/FileRegions.h"
#include "BulkDataCommon.h"
#include "BulkData2.h"
#if WITH_IOSTORE_IN_EDITOR
#include "UObject/PackageId.h"
#endif

#if WITH_EDITOR == 0 && WITH_EDITORONLY_DATA == 0 //Runtime
	#define USE_NEW_BULKDATA 1 // Set to 1 to enable 
#else
	#define USE_NEW_BULKDATA 0
#endif

// Enable the following to use the more compact FBulkDataStreamingToken in places where it is implemented
#define USE_BULKDATA_STREAMING_TOKEN !USE_NEW_BULKDATA

#if USE_BULKDATA_STREAMING_TOKEN
	#define STREAMINGTOKEN_PARAM(param) param,
#else
	#define STREAMINGTOKEN_PARAM(param) 
#endif

class IMappedFileHandle;
class IMappedFileRegion;

/*-----------------------------------------------------------------------------
	Base version of untyped bulk data.
-----------------------------------------------------------------------------*/

/**
 * @documentation @todo documentation
 */
struct COREUOBJECT_API FOwnedBulkDataPtr
{
	explicit FOwnedBulkDataPtr(void* InAllocatedData)
		: AllocatedData(InAllocatedData)
		, MappedHandle(nullptr)
		, MappedRegion(nullptr)
	{
		
	}

	FOwnedBulkDataPtr(IMappedFileHandle* Handle, IMappedFileRegion* Region)
		: AllocatedData(nullptr)
		, MappedHandle(Handle)
		, MappedRegion(Region)
	{
		
	}

	~FOwnedBulkDataPtr();
	const void* GetPointer();

	IMappedFileHandle* GetMappedHandle()
	{
		return MappedHandle;
	}
	IMappedFileRegion* GetMappedRegion()
	{
		return MappedRegion;
	}

	void RelinquishOwnership()
	{
		AllocatedData = nullptr;
		MappedHandle = nullptr;
		MappedRegion = nullptr;
	}

private:
	// hidden
	FOwnedBulkDataPtr() {}


	// if allocated memory was used, this will be non-null
	void* AllocatedData;
	
	// if memory mapped IO was used, these will be non-null
	IMappedFileHandle* MappedHandle;
	IMappedFileRegion* MappedRegion;
};

class FBulkDataIORequest : public IBulkDataIORequest
{
public:
	FBulkDataIORequest(IAsyncReadFileHandle* InFileHandle);
	FBulkDataIORequest(IAsyncReadFileHandle* InFileHandle, IAsyncReadRequest* InReadRequest, int64 BytesToRead);

	virtual ~FBulkDataIORequest();

	bool MakeReadRequest(int64 Offset, int64 BytesToRead, EAsyncIOPriorityAndFlags PriorityAndFlags, FBulkDataIORequestCallBack* CompleteCallback, uint8* UserSuppliedMemory);

	virtual bool PollCompletion() const override;
	virtual bool WaitCompletion( float TimeLimitSeconds = 0.0f ) override;

	virtual uint8* GetReadResults() override;
	virtual int64 GetSize() const override;

	virtual void Cancel() override;

private:
	IAsyncReadFileHandle* FileHandle;
	IAsyncReadRequest* ReadRequest;

	int64 Size;
};

#if USE_BULKDATA_STREAMING_TOKEN 
/**
 * Some areas of code currently find FUntypedBulkData too bloated for use so were storing smaller parts of it
 * and performing the file IO manually. This is an attempt to wrap that functionality in a single place to make
 * it easier to replace in the future.
 * This structure will represent the area of a file that a FUntypedBulkData points to.
 * The structure does not contain info on which file it is however (as that would increase the data
 * size by an unacceptable amount) and it is assumed that this knowledge is being kept at a higher level.
 *
 * This is intended only to exist for a short time to aid the transition to a new system.
 */
struct COREUOBJECT_API FBulkDataStreamingToken
{
public:
	static constexpr uint32 InvalidOffset = TNumericLimits<uint32>::Max();

public:
	FBulkDataStreamingToken() {}
	FBulkDataStreamingToken(uint32 InOffsetInFile, uint32 InBulkDataSize)
		: OffsetInFile(InOffsetInFile)
		, BulkDataSize(InBulkDataSize)
	{
	}

	bool IsValid() const
	{ 
		return OffsetInFile != InvalidOffset && BulkDataSize > 0;
	}

	uint32 GetOffset() const
	{
		return OffsetInFile;
	}

	uint32 GetBulkDataSize() const
	{
		return BulkDataSize;
	}

private:
	uint32 OffsetInFile{ InvalidOffset };
	uint32 BulkDataSize{ 0 };
};
#else
class FBulkDataStreamingToken; // Forward declared but not implemented
#endif

/**
 * @documentation @todo documentation
 */
struct COREUOBJECT_API FUntypedBulkData
{
private:
	// This struct represents an optional allocation.
	struct FAllocatedPtr
	{
		FAllocatedPtr()
			: Ptr       (nullptr)
			, MappedHandle(nullptr)
			, MappedRegion(nullptr)
			, bAllocated(false)
		{
		}

		FAllocatedPtr(FAllocatedPtr&& Other)
			: Ptr       (Other.Ptr)
			, MappedHandle(Other.MappedHandle)
			, MappedRegion(Other.MappedRegion)
			, bAllocated(Other.bAllocated)
		{
			Other.Ptr = nullptr;
			Other.MappedHandle = nullptr;
			Other.MappedRegion = nullptr;
			Other.bAllocated = false;
		}

		FAllocatedPtr& operator=(FAllocatedPtr&& Other)
		{
			Swap(*this, Other);
			Other.Deallocate();

			return *this;
		}

		~FAllocatedPtr()
		{
			Deallocate();
		}

		void* Get() const
		{
			return Ptr;
		}

		FORCEINLINE explicit operator bool() const
		{
			return bAllocated;
		}

		void Reallocate(int64 Count, int32 Alignment = DEFAULT_ALIGNMENT)
		{
			check(!MappedHandle && !MappedRegion); // not legal for mapped bulk data
			if (Count)
			{
				Ptr = FMemory::Realloc(Ptr, Count, Alignment);
			}
			else
			{
				FMemory::Free(Ptr);
				Ptr = nullptr;
			}

			bAllocated = true;
		}

		void* ReleaseWithoutDeallocating()
		{
			if (MappedHandle || MappedRegion)
			{
				// Super scary, we returned a pointer to a mapped file but we have no guarantees that this outlives the pointer we let out into the engine
				// @todo transfer ownership properly by making FAllocatedPtr a public thing that can be used by people to take ownership of the entire mapping. 
			}
			void* Result = Ptr;
			Ptr = nullptr;
			bAllocated = false;
			return Result;
		}

		void Deallocate()
		{
			if (MappedHandle || MappedRegion)
			{
				UnmapFile();
			}
			FMemory::Free(Ptr);
			Ptr = nullptr;
			bAllocated = false;
		}

		COREUOBJECT_API bool MapFile(const TCHAR *Filename, int64 Offset, int64 Size);
		COREUOBJECT_API void UnmapFile();

		FOwnedBulkDataPtr* StealFileMapping()
		{
			FOwnedBulkDataPtr* Result;
			// make the proper kind of owner pointer info
			if (MappedHandle && MappedRegion && Ptr && bAllocated)
			{
				Result = new FOwnedBulkDataPtr(MappedHandle, MappedRegion);
			}
			else
			{
				Result = new FOwnedBulkDataPtr(Ptr);
			}

			// no matter what, this allocated pointer is now fully owned by the caller, so we just null everything out, no deletions
			MappedHandle = nullptr;
			MappedRegion = nullptr;
			Ptr = nullptr;
			bAllocated = false;

			return Result;
		}
		
	private:
		FAllocatedPtr(const FAllocatedPtr&);
		FAllocatedPtr& operator=(const FAllocatedPtr&);

		void* Ptr;
		IMappedFileHandle* MappedHandle;
		IMappedFileRegion* MappedRegion;
		bool  bAllocated;
	};

public:
	friend class FLinkerLoad;
	using BulkDataRangeArray = TArray<FBulkDataStreamingToken*, TInlineAllocator<8>>;

	/*-----------------------------------------------------------------------------
		Constructors and operators
	-----------------------------------------------------------------------------*/

	/**
	 * Constructor, initializing all member variables.
	 */
	FUntypedBulkData();

	/**
	 * Copy constructor. Use the common routine to perform the copy.
	 *
	 * @param Other the source array to copy
	 */
	FUntypedBulkData( const FUntypedBulkData& Other );

	/**
	 * Virtual destructor, free'ing allocated memory.
	 */
	virtual ~FUntypedBulkData();

	/**
	 * Copies the source array into this one after detaching from archive.
	 *
	 * @param Other the source array to copy
	 */
	FUntypedBulkData& operator=( const FUntypedBulkData& Other );

	/*-----------------------------------------------------------------------------
		Static functions.
	-----------------------------------------------------------------------------*/

	/**
	 * Dumps detailed information of bulk data usage.
	 *
	 * @param Log FOutputDevice to use for logging
	 */
	static void DumpBulkDataUsage( FOutputDevice& Log );

	/*-----------------------------------------------------------------------------
		Accessors
	-----------------------------------------------------------------------------*/

	/**
	 * Returns the number of elements in this bulk data array.
	 *
	 * @return Number of elements in this bulk data array
	 */
	int64 GetElementCount() const;
	/**
	 * Returns size in bytes of single element.
	 *
	 * Pure virtual that needs to be overloaded in derived classes.
	 *
	 * @return Size in bytes of single element
	 */
	virtual int32 GetElementSize() const = 0;
	/**
	 * Returns the size of the bulk data in bytes.
	 *
	 * @return Size of the bulk data in bytes
	 */
	int64 GetBulkDataSize() const;
	/**
	 * Returns the size of the bulk data on disk. This can differ from GetBulkDataSize if
	 * BULKDATA_SerializeCompressed is set.
	 *
	 * @return Size of the bulk data on disk or INDEX_NONE in case there's no association
	 */
	int64 GetBulkDataSizeOnDisk() const;
	/**
	 * Returns the offset into the file the bulk data is located at.
	 *
	 * @return Offset into the file or INDEX_NONE in case there is no association
	 */
	int64 GetBulkDataOffsetInFile() const;
	/**
	 * Returns whether the bulk data is stored compressed on disk.
	 *
	 * @return true if data is compressed on disk, false otherwise
	 */
	bool IsStoredCompressedOnDisk() const;

	/**
	 * Returns true if the data can be loaded from disk.
	 */
	bool CanLoadFromDisk() const;
	
	/**
	 * Returns true if the data references a file that currently exists and can be referenced by the file system.
	 */
	bool DoesExist() const;

	/**
	 * Returns flags usable to decompress the bulk data
	 * 
	 * @return NAME_None if the data was not compressed on disk, or valid format to pass to FCompression::UncompressMemory for this data
	 */
	FName GetDecompressionFormat() const;

	/**
	 * Returns whether the bulk data is currently loaded and resident in memory.
	 *
	 * @return true if bulk data is loaded, false otherwise
	 */
	bool IsBulkDataLoaded() const;

	/**
	* Returns whether the bulk data asynchronous load has completed.
	*
	* @return true if bulk data has been loaded or async loading was not used to load this data, false otherwise
	*/
	bool IsAsyncLoadingComplete() const;

	/**
	* Returns whether this bulk data is used
	* @return true if BULKDATA_Unused is not set
	*/
	bool IsAvailableForUse() const;

	/**
	* Returns whether this bulk data represents optional data or not
	* @return true if BULKDATA_OptionalPayload is set
	*/
	bool IsOptional() const
	{
		return (GetBulkDataFlags() & BULKDATA_OptionalPayload) != 0;
	}

	/**
	* Returns whether this bulk data is currently stored inline or not
	* @return true if BULKDATA_PayloadAtEndOfFile is not set
	*/
	bool IsInlined() const
	{
		return   (GetBulkDataFlags() & BULKDATA_PayloadAtEndOfFile) == 0;
	}

	UE_DEPRECATED(4.25, "Use ::IsInSeparateFile() instead")
	FORCEINLINE bool InSeperateFile() const { return IsInSeparateFile(); }

	/**
	* Returns whether this bulk data is currently stored in it's own file or not
	* @return true if BULKDATA_PayloadInSeperateFile is not set
	*/
	bool IsInSeparateFile() const
	{
		return	(GetBulkDataFlags() & BULKDATA_PayloadInSeperateFile) != 0;
	}

	/**
	* Returns whether this bulk data is accessed via the IoDispatcher or not.
	* @return false as the old BulkData API does not support it
	*/
	bool IsUsingIODispatcher() const
	{
		return (BulkDataFlags & BULKDATA_UsesIoDispatcher) != 0;
	}

	/**
	 * Enables the given flags without affecting any previously set flags.
	 *
	 * @param BulkDataFlagsToSet	Bulk data flags to set
	 */
	void SetBulkDataFlags( uint32 BulkDataFlagsToSet );

	/**
	 * Enable the given flags and disable all other flags.
	 * This can be used with ::GetBulkDataFlags() to effectively
	 * reset the flags to a previous state.
	 *
	 * @param BulkDataFlagsToSet	Bulk data flags to set
	 */
	void ResetBulkDataFlags(uint32 BulkDataFlagsToSet);

	/**
	* Gets the current bulk data flags.
	*
	* @return Bulk data flags currently set
	*/
	uint32 GetBulkDataFlags() const;

	/**
	 * Sets the passed in bulk data alignment.
	 *
	 * @param BulkDataAlignmentToSet	Bulk data alignment to set
	 */
	void SetBulkDataAlignment( uint32 BulkDataAlignmentToSet );

	/**
	* Gets the current bulk data alignment.
	*
	* @return Bulk data alignment currently set
	*/
	uint32 GetBulkDataAlignment() const;

	/**
	 * Clears the passed in bulk data flags.
	 *
	 * @param BulkDataFlagsToClear	Bulk data flags to clear
	 */
	void ClearBulkDataFlags( uint32 BulkDataFlagsToClear );

	/** 
	 * Returns the filename this bulkdata resides in
	 *
	 * @return Filename where this bulkdata can be loaded from
	 **/
	const FString& GetFilename() const { return Filename; }

	/** 
	 * Returns the io filename hash associated with this bulk data.
	 *
	 * @return Hash or INVALID_IO_FILENAME_HASH if invalid.
	 **/
	FIoFilenameHash GetIoFilenameHash() const { return MakeIoFilenameHash(Filename); }

	/*-----------------------------------------------------------------------------
		Data retrieval and manipulation.
	-----------------------------------------------------------------------------*/

	/**
	 * Retrieves a copy of the bulk data.
	 *
	 * @param Dest [in/out] Pointer to pointer going to hold copy, can point to NULL pointer in which case memory is allocated
	 * @param bDiscardInternalCopy Whether to discard/ free the potentially internally allocated copy of the data
	 */
	void GetCopy( void** Dest, bool bDiscardInternalCopy = true );

	/**
	 * Returns a copy encapsulated by a FBulkDataBuffer.
	 *
	 * @param RequestedElementCount If set to greater than 0, the returned FBulkDataBuffer will be limited to
	 * this number of elements. This will give an error if larger than the actual number of elements in the BulkData object.
	 * @param bDiscardInternalCopy If true then the BulkData object will free it's internal buffer once called.
	 *
	 * @return A FBulkDataBuffer that owns a copy of the BulkData, this might be a subset of the data depending on the value of RequestedSize.
	 */
	template<typename ElementType>
	FBulkDataBuffer<ElementType> GetCopyAsBuffer(int64 RequestedElementCount, bool bDiscardInternalCopy);

	/**
	 * Locks the bulk data and returns a pointer to it.
	 *
	 * @param	LockFlags	Flags determining lock behavior
	 */
	void* Lock( uint32 LockFlags );

	/**
	 * Locks the bulk data and returns a read-only pointer to it.
	 * This variant can be called on a const bulkdata
	 */
	const void* LockReadOnly() const;

	/**
	 * Change size of locked bulk data. Only valid if locked via read-write lock.
	 *
	 * @param InElementCount	Number of elements array should be resized to
	 */
	void* Realloc( int64 InElementCount );

	/** 
	 * Unlocks bulk data after which point the pointer returned by Lock no longer is valid.
	 */
	void Unlock() const;

	/** 
	 * Checks if this bulk is locked
	 */
	bool IsLocked() const { return LockStatus != LOCKSTATUS_Unlocked; }

	/**
	 * Clears/ removes the bulk data and resets element count to 0.
	 */
	void RemoveBulkData();

	/**
	 * Load the bulk data using a file reader. Works even when no archive is attached to the bulk data..
  	 * @return Whether the operation succeeded.
	 */
	bool LoadBulkDataWithFileReader();

	/**
	 * Forces the bulk data to be resident in memory and detaches the archive.
	 */
	void ForceBulkDataResident();

	/** 
	* Initiates a new asynchronous operation to load the dulkdata from disk assuming that it is not already
	* loaded.
	* Note that a new asynchronous loading operation will not be created if one is already in progress.
	*
	* @return True if an asynchronous loading operation is in progress by the time that the method returns
	* and false if the data is already loaded or cannot be loaded from disk.
	*/
	bool StartAsyncLoading();
	
	/**
	 * Sets whether we should store the data compressed on disk.
	 *
	 * @param CompressionFlags	Flags to use for compressing the data. Use COMPRESS_NONE for no compression, or something like COMPRESS_ZLIB to compress the data
	 */
	UE_DEPRECATED(4.21, "Use the FName version of StoreCompressedOnDisk")
	void StoreCompressedOnDisk( ECompressionFlags CompressionFlags );
	void StoreCompressedOnDisk( FName CompressionFormat );

	/**
	 * Deallocates bulk data without detaching the archive, so that further bulk data accesses require a reload.
	 * Only supported in editor builds.
	 *
	 * @return Whether the operation succeeded.
	 */
	bool UnloadBulkData();

	/*-----------------------------------------------------------------------------
		Serialization.
	-----------------------------------------------------------------------------*/

	/**
	 * Serialize function used to serialize this bulk data structure.
	 *
	 * @param Ar	Archive to serialize with
	 * @param Owner	Object owning the bulk data
	 * @param Idx	Index of bulk data item being serialized
	 * @param bAttemptFileMapping	If true, attempt to map this instead of loading it into malloc'ed memory
	 * @param FileRegionType	When cooking, a hint describing the type of data, used by some platforms to improve compression ratios
	 */
	void Serialize( FArchive& Ar, UObject* Owner, int32 Idx=INDEX_NONE, bool bAttemptFileMapping = false, EFileRegionType FileRegionType = EFileRegionType::None );

	FOwnedBulkDataPtr* StealFileMapping()
	{
		// @todo if non-mapped bulk data, do we need to detach this, or mimic GetCopy more than we do?
		return BulkData.StealFileMapping();
	}

	/**
	 * Serialize just the bulk data portion to/ from the passed in memory.
	 *
	 * @param	Ar					Archive to serialize with
	 * @param	Data				Memory to serialize either to or from
	 */ 
	void SerializeBulkData( FArchive& Ar, void* Data );

	/*-----------------------------------------------------------------------------
		Async Streaming Interface.
	-----------------------------------------------------------------------------*/

	/**
	 * Opens a new IAsyncReadFileHandle that references the file that the BulkData object represents.
	 *
	 * @return A valid handle if the file can be accessed, if it cannot then nullptr.
	 */
	IAsyncReadFileHandle* OpenAsyncReadHandle() const;

	/**
	 * Create an async read request for the bulk data.
	 * This version will load the entire data range that the FUntypedBulkData represents.
	 *
	 * @param Priority				Priority and flags of the request. If this includes AIOP_FLAG_PRECACHE, then memory will never be returned. The request should always be canceled and waited for, even for a precache request.
	 * @param CompleteCallback		Called from an arbitrary thread when the request is complete. Can be nullptr, if non-null, must remain valid until it is called. It will always be called.
	 * @param UserSuppliedMemory	A pointer to memory for the IO request to be written to, it is up to the caller to make sure that it is large enough. If the pointer is null then the system will allocate memory instead.
	 * @return						A request for the read. This is owned by the caller and must be deleted by the caller.
	 */
	IBulkDataIORequest* CreateStreamingRequest(EAsyncIOPriorityAndFlags Priority, FBulkDataIORequestCallBack* CompleteCallback, uint8* UserSuppliedMemory) const;

	/**
	 * Create an async read request for the bulk data.
	 * This version allows the user to request a subset of the data that the FUntypedBulkData represents.
	 *
	 * @param OffsetInBulkData		Offset into the bulk data to start reading from.
	 * @param BytesToRead			The number of bytes to read. If this request is AIOP_Preache, the size can be anything, even MAX_int64, otherwise the size and offset must be fully contained in the file.
	 * @param Priority				Priority and flags of the request. If this includes AIOP_FLAG_PRECACHE, then memory will never be returned. The request should always be canceled and waited for, even for a precache request.
	 * @param CompleteCallback		Called from an arbitrary thread when the request is complete. Can be nullptr, if non-null, must remain valid until it is called. It will always be called.
	 * @param UserSuppliedMemory	A pointer to memory for the IO request to be written to, it is up to the caller to make sure that it is large enough. If the pointer is null then the system will allocate memory instead.
	 * @return						A request for the read. This is owned by the caller and must be deleted by the caller.
	 */
	IBulkDataIORequest* CreateStreamingRequest(int64 OffsetInBulkData, int64 BytesToRead, EAsyncIOPriorityAndFlags Priority, FBulkDataIORequestCallBack* CompleteCallback, uint8* UserSuppliedMemory) const;

#if USE_BULKDATA_STREAMING_TOKEN 

	/**
	 * Creates a FBulkDataStreamingToken representing the area of the file that the FUntypedBulkData represents. See the declaration of
	 * FBulkDataStreamingToken for further details.
	 *
	 * @return	A FBulkDataStreamingToken valid for the FUntypedBulkData.
	 */
	FBulkDataStreamingToken CreateStreamingToken() const;

	/**
	 * Create an async read request for a range of bulk data streaming tokens
	 * The request will read all data between the two given streaming tokens objects. They must both represent areas of data in the file!
	 * There is no way to validate this and it is up to the caller to make sure that it is correct.
	 * The memory to be read into will be automatically allocated the size of which can be retrieved by calling IBulkDataIORequest::GetSize()
	 *
	 * @param Filename			The file to read from.
	 * @param Start				The bulk data to start reading from.
	 * @param End				The bulk data to finish reading from.
	 * @param Priority			Priority and flags of the request. If this includes AIOP_FLAG_PRECACHE, then memory will never be returned. The request should always be canceled and waited for, even for a precache request.
	 * @param CompleteCallback	Called from an arbitrary thread when the request is complete. Can be nullptr, if non-null, must remain valid until it is called. It will always be called.
	 * @return					A request for the read. This is owned by the caller and must be deleted by the caller.
	**/
	static IBulkDataIORequest* CreateStreamingRequestForRange(const FString& Filename, const BulkDataRangeArray& RangeArray, EAsyncIOPriorityAndFlags Priority, FBulkDataIORequestCallBack* CompleteCallback);
#endif

	/*-----------------------------------------------------------------------------
		Class specific virtuals.
	-----------------------------------------------------------------------------*/

protected:

	/**
	* Serializes all elements, a single element at a time, allowing backward compatible serialization
	* and endian swapping to be performed.
	*
	* @param Ar			Archive to serialize with
	* @param Data			Base pointer to data
	*/
	virtual void SerializeElements(FArchive& Ar, void* Data);

	/**
	 * Serializes a single element at a time, allowing backward compatible serialization
	 * and endian swapping to be performed. Needs to be overloaded by derived classes.
	 *
	 * @param Ar			Archive to serialize with
	 * @param Data			Base pointer to data
	 * @param ElementIndex	Index of element to serialize
	 */
	virtual void SerializeElement( FArchive& Ar, void* Data, int64 ElementIndex ) = 0;

	/**
	 * Returns whether single element serialization is required given an archive. This e.g.
	 * can be the case if the serialization for an element changes and the single element
	 * serialization code handles backward compatibility.
	 */
	virtual bool RequiresSingleElementSerialization( FArchive& Ar );
		
private:
#if WITH_EDITOR
	/**
	 * Detaches the bulk data from the passed in archive. Needs to match the archive we are currently
	 * attached to.
	 *
	 * @param Ar						Archive to detach from
	 * @param bEnsureBulkDataIsLoaded	whether to ensure that bulk data is load before detaching from archive
	 */
	void DetachFromArchive( FArchive* Ar, bool bEnsureBulkDataIsLoaded );
#endif // WITH_EDITOR

#if WITH_IOSTORE_IN_EDITOR
	/**
	 * Serialize bulk data structure from I/O store.
	 *
	 * @param Ar	Archive to serialize with
	 * @param Owner	Object owning the bulk data
	 * @param Idx	Index of bulk data item being serialized
	 * @param bAttemptFileMapping	If true, attempt to map this instead of loading it into malloc'ed memory
	 */
	void SerializeFromIoStore( FArchive& Ar, UObject* Owner, int32 Idx, bool bAttemptFileMapping );
#endif

	/*-----------------------------------------------------------------------------
		Internal helpers
	-----------------------------------------------------------------------------*/

	/**
	 * Copies bulk data from passed in structure.
	 *
	 * @param	Other	Bulk data object to copy from.
	 */
	void Copy( const FUntypedBulkData& Other );

	/**
	 * Helper function initializing all member variables.
	 */
	void InitializeMemberVariables();

	/**
	 * Loads the bulk data if it is not already loaded.
	 */
	void MakeSureBulkDataIsLoaded();

	/**
	 * Loads the data from disk into the specified memory block. This requires us still being attached to an
	 * archive we can use for serialization.
	 *
	 * @param Dest Memory to serialize data into
	 */
	void LoadDataIntoMemory( void* Dest );

	/** Create the async load task */
	void AsyncLoadBulkData();

	/** Starts serializing bulk data asynchronously */
	void StartSerializingBulkData(FArchive& Ar, UObject* Owner, int32 Idx, bool bPayloadInline);

	/** Flushes any pending async load of bulk data  and copies the data to Dest buffer*/
	bool FlushAsyncLoading();

	/** Waits until pending async load finishes */
	void WaitForAsyncLoading();

	/** Resets async loading state */
	void ResetAsyncData();
	
	/** Returns true if bulk data should be loaded asynchronously */
	bool ShouldStreamBulkData();

	/** Returns if the offset needs fixing when serialized */
	bool NeedsOffsetFixup() const;

	/*-----------------------------------------------------------------------------
		Member variables.
	-----------------------------------------------------------------------------*/

	/** Serialized flags for bulk data																					*/
	EBulkDataFlags			BulkDataFlags;
	/** Alignment of bulk data																							*/
	uint16					BulkDataAlignment;
	/** Current lock status																								*/
	uint16					LockStatus;
	/** Number of elements in bulk data array																			*/
	int64					ElementCount;
	/** Offset of bulk data into file or INDEX_NONE if no association													*/
	int64					BulkDataOffsetInFile;
	/** Size of bulk data on disk or INDEX_NONE if no association														*/
	int64					BulkDataSizeOnDisk;

	/** Pointer to cached bulk data																						*/
	FAllocatedPtr		BulkData;
	/** Pointer to cached async bulk data																				*/
	FAllocatedPtr		BulkDataAsync;
	/** Async helper for loading bulk data on a separate thread */
	TFuture<bool> SerializeFuture;

protected:
	/** name of the package file containing the bulkdata */
	FString				Filename;
#if WITH_EDITOR
	/** Archive associated with bulk data for serialization																*/
	FArchive*			AttachedAr;
	/** Used to make sure the linker doesn't get garbage collected at runtime for things with attached archives			*/
	FLinkerLoad*		Linker;
#else
	/** weak pointer to the linker this bulk data originally belonged to. */
	TWeakObjectPtr<UPackage> Package;
#endif // WITH_EDITOR
#if WITH_IOSTORE_IN_EDITOR
	/** Package ID used for creating I/O chunk IDs */
	FPackageId			PackageId;
#endif
};

template<typename ElementType>
FBulkDataBuffer<ElementType> FUntypedBulkData::GetCopyAsBuffer(int64 RequestedElementCount, bool bDiscardInternalCopy)
{
	const int64 MaxElementCount = GetElementCount();

	check(RequestedElementCount <= MaxElementCount);

	ElementType* Ptr = nullptr;
	GetCopy((void**)& Ptr, bDiscardInternalCopy);

	const int64 BufferSize = (RequestedElementCount > 0 ? RequestedElementCount : MaxElementCount);

	return FBulkDataBuffer<ElementType>(Ptr, BufferSize);
}
/*-----------------------------------------------------------------------------
	uint8 version of bulk data.
-----------------------------------------------------------------------------*/

struct COREUOBJECT_API FByteBulkDataOld : public FUntypedBulkData
{
	/**
	 * Returns size in bytes of single element.
	 *
	 * @return Size in bytes of single element
	 */
	virtual int32 GetElementSize() const;

	/**
	 * Serializes an element at a time allowing and dealing with endian conversion and backward compatiblity.
	 * 
	 * @param Ar			Archive to serialize with
	 * @param Data			Base pointer to data
	 * @param ElementIndex	Element index to serialize
	 */
	virtual void SerializeElement( FArchive& Ar, void* Data, int64 ElementIndex );

	/**
	 * Returns a copy encapsulated by a FBulkDataBuffer.
	 *
	 * @param RequestedElementCount If set to greater than 0, the returned FBulkDataBuffer will be limited to
	 * this number of elements. This will give an error if larger than the actual number of elements in the BulkData object.
	 * @param bDiscardInternalCopy If true then the BulkData object will free it's internal buffer once called.
	 *
	 * @return A FBulkDataBuffer that owns a copy of the BulkData, this might be a subset of the data depending on the value of RequestedSize.
	 */
	FBulkDataBuffer<uint8> GetCopyAsBuffer(int64 RequestedElementCount, bool bDiscardInternalCopy) { return FUntypedBulkData::GetCopyAsBuffer<uint8>(RequestedElementCount, bDiscardInternalCopy); }
};

/*-----------------------------------------------------------------------------
	WORD version of bulk data.
-----------------------------------------------------------------------------*/

struct COREUOBJECT_API FWordBulkDataOld : public FUntypedBulkData
{
	/**
	 * Returns size in bytes of single element.
	 *
	 * @return Size in bytes of single element
	 */
	virtual int32 GetElementSize() const;

	/**
	 * Serializes an element at a time allowing and dealing with endian conversion and backward compatiblity.
	 * 
	 * @param Ar			Archive to serialize with
	 * @param Data			Base pointer to data
	 * @param ElementIndex	Element index to serialize
	 */
	virtual void SerializeElement( FArchive& Ar, void* Data, int64 ElementIndex );

	/**
	 * Returns a copy encapsulated by a FBulkDataBuffer.
	 *
	 * @param RequestedElementCount If set to greater than 0, the returned FBulkDataBuffer will be limited to
	 * this number of elements. This will give an error if larger than the actual number of elements in the BulkData object.
	 * @param bDiscardInternalCopy If true then the BulkData object will free it's internal buffer once called.
	 *
	 * @return A FBulkDataBuffer that owns a copy of the BulkData, this might be a subset of the data depending on the value of RequestedSize.
	 */
	FBulkDataBuffer<uint16> GetCopyAsBuffer(int64 RequestedElementCount, bool bDiscardInternalCopy) { return FUntypedBulkData::GetCopyAsBuffer<uint16>(RequestedElementCount, bDiscardInternalCopy); }
};

/*-----------------------------------------------------------------------------
	int32 version of bulk data.
-----------------------------------------------------------------------------*/

struct COREUOBJECT_API FIntBulkDataOld : public FUntypedBulkData
{
	/**
	 * Returns size in bytes of single element.
	 *
	 * @return Size in bytes of single element
	 */
	virtual int32 GetElementSize() const;

	/**
	 * Serializes an element at a time allowing and dealing with endian conversion and backward compatiblity.
	 * 
	 * @param Ar			Archive to serialize with
	 * @param Data			Base pointer to data
	 * @param ElementIndex	Element index to serialize
	 */
	virtual void SerializeElement( FArchive& Ar, void* Data, int64 ElementIndex );

	/**
	 * Returns a copy encapsulated by a FBulkDataBuffer.
	 *
	 * @param RequestedElementCount If set to greater than 0, the returned FBulkDataBuffer will be limited to
	 * this number of elements. This will give an error if larger than the actual number of elements in the BulkData object.
	 * @param bDiscardInternalCopy If true then the BulkData object will free it's internal buffer once called.
	 *
	 * @return A FBulkDataBuffer that owns a copy of the BulkData, this might be a subset of the data depending on the value of RequestedSize.
	 */
	FBulkDataBuffer<int32> GetCopyAsBuffer(int64 RequestedElementCount, bool bDiscardInternalCopy) { return FUntypedBulkData::GetCopyAsBuffer<int32>(RequestedElementCount, bDiscardInternalCopy); }
};

/*-----------------------------------------------------------------------------
	float version of bulk data.
-----------------------------------------------------------------------------*/

struct COREUOBJECT_API FFloatBulkDataOld : public FUntypedBulkData
{
	/**
	 * Returns size in bytes of single element.
	 *
	 * @return Size in bytes of single element
	 */
	virtual int32 GetElementSize() const;

	/**
	 * Serializes an element at a time allowing and dealing with endian conversion and backward compatiblity.
	 * 
	 * @param Ar			Archive to serialize with
	 * @param Data			Base pointer to data
	 * @param ElementIndex	Element index to serialize
	 */
	virtual void SerializeElement( FArchive& Ar, void* Data, int64 ElementIndex );

	/**
	 * Returns a copy encapsulated by a FBulkDataBuffer.
	 *
	 * @param RequestedElementCount If set to greater than 0, the returned FBulkDataBuffer will be limited to
	 * this number of elements. This will give an error if larger than the actual number of elements in the BulkData object.
	 * @param bDiscardInternalCopy If true then the BulkData object will free it's internal buffer once called.
	 *
	 * @return A FBulkDataBuffer that owns a copy of the BulkData, this might be a subset of the data depending on the value of RequestedSize.
	 */
	FBulkDataBuffer<float> GetCopyAsBuffer(int64 RequestedElementCount, bool bDiscardInternalCopy) { return FUntypedBulkData::GetCopyAsBuffer<float>(RequestedElementCount, bDiscardInternalCopy); }
};

// Switch between the old and new types based on USE_NEW_BULKDATA
#if !USE_NEW_BULKDATA
using FBulkDataInterface	= FUntypedBulkData;
using FByteBulkData			= FByteBulkDataOld;
using FWordBulkData			= FWordBulkDataOld;
using FIntBulkData			= FIntBulkDataOld;
using FFloatBulkData		= FFloatBulkDataOld;
#else
using FBulkDataInterface	= FBulkDataBase;
using FByteBulkData			= FByteBulkData2;
using FWordBulkData			= FWordBulkData2;
using FIntBulkData			= FIntBulkData2;
using FFloatBulkData		= FFloatBulkData2;
#endif

class FFormatContainer
{
	friend class UBodySetup;

	TSortedMap<FName, FByteBulkData*, FDefaultAllocator, FNameFastLess> Formats;
	uint32 Alignment;
public:
	~FFormatContainer()
	{
		FlushData();
	}
	bool Contains(FName Format) const
	{
		return Formats.Contains(Format);
	}
	FByteBulkData& GetFormat(FName Format)
	{
		FByteBulkData* Result = Formats.FindRef(Format);
		if (!Result)
		{
			Result = new FByteBulkData;
			Formats.Add(Format, Result);
		}
		return *Result;
	}
	void FlushData()
	{
		for (const TPair<FName, FByteBulkData*>& Format : Formats)
		{
			delete Format.Value;
		}
		Formats.Empty();
	}
	COREUOBJECT_API void Serialize(FArchive& Ar, UObject* Owner, const TArray<FName>* FormatsToSave = nullptr, bool bSingleUse = true, uint32 InAlignment = DEFAULT_ALIGNMENT, bool bInline = true, bool bMapped = false);
	COREUOBJECT_API void SerializeAttemptMappedLoad(FArchive& Ar, UObject* Owner);
};
