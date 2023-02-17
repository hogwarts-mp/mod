// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/SavePackage/SavePackageUtilities.h"

#include "Algo/Sort.h"
#include "Algo/Unique.h"
#include "Blueprint/BlueprintSupport.h"
#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "Interfaces/ITargetPlatform.h"
#include "Misc/AssetRegistryInterface.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Serialization/BulkData.h"
#include "Serialization/BulkDataManifest.h"
#include "Serialization/LargeMemoryWriter.h"
#include "UObject/AsyncWorkSequence.h"
#include "UObject/Class.h"
#include "UObject/GCScopeLock.h"
#include "UObject/Linker.h"
#include "UObject/LinkerLoad.h"
#include "UObject/LinkerSave.h"
#include "UObject/Object.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectThreadContext.h"

DEFINE_LOG_CATEGORY(LogSavePackage);

#if ENABLE_COOK_STATS
#include "ProfilingDebugging/ScopedTimers.h"

int32 FSavePackageStats::NumPackagesSaved = 0;
double FSavePackageStats::SavePackageTimeSec = 0.0;
double FSavePackageStats::TagPackageExportsPresaveTimeSec = 0.0;
double FSavePackageStats::TagPackageExportsTimeSec = 0.0;
double FSavePackageStats::FullyLoadLoadersTimeSec = 0.0;
double FSavePackageStats::ResetLoadersTimeSec = 0.0;
double FSavePackageStats::TagPackageExportsGetObjectsWithOuter = 0.0;
double FSavePackageStats::TagPackageExportsGetObjectsWithMarks = 0.0;
double FSavePackageStats::SerializeImportsTimeSec = 0.0;
double FSavePackageStats::SortExportsSeekfreeInnerTimeSec = 0.0;
double FSavePackageStats::SerializeExportsTimeSec = 0.0;
double FSavePackageStats::SerializeBulkDataTimeSec = 0.0;
double FSavePackageStats::AsyncWriteTimeSec = 0.0;
double FSavePackageStats::MBWritten = 0.0;
TMap<FName, FArchiveDiffStats> FSavePackageStats::PackageDiffStats;
int32 FSavePackageStats::NumberOfDifferentPackages= 0;

FCookStatsManager::FAutoRegisterCallback FSavePackageStats::RegisterCookStats(FSavePackageStats::AddSavePackageStats);

void FSavePackageStats::AddSavePackageStats(FCookStatsManager::AddStatFuncRef AddStat)
{
	// Don't use FCookStatsManager::CreateKeyValueArray because there's just too many arguments. Don't need to overburden the compiler here.
	TArray<FCookStatsManager::StringKeyValue> StatsList;
	StatsList.Empty(15);
#define ADD_COOK_STAT(Name) StatsList.Emplace(TEXT(#Name), LexToString(Name))
	ADD_COOK_STAT(NumPackagesSaved);
	ADD_COOK_STAT(SavePackageTimeSec);
	ADD_COOK_STAT(TagPackageExportsPresaveTimeSec);
	ADD_COOK_STAT(TagPackageExportsTimeSec);
	ADD_COOK_STAT(FullyLoadLoadersTimeSec);
	ADD_COOK_STAT(ResetLoadersTimeSec);
	ADD_COOK_STAT(TagPackageExportsGetObjectsWithOuter);
	ADD_COOK_STAT(TagPackageExportsGetObjectsWithMarks);
	ADD_COOK_STAT(SerializeImportsTimeSec);
	ADD_COOK_STAT(SortExportsSeekfreeInnerTimeSec);
	ADD_COOK_STAT(SerializeExportsTimeSec);
	ADD_COOK_STAT(SerializeBulkDataTimeSec);
	ADD_COOK_STAT(AsyncWriteTimeSec);
	ADD_COOK_STAT(MBWritten);

	AddStat(TEXT("Package.Save"), StatsList);

	{
		PackageDiffStats.ValueSort([](const FArchiveDiffStats& Lhs, const FArchiveDiffStats& Rhs) { return Lhs.NewFileTotalSize > Rhs.NewFileTotalSize; });

		StatsList.Empty(15);
		for (const TPair<FName, FArchiveDiffStats>& Stat : PackageDiffStats)
		{
			StatsList.Emplace(Stat.Key.ToString(), LexToString((double)Stat.Value.NewFileTotalSize / 1024.0 / 1024.0));
		}

		AddStat(TEXT("Package.DifferentPackagesSizeMBPerAsset"), StatsList);
	}

	{
		PackageDiffStats.ValueSort([](const FArchiveDiffStats& Lhs, const FArchiveDiffStats& Rhs) { return Lhs.NumDiffs > Rhs.NumDiffs; });

		StatsList.Empty(15);
		for (const TPair<FName, FArchiveDiffStats>& Stat : PackageDiffStats)
		{
			StatsList.Emplace(Stat.Key.ToString(), LexToString(Stat.Value.NumDiffs));
		}

		AddStat(TEXT("Package.NumberOfDifferencesInPackagesPerAsset"), StatsList);
	}

	{
		PackageDiffStats.ValueSort([](const FArchiveDiffStats& Lhs, const FArchiveDiffStats& Rhs) { return Lhs.DiffSize > Rhs.DiffSize; });

		StatsList.Empty(15);
		for (const TPair<FName, FArchiveDiffStats>& Stat : PackageDiffStats)
		{
			StatsList.Emplace(Stat.Key.ToString(), LexToString((double)Stat.Value.DiffSize / 1024.0 / 1024.0));
		}

		AddStat(TEXT("Package.PackageDifferencesSizeMBPerAsset"), StatsList);
	}

	int64 NewFileTotalSize = 0;
	int64 NumDiffs = 0;
	int64 DiffSize = 0;
	for (const TPair<FName, FArchiveDiffStats>& PackageStat : PackageDiffStats)
	{
		NewFileTotalSize += PackageStat.Value.NewFileTotalSize;
		NumDiffs += PackageStat.Value.NumDiffs;
		DiffSize += PackageStat.Value.DiffSize;
	}

	double DifferentPackagesSizeMB = (double)NewFileTotalSize / 1024.0 / 1024.0;
	int32  NumberOfDifferencesInPackages = NumDiffs;
	double PackageDifferencesSizeMB = (double)DiffSize / 1024.0 / 1024.0;

	StatsList.Empty(15);
	ADD_COOK_STAT(NumberOfDifferentPackages);
	ADD_COOK_STAT(DifferentPackagesSizeMB);
	ADD_COOK_STAT(NumberOfDifferencesInPackages);
	ADD_COOK_STAT(PackageDifferencesSizeMB);

	AddStat(TEXT("Package.DiffTotal"), StatsList);

#undef ADD_COOK_STAT		
	const FString TotalString = TEXT("Total");
}

void FSavePackageStats::MergeStats(const TMap<FName, FArchiveDiffStats>& ToMerge)
{
	for (const TPair<FName, FArchiveDiffStats>& Stat : ToMerge)
	{
		PackageDiffStats.FindOrAdd(Stat.Key).DiffSize += Stat.Value.DiffSize;
		PackageDiffStats.FindOrAdd(Stat.Key).NewFileTotalSize += Stat.Value.NewFileTotalSize;
		PackageDiffStats.FindOrAdd(Stat.Key).NumDiffs += Stat.Value.NumDiffs;
	}
};

#endif

#if WITH_EDITORONLY_DATA

void FArchiveObjectCrc32NonEditorProperties::Serialize(void* Data, int64 Length)
{
	int32 NewEditorOnlyProp = EditorOnlyProp + this->IsEditorOnlyPropertyOnTheStack();
	TGuardValue<int32> Guard(EditorOnlyProp, NewEditorOnlyProp);
	if (NewEditorOnlyProp == 0)
	{
		Super::Serialize(Data, Length);
	}
}

#endif

static FThreadSafeCounter OutstandingAsyncWrites;

namespace SavePackageUtilities
{
const FName NAME_World("World");
const FName NAME_Level("Level");
const FName NAME_PrestreamPackage("PrestreamPackage");

void GetBlueprintNativeCodeGenReplacement(UObject* InObj, UClass*& ObjClass, UObject*& ObjOuter, FName& ObjName, const ITargetPlatform* TargetPlatform)
{
#if WITH_EDITOR
	if (const IBlueprintNativeCodeGenCore* Coordinator = IBlueprintNativeCodeGenCore::Get())
	{
		const FCompilerNativizationOptions& NativizationOptions = Coordinator->GetNativizationOptionsForPlatform(TargetPlatform);
		if (UClass* ReplacedClass = Coordinator->FindReplacedClassForObject(InObj, NativizationOptions))
		{
			ObjClass = ReplacedClass;
		}
		if (UObject* ReplacedOuter = Coordinator->FindReplacedNameAndOuter(InObj, /*out*/ObjName, NativizationOptions))
		{
			ObjOuter = ReplacedOuter;
		}
	}
#endif
}

void IncrementOutstandingAsyncWrites()
{
	OutstandingAsyncWrites.Increment();
}

void DecrementOutstandingAsyncWrites()
{
	OutstandingAsyncWrites.Decrement();
}

bool HasUnsaveableOuter(UObject* InObj, UPackage* InSavingPackage)
{
	UObject* Obj = InObj;
	while (Obj)
	{
		if (Obj->GetClass()->HasAnyClassFlags(CLASS_Deprecated) && !Obj->HasAnyFlags(RF_ClassDefaultObject))
		{
			if (!InObj->IsPendingKill() && InObj->GetOutermost() == InSavingPackage)
			{
				UE_LOG(LogSavePackage, Warning, TEXT("%s has a deprecated outer %s, so it will not be saved"), *InObj->GetFullName(), *Obj->GetFullName());
			}
			return true; 
		}

		if(Obj->IsPendingKill())
		{
			return true;
		}

		if (Obj->HasAnyFlags(RF_Transient) && !Obj->IsNative())
		{
			return true;
		}

		Obj = Obj->GetOuter();
	}
	return false;
}

void CheckObjectPriorToSave(FArchiveUObject& Ar, UObject* InObj, UPackage* InSavingPackage)
{
	if (!InObj)
	{
		return;
	}
	UObject* SerializedObject = nullptr;
	FUObjectSerializeContext* SaveContext = Ar.GetSerializeContext();
	check(SaveContext);
	SerializedObject = SaveContext->SerializedObject;

	if (!InObj->IsValidLowLevelFast() || !InObj->IsValidLowLevel())
	{
		UE_LOG(LogLinker, Fatal, TEXT("Attempt to save bogus object %p SaveContext.SerializedObject=%s  SerializedProperty=%s"), (void*)InObj, *GetFullNameSafe(SerializedObject), *GetFullNameSafe(Ar.GetSerializedProperty()));
		return;
	}
	// if the object class is abstract or has been marked as deprecated, mark this
	// object as transient so that it isn't serialized
	if ( InObj->GetClass()->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) )
	{
		if ( !InObj->HasAnyFlags(RF_ClassDefaultObject) || InObj->GetClass()->HasAnyClassFlags(CLASS_Deprecated) )
		{
			InObj->SetFlags(RF_Transient);
		}
		if ( !InObj->HasAnyFlags(RF_ClassDefaultObject) && InObj->GetClass()->HasAnyClassFlags(CLASS_HasInstancedReference) )
		{
			TArray<UObject*> ComponentReferences;
			FReferenceFinder ComponentCollector(ComponentReferences, InObj, false, true, true);
			ComponentCollector.FindReferences(InObj, SerializedObject, Ar.GetSerializedProperty());

			for ( int32 Index = 0; Index < ComponentReferences.Num(); Index++ )
			{
				ComponentReferences[Index]->SetFlags(RF_Transient);
			}
		}
	}
	else if ( HasUnsaveableOuter(InObj, InSavingPackage) )
	{
		InObj->SetFlags(RF_Transient);
	}

	if ( InObj->HasAnyFlags(RF_ClassDefaultObject)
		&& (InObj->GetClass()->ClassGeneratedBy == nullptr || !InObj->GetClass()->HasAnyFlags(RF_Transient)) )
	{
		// if this is the class default object, make sure it's not
		// marked transient for any reason, as we need it to be saved
		// to disk (unless it's associated with a transient generated class)
		InObj->ClearFlags(RF_Transient);
	}
}

/**
 * Determines the set of object marks that should be excluded for the target platform
 *
 * @param TargetPlatform	The platform being saved for, or null for saving platform-agnostic version
 *
 * @return Excluded object marks specific for the particular target platform, objects with any of these marks will be rejected from the cook
 */
EObjectMark GetExcludedObjectMarksForTargetPlatform(const class ITargetPlatform* TargetPlatform)
{
	EObjectMark ObjectMarks = OBJECTMARK_NOMARKS;

	if (TargetPlatform)
	{
		if (!TargetPlatform->HasEditorOnlyData())
		{
			ObjectMarks = (EObjectMark)(ObjectMarks | OBJECTMARK_EditorOnly);
		}

		const bool bIsServerOnly = TargetPlatform->IsServerOnly();
		const bool bIsClientOnly = TargetPlatform->IsClientOnly();

		if (bIsServerOnly)
		{
			ObjectMarks = (EObjectMark)(ObjectMarks | OBJECTMARK_NotForServer);
		}
		else if (bIsClientOnly)
		{
			ObjectMarks = (EObjectMark)(ObjectMarks | OBJECTMARK_NotForClient);
		}
	}

	return ObjectMarks;
}

/**
 * Marks object as not for client, not for server, or editor only. Recurses up outer/class chain as necessary
 */
void ConditionallyExcludeObjectForTarget(UObject* Obj, EObjectMark ExcludedObjectMarks, const ITargetPlatform* TargetPlatform)
{
#if WITH_EDITOR
	if (!Obj || Obj->GetOutermost()->GetFName() == GLongCoreUObjectPackageName)
	{
		// No object or in CoreUObject, don't exclude
		return;
	}

	auto InheritMarks = [](EObjectMark& MarksToModify, UObject* ObjToCheck, uint32 MarkMask)
	{
		EObjectMark ObjToCheckMarks = ObjToCheck->GetAllMarks();

		MarksToModify = (EObjectMark)(MarksToModify | (ObjToCheckMarks & MarkMask));
	};

	// MarksToProcess is a superset of marks retrieved from UPackage::GetExcludedObjectMarksForTargetPlatform
	const uint32 MarksToProcess = OBJECTMARK_EditorOnly | OBJECTMARK_NotForClient | OBJECTMARK_NotForServer | OBJECTMARK_KeepForTargetPlatform;
	check((ExcludedObjectMarks & ~MarksToProcess) == 0);

	EObjectMark CurrentMarks = OBJECTMARK_NOMARKS;
	InheritMarks(CurrentMarks, Obj, MarksToProcess);

	if ((CurrentMarks & MarksToProcess) != 0)
	{
		// Already marked
		return;
	}

	UObject* ObjOuter = Obj->GetOuter();
	UClass* ObjClass = Obj->GetClass();

	// if TargetPlatorm != nullptr then we are cooking
	if (TargetPlatform)
	{
		// Check for nativization replacement
		if (const IBlueprintNativeCodeGenCore* Coordinator = IBlueprintNativeCodeGenCore::Get())
		{
			const FCompilerNativizationOptions& NativizationOptions = Coordinator->GetNativizationOptionsForPlatform(TargetPlatform);
			FName UnusedName;
			if (UClass* ReplacedClass = Coordinator->FindReplacedClassForObject(Obj, NativizationOptions))
			{
				ObjClass = ReplacedClass;
			}
			if (UObject* ReplacedOuter = Coordinator->FindReplacedNameAndOuter(Obj, /*out*/UnusedName, NativizationOptions))
			{
				ObjOuter = ReplacedOuter;
			}
		}
	}

	EObjectMark NewMarks = CurrentMarks;

	// Recurse into parents, then compute inherited marks
	ConditionallyExcludeObjectForTarget(ObjClass, ExcludedObjectMarks, TargetPlatform);
	InheritMarks(NewMarks, ObjClass, OBJECTMARK_EditorOnly | OBJECTMARK_NotForClient | OBJECTMARK_NotForServer);

	if (ObjOuter)
	{
		ConditionallyExcludeObjectForTarget(ObjOuter, ExcludedObjectMarks, TargetPlatform);
		InheritMarks(NewMarks, ObjOuter, OBJECTMARK_EditorOnly | OBJECTMARK_NotForClient | OBJECTMARK_NotForServer);
	}

	// Check parent struct if we have one
	UStruct* ThisStruct = Cast<UStruct>(Obj);
	if (ThisStruct && ThisStruct->GetSuperStruct())
	{
		UObject* SuperStruct = ThisStruct->GetSuperStruct();
		ConditionallyExcludeObjectForTarget(SuperStruct, ExcludedObjectMarks, TargetPlatform);
		InheritMarks(NewMarks, SuperStruct, OBJECTMARK_EditorOnly | OBJECTMARK_NotForClient | OBJECTMARK_NotForServer);
	}

	// Check archetype, this may not have been covered in the case of components
	UObject* Archetype = Obj->GetArchetype();
	if (Archetype)
	{
		ConditionallyExcludeObjectForTarget(Archetype, ExcludedObjectMarks, TargetPlatform);
		InheritMarks(NewMarks, Archetype, OBJECTMARK_EditorOnly | OBJECTMARK_NotForClient | OBJECTMARK_NotForServer);
	}

	if (!Obj->HasAnyFlags(RF_ClassDefaultObject))
	{
		// CDOs must be included if their class is so only inherit marks, for everything else we check the native overrides as well
		if (!(NewMarks & OBJECTMARK_EditorOnly) && IsEditorOnlyObject(Obj, false, false))
		{
			NewMarks = (EObjectMark)(NewMarks | OBJECTMARK_EditorOnly);
		}

		if (!(NewMarks & OBJECTMARK_NotForClient) && !Obj->NeedsLoadForClient())
		{
			NewMarks = (EObjectMark)(NewMarks | OBJECTMARK_NotForClient);
		}

		if (!(NewMarks & OBJECTMARK_NotForServer) && !Obj->NeedsLoadForServer())
		{
			NewMarks = (EObjectMark)(NewMarks | OBJECTMARK_NotForServer);
		}

		if ((!(NewMarks & OBJECTMARK_NotForServer) || !(NewMarks & OBJECTMARK_NotForClient)) && TargetPlatform && !Obj->NeedsLoadForTargetPlatform(TargetPlatform))
		{
			NewMarks = (EObjectMark)(NewMarks | OBJECTMARK_NotForClient | OBJECTMARK_NotForServer);
		}
	}

	// If NotForClient and NotForServer, it is implicitly editor only
	if ((NewMarks & OBJECTMARK_NotForClient) && (NewMarks & OBJECTMARK_NotForServer))
	{
		NewMarks = (EObjectMark)(NewMarks | OBJECTMARK_EditorOnly);
	}

	// If not excluded after a full set of tests, it is implicitly a keep
	if (NewMarks == 0)
	{
		NewMarks = OBJECTMARK_KeepForTargetPlatform;
	}

	// If our marks are different than original, set them on the object
	if (CurrentMarks != NewMarks)
	{
		Obj->Mark(NewMarks);
	}
#endif
}

/**
 * Find most likely culprit that caused the objects in the passed in array to be considered for saving.
 *
 * @param	BadObjects	array of objects that are considered "bad" (e.g. non- RF_Public, in different map package, ...)
 * @return	UObject that is considered the most likely culprit causing them to be referenced or NULL
 */
void FindMostLikelyCulprit(TArray<UObject*> BadObjects, UObject*& MostLikelyCulprit, const FProperty*& PropertyRef)
{
	MostLikelyCulprit = nullptr;

	// Iterate over all objects that are marked as unserializable/ bad and print out their referencers.
	for (int32 BadObjIndex = 0; BadObjIndex < BadObjects.Num(); BadObjIndex++)
	{
		UObject* Obj = BadObjects[BadObjIndex];

		UE_LOG(LogSavePackage, Warning, TEXT("\r\nReferencers of %s:"), *Obj->GetFullName());

		FReferencerInformationList Refs;

		if (IsReferenced(Obj, RF_Public, EInternalObjectFlags::Native, true, &Refs))
		{
			for (int32 i = 0; i < Refs.ExternalReferences.Num(); i++)
			{
				UObject* RefObj = Refs.ExternalReferences[i].Referencer;
				if (RefObj->HasAnyMarks(EObjectMark(OBJECTMARK_TagExp | OBJECTMARK_TagImp)))
				{
					if (RefObj->GetFName() == NAME_PersistentLevel || RefObj->GetClass()->GetFName() == NAME_World)
					{
						// these types of references should be ignored
						continue;
					}

					UE_LOG(LogSavePackage, Warning, TEXT("\t%s (%i refs)"), *RefObj->GetFullName(), Refs.ExternalReferences[i].TotalReferences);
					for (int32 j = 0; j < Refs.ExternalReferences[i].ReferencingProperties.Num(); j++)
					{
						const FProperty* Prop = Refs.ExternalReferences[i].ReferencingProperties[j];
						UE_LOG(LogSavePackage, Warning, TEXT("\t\t%i) %s"), j, *Prop->GetFullName());
						PropertyRef = Prop;
					}

					MostLikelyCulprit = Obj;
				}
			}
		}
	}
}

void AddFileToHash(FString const& Filename, FMD5& Hash)
{
	TArray<uint8> LocalScratch;
	LocalScratch.SetNumUninitialized(1024 * 64);

	FArchive* Ar = IFileManager::Get().CreateFileReader(*Filename);

	const int64 Size = Ar->TotalSize();
	int64 Position = 0;

	while (Position < Size)
	{
		const auto ReadNum = FMath::Min(Size - Position, (int64)LocalScratch.Num());
		Ar->Serialize(LocalScratch.GetData(), ReadNum);
		Hash.Update(LocalScratch.GetData(), ReadNum);
		Position += ReadNum;
	}
	delete Ar;
}

void WriteToFile(const FString& Filename, const uint8* InDataPtr, int64 InDataSize)
{
	IFileManager& FileManager = IFileManager::Get();

	for (int tries = 0; tries < 3; ++tries)
	{
		if (FArchive* Ar = FileManager.CreateFileWriter(*Filename))
		{
			Ar->Serialize(/* grrr */ const_cast<uint8*>(InDataPtr), InDataSize);
			delete Ar;

			if (FileManager.FileSize(*Filename) != InDataSize)
			{
				FileManager.Delete(*Filename);

				UE_LOG(LogSavePackage, Fatal, TEXT("Could not save to %s!"), *Filename);
			}
			return;
		}
	}

	UE_LOG(LogSavePackage, Fatal, TEXT("Could not write to %s!"), *Filename);
}

void AsyncWriteFile(TAsyncWorkSequence<FMD5>& AsyncWriteAndHashSequence, FLargeMemoryPtr Data, const int64 DataSize, const TCHAR* Filename, EAsyncWriteOptions Options, TArrayView<const FFileRegion> InFileRegions)
{
	OutstandingAsyncWrites.Increment();
	FString OutputFilename(Filename);
	AsyncWriteAndHashSequence.AddWork([Data = MoveTemp(Data), DataSize, OutputFilename = MoveTemp(OutputFilename), Options, FileRegions = TArray<FFileRegion>(InFileRegions)](FMD5& State) mutable
	{
		if (EnumHasAnyFlags(Options, EAsyncWriteOptions::ComputeHash))
		{
			State.Update(Data.Get(), DataSize);
		}

		if (EnumHasAnyFlags(Options, EAsyncWriteOptions::WriteFileToDisk))
		{
			WriteToFile(OutputFilename, Data.Get(), DataSize);
		}

		if (FileRegions.Num() > 0)
		{
			TArray<uint8> Memory;
			FMemoryWriter Ar(Memory);
			FFileRegion::SerializeFileRegions(Ar, FileRegions);

			WriteToFile(OutputFilename + FFileRegion::RegionsFileExtension, Memory.GetData(), Memory.Num());
		}

		OutstandingAsyncWrites.Decrement();
	});
}

void AsyncWriteFileWithSplitExports(TAsyncWorkSequence<FMD5>& AsyncWriteAndHashSequence, FLargeMemoryPtr Data, const int64 DataSize, const int64 HeaderSize, const TCHAR* Filename, EAsyncWriteOptions Options, TArrayView<const FFileRegion> InFileRegions)
{
	OutstandingAsyncWrites.Increment();
	FString OutputFilename(Filename);
	AsyncWriteAndHashSequence.AddWork([Data = MoveTemp(Data), DataSize, HeaderSize, OutputFilename = MoveTemp(OutputFilename), Options, FileRegions = TArray<FFileRegion>(InFileRegions)](FMD5& State) mutable
	{
		if (EnumHasAnyFlags(Options, EAsyncWriteOptions::ComputeHash))
		{
			State.Update(Data.Get(), DataSize);
		}

		if (EnumHasAnyFlags(Options, EAsyncWriteOptions::WriteFileToDisk))
		{
			// Write .uasset file
			WriteToFile(OutputFilename, Data.Get(), HeaderSize);

			// Write .uexp file
			const FString FilenameExports = FPaths::ChangeExtension(OutputFilename, TEXT(".uexp"));
			WriteToFile(FilenameExports, Data.Get() + HeaderSize, DataSize - HeaderSize);

			if (FileRegions.Num() > 0)
			{
				// Adjust regions so they are relative to the start of the uexp file
				for (FFileRegion& Region : FileRegions)
				{
					Region.Offset -= HeaderSize;
				}

				TArray<uint8> Memory;
				FMemoryWriter Ar(Memory);
				FFileRegion::SerializeFileRegions(Ar, FileRegions);

				WriteToFile(FilenameExports + FFileRegion::RegionsFileExtension, Memory.GetData(), Memory.Num());
			}
		}

		OutstandingAsyncWrites.Decrement();
	});
}

/** For a CDO get all of the subobjects templates nested inside it or it's class */
void GetCDOSubobjects(UObject* CDO, TArray<UObject*>& Subobjects)
{
	TArray<UObject*> CurrentSubobjects;
	TArray<UObject*> NextSubobjects;

	// Recursively search for subobjects. Only care about ones that have a full subobject chain as some nested objects are set wrong
	GetObjectsWithOuter(CDO->GetClass(), NextSubobjects, false);
	GetObjectsWithOuter(CDO, NextSubobjects, false);

	while (NextSubobjects.Num() > 0)
	{
		CurrentSubobjects = NextSubobjects;
		NextSubobjects.Empty();
		for (UObject* SubObj : CurrentSubobjects)
		{
			if (SubObj->HasAnyFlags(RF_DefaultSubObject | RF_ArchetypeObject))
			{
				Subobjects.Add(SubObj);
				GetObjectsWithOuter(SubObj, NextSubobjects, false);
			}
		}
	}
}

} // end namespace SavePackageUtilities

bool IsEditorOnlyObject(const UObject* InObject, bool bCheckRecursive, bool bCheckMarks)
{
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("IsEditorOnlyObject"), STAT_IsEditorOnlyObject, STATGROUP_LoadTime);

	// Configurable via ini setting
	static struct FCanStripEditorOnlyExportsAndImports
	{
		bool bCanStripEditorOnlyObjects;
		FCanStripEditorOnlyExportsAndImports()
			: bCanStripEditorOnlyObjects(true)
		{
			GConfig->GetBool(TEXT("Core.System"), TEXT("CanStripEditorOnlyExportsAndImports"), bCanStripEditorOnlyObjects, GEngineIni);
		}
		FORCEINLINE operator bool() const { return bCanStripEditorOnlyObjects; }
	} CanStripEditorOnlyExportsAndImports;
	if (!CanStripEditorOnlyExportsAndImports)
	{
		return false;
	}
	check(InObject);

	if ((bCheckMarks && InObject->HasAnyMarks(OBJECTMARK_EditorOnly)) || InObject->IsEditorOnly())
	{
		return true;
	}

	// If this is a package that is editor only or the object is in editor-only package,
	// the object is editor-only too.
	const bool bIsAPackage = InObject->IsA<UPackage>();
	const UPackage* Package;
	if (bIsAPackage)
	{
		if (InObject->HasAnyFlags(RF_ClassDefaultObject))
		{
			// The default package is not editor-only, and it is part of a cycle that would cause infinite recursion: DefaultPackage -> GetOuter() -> Package:/Script/CoreUObject -> GetArchetype() -> DefaultPackage
			return false;
		}
		Package = static_cast<const UPackage*>(InObject);
	}
	else
	{
		Package = InObject->GetOutermost();
	}
	
	if (Package && Package->HasAnyPackageFlags(PKG_EditorOnly))
	{
		return true;
	}

	if (bCheckRecursive && !InObject->IsNative())
	{
		UObject* Outer = InObject->GetOuter();
		if (Outer && Outer != Package)
		{
			if (IsEditorOnlyObject(Outer, true, bCheckMarks))
			{
				return true;
			}
		}
		const UStruct* InStruct = Cast<UStruct>(InObject);
		if (InStruct)
		{
			const UStruct* SuperStruct = InStruct->GetSuperStruct();
			if (SuperStruct && IsEditorOnlyObject(SuperStruct, true, bCheckMarks))
			{
				return true;
			}
		}
		else
		{
			if (IsEditorOnlyObject(InObject->GetClass(), true, bCheckMarks))
			{
				return true;
			}

			UObject* Archetype = InObject->GetArchetype();
			if (Archetype && IsEditorOnlyObject(Archetype, true, bCheckMarks))
			{
				return true;
			}
		}
	}
	return false;
}

bool FObjectExportSortHelper::operator()(const FObjectExport& A, const FObjectExport& B) const
{
	int32 Result = 0;
	if (A.Object == nullptr)
	{
		Result = 1;
	}
	else if (B.Object == nullptr)
	{
		Result = -1;
	}
	else
	{
		if (bUseFObjectFullName)
		{
			const FObjectFullName* FullNameA = ObjectToObjectFullNameMap.Find(A.Object);
			const FObjectFullName* FullNameB = ObjectToObjectFullNameMap.Find(B.Object);
			checkSlow(FullNameA);
			checkSlow(FullNameB);

			if (FullNameA->ClassName != FullNameB->ClassName)
			{
				Result = FCString::Stricmp(*FullNameA->ClassName.ToString(), *FullNameB->ClassName.ToString());
			}
			else
			{
				int Num = FMath::Min(FullNameA->Path.Num(), FullNameB->Path.Num());
				for (int I = 0; I < Num; ++I)
				{
					if (FullNameA->Path[I] != FullNameB->Path[I])
					{
						Result = FCString::Stricmp(*FullNameA->Path[I].ToString(), *FullNameB->Path[I].ToString());
						break;
					}
				}
				if (Result == 0)
				{
					Result = FullNameA->Path.Num() - FullNameB->Path.Num();
				}
			}
		}
		else
		{
			const FString* FullNameA = ObjectToFullNameMap.Find(A.Object);
			const FString* FullNameB = ObjectToFullNameMap.Find(B.Object);
			checkSlow(FullNameA);
			checkSlow(FullNameB);

			Result = FCString::Stricmp(**FullNameA, **FullNameB);
		}
	}

	return Result < 0;
}

FObjectExportSortHelper::FObjectFullName::FObjectFullName(const UObject* Object, const UObject* Root)
{
	ClassName = Object->GetClass()->GetFName();
	const UObject* Current = Object;
	while (Current != nullptr && Current != Root)
	{
		Path.Insert(Current->GetFName(), 0);
		Current = Current->GetOuter();
	}
}

FObjectExportSortHelper::FObjectFullName::FObjectFullName(FObjectFullName&& InFullName)
{
	ClassName = InFullName.ClassName;
	Swap(Path, InFullName.Path);
}

void FObjectExportSortHelper::SortExports( FLinkerSave* Linker, FLinkerLoad* LinkerToConformTo, bool InbUseFObjectFullName)
{
	bUseFObjectFullName = InbUseFObjectFullName;

	if (bUseFObjectFullName)
	{
		ObjectToObjectFullNameMap.Reserve(Linker->ExportMap.Num());
	}
	else
	{
		ObjectToFullNameMap.Reserve(Linker->ExportMap.Num());
	}

	int32 SortStartPosition=0;
	if ( LinkerToConformTo )
	{
		// build a map of object full names to the index into the new linker's export map prior to sorting.
		// we need to do a little trickery here to generate an object path name that will match what we'll get back
		// when we call GetExportFullName on the LinkerToConformTo's exports, due to localized packages and forced exports.
		const FString LinkerName = Linker->LinkerRoot->GetName();
		const FString PathNamePrefix = LinkerName + TEXT(".");

		// Populate object to current index map.
		TMap<FString,int32> OriginalExportIndexes;
		OriginalExportIndexes.Reserve(Linker->ExportMap.Num());
		for( int32 ExportIndex=0; ExportIndex < Linker->ExportMap.Num(); ExportIndex++ )
		{
			const FObjectExport& Export = Linker->ExportMap[ExportIndex];
			if( Export.Object )
			{
				// get the path name for this object; if the object is contained within the package we're saving,
				// we don't want the returned path name to contain the package name since we'll be adding that on
				// to ensure that forced exports have the same outermost name as the non-forced exports
				FString ObjectPathName = Export.Object != Linker->LinkerRoot
					? Export.Object->GetPathName(Linker->LinkerRoot)
					: LinkerName;
						
				FString ExportFullName = Export.Object->GetClass()->GetName() + TEXT(" ") + PathNamePrefix + ObjectPathName;

				// Set the index (key) in the map to the index of this object into the export map.
				OriginalExportIndexes.Add( *ExportFullName, ExportIndex );
				if (bUseFObjectFullName)
				{
					FObjectFullName ObjectFullName(Export.Object, Linker->LinkerRoot);
					ObjectToObjectFullNameMap.Add(Export.Object, MoveTemp(ObjectFullName)); 
				}
				else
				{
					ObjectToFullNameMap.Add(Export.Object, *ExportFullName);
				}
			}
		}

		// backup the existing export list so we can empty the linker's actual list
		TArray<FObjectExport> OldExportMap = Linker->ExportMap;
		Linker->ExportMap.Empty(Linker->ExportMap.Num());

		// this array tracks which exports from the new package exist in the old package
		TArray<uint8> Used;
		Used.AddZeroed(OldExportMap.Num());

		for( int32 i = 0; i<LinkerToConformTo->ExportMap.Num(); i++ )
		{
			// determine whether the new version of the package contains this export from the old package
			FString ExportFullName = LinkerToConformTo->GetExportFullName(i, *LinkerName);
			int32* OriginalExportPosition = OriginalExportIndexes.Find( *ExportFullName );
			if( OriginalExportPosition )
			{
				// this export exists in the new package as well,
				// create a copy of the FObjectExport located at the original index and place it
				// into the matching position in the new package's export map
				FObjectExport* NewExport = new(Linker->ExportMap) FObjectExport( OldExportMap[*OriginalExportPosition] );
				check(NewExport->Object == OldExportMap[*OriginalExportPosition].Object);
				Used[ *OriginalExportPosition ] = 1;
			}
			else
			{

				// this export no longer exists in the new package; to ensure that the _LinkerIndex matches, add an empty entry to pad the list
				new(Linker->ExportMap)FObjectExport( nullptr );
				UE_LOG(LogSavePackage, Log, TEXT("No matching export found in new package for original export %i: %s"), i, *ExportFullName);
			}
		}



		SortStartPosition = LinkerToConformTo->ExportMap.Num();
		for( int32 i=0; i<Used.Num(); i++ )
		{
			if( !Used[i] )
			{
				// the FObjectExport located at pos "i" in the original export table did not
				// exist in the old package - add it to the end of the export table
				new(Linker->ExportMap) FObjectExport( OldExportMap[i] );
			}
		}

#if DO_GUARD_SLOW

		// sanity-check: make sure that all exports which existed in the linker before we sorted exist in the linker's export map now
		{
			TSet<UObject*> ExportObjectList;
			for( int32 ExportIndex=0; ExportIndex<Linker->ExportMap.Num(); ExportIndex++ )
			{
				ExportObjectList.Add(Linker->ExportMap[ExportIndex].Object);
			}

			for( int32 OldExportIndex=0; OldExportIndex<OldExportMap.Num(); OldExportIndex++ )
			{
				check(ExportObjectList.Contains(OldExportMap[OldExportIndex].Object));
			}
		}
#endif
	}
	else
	{
		for ( int32 ExportIndex = 0; ExportIndex < Linker->ExportMap.Num(); ExportIndex++ )
		{
			const FObjectExport& Export = Linker->ExportMap[ExportIndex];
			if ( Export.Object )
			{
				if (bUseFObjectFullName)
				{
					FObjectFullName ObjectFullName(Export.Object, nullptr);
					ObjectToObjectFullNameMap.Add(Export.Object, MoveTemp(ObjectFullName));
				}
				else
				{
					ObjectToFullNameMap.Add(Export.Object, Export.Object->GetFullName());
				}
			}
		}
	}

	if ( SortStartPosition < Linker->ExportMap.Num() )
	{
		Sort( &Linker->ExportMap[SortStartPosition], Linker->ExportMap.Num() - SortStartPosition, *this );
	}
}

FEDLCookChecker::FEDLNodeHash::FEDLNodeHash()
{
}

FEDLCookChecker::FEDLNodeHash::FEDLNodeHash(const TArray<FEDLNodeData>* InNodes, FEDLNodeID InNodeID, EObjectEvent InObjectEvent)
	: Nodes(InNodes)
	, NodeID(InNodeID)
	, bIsNode(true)
	, ObjectEvent(InObjectEvent)
{
}

FEDLCookChecker::FEDLNodeHash::FEDLNodeHash(const UObject* InObject, EObjectEvent InObjectEvent)
	: Object(InObject)
	, bIsNode(false)
	, ObjectEvent(InObjectEvent)
{
}

bool FEDLCookChecker::FEDLNodeHash::operator==(const FEDLNodeHash& Other) const
{
	if (ObjectEvent != Other.ObjectEvent)
	{
		return false;
	}

	uint32 LocalNodeID;
	uint32 OtherNodeID;
	const UObject* LocalObject;
	const UObject* OtherObject;
	FName LocalName = ObjectNameFirst(*this, LocalNodeID, LocalObject);
	FName OtherName = ObjectNameFirst(Other, OtherNodeID, OtherObject);

	do
	{
		if (LocalName != OtherName)
		{
			return false;
		}
		LocalName = ObjectNameNext(*this, LocalNodeID, LocalObject);
		OtherName = ObjectNameNext(Other, OtherNodeID, OtherObject);
	} while (!LocalName.IsNone() && !OtherName.IsNone());
	return LocalName.IsNone() == OtherName.IsNone();
}

uint32 GetTypeHash(const FEDLCookChecker::FEDLNodeHash& A)
{
	uint32 Hash = 0;

	uint32 LocalNodeID;
	const UObject* LocalObject;
	FName LocalName = FEDLCookChecker::FEDLNodeHash::ObjectNameFirst(A, LocalNodeID, LocalObject);
	do
	{
		Hash = HashCombine(Hash, GetTypeHash(LocalName));
		LocalName = FEDLCookChecker::FEDLNodeHash::ObjectNameNext(A, LocalNodeID, LocalObject);
	} while (!LocalName.IsNone());

	return (Hash << 1) | (uint32)A.ObjectEvent;
}

FName FEDLCookChecker::FEDLNodeHash::GetName() const
{
	if (bIsNode)
	{
		return (*Nodes)[NodeID].Name;
	}
	else
	{
		return Object->GetFName();
	}
}

bool FEDLCookChecker::FEDLNodeHash::TryGetParent(FEDLCookChecker::FEDLNodeHash& Parent) const
{
	EObjectEvent ParentObjectEvent = EObjectEvent::Create; // For purposes of parents, which is used only to get the ObjectPath, we always use the Create version of the node as the parent
	if (bIsNode)
	{
		FEDLNodeID ParentID = (*Nodes)[NodeID].ParentID;
		if (ParentID != NodeIDInvalid)
		{
			Parent = FEDLNodeHash(Nodes, ParentID, ParentObjectEvent);
			return true;
		}
	}
	else
	{
		UObject* ParentObject = Object->GetOuter();
		if (ParentObject)
		{
			Parent = FEDLNodeHash(ParentObject, ParentObjectEvent);
			return true;
		}
	}
	return false;
}

FEDLCookChecker::EObjectEvent FEDLCookChecker::FEDLNodeHash::GetObjectEvent() const
{
	return ObjectEvent;
}

void FEDLCookChecker::FEDLNodeHash::SetNodes(const TArray<FEDLNodeData>* InNodes)
{
	if (bIsNode)
	{
		Nodes = InNodes;
	}
}

FName FEDLCookChecker::FEDLNodeHash::ObjectNameFirst(const FEDLNodeHash& InNode, uint32& OutNodeID, const UObject*& OutObject)
{
	if (InNode.bIsNode)
	{
		OutNodeID = InNode.NodeID;
		return (*InNode.Nodes)[OutNodeID].Name;
	}
	else
	{
		OutObject = InNode.Object;
		return OutObject->GetFName();
	}
}

FName FEDLCookChecker::FEDLNodeHash::ObjectNameNext(const FEDLNodeHash& InNode, uint32& OutNodeID, const UObject*& OutObject)
{
	if (InNode.bIsNode)
	{
		OutNodeID = (*InNode.Nodes)[OutNodeID].ParentID;
		return OutNodeID != NodeIDInvalid ? (*InNode.Nodes)[OutNodeID].Name : NAME_None;
	}
	else
	{
		OutObject = OutObject->GetOuter();
		return OutObject ? OutObject->GetFName() : NAME_None;
	}
}

FEDLCookChecker::FEDLNodeData::FEDLNodeData(FEDLNodeID InID, FEDLNodeID InParentID, FName InName, EObjectEvent InObjectEvent)
	: Name(InName)
	, ID(InID)
	, ParentID(InParentID)
	, ObjectEvent(InObjectEvent)
	, bIsExport(false)
{
}

FEDLCookChecker::FEDLNodeData::FEDLNodeData(FEDLNodeID InID, FEDLNodeID InParentID, FName InName, FEDLNodeData&& Other)
	: Name(InName)
	, ID(InID)
	, ImportingPackagesSorted(MoveTemp(Other.ImportingPackagesSorted))
	, ParentID(InParentID)
	, ObjectEvent(Other.ObjectEvent)
	, bIsExport(Other.bIsExport)
{
	// Note that Other Name and ParentID must be unmodified, since they might still be needed for GetHashCode calls from children
	Other.ImportingPackagesSorted.Empty();
}

FEDLCookChecker::FEDLNodeHash FEDLCookChecker::FEDLNodeData::GetNodeHash(const FEDLCookChecker& Owner) const
{
	return FEDLNodeHash(&Owner.Nodes, ID, ObjectEvent);
}

FString FEDLCookChecker::FEDLNodeData::ToString(const FEDLCookChecker& Owner) const
{
	TStringBuilder<NAME_SIZE> Result;
	switch (ObjectEvent)
	{
	case EObjectEvent::Create:
		Result << TEXT("Create:");
		break;
	case EObjectEvent::Serialize:
		Result << TEXT("Serialize:");
		break;
	default:
		check(false);
		break;
	}
	AppendPathName(Owner, Result);
	return FString(Result);
}

void FEDLCookChecker::FEDLNodeData::AppendPathName(const FEDLCookChecker& Owner, FStringBuilderBase& Result) const
{
	if (ParentID != NodeIDInvalid)
	{
		const FEDLNodeData& ParentNode = Owner.Nodes[ParentID];
		ParentNode.AppendPathName(Owner, Result);
		bool bParentIsOutermost = ParentNode.ParentID == NodeIDInvalid;
		Result << (bParentIsOutermost ? TEXT(".") : SUBOBJECT_DELIMITER);
	}
	Name.AppendString(Result);
}

void FEDLCookChecker::FEDLNodeData::Merge(FEDLCookChecker::FEDLNodeData&& Other)
{
	check(ObjectEvent == Other.ObjectEvent);
	bIsExport = bIsExport | Other.bIsExport;

	ImportingPackagesSorted.Append(Other.ImportingPackagesSorted);
	Algo::Sort(ImportingPackagesSorted, FNameFastLess());
	ImportingPackagesSorted.SetNum(Algo::Unique(ImportingPackagesSorted), true /* bAllowShrinking */);
}

FEDLCookChecker::FEDLCookChecker(EInternalConstruct)
	: bIsActive(false)
{

}

FEDLCookChecker::FEDLCookChecker()
	:FEDLCookChecker(EInternalConstruct::Type)
{
	SetActiveIfNeeded();

	FScopeLock CookCheckerInstanceLock(&CookCheckerInstanceCritical);
	CookCheckerInstances.Add(this);
}

void FEDLCookChecker::SetActiveIfNeeded()
{
	bIsActive = IsEventDrivenLoaderEnabledInCookedBuilds() && !FParse::Param(FCommandLine::Get(), TEXT("DisableEDLCookChecker"));
}

void FEDLCookChecker::Reset()
{
	check(!GIsSavingPackage);

	Nodes.Empty();
	NodeHashToNodeID.Empty();
	NodePrereqs.Empty();
	bIsActive = false;
}

void FEDLCookChecker::AddImport(UObject* Import, UPackage* ImportingPackage)
{
	if (bIsActive)
	{
		if (!Import->GetOutermost()->HasAnyPackageFlags(PKG_CompiledIn))
		{
			FEDLNodeID NodeId = FindOrAddNode(FEDLNodeHash(Import, EObjectEvent::Serialize));
			FEDLNodeData& NodeData = Nodes[NodeId];
			FName ImportingPackageName = ImportingPackage->GetFName();
			TArray<FName>& Sorted = NodeData.ImportingPackagesSorted;
			int32 InsertionIndex = Algo::LowerBound(Sorted, ImportingPackageName, FNameFastLess());
			if (InsertionIndex == Sorted.Num() || Sorted[InsertionIndex] != ImportingPackageName)
			{
				Sorted.Insert(ImportingPackageName, InsertionIndex);
			}
		}
	}
}
void FEDLCookChecker::AddExport(UObject* Export)
{
	if (bIsActive)
	{
		FEDLNodeID SerializeID = FindOrAddNode(FEDLNodeHash(Export, EObjectEvent::Serialize));
		Nodes[SerializeID].bIsExport = true;
		FEDLNodeID CreateID = FindOrAddNode(FEDLNodeHash(Export, EObjectEvent::Create));
		Nodes[CreateID].bIsExport = true;
		AddDependency(SerializeID, CreateID); // every export must be created before it can be serialize...these arcs are implicit and not listed in any table.
	}
}

void FEDLCookChecker::AddArc(UObject* DepObject, bool bDepIsSerialize, UObject* Export, bool bExportIsSerialize)
{
	if (bIsActive)
	{
		FEDLNodeID ExportID = FindOrAddNode(FEDLNodeHash(Export, bExportIsSerialize ? EObjectEvent::Serialize : EObjectEvent::Create));
		FEDLNodeID DepID = FindOrAddNode(FEDLNodeHash(DepObject, bDepIsSerialize ? EObjectEvent::Serialize : EObjectEvent::Create));
		AddDependency(ExportID, DepID);
	}
}

void FEDLCookChecker::AddDependency(FEDLNodeID SourceID, FEDLNodeID TargetID)
{
	NodePrereqs.Add(SourceID, TargetID);
}

void FEDLCookChecker::StartSavingEDLCookInfoForVerification()
{
	FScopeLock CookCheckerInstanceLock(&CookCheckerInstanceCritical);
	for (FEDLCookChecker* Checker : CookCheckerInstances)
	{
		Checker->Reset();
		Checker->SetActiveIfNeeded();
	}
}

bool FEDLCookChecker::CheckForCyclesInner(TSet<FEDLNodeID>& Visited, TSet<FEDLNodeID>& Stack, const FEDLNodeID& Visit, FEDLNodeID& FailNode)
{
	bool bResult = false;
	if (Stack.Contains(Visit))
	{
		FailNode = Visit;
		bResult = true;
	}
	else
	{
		bool bWasAlreadyTested = false;
		Visited.Add(Visit, &bWasAlreadyTested);
		if (!bWasAlreadyTested)
		{
			Stack.Add(Visit);
			for (auto It = NodePrereqs.CreateConstKeyIterator(Visit); !bResult && It; ++It)
			{
				bResult = CheckForCyclesInner(Visited, Stack, It.Value(), FailNode);
			}
			Stack.Remove(Visit);
		}
	}
	UE_CLOG(bResult && Stack.Contains(FailNode), LogSavePackage, Error, TEXT("Cycle Node %s"), *Nodes[Visit].ToString(*this));
	return bResult;
}

FEDLCookChecker::FEDLNodeID FEDLCookChecker::FindOrAddNode(const FEDLNodeHash& NodeHash)
{
	uint32 TypeHash = GetTypeHash(NodeHash);
	FEDLNodeID* NodeIDPtr = NodeHashToNodeID.FindByHash(TypeHash, NodeHash);
	if (NodeIDPtr)
	{
		return *NodeIDPtr;
	}

	FName Name = NodeHash.GetName();
	FEDLNodeHash ParentHash;
	FEDLNodeID ParentID = NodeHash.TryGetParent(ParentHash) ? FindOrAddNode(ParentHash) : NodeIDInvalid;
	FEDLNodeID NodeID = Nodes.Num();
	FEDLNodeData& NewNodeData = Nodes.Emplace_GetRef(NodeID, ParentID, Name, NodeHash.GetObjectEvent());
	NodeHashToNodeID.AddByHash(TypeHash, NewNodeData.GetNodeHash(*this), NodeID);
	return NodeID;
}

FEDLCookChecker::FEDLNodeID FEDLCookChecker::FindOrAddNode(FEDLNodeData&& NodeData, const FEDLCookChecker& OldOwnerOfNode, FEDLNodeID ParentIDInThis, bool& bNew)
{
	// Note that NodeData's Name and ParentID must be unmodified, since they might still be needed for GetHashCode calls from children

	FEDLNodeHash NodeHash = NodeData.GetNodeHash(OldOwnerOfNode);
	uint32 TypeHash = GetTypeHash(NodeHash);
	FEDLNodeID* NodeIDPtr = NodeHashToNodeID.FindByHash(TypeHash, NodeHash);
	if (NodeIDPtr)
	{
		bNew = false;
		return *NodeIDPtr;
	}

	FEDLNodeID NodeID = Nodes.Num();
	FEDLNodeData& NewNodeData = Nodes.Emplace_GetRef(NodeID, ParentIDInThis, NodeData.Name, MoveTemp(NodeData));
	NodeHashToNodeID.AddByHash(TypeHash, NewNodeData.GetNodeHash(*this), NodeID);
	bNew = true;
	return NodeID;
}

FEDLCookChecker::FEDLNodeID FEDLCookChecker::FindNode(const FEDLNodeHash& NodeHash)
{
	const FEDLNodeID* NodeIDPtr = NodeHashToNodeID.Find(NodeHash);
	return NodeIDPtr ? *NodeIDPtr : NodeIDInvalid;
}

void FEDLCookChecker::Merge(FEDLCookChecker&& Other)
{
	if (Nodes.Num() == 0)
	{
		Swap(Nodes, Other.Nodes);
		Swap(NodeHashToNodeID, Other.NodeHashToNodeID);
		Swap(NodePrereqs, Other.NodePrereqs);

		// Switch the pointers in all of the swapped data to point at this instead of Other
		for (TPair<FEDLNodeHash, FEDLNodeID>& KVPair : NodeHashToNodeID)
		{
			FEDLNodeHash& NodeHash = KVPair.Key;
			NodeHash.SetNodes(&Nodes);
		}
	}
	else
	{
		Other.NodeHashToNodeID.Empty(); // We will be invalidating the data these NodeHashes point to in the Other.Nodes loop, so empty the array now to avoid using it by accident

		TArray<FEDLNodeID> RemapIDs;
		RemapIDs.Reserve(Other.Nodes.Num());
		for (FEDLNodeData& NodeData : Other.Nodes)
		{
			FEDLNodeID ParentID;
			if (NodeData.ParentID == NodeIDInvalid)
			{
				ParentID = NodeIDInvalid;
			}
			else
			{
				// Parents should be earlier in the nodes list than children, since we always FindOrAdd the parent (and hence add it to the nodelist) when creating the child.
				// Since the parent is earlier in the nodes list, we have already transferred it, and its ID in this->Nodes is therefore RemapIDs[Other.ParentID]
				check(NodeData.ParentID < NodeData.ID);
				ParentID = RemapIDs[NodeData.ParentID];
			}

			bool bNew;
			FEDLNodeID NodeID = FindOrAddNode(MoveTemp(NodeData), Other, ParentID, bNew);
			if (!bNew)
			{
				Nodes[NodeID].Merge(MoveTemp(NodeData));
			}
			RemapIDs.Add(NodeID);
		}

		for (const TPair<FEDLNodeID, FEDLNodeID>& Prereq : Other.NodePrereqs)
		{
			FEDLNodeID SourceID = RemapIDs[Prereq.Key];
			FEDLNodeID TargetID = RemapIDs[Prereq.Value];
			AddDependency(SourceID, TargetID);
		}

		Other.NodePrereqs.Empty();
		Other.Nodes.Empty();
	}
}

void FEDLCookChecker::Verify(bool bFullReferencesExpected)
{
	check(!GIsSavingPackage);

	FEDLCookChecker Accumulator(EInternalConstruct::Type);

	{
		FScopeLock CookCheckerInstanceLock(&CookCheckerInstanceCritical);
		for (FEDLCookChecker* Checker : CookCheckerInstances)
		{
			if (Checker->bIsActive)
			{
				Accumulator.bIsActive = true;
				Accumulator.Merge(MoveTemp(*Checker));
			}
			Checker->Reset();
		}			
	}

	if (Accumulator.bIsActive)
	{
		double StartTime = FPlatformTime::Seconds();
			
		if (bFullReferencesExpected)
		{
			// imports to things that are not exports...
			for (const FEDLNodeData& NodeData : Accumulator.Nodes)
			{
				if (NodeData.bIsExport)
				{
					continue;
				}

				// Any imports of this non-exported node are an error; log them all if they exist
				for (FName PackageName : NodeData.ImportingPackagesSorted)
				{
					UE_LOG(LogSavePackage, Warning, TEXT("%s imported %s, but it was never saved as an export."), *PackageName.ToString(), *NodeData.ToString(Accumulator));
				}
			}
		}

		// cycles in the dep graph
		TSet<FEDLNodeID> Visited;
		TSet<FEDLNodeID> Stack;
		bool bHadCycle = false;
		for (const FEDLNodeData& NodeData : Accumulator.Nodes)
		{
			if (!NodeData.bIsExport)
			{
				continue;
			}
			FEDLNodeID FailNode;
			if (Accumulator.CheckForCyclesInner(Visited, Stack, NodeData.ID, FailNode))
			{
				UE_LOG(LogSavePackage, Error, TEXT("----- %s contained a cycle (listed above)."), *Accumulator.Nodes[FailNode].ToString(Accumulator));
				bHadCycle = true;
			}
		}
		if (bHadCycle)
		{
			UE_LOG(LogSavePackage, Fatal, TEXT("EDL dep graph contained a cycle (see errors, above). This is fatal at runtime so it is fatal at cook time."));
		}
		UE_LOG(LogSavePackage, Display, TEXT("Took %fs to verify the EDL loading graph."), float(FPlatformTime::Seconds() - StartTime));
	}
}

FCriticalSection FEDLCookChecker::CookCheckerInstanceCritical;
TArray<FEDLCookChecker*> FEDLCookChecker::CookCheckerInstances;

void StartSavingEDLCookInfoForVerification()
{
	FEDLCookChecker::StartSavingEDLCookInfoForVerification();
}

void VerifyEDLCookInfo(bool bFullReferencesExpected)
{
	FEDLCookChecker::Verify(bFullReferencesExpected);
}

FScopedSavingFlag::FScopedSavingFlag(bool InSavingConcurrent)
	: bSavingConcurrent(InSavingConcurrent)
{
	check(!IsGarbageCollecting());

	// We need the same lock as GC so that no StaticFindObject can happen in parallel to saving a package
	if (IsInGameThread())
	{
		FGCCSyncObject::Get().GCLock();
	}
	else
	{
		FGCCSyncObject::Get().LockAsync();
	}

	// Do not change GIsSavingPackage while saving concurrently. It should have been set before and after all packages are saved
	if (!bSavingConcurrent)
	{
		GIsSavingPackage = true;
	}
}

FScopedSavingFlag::~FScopedSavingFlag()
{
	if (!bSavingConcurrent)
	{
		GIsSavingPackage = false;
	}
	if (IsInGameThread())
	{
		FGCCSyncObject::Get().GCUnlock();
	}
	else
	{
		FGCCSyncObject::Get().UnlockAsync();
	}
}

FSavePackageDiffSettings::FSavePackageDiffSettings(bool bDiffing)
	: MaxDiffsToLog(5)
	, bIgnoreHeaderDiffs(false)
	, bSaveForDiff(false)
{
	if (bDiffing)
	{
		GConfig->GetInt(TEXT("CookSettings"), TEXT("MaxDiffsToLog"), MaxDiffsToLog, GEditorIni);
		// Command line override for MaxDiffsToLog
		FParse::Value(FCommandLine::Get(), TEXT("MaxDiffstoLog="), MaxDiffsToLog);

		GConfig->GetBool(TEXT("CookSettings"), TEXT("IgnoreHeaderDiffs"), bIgnoreHeaderDiffs, GEditorIni);
		// Command line override for IgnoreHeaderDiffs
		if (bIgnoreHeaderDiffs)
		{
			bIgnoreHeaderDiffs = !FParse::Param(FCommandLine::Get(), TEXT("HeaderDiffs"));
		}
		else
		{
			bIgnoreHeaderDiffs = FParse::Param(FCommandLine::Get(), TEXT("IgnoreHeaderDiffs"));
		}
		bSaveForDiff = FParse::Param(FCommandLine::Get(), TEXT("SaveForDiff"));
	}
}

FCanSkipEditorReferencedPackagesWhenCooking::FCanSkipEditorReferencedPackagesWhenCooking()
	: bCanSkipEditorReferencedPackagesWhenCooking(true)
{
	GConfig->GetBool(TEXT("Core.System"), TEXT("CanSkipEditorReferencedPackagesWhenCooking"), bCanSkipEditorReferencedPackagesWhenCooking, GEngineIni);
}


namespace SavePackageUtilities
{
/**
 * Static: Saves thumbnail data for the specified package outer and linker
 *
 * @param	InOuter							the outer to use for the new package
 * @param	Linker							linker we're currently saving with
 * @param	Slot							structed archive slot we are saving too (temporary)
 */
void SaveThumbnails(UPackage* InOuter, FLinkerSave* Linker, FStructuredArchive::FSlot Slot)
{
	FStructuredArchive::FRecord Record = Slot.EnterRecord();

	Linker->Summary.ThumbnailTableOffset = 0;

#if WITH_EDITORONLY_DATA
	// Do we have any thumbnails to save?
	if( !(Linker->Summary.PackageFlags & PKG_FilterEditorOnly) && InOuter->HasThumbnailMap() )
	{
		const FThumbnailMap& PackageThumbnailMap = InOuter->GetThumbnailMap();


		// Figure out which objects have thumbnails.  Note that we only want to save thumbnails
		// for objects that are actually in the export map.  This is so that we avoid saving out
		// thumbnails that were cached for deleted objects and such.
		TArray< FObjectFullNameAndThumbnail > ObjectsWithThumbnails;
		for( int32 i=0; i<Linker->ExportMap.Num(); i++ )
		{
			FObjectExport& Export = Linker->ExportMap[i];
			if( Export.Object )
			{
				const FName ObjectFullName( *Export.Object->GetFullName() );
				const FObjectThumbnail* ObjectThumbnail = PackageThumbnailMap.Find( ObjectFullName );
		
				// if we didn't find the object via full name, try again with ??? as the class name, to support having
				// loaded old packages without going through the editor (ie cooking old packages)
				if (ObjectThumbnail == nullptr)
				{
					// can't overwrite ObjectFullName, so that we add it properly to the map
					FName OldPackageStyleObjectFullName = FName(*FString::Printf(TEXT("??? %s"), *Export.Object->GetPathName()));
					ObjectThumbnail = PackageThumbnailMap.Find(OldPackageStyleObjectFullName);
				}
				if( ObjectThumbnail != nullptr )
				{
					// IMPORTANT: We save all thumbnails here, even if they are a shared (empty) thumbnail!
					// Empty thumbnails let us know that an asset is in a package without having to
					// make a linker for it.
					ObjectsWithThumbnails.Add( FObjectFullNameAndThumbnail( ObjectFullName, ObjectThumbnail ) );
				}
			}
		}

		// preserve thumbnail rendered for the level
		const FObjectThumbnail* ObjectThumbnail = PackageThumbnailMap.Find(FName(*InOuter->GetFullName()));
		if (ObjectThumbnail != nullptr)
		{
			ObjectsWithThumbnails.Add( FObjectFullNameAndThumbnail(FName(*InOuter->GetFullName()), ObjectThumbnail ) );
		}
		
		// Do we have any thumbnails?  If so, we'll save them out along with a table of contents
		if( ObjectsWithThumbnails.Num() > 0 )
		{
			// Save out the image data for the thumbnails
			FStructuredArchive::FStream ThumbnailStream = Record.EnterStream(SA_FIELD_NAME(TEXT("Thumbnails")));

			for( int32 CurObjectIndex = 0; CurObjectIndex < ObjectsWithThumbnails.Num(); ++CurObjectIndex )
			{
				FObjectFullNameAndThumbnail& CurObjectThumb = ObjectsWithThumbnails[ CurObjectIndex ];

				// Store the file offset to this thumbnail
				CurObjectThumb.FileOffset = Linker->Tell();

				// Serialize the thumbnail!
				FObjectThumbnail* SerializableThumbnail = const_cast< FObjectThumbnail* >( CurObjectThumb.ObjectThumbnail );
				SerializableThumbnail->Serialize(ThumbnailStream.EnterElement());
			}


			// Store the thumbnail table of contents
			{
				Linker->Summary.ThumbnailTableOffset = Linker->Tell();

				// Save number of thumbnails
				int32 ThumbnailCount = ObjectsWithThumbnails.Num();
				FStructuredArchive::FArray IndexArray = Record.EnterField(SA_FIELD_NAME(TEXT("Index"))).EnterArray(ThumbnailCount);

				// Store a list of object names along with the offset in the file where the thumbnail is stored
				for( int32 CurObjectIndex = 0; CurObjectIndex < ObjectsWithThumbnails.Num(); ++CurObjectIndex )
				{
					const FObjectFullNameAndThumbnail& CurObjectThumb = ObjectsWithThumbnails[ CurObjectIndex ];

					// Object name
					const FString ObjectFullName = CurObjectThumb.ObjectFullName.ToString();

					// Break the full name into it's class and path name parts
					const int32 FirstSpaceIndex = ObjectFullName.Find( TEXT( " " ) );
					check( FirstSpaceIndex != INDEX_NONE && FirstSpaceIndex > 0 );
					FString ObjectClassName = ObjectFullName.Left( FirstSpaceIndex );
					const FString ObjectPath = ObjectFullName.Mid( FirstSpaceIndex + 1 );

					// Remove the package name from the object path since that will be implicit based
					// on the package file name
					FString ObjectPathWithoutPackageName = ObjectPath.Mid( ObjectPath.Find( TEXT( "." ) ) + 1 );

					// File offset for the thumbnail (already saved out.)
					int32 FileOffset = CurObjectThumb.FileOffset;

					IndexArray.EnterElement().EnterRecord()
						<< SA_VALUE(TEXT("ObjectClassName"), ObjectClassName)
						<< SA_VALUE(TEXT("ObjectPathWithoutPackageName"), ObjectPathWithoutPackageName)
						<< SA_VALUE(TEXT("FileOffset"), FileOffset);
				}
			}
		}
	}

	// if content browser isn't enabled, clear the thumbnail map so we're not using additional memory for nothing
	if ( !GIsEditor || IsRunningCommandlet() )
	{
		InOuter->ThumbnailMap.Reset();
	}
#endif
}

void SaveBulkData(FLinkerSave* Linker, const UPackage* InOuter, const TCHAR* Filename, const ITargetPlatform* TargetPlatform,
				  FSavePackageContext* SavePackageContext, const bool bTextFormat, const bool bDiffing, const bool bComputeHash, TAsyncWorkSequence<FMD5>& AsyncWriteAndHashSequence, int64& TotalPackageSizeUncompressed)
{
	// Now we write all the bulkdata that is supposed to be at the end of the package
	// and fix up the offset
	const int64 StartOfBulkDataArea = Linker->Tell();
	Linker->Summary.BulkDataStartOffset = StartOfBulkDataArea;

	check(!bTextFormat || Linker->BulkDataToAppend.Num() == 0);

	if (!bTextFormat && Linker->BulkDataToAppend.Num() > 0)
	{
		COOK_STAT(FScopedDurationTimer SaveTimer(FSavePackageStats::SerializeBulkDataTimeSec));

		FScopedSlowTask BulkDataFeedback(Linker->BulkDataToAppend.Num());

		class FLargeMemoryWriterWithRegions : public FLargeMemoryWriter
		{
		public:
			FLargeMemoryWriterWithRegions()
				: FLargeMemoryWriter(0, /* IsPersistent */ true)
			{}

			TArray<FFileRegion> FileRegions;
		};

		TUniquePtr<FLargeMemoryWriterWithRegions> BulkArchive;
		TUniquePtr<FLargeMemoryWriterWithRegions> OptionalBulkArchive;
		TUniquePtr<FLargeMemoryWriterWithRegions> MappedBulkArchive;

		uint32 ExtraBulkDataFlags = 0;

		static const struct FUseSeparateBulkDataFiles
		{
			bool bEnable = false;

			FUseSeparateBulkDataFiles()
			{
				GConfig->GetBool(TEXT("Core.System"), TEXT("UseSeperateBulkDataFiles"), /* out */ bEnable, GEngineIni);

				if (IsEventDrivenLoaderEnabledInCookedBuilds())
				{
					// Always split bulk data when splitting cooked files
					bEnable = true;
				}
			}
		} ShouldUseSeparateBulkDataFiles;

		const bool bShouldUseSeparateBulkFile = ShouldUseSeparateBulkDataFiles.bEnable && Linker->IsCooking();

		if (bShouldUseSeparateBulkFile)
		{
			ExtraBulkDataFlags = BULKDATA_PayloadInSeperateFile;

			BulkArchive.Reset(new FLargeMemoryWriterWithRegions);
			OptionalBulkArchive.Reset(new FLargeMemoryWriterWithRegions);
			MappedBulkArchive.Reset(new FLargeMemoryWriterWithRegions);
		}

		// If we are not allowing BulkData to go to the IoStore and we will be saving the BulkData to a separate file then 
		// we cannot manipulate the offset as we cannot 'fix' it at runtime with the AsyncLoader2
		// 
		// We should remove the manipulated offset entirely, at least for separate files but for now we need to leave it to
		// prevent larger patching sizes.
		if (SavePackageContext != nullptr && SavePackageContext->bForceLegacyOffsets == false && bShouldUseSeparateBulkFile)
		{
			ExtraBulkDataFlags |= BULKDATA_NoOffsetFixUp;
		}

		bool bAlignBulkData = false;
		bool bUseFileRegions = false;
		int64 BulkDataAlignment = 0;

		if (TargetPlatform)
		{
			bAlignBulkData = TargetPlatform->SupportsFeature(ETargetPlatformFeatures::MemoryMappedFiles);
			bUseFileRegions = TargetPlatform->SupportsFeature(ETargetPlatformFeatures::CookFileRegionMetadata);
			BulkDataAlignment = TargetPlatform->GetMemoryMappingAlignment();
		}

		uint16 BulkDataIndex = 1;
		for (FLinkerSave::FBulkDataStorageInfo& BulkDataStorageInfo : Linker->BulkDataToAppend)
		{
			BulkDataFeedback.EnterProgressFrame();

			// Set bulk data flags to what they were during initial serialization (they might have changed after that)
			const uint32 OldBulkDataFlags = BulkDataStorageInfo.BulkData->GetBulkDataFlags();
			uint32 ModifiedBulkDataFlags = BulkDataStorageInfo.BulkDataFlags | ExtraBulkDataFlags;
			const bool bBulkItemIsOptional = (ModifiedBulkDataFlags & BULKDATA_OptionalPayload) != 0;
			bool bBulkItemIsMapped = bAlignBulkData && ((ModifiedBulkDataFlags & BULKDATA_MemoryMappedPayload) != 0);

			if (bBulkItemIsMapped && bBulkItemIsOptional)
			{
				UE_LOG(LogSavePackage, Warning, TEXT("%s has bulk data that is both mapped and optional. This is not currently supported. Will not be mapped."), Filename);
				ModifiedBulkDataFlags &= ~BULKDATA_MemoryMappedPayload;
				bBulkItemIsMapped = false;
			}

			BulkDataStorageInfo.BulkData->ClearBulkDataFlags(0xFFFFFFFF);
			BulkDataStorageInfo.BulkData->SetBulkDataFlags(ModifiedBulkDataFlags);

			TArray<FFileRegion>* TargetRegions = &Linker->FileRegions;
			FArchive* TargetArchive = Linker;

			if (bShouldUseSeparateBulkFile)
			{
				if (bBulkItemIsOptional)
				{
					TargetArchive = OptionalBulkArchive.Get();
					TargetRegions = &OptionalBulkArchive->FileRegions;
				}
				else if (bBulkItemIsMapped)
				{
					TargetArchive = MappedBulkArchive.Get();
					TargetRegions = &MappedBulkArchive->FileRegions;
				}
				else
				{
					TargetArchive = BulkArchive.Get();
					TargetRegions = &BulkArchive->FileRegions;
				}
			}
			check(TargetArchive && TargetRegions);

			// Pad archive for proper alignment for memory mapping
			if (bBulkItemIsMapped && BulkDataAlignment > 0)
			{
				const int64 BulkStartOffset = TargetArchive->Tell();

				if (!IsAligned(BulkStartOffset, BulkDataAlignment))
				{
					const int64 AlignedOffset = Align(BulkStartOffset, BulkDataAlignment);

					int64 Padding = AlignedOffset - BulkStartOffset;
					check(Padding > 0);

					uint64 Zero64 = 0;
					while (Padding >= 8)
					{
						*TargetArchive << Zero64;
						Padding -= 8;
					}

					uint8 Zero8 = 0;
					while (Padding > 0)
					{
						*TargetArchive << Zero8;
						Padding--;
					}

					check(TargetArchive->Tell() == AlignedOffset);
				}
			}

			const int64 BulkStartOffset = TargetArchive->Tell();

			int64 StoredBulkStartOffset = (ModifiedBulkDataFlags & BULKDATA_NoOffsetFixUp) == 0 ? BulkStartOffset - StartOfBulkDataArea : BulkStartOffset;

			BulkDataStorageInfo.BulkData->SerializeBulkData(*TargetArchive, BulkDataStorageInfo.BulkData->Lock(LOCK_READ_ONLY));

			int64 BulkEndOffset = TargetArchive->Tell();
			const int64 LinkerEndOffset = Linker->Tell();

			int64 SizeOnDisk = BulkEndOffset - BulkStartOffset;

			Linker->Seek(BulkDataStorageInfo.BulkDataFlagsPos);
			*Linker << ModifiedBulkDataFlags;

			Linker->Seek(BulkDataStorageInfo.BulkDataOffsetInFilePos);
			*Linker << StoredBulkStartOffset;

			Linker->Seek(BulkDataStorageInfo.BulkDataSizeOnDiskPos);
			if (ModifiedBulkDataFlags & BULKDATA_Size64Bit)
			{
				*Linker << SizeOnDisk;
			}
			else
			{
				check(SizeOnDisk < (1LL << 31));
				int32 SizeOnDiskAsInt32 = SizeOnDisk;
				*Linker << SizeOnDiskAsInt32;
			}

			if (SavePackageContext != nullptr && SavePackageContext->BulkDataManifest != nullptr)
			{
				auto BulkDataTypeFromFlags = [](uint32 BulkDataFlags)
				{
					if (BulkDataFlags & BULKDATA_MemoryMappedPayload)
					{
						return FPackageStoreBulkDataManifest::EBulkdataType::MemoryMapped;
					}

					if (BulkDataFlags & BULKDATA_OptionalPayload)
					{
						return FPackageStoreBulkDataManifest::EBulkdataType::Optional;
					}

					return FPackageStoreBulkDataManifest::EBulkdataType::Normal;
				};

				const FPackageStoreBulkDataManifest::EBulkdataType Type = BulkDataTypeFromFlags(BulkDataStorageInfo.BulkDataFlags);

				SavePackageContext->BulkDataManifest->AddFileAccess(Filename, Type, StoredBulkStartOffset, BulkStartOffset, SizeOnDisk);
			}

			if (bUseFileRegions && BulkDataStorageInfo.BulkDataFileRegionType != EFileRegionType::None && SizeOnDisk > 0)
			{
				TargetRegions->Add(FFileRegion(BulkStartOffset, SizeOnDisk, BulkDataStorageInfo.BulkDataFileRegionType));
			}

			Linker->Seek(LinkerEndOffset);

			// Restore BulkData flags to before serialization started
			BulkDataStorageInfo.BulkData->ClearBulkDataFlags(0xFFFFFFFF);
			BulkDataStorageInfo.BulkData->SetBulkDataFlags(OldBulkDataFlags);
			BulkDataStorageInfo.BulkData->Unlock();
		}

		if (BulkArchive)
		{
			check(OptionalBulkArchive);
			check(MappedBulkArchive);

			const bool bWriteBulkToDisk = !bDiffing;

			if (SavePackageContext != nullptr && SavePackageContext->PackageStoreWriter != nullptr && bWriteBulkToDisk)
			{
				auto AddSizeAndConvertToIoBuffer = [&TotalPackageSizeUncompressed](FLargeMemoryWriter* Writer)
				{
					const int64 TotalSize = Writer->TotalSize();
					TotalPackageSizeUncompressed += TotalSize;
					return FIoBuffer(FIoBuffer::AssumeOwnership, Writer->ReleaseOwnership(), TotalSize);
				};

				FPackageStoreWriter::FBulkDataInfo BulkInfo;
				BulkInfo.PackageName = InOuter->GetFName();
				BulkInfo.LooseFilePath = Filename;
				BulkInfo.BulkdataType = FPackageStoreWriter::FBulkDataInfo::Standard;

				SavePackageContext->PackageStoreWriter->WriteBulkdata(BulkInfo, AddSizeAndConvertToIoBuffer(BulkArchive.Get()), BulkArchive->FileRegions);

				BulkInfo.BulkdataType = FPackageStoreWriter::FBulkDataInfo::Optional;
				SavePackageContext->PackageStoreWriter->WriteBulkdata(BulkInfo, AddSizeAndConvertToIoBuffer(OptionalBulkArchive.Get()), OptionalBulkArchive->FileRegions);

				BulkInfo.BulkdataType = FPackageStoreWriter::FBulkDataInfo::Mmap;
				SavePackageContext->PackageStoreWriter->WriteBulkdata(BulkInfo, AddSizeAndConvertToIoBuffer(MappedBulkArchive.Get()), MappedBulkArchive->FileRegions);
			}
			else
			{
				auto WriteBulkData = [&](FLargeMemoryWriterWithRegions* Archive, const TCHAR* BulkFileExtension)
				{
					if (const int64 DataSize = Archive->TotalSize())
					{
						TotalPackageSizeUncompressed += DataSize;

						if (bComputeHash || bWriteBulkToDisk)
						{
							FLargeMemoryPtr DataPtr(Archive->ReleaseOwnership());

							const FString ArchiveFilename = FPaths::ChangeExtension(Filename, BulkFileExtension);

							EAsyncWriteOptions WriteOptions(EAsyncWriteOptions::None);
							if (bComputeHash)
							{
								WriteOptions |= EAsyncWriteOptions::ComputeHash;
							}
							if (bWriteBulkToDisk)
							{
								WriteOptions |= EAsyncWriteOptions::WriteFileToDisk;
							}
							SavePackageUtilities::AsyncWriteFile(AsyncWriteAndHashSequence, MoveTemp(DataPtr), DataSize, *ArchiveFilename, WriteOptions, Archive->FileRegions);
						}
					}
				};

				WriteBulkData(BulkArchive.Get(), TEXT(".ubulk"));			// Regular separate bulk data file
				WriteBulkData(OptionalBulkArchive.Get(), TEXT(".uptnl"));	// Optional bulk data
				WriteBulkData(MappedBulkArchive.Get(), TEXT(".m.ubulk"));	// Memory-mapped bulk data
			}
		}
	}

	Linker->BulkDataToAppend.Empty();
}

void SaveWorldLevelInfo(UPackage* InOuter, FLinkerSave* Linker, FStructuredArchive::FRecord Record)
{
	Linker->Summary.WorldTileInfoDataOffset = 0;
	
	if(InOuter->WorldTileInfo.IsValid())
	{
		Linker->Summary.WorldTileInfoDataOffset = Linker->Tell();
		Record << SA_VALUE(TEXT("WorldLevelInfo"), *(InOuter->WorldTileInfo));
	}
}

} // end namespace SavePackageUtilities

void UPackage::WaitForAsyncFileWrites()
{
	while (OutstandingAsyncWrites.GetValue())
	{
		FPlatformProcess::Sleep(0.0f);
	}
}

bool UPackage::IsEmptyPackage(UPackage* Package, const UObject* LastReferencer)
{
	// Don't count null or volatile packages as empty, just let them be NULL or get GCed
	if ( Package != nullptr )
	{
		// Make sure the package is fully loaded before determining if it is empty
		if( !Package->IsFullyLoaded() )
		{
			Package->FullyLoad();
		}

		bool bIsEmpty = true;
		ForEachObjectWithPackage(Package, [LastReferencer, &bIsEmpty](UObject* InObject)
		{
			// if the package contains at least one object that has asset registry data and isn't the `LastReferencer` consider it not empty
			if (InObject->IsAsset() && InObject != LastReferencer)
			{
				bIsEmpty = false;
				// we can break out of the iteration as soon as we find one valid object
				return false;
			}
			return true;
		// Don't consider transient, class default or pending kill objects
		}, false, RF_Transient | RF_ClassDefaultObject, EInternalObjectFlags::PendingKill);
		return bIsEmpty;
	}

	// Invalid package
	return false;
}


namespace UE
{
namespace AssetRegistry
{
	// See the corresponding ReadPackageDataMain and ReadPackageDataDependencies defined in PackageReader.cpp in AssetRegistry module
	void WritePackageData(FStructuredArchiveRecord& ParentRecord, bool bIsCooking, const UPackage* Package, FLinkerSave* Linker, const TSet<UObject*>& ImportsUsedInGame, const TSet<FName>& SoftPackagesUsedInGame)
	{
		// To avoid large patch sizes, we have frozen cooked package format at the format before VER_UE4_ASSETREGISTRY_DEPENDENCYFLAGS
		bool bPreDependencyFormat = bIsCooking;

		// WritePackageData is currently only called if not bTextFormat; we rely on that to save offsets
		FArchive& BinaryArchive = ParentRecord.GetUnderlyingArchive();
		check(!BinaryArchive.IsTextFormat());

		// Store the asset registry offset in the file and enter a record for the asset registry data
		Linker->Summary.AssetRegistryDataOffset = BinaryArchive.Tell();
		FStructuredArchiveRecord AssetRegistryRecord = ParentRecord.EnterField(SA_FIELD_NAME(TEXT("AssetRegistry"))).EnterRecord();

		// Offset to Dependencies
		int64 OffsetToAssetRegistryDependencyDataOffset = INDEX_NONE;
		if (!bPreDependencyFormat)
		{
			// Write placeholder data for the offset to the separately-serialized AssetRegistryDependencyData
			OffsetToAssetRegistryDependencyDataOffset = BinaryArchive.Tell();
			int64 AssetRegistryDependencyDataOffset = 0;
			AssetRegistryRecord << SA_VALUE(TEXT("AssetRegistryDependencyDataOffset"), AssetRegistryDependencyDataOffset);
			check(BinaryArchive.Tell() == OffsetToAssetRegistryDependencyDataOffset + sizeof(AssetRegistryDependencyDataOffset));
		}

		// Collect the tag map
		TArray<UObject*> AssetObjects;
		if (!(Linker->Summary.PackageFlags & PKG_FilterEditorOnly))
		{
			// Find any exports which are not in the tag map
			for (int32 i = 0; i < Linker->ExportMap.Num(); i++)
			{
				FObjectExport& Export = Linker->ExportMap[i];
				if (Export.Object && Export.Object->IsAsset())
				{
					AssetObjects.Add(Export.Object);
				}
			}
		}
		int32 ObjectCount = AssetObjects.Num();
		FStructuredArchive::FArray AssetArray = AssetRegistryRecord.EnterArray(SA_FIELD_NAME(TEXT("TagMap")), ObjectCount);
		for (int32 ObjectIdx = 0; ObjectIdx < AssetObjects.Num(); ++ObjectIdx)
		{
			const UObject* Object = AssetObjects[ObjectIdx];

			// Exclude the package name in the object path, we just need to know the path relative to the package we are saving
			FString ObjectPath = Object->GetPathName(Package);
			FString ObjectClassName = Object->GetClass()->GetName();

			TArray<UObject::FAssetRegistryTag> SourceTags;
			Object->GetAssetRegistryTags(SourceTags);

			TArray<UObject::FAssetRegistryTag> Tags;
			for (UObject::FAssetRegistryTag& SourceTag : SourceTags)
			{
				UObject::FAssetRegistryTag* Existing = Tags.FindByPredicate([SourceTag](const UObject::FAssetRegistryTag& InTag) { return InTag.Name == SourceTag.Name; });
				if (Existing)
				{
					Existing->Value = SourceTag.Value;
				}
				else
				{
					Tags.Add(SourceTag);
				}
			}

			int32 TagCount = Tags.Num();

			FStructuredArchive::FRecord AssetRecord = AssetArray.EnterElement().EnterRecord();
			AssetRecord << SA_VALUE(TEXT("Path"), ObjectPath) << SA_VALUE(TEXT("Class"), ObjectClassName);

			FStructuredArchive::FMap TagMap = AssetRecord.EnterField(SA_FIELD_NAME(TEXT("Tags"))).EnterMap(TagCount);

			for (TArray<UObject::FAssetRegistryTag>::TConstIterator TagIter(Tags); TagIter; ++TagIter)
			{
				FString Key = TagIter->Name.ToString();
				FString Value = TagIter->Value;

				TagMap.EnterElement(Key) << Value;
			}
		}
		if (bPreDependencyFormat)
		{
			// The legacy format did not write the other sections, or the offsets to those other sections
			return;
		}

		// Overwrite the placeholder offset for the AssetRegistryDependencyData and enter a record for the asset registry dependency data
		{
			int64 AssetRegistryDependencyDataOffset = Linker->Tell();
			BinaryArchive.Seek(OffsetToAssetRegistryDependencyDataOffset);
			BinaryArchive << AssetRegistryDependencyDataOffset;
			BinaryArchive.Seek(AssetRegistryDependencyDataOffset);
		}
		FStructuredArchiveRecord DependencyDataRecord = ParentRecord.EnterField(SA_FIELD_NAME(TEXT("AssetRegistryDependencyData"))).EnterRecord();

		// Convert the IsUsedInGame sets into a bitarray with a value per import/softpackagereference
		TBitArray<> ImportUsedInGameBits;
		TBitArray<> SoftPackageUsedInGameBits;
		ImportUsedInGameBits.Reserve(Linker->ImportMap.Num());
		for (int32 ImportIndex = 0; ImportIndex < Linker->ImportMap.Num(); ++ImportIndex)
		{
			ImportUsedInGameBits.Add(ImportsUsedInGame.Contains(Linker->ImportMap[ImportIndex].XObject));
		}
		SoftPackageUsedInGameBits.Reserve(Linker->SoftPackageReferenceList.Num());
		for (int32 SoftPackageIndex = 0; SoftPackageIndex < Linker->SoftPackageReferenceList.Num(); ++SoftPackageIndex)
		{
			SoftPackageUsedInGameBits.Add(SoftPackagesUsedInGame.Contains(Linker->SoftPackageReferenceList[SoftPackageIndex]));
		}

		// Serialize the Dependency section
		DependencyDataRecord << SA_VALUE(TEXT("ImportUsedInGame"), ImportUsedInGameBits);
		DependencyDataRecord << SA_VALUE(TEXT("SoftPackageUsedInGame"), SoftPackageUsedInGameBits);
	}
}
}