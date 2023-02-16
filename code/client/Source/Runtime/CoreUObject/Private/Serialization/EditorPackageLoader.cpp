// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/EditorPackageLoader.h"
#include "Serialization/AsyncPackageLoader.h"
#include "Serialization/AsyncLoadingThread.h"
#include "Serialization/AsyncLoading2.h"
#include "UObject/UObjectThreadContext.h"
#include "Misc/PackageName.h"

DEFINE_LOG_CATEGORY_STATIC(LogEditorPackageLoader, Log, All);

#if WITH_IOSTORE_IN_EDITOR

class FEditorPackageLoader final
	: public IAsyncPackageLoader
{
public:
	FEditorPackageLoader(FIoDispatcher& InIoDispatcher, IEDLBootNotificationManager& InEDLBootNotificationManager)
	{
		CookedPackageLoader.Reset(MakeAsyncPackageLoader2(InIoDispatcher));
		UncookedPackageLoader.Reset(new FAsyncLoadingThread(/** ThreadIndex = */ 0, InEDLBootNotificationManager));
	}

	virtual ~FEditorPackageLoader () { }

	virtual void InitializeLoading() override
	{
		UE_LOG(LogEditorPackageLoader, Log, TEXT("Initializing EDL loader for cooked packages in editor"));
		CookedPackageLoader->InitializeLoading();
		UncookedPackageLoader->InitializeLoading();
	}

	virtual void ShutdownLoading() override
	{
		CookedPackageLoader->ShutdownLoading();
		UncookedPackageLoader->ShutdownLoading();
	}

	virtual void StartThread() override
	{
		CookedPackageLoader->StartThread();
		UncookedPackageLoader->StartThread();
	}

	virtual int32 LoadPackage(
			const FString& InPackageName,
			const FGuid* InGuid,
			const TCHAR* InPackageToLoadFrom,
			FLoadPackageAsyncDelegate InCompletionDelegate,
			EPackageFlags InPackageFlags,
			int32 InPIEInstanceID,
			int32 InPackagePriority,
			const FLinkerInstancingContext* InstancingContext) override
	{
		const TCHAR* PackageName = InPackageToLoadFrom ? InPackageToLoadFrom : *InPackageName;

		// Use the old loader if an uncooked package exists on disk
		const bool bDoesUncookedPackageExist = FPackageName::DoesPackageExist(InPackageName, nullptr, nullptr, true) && !DoesPackageExistInIoStore(FName(*InPackageName));
		if (bDoesUncookedPackageExist)
		{
			UE_LOG(LogEditorPackageLoader, Verbose, TEXT("Loading uncooked package '%s' from filesystem"), PackageName);
			return UncookedPackageLoader->LoadPackage(InPackageName, InGuid, InPackageToLoadFrom, InCompletionDelegate, InPackageFlags, InPIEInstanceID, InPackagePriority, InstancingContext);
		}
		else
		{
			UE_LOG(LogEditorPackageLoader, Verbose, TEXT("Loading cooked package '%s' from I/O Store"), PackageName);
			return CookedPackageLoader->LoadPackage(InPackageName, InGuid, InPackageToLoadFrom, InCompletionDelegate, InPackageFlags, InPIEInstanceID, InPackagePriority, InstancingContext);
		}
	}

	virtual EAsyncPackageState::Type ProcessLoading(bool bUseTimeLimit, bool bUseFullTimeLimit, float TimeLimit) override
	{
		EAsyncPackageState::Type CookedLoadingState = CookedPackageLoader->ProcessLoading(bUseTimeLimit, bUseFullTimeLimit, TimeLimit);
		EAsyncPackageState::Type UncookedLoadingState = UncookedPackageLoader->ProcessLoading(bUseTimeLimit, bUseFullTimeLimit, TimeLimit);

		return CookedLoadingState == EAsyncPackageState::Complete && UncookedLoadingState == EAsyncPackageState::Complete
			? EAsyncPackageState::Complete
			: EAsyncPackageState::TimeOut;
	}

	virtual EAsyncPackageState::Type ProcessLoadingUntilComplete(TFunctionRef<bool()> CompletionPredicate, float TimeLimit) override
	{
		const EAsyncPackageState::Type LoadingState = CookedPackageLoader->ProcessLoadingUntilComplete(CompletionPredicate, TimeLimit);
		if (LoadingState != EAsyncPackageState::Complete)
		{
			return LoadingState;
		}
		else if (CompletionPredicate())
		{
			return EAsyncPackageState::Complete;
		}
		else
		{
			return UncookedPackageLoader->ProcessLoadingUntilComplete(CompletionPredicate, TimeLimit);
		}
	}

	virtual void CancelLoading() override
	{
		CookedPackageLoader->CancelLoading();
		UncookedPackageLoader->CancelLoading();
	}

	virtual void SuspendLoading() override
	{
		CookedPackageLoader->SuspendLoading();
		UncookedPackageLoader->SuspendLoading();
	}

	virtual void ResumeLoading() override
	{
		CookedPackageLoader->ResumeLoading();
		UncookedPackageLoader->ResumeLoading();
	}

	virtual void FlushLoading(int32 PackageId) override
	{
		CookedPackageLoader->FlushLoading(PackageId);
		UncookedPackageLoader->FlushLoading(PackageId);
	}

	virtual int32 GetNumQueuedPackages() override
	{
		return CookedPackageLoader->GetNumQueuedPackages() + UncookedPackageLoader->GetNumQueuedPackages();
	}

	virtual int32 GetNumAsyncPackages() override
	{
		return CookedPackageLoader->GetNumAsyncPackages() + UncookedPackageLoader->GetNumAsyncPackages();
	}

	virtual float GetAsyncLoadPercentage(const FName& PackageName) override
	{
		float Percentage = CookedPackageLoader->GetAsyncLoadPercentage(PackageName);
		if (Percentage < 0.0f)
		{
			Percentage = UncookedPackageLoader->GetAsyncLoadPercentage(PackageName);
		}

		return Percentage;
	}

	virtual bool IsAsyncLoadingSuspended() override
	{
		return CookedPackageLoader->IsAsyncLoadingSuspended() | UncookedPackageLoader->IsAsyncLoadingSuspended();
	}

	virtual bool IsInAsyncLoadThread() override
	{
		return CookedPackageLoader->IsInAsyncLoadThread() | UncookedPackageLoader->IsInAsyncLoadThread();
	}

	virtual bool IsMultithreaded() override
	{
		check(CookedPackageLoader->IsMultithreaded() == UncookedPackageLoader->IsMultithreaded());
		return CookedPackageLoader->IsMultithreaded();
	}

	virtual bool IsAsyncLoadingPackages() override
	{
		const bool bIsAsyncLoadingCookedPackages = CookedPackageLoader->IsAsyncLoadingPackages();
		const bool bIsAsyncLoadingUncookedPackages = UncookedPackageLoader->IsAsyncLoadingPackages();

		return bIsAsyncLoadingCookedPackages | bIsAsyncLoadingUncookedPackages;
	}

	virtual void NotifyConstructedDuringAsyncLoading(UObject* Object, bool bSubObject) override
	{
		FUObjectThreadContext& ThreadContext = FUObjectThreadContext::Get();

		if (ThreadContext.AsyncPackageLoader == CookedPackageLoader.Get())
		{
			CookedPackageLoader->NotifyConstructedDuringAsyncLoading(Object, bSubObject);
		}
		else
		{
			check(ThreadContext.AsyncPackageLoader == UncookedPackageLoader.Get());
			UncookedPackageLoader->NotifyConstructedDuringAsyncLoading(Object, bSubObject);
		}
	}

	virtual void NotifyUnreachableObjects(const TArrayView<FUObjectItem*>& UnreachableObjects) override
	{
		// Only used in the new loader
		CookedPackageLoader->NotifyUnreachableObjects(UnreachableObjects);
	}

	virtual void FireCompletedCompiledInImport(void* AsyncPackage, FPackageIndex Import) override
	{
		// Only used in the old EDL loader which is not enabled in editor builds
	}

private:
	TUniquePtr<IAsyncPackageLoader> CookedPackageLoader;
	TUniquePtr<IAsyncPackageLoader> UncookedPackageLoader;
};

TUniquePtr<IAsyncPackageLoader> MakeEditorPackageLoader(FIoDispatcher& InIoDispatcher, IEDLBootNotificationManager& InEDLBootNotificationManager)
{
	return TUniquePtr<IAsyncPackageLoader>(new FEditorPackageLoader(InIoDispatcher, InEDLBootNotificationManager));
}

#endif // WITH_IOSTORE_IN_EDITOR
