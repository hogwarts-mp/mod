// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "Serialization/BulkData.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace BulkDataTest
{
	#define TEST_NAME_ROOT "System.CoreUObject.Serialization.BulkData"
	constexpr const uint32 TestFlags = EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	// Test code paths for BulkData objects that do not reference a file on disk.
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBulkDataTestTransient, TEST_NAME_ROOT ".Transient", TestFlags)
	bool FBulkDataTestTransient::RunTest(const FString& Parameters)
	{
		FByteBulkData BulkData;

		// We should be able to lock for read access but there should be no valid data
		const void* ReadOnlyDataPtr = BulkData.LockReadOnly();
		TestNull(TEXT("Locking an empty BulkData object for reading should return nullptr!"), ReadOnlyDataPtr);
		BulkData.Unlock();

		void* DataPtr = BulkData.Lock(LOCK_READ_WRITE);
		TestNull(TEXT("Locking an empty BulkData object for writing should return nullptr!"), DataPtr);
		BulkData.Unlock();

		void* CopyEmptyPtr = nullptr;
		BulkData.GetCopy(&CopyEmptyPtr, true);
		TestNull(TEXT("Getting a copy of an empty BulkData object for writing should return nullptr!"), CopyEmptyPtr);

		BulkData.Lock(LOCK_READ_WRITE);
		DataPtr = BulkData.Realloc(32 * 32 * 4);
		TestNotNull(TEXT("Reallocating an empty BulkData object should return a valid pointer!"), DataPtr);
		BulkData.Unlock();

		TestTrue(TEXT("BulkData should be loaded now that it has been reallocated"), BulkData.IsBulkDataLoaded());

		void* CopyWithDiscardPtr = nullptr;
		BulkData.GetCopy(&CopyWithDiscardPtr, true); // The second parameter should be ignored because the bulkdata cannot be reloaded from disk!
		TestNotNull(TEXT("GetCopy should return a valid pointer!"), CopyWithDiscardPtr);
		TestNotEqual(TEXT("GetCopy should return a copy of the data so the pointers should be different!"), DataPtr, CopyWithDiscardPtr);
		TestTrue(TEXT("BulkData should still loaded after taking a copy"), BulkData.IsBulkDataLoaded());

		// Now try GetCopy again but this time without the discard request
		void* CopyNoDiscardPtr = nullptr;
		BulkData.GetCopy(&CopyNoDiscardPtr, false);
		TestNotNull(TEXT("GetCopy should return a valid pointer!"), CopyNoDiscardPtr);
		TestNotEqual(TEXT("GetCopy should return a copy of the data so the pointers should be different!"), DataPtr, CopyNoDiscardPtr);
		TestNotEqual(TEXT("GetCopy should return a copy of the data so the pointers should be different!"), CopyWithDiscardPtr, CopyNoDiscardPtr);
		TestTrue(TEXT("BulkData should still loaded after taking a copy"), BulkData.IsBulkDataLoaded());

		// Clean up allocations from GetCopy
		FMemory::Free(CopyWithDiscardPtr);
		FMemory::Free(CopyNoDiscardPtr);

		// Now do one last lock test after GetCopy
		DataPtr = BulkData.Lock(LOCK_READ_WRITE);
		BulkData.Unlock();

		TestTrue(TEXT("BulkData should still loaded after locking for write"), BulkData.IsBulkDataLoaded());
		TestNotNull(TEXT("Locking for write should return a valid pointer!"), DataPtr);

		// Now remove the bulkdata and make sure that we cannot access the old data anymore
		BulkData.RemoveBulkData();
		TestFalse(TEXT("RemoveBulkData should've discarded the BulkData"), BulkData.IsBulkDataLoaded());

#if USE_NEW_BULKDATA // The warning only exists for BulkData2
		// Both the ::Lock and ::GetCopy calls should warn that we cannot load the missing data (as it will still have a valid size)
		AddExpectedError(TEXT("Attempting to load a BulkData object that cannot be loaded from disk"), EAutomationExpectedErrorFlags::Exact, 2);
#endif //USE_NEW_BULKDATA

		DataPtr = BulkData.Lock(LOCK_READ_WRITE);
		BulkData.Unlock();

		TestNull(TEXT("Locking for write after calling ::RemoveBulkData should return a nullptr!"), DataPtr);

		CopyEmptyPtr = nullptr;
		BulkData.GetCopy(&CopyNoDiscardPtr, true);
		TestNull(TEXT("Getting a copy of BulkData object after calling ::RemoveBulkData should return nullptr!"), DataPtr);

		return true;
	}	
}

#endif // WITH_DEV_AUTOMATION_TESTS
