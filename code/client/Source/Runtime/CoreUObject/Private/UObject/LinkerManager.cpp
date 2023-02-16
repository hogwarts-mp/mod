// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	LinkerManager.h: Unreal object linker manager
=============================================================================*/
#include "UObject/LinkerManager.h"
#include "Internationalization/GatherableTextData.h"
#include "UObject/Package.h"
#include "UObject/ObjectResource.h"
#include "UObject/LinkerLoad.h"
#include "UObject/UObjectThreadContext.h"
#include "ProfilingDebugging/CsvProfiler.h"

FLinkerManager& FLinkerManager::Get()
{
	static TUniquePtr<FLinkerManager> Singleton = MakeUnique<FLinkerManager>();
	return *Singleton;
}

FLinkerManager::FLinkerManager() :
	bHasPendingCleanup(false)
{
}

FLinkerManager::~FLinkerManager()
{
}

bool FLinkerManager::Exec(class UWorld* InWorld, const TCHAR* Cmd, FOutputDevice& Ar)
{
#if !UE_BUILD_SHIPPING
	if (FParse::Command(&Cmd, TEXT("LinkerLoadList")))
	{		
		UE_LOG(LogLinker, Display, TEXT("ObjectLoaders: %d"), ObjectLoaders.Num());
		for (auto Linker : ObjectLoaders)
		{
			UE_LOG(LogLinker, Display, TEXT("%s"), *Linker->Filename);
		}

		UE_LOG(LogLinker, Display, TEXT("LoadersWithNewImports: %d"), LoadersWithNewImports.Num());
		for (auto Linker : LoadersWithNewImports)
		{
			UE_LOG(LogLinker, Display, TEXT("%s"), *Linker->Filename);
		}
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
		UE_LOG(LogLinker, Display, TEXT("LiveLinkers: %d"), LiveLinkers.Num());
		for (auto Linker : LiveLinkers)
		{
			UE_LOG(LogLinker, Display, TEXT("%s"), *Linker->Filename);
		}
#endif
		return true;
	}
	else if (FParse::Command(&Cmd, TEXT("LINKERS")))
	{
		Ar.Logf(TEXT("Linkers:"));
		for (auto Linker : ObjectLoaders)
		{
			int32 NameSize = 0;
			for (int32 j = 0; j < Linker->NameMap.Num(); j++)
			{
				if (FNameEntryId Id = Linker->NameMap[j])
				{
					NameSize += FName::GetEntry(Id)->GetSizeInBytes();
				}
			}
			Ar.Logf
				(
				TEXT("%s (%s): Names=%i (%iK/%iK) Text=%i (%iK) Imports=%i (%iK) Exports=%i (%iK) Gen=%i Bulk=%i"),
				*Linker->Filename,
				*Linker->LinkerRoot->GetFullName(),
				Linker->NameMap.Num(),
				Linker->NameMap.Num() * sizeof(FName) / 1024,
				NameSize / 1024,
				Linker->GatherableTextDataMap.Num(),
				Linker->GatherableTextDataMap.Num() * sizeof(FGatherableTextData) / 1024,
				Linker->ImportMap.Num(),
				Linker->ImportMap.Num() * sizeof(FObjectImport) / 1024,
				Linker->ExportMap.Num(),
				Linker->ExportMap.Num() * sizeof(FObjectExport) / 1024,
				Linker->Summary.Generations.Num(),
#if WITH_EDITOR
				Linker->BulkDataLoaders.Num()
#else
				0
#endif // WITH_EDITOR
				);
		}

		return true;
	}
#endif // !UE_BUILD_SHIPPING

	return false;
}

void FLinkerManager::ResetLoaders(UObject* InPkg)
{
	// Top level package to reset loaders for.
	UObject*		TopLevelPackage = InPkg ? InPkg->GetOutermost() : NULL;

	// Find loader/ linker associated with toplevel package. We do this upfront as Detach resets LinkerRoot.
	if (TopLevelPackage)
	{
		// Linker to reset/ detach.
		auto LinkerToReset = FLinkerLoad::FindExistingLinkerForPackage(CastChecked<UPackage>(TopLevelPackage));
		if (LinkerToReset)
		{
			{
#if THREADSAFE_UOBJECTS
				FScopeLock ObjectLoadersLock(&ObjectLoadersCritical);
#endif
				for (auto Linker : ObjectLoaders)
				{
					// Detach LinkerToReset from other linker's import table.
					if (Linker->LinkerRoot != TopLevelPackage)
					{
						for (auto& Import : Linker->ImportMap)
						{
							if (Import.SourceLinker == LinkerToReset)
							{
								Import.SourceLinker = NULL;
								Import.SourceIndex = INDEX_NONE;
							}
						}
					}
					else
					{
						check(Linker == LinkerToReset);
					}
				}
			}
			// Detach linker, also removes from array and sets LinkerRoot to NULL.
			LinkerToReset->LoadAndDetachAllBulkData();
			LinkerToReset->Detach();
			RemoveLinker(LinkerToReset);
			LinkerToReset = nullptr;
		}
	}
	else
	{
		// We just want a copy here
		TSet<FLinkerLoad*> LinkersToDetach;
		GetLoaders(LinkersToDetach);
		for (auto Linker : LinkersToDetach)
		{
			// Detach linker, also removes from array and sets LinkerRoot to NULL.
			Linker->LoadAndDetachAllBulkData();
			Linker->Detach();
			RemoveLinker(Linker);
		}
	}
}

void FLinkerManager::ResetLoaders(const TSet<FLinkerLoad*>& InLinkerLoads)
{
	// Remove import references
	{
#if THREADSAFE_UOBJECTS
		FScopeLock ObjectLoadersLock(&ObjectLoadersCritical);
#endif
		for (FLinkerLoad* Linker : ObjectLoaders)
		{
			// Detach LinkerToReset from other linker's import table.
			if (!InLinkerLoads.Contains(Linker))
			{
				for (auto& Import : Linker->ImportMap)
				{
					if (InLinkerLoads.Contains(Import.SourceLinker))
					{
						Import.SourceLinker = NULL;
						Import.SourceIndex = INDEX_NONE;
					}
				}
			}
		}
	}
	for (FLinkerLoad* LinkerToReset : InLinkerLoads)
	{
		// Detach linker, also removes from array and sets LinkerRoot to NULL.
		LinkerToReset->LoadAndDetachAllBulkData();
		LinkerToReset->Detach();
	}
	// Remove all linkers in the specified set
	{
#if THREADSAFE_UOBJECTS
		FScopeLock PendingCleanupListLock(&PendingCleanupListCritical);
#endif
		PendingCleanupList.Append(InLinkerLoads.Array());
		bHasPendingCleanup = true;
	}
}

void FLinkerManager::EnsureLoadingComplete(UPackage* Package)
{
	if (!Package)
	{
		return;
	}
	FLinkerLoad* Linker = FLinkerLoad::FindExistingLinkerForPackage(Package);
	if (!Linker)
	{
		return;
	}

	if (!Package->HasAnyPackageFlags(PKG_FilterEditorOnly))
	{
		Linker->SerializeThumbnails();
	}
}

void FLinkerManager::DissociateImportsAndForcedExports()
{
	{
		// In cooked builds linkers don't stick around long enough to make this worthwhile
		TSet<FLinkerLoad*> LocalLoadersWithNewImports;
		GetLoadersWithNewImportsAndEmpty(LocalLoadersWithNewImports);

		for (FLinkerLoad* Linker : LocalLoadersWithNewImports)
		{
			for (int32 ImportIndex = 0; ImportIndex < Linker->ImportMap.Num(); ImportIndex++)
			{
				FObjectImport& Import = Linker->ImportMap[ImportIndex];
				// The import object could be stale if it has been replaced by patching
				// logic or compile on load..
				bool bIsStale = false;
				if (Import.SourceLinker && Import.SourceIndex != INDEX_NONE)
				{
					bIsStale = Import.SourceLinker->ExportMap[Import.SourceIndex].Object != Import.XObject;
				}
				if (bIsStale || (Import.XObject && !Import.XObject->IsNative()))
				{
					Import.XObject = nullptr;
				}
				Import.SourceLinker = nullptr;
				// when the SourceLinker is reset, the SourceIndex must also be reset, or recreating
				// an import that points to a redirector will fail to find the redirector
				Import.SourceIndex = INDEX_NONE;
			}
			if (Linker->GetSerializeContext())
			{
				Linker->GetSerializeContext()->ResetImportCount();
			}
		}
	}

	{		
		TSet<FLinkerLoad*> LocalLoadersWithForcedExports;
		GetLoadersWithForcedExportsAndEmpty(LocalLoadersWithForcedExports);
		for (FLinkerLoad* Linker : LocalLoadersWithForcedExports)
		{
			for (FObjectExport& Export : Linker->ExportMap)
			{
				if (Export.Object && Export.bForcedExport)
				{
					Export.Object->SetLinker(nullptr, INDEX_NONE);
					Export.ResetObject();
				}
			}
			if (Linker->GetSerializeContext())
			{
				Linker->GetSerializeContext()->ResetForcedExports();
			}
		}
	}
}

void FLinkerManager::DeleteLinkers()
{
	check(IsInGameThread());

	if(bHasPendingCleanup)
	{
		bHasPendingCleanup = false;
	
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FLinkerManager_DeleteLinkers);
		CSV_SCOPED_TIMING_STAT_EXCLUSIVE(DeleteLinkers);

		TArray<FLinkerLoad*> CleanupArray;
		{
#if THREADSAFE_UOBJECTS
			FScopeLock PendingCleanupListLock(&PendingCleanupListCritical);
#endif
			CleanupArray = PendingCleanupList.Array();
			PendingCleanupList.Empty();
		}

		// Note that even though DeleteLinkers can only be called on the main thread,
		// we store the IsDeletingLinkers in TLS so that we're sure nothing on
		// another thread can delete linkers except FLinkerManager at the time
		// we enter this loop.
		FUObjectThreadContext& ThreadContext = FUObjectThreadContext::Get();
		ThreadContext.IsDeletingLinkers = true;
		for (FLinkerLoad* Linker : CleanupArray)
		{
			delete Linker;
		}
		ThreadContext.IsDeletingLinkers = false;
	}
}

void FLinkerManager::RemoveLinker(FLinkerLoad* Linker)
{
#if THREADSAFE_UOBJECTS
	FScopeLock PendingCleanupListLock(&PendingCleanupListCritical);
#endif
	if (Linker && !PendingCleanupList.Contains(Linker))
	{
		PendingCleanupList.Add(Linker);
		bHasPendingCleanup = true;
	}
}
