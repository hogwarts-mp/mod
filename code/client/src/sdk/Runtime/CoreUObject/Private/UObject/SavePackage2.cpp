// Copyright Epic Games, Inc. All Rights Reserved.


#include "UObject/SavePackage.h"

#if UE_WITH_SAVEPACKAGE
#include "Async/ParallelFor.h"
#include "Blueprint/BlueprintSupport.h"
#include "HAL/FileManager.h"
#include "Internationalization/TextPackageNamespaceUtil.h"
#include "IO/IoDispatcher.h"
#include "Misc/AssetRegistryInterface.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FeedbackContext.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Misc/ScopedSlowTask.h"
#include "Misc/SecureHash.h"
#include "ProfilingDebugging/CookStats.h"
#include "Serialization/ArchiveUObjectFromStructuredArchive.h"
#include "Serialization/ArchiveStackTrace.h"
#include "Serialization/Formatters/JsonArchiveOutputFormatter.h"
#include "Serialization/LargeMemoryWriter.h"
#include "Serialization/PropertyLocalizationDataGathering.h"
#include "Serialization/UnversionedPropertySerialization.h"
#include "UObject/AsyncWorkSequence.h"
#include "UObject/DebugSerializationFlags.h"
#include "UObject/EditorObjectVersion.h"
#include "UObject/LinkerSave.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ObjectRedirector.h"
#include "UObject/Package.h"
#include "UObject/SavePackage/PackageHarvester.h"
#include "UObject/SavePackage/SaveContext.h"
#include "UObject/SavePackage/SavePackageUtilities.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"

#if ENABLE_COOK_STATS
#include "ProfilingDebugging/ScopedTimers.h"
#endif

// defined in UObjectGlobals.cpp
COREUOBJECT_API extern bool GOutputCookingWarnings;

// bring the UObectGlobal declaration visible to non editor
bool IsEditorOnlyObject(const UObject* InObject, bool bCheckRecursive, bool bCheckMarks);

ESavePackageResult ReturnSuccessOrCancel()
{
	return !GWarn->ReceivedUserCancel() ? ESavePackageResult::Success : ESavePackageResult::Canceled;
}

ESavePackageResult ValidateBlueprintNativeCodeGenReplacement(FSaveContext& SaveContext)
{
#if WITH_EDITOR
	if (const IBlueprintNativeCodeGenCore* Coordinator = IBlueprintNativeCodeGenCore::Get())
	{
		EReplacementResult ReplacementResult = Coordinator->IsTargetedForReplacement(SaveContext.GetPackage(), Coordinator->GetNativizationOptionsForPlatform(SaveContext.GetTargetPlatform()));
		if (ReplacementResult == EReplacementResult::ReplaceCompletely)
		{
			UE_LOG(LogSavePackage, Verbose, TEXT("Package %s contains assets that are being converted to native code."), *SaveContext.GetPackage()->GetName());
			return ESavePackageResult::ReplaceCompletely;
		}
		else if (ReplacementResult == EReplacementResult::GenerateStub)
		{
			SaveContext.RequestStubFile();
		}
	}
#endif
	return ReturnSuccessOrCancel();
}

ESavePackageResult ValidatePackage(FSaveContext& SaveContext)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_ValidatePackage);

	// Platform can't save the package
	if (!FPlatformProperties::HasEditorOnlyData())
	{
		return ESavePackageResult::Error;
	}

	// Check recursive save package call
	if (GIsSavingPackage && !SaveContext.IsConcurrent())
	{
		ensureMsgf(false, TEXT("Recursive SavePackage() is not supported"));
		return ESavePackageResult::Error;
	}

	FString FilenameStr(SaveContext.GetFilename());

	// If an asset is provided, validate it is in the package
	UObject* Asset = SaveContext.GetAsset();
	if (Asset && !Asset->IsInPackage(SaveContext.GetPackage()))
	{
		if (SaveContext.IsGenerateSaveError() && SaveContext.GetError())
		{
			FFormatNamedArguments Arguments;
			Arguments.Add(TEXT("Name"), FText::FromString(FilenameStr));
			FText ErrorText = FText::Format(NSLOCTEXT("SavePackage2", "AssetSaveNotInPackage", "The Asset '{Name}' being saved is not in the provided is not in the provided package."), Arguments);
			SaveContext.GetError()->Logf(ELogVerbosity::Warning, TEXT("%s"), *ErrorText.ToString());
		}
		return ESavePackageResult::Error;
	}

	// Make sure package is allowed to be saved.
	if (!SaveContext.IsCooking() && FCoreUObjectDelegates::IsPackageOKToSaveDelegate.IsBound())
	{
		bool bIsOKToSave = FCoreUObjectDelegates::IsPackageOKToSaveDelegate.Execute(SaveContext.GetPackage(), SaveContext.GetFilename(), SaveContext.GetError());
		if (!bIsOKToSave)
		{
			if (SaveContext.IsGenerateSaveError())
			{
				FFormatNamedArguments Arguments;
				Arguments.Add(TEXT("Name"), FText::FromString(FilenameStr));
				FText FormatText = SaveContext.GetPackage()->ContainsMap() 
					? NSLOCTEXT("SavePackage2", "MapSaveNotAllowed", "Map '{Name}' is not allowed to save (see log for reason)") 
					: NSLOCTEXT("SavePackage2", "AssetSaveNotAllowed", "Asset '{Name}' is not allowed to save (see log for reason");
				FText ErrorText = FText::Format(FormatText, Arguments);
				SaveContext.GetError()->Logf(ELogVerbosity::Warning, TEXT("%s"), *ErrorText.ToString());
			}
			return ESavePackageResult::Error;
		}
	}

	// Check if the package is fully loaded
	if (!SaveContext.GetPackage()->IsFullyLoaded())
	{
		if (SaveContext.IsGenerateSaveError())
		{
			// We cannot save packages that aren't fully loaded as it would clobber existing not loaded content.
			FFormatNamedArguments Arguments;
			Arguments.Add(TEXT("Name"), FText::FromString(FilenameStr));
			FText FormatText = SaveContext.GetPackage()->ContainsMap()
				? NSLOCTEXT("SavePackage2", "CannotSaveMapPartiallyLoaded", "Map '{Name}' cannot be saved as it has only been partially loaded")
				: NSLOCTEXT("SavePackage2", "CannotSaveAssetPartiallyLoaded", "Asset '{Name}' cannot be saved as it has only been partially loaded");
			FText ErrorText = FText::Format(FormatText, Arguments);
			SaveContext.GetError()->Logf(ELogVerbosity::Warning, TEXT("%s"), *ErrorText.ToString());
		}
		return ESavePackageResult::Error;
	}

	/// Cooking checks
	if (SaveContext.IsCooking())
	{
#if WITH_EDITORONLY_DATA
		// if we strip editor only data, validate the package isn't referenced only by editor data
		if (SaveContext.IsStripEditorOnly())
		{
			// Don't save packages marked as editor-only.
			if (SaveContext.CanSkipEditorReferencedPackagesWhenCooking() && SaveContext.GetPackage()->IsLoadedByEditorPropertiesOnly())
			{
				UE_CLOG(SaveContext.IsGenerateSaveError(), LogSavePackage, Display, TEXT("Package loaded by editor-only properties: %s. Package will not be saved."), *SaveContext.GetPackage()->GetName());
				return ESavePackageResult::ReferencedOnlyByEditorOnlyData;
			}
			else if (SaveContext.GetPackage()->HasAnyPackageFlags(PKG_EditorOnly))
			{
				UE_CLOG(SaveContext.IsGenerateSaveError(), LogSavePackage, Display, TEXT("Package marked as editor-only: %s. Package will not be saved."), *SaveContext.GetPackage()->GetName());
				return ESavePackageResult::ReferencedOnlyByEditorOnlyData;
			}			
		}
#endif
	}

	// Warn about long package names, which may be bad for consoles with limited filename lengths.
	if (SaveContext.IsWarningLongFilename())
	{
		int32 MaxFilenameLength = FPlatformMisc::GetMaxPathLength();

		// If the name is of the form "_LOC_xxx.ext", remove the loc data before the length check
		FString BaseFilename = FPaths::GetBaseFilename(FilenameStr);
		FString CleanBaseFilename = BaseFilename;
		if (CleanBaseFilename.Find("_LOC_") == BaseFilename.Len() - 8)
		{
			CleanBaseFilename = BaseFilename.LeftChop(8);
		}
		if (CleanBaseFilename.Len() > MaxFilenameLength)
		{
			if (SaveContext.IsGenerateSaveError())
			{
				FFormatNamedArguments Arguments;
				Arguments.Add(TEXT("FileName"), FText::FromString(BaseFilename));
				Arguments.Add(TEXT("MaxLength"), FText::AsNumber(MaxFilenameLength));
				SaveContext.GetError()->Logf(ELogVerbosity::Warning, TEXT("%s"), *FText::Format(NSLOCTEXT("Core", "Error_FilenameIsTooLongForCooking", "Filename '{FileName}' is too long; this may interfere with cooking for consoles. Unreal filenames should be no longer than {MaxLength} characters."), Arguments).ToString());
			}
			else
			{
				UE_LOG(LogSavePackage, Warning, TEXT("%s"), *FString::Printf(TEXT("Filename is too long (%d characters); this may interfere with cooking for consoles. Unreal filenames should be no longer than %s characters. Filename value: %s"), BaseFilename.Len(), MaxFilenameLength, *BaseFilename));
			}
		}
	}
	return ReturnSuccessOrCancel();
}

FORCEINLINE void EnsurePackageLocalization(UPackage* InPackage)
{
#if USE_STABLE_LOCALIZATION_KEYS
	if (GIsEditor)
	{
		// We need to ensure that we have a package localization namespace as the package loading will need it
		// We need to do this before entering the GIsSavingPackage block as it may change the package meta-data
		TextNamespaceUtil::EnsurePackageNamespace(InPackage);
	}
#endif // USE_STABLE_LOCALIZATION_KEYS
}

ESavePackageResult RoutePresave(FSaveContext& SaveContext)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_RoutePresave);

	// Just route presave on all objects in the package while skipping unsaveable objects. 
	// This should be more efficient then trying to restrict to just the actual export, 
	// objects likely to not be export will probably not care about PreSave and should be mainly noop
	TArray<UObject*> ObjectsInPackage;
	GetObjectsWithPackage(SaveContext.GetPackage(), ObjectsInPackage);
	for (UObject* Object : ObjectsInPackage)
	{
		if (!SaveContext.IsUnsaveable(Object))
		{
			if (SaveContext.IsCooking() && Object->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
			{
				FArchiveObjectCrc32NonEditorProperties CrcArchive;
				int32 Before = CrcArchive.Crc32(Object);
				Object->PreSave(SaveContext.GetTargetPlatform());
				int32 After = CrcArchive.Crc32(Object);

				if (Before != After)
				{
					UE_ASSET_LOG(
						LogSavePackage,
						Warning,
						Object,
						TEXT("Non-deterministic cook warning - PreSave() has modified %s '%s' - a resave may be required"),
						Object->HasAnyFlags(RF_ClassDefaultObject) ? TEXT("CDO") : TEXT("archetype"),
						*Object->GetName()
					);
				}
			}
			else
			{
				Object->PreSave(SaveContext.GetTargetPlatform());
			}
		}
	}

	return ReturnSuccessOrCancel();
}

ESavePackageResult HarvestPackage(FSaveContext& SaveContext)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_Save_HarvestPackage);

	FPackageHarvester Harvester(SaveContext);
	EObjectFlags TopLevelFlags = SaveContext.GetTopLevelFlags();
	UObject* Asset = SaveContext.GetAsset();

	// if no top level flags are passed, process just the provided package asset
	if (TopLevelFlags == RF_NoFlags)
	{
		Harvester.TryHarvestExport(Asset);
		while (UObject* Export = Harvester.PopExportToProcess())
		{
			Harvester.ProcessExport(Export);
		}
	}
	// Otherwise process all objects which have the relevant flags
	else
	{
		// Validate that if an asset is provided it has the appropriate top level flags
		ensure(!Asset || Asset->HasAnyFlags(TopLevelFlags));

		ForEachObjectWithPackage(SaveContext.GetPackage(), [&Harvester, TopLevelFlags](UObject* InObject)
			{
				if (InObject->HasAnyFlags(TopLevelFlags))
				{
					Harvester.TryHarvestExport(InObject);
				}
				return true;
			}, true/*bIncludeNestedObjects */, RF_Transient);
		while (UObject* Export = Harvester.PopExportToProcess())
		{
			Harvester.ProcessExport(Export);
		}
	}

	// Harvest Prestream package class name if needed
	if (SaveContext.GetPrestreamPackages().Num() > 0)
	{
		Harvester.HarvestName(SavePackageUtilities::NAME_PrestreamPackage);
		
		//@todo FH: is this really needed?
		//TSet<UPackage*> KeptPrestreamPackages;
		//for (UPackage* Pkg : PrestreamPackages)
		//{
		//	if (!Pkg->HasAnyMarks(OBJECTMARK_TagImp))
		//	{
		//		Pkg->Mark(OBJECTMARK_TagImp);
		//		KeptPrestreamPackages.Add(Pkg);
		//	}
		//}
		//Exchange(PrestreamPackages, KeptPrestreamPackages);
	}

	// if we have a WorldTileInfo, we need to harvest its dependencies as well, i.e. Custom Version
	if (SaveContext.GetPackage()->WorldTileInfo.IsValid())
	{
		Harvester << *(SaveContext.GetPackage()->WorldTileInfo);
	}

	// The Editor version is used as part of the check to see if a package is too old to use the gather cache, so we always have to add it if we have gathered loc for this asset
	// We need to set the editor custom version before we copy the version container to the summary, otherwise we may end up with corrupt assets
	// because we later do it on the Linker when actually gathering loc data
	if (!SaveContext.IsFilterEditorOnly())
	{
		Harvester.UsingCustomVersion(FEditorObjectVersion::GUID);
	}
	SaveContext.SetCustomVersions(Harvester.GetCustomVersions());

	return ReturnSuccessOrCancel();
}

static FNameEntryId NAME_UniqueObjectNameForCookingComparisonIndex = FName("UniqueObjectNameForCooking").GetComparisonIndex();

ESavePackageResult ValidateExports(FSaveContext& SaveContext)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_Save_ValidateExports);

	// Check if we gathered any exports
	if (SaveContext.GetExports().Num() == 0)
	{
		UE_CLOG(SaveContext.IsGenerateSaveError(), LogSavePackage, Verbose, TEXT("No exports found (or all exports are editor-only) for %s. Package will not be saved."), SaveContext.GetFilename());
		return SaveContext.IsCooking() ? ESavePackageResult::ContainsEditorOnlyData : ESavePackageResult::Error;
	}

#if WITH_EDITOR
	if (GOutputCookingWarnings)
	{
		// check the name list for UniqueObjectNameForCooking cooking
		if (SaveContext.NameExists(NAME_UniqueObjectNameForCookingComparisonIndex))
		{
			for (const FTaggedExport& Export : SaveContext.GetExports())
			{
				FName NameInUse = Export.Obj->GetFName();
				if (NameInUse.GetComparisonIndex() == NAME_UniqueObjectNameForCookingComparisonIndex)
				{
					UObject* Outer = Export.Obj->GetOuter();
					UE_LOG(LogSavePackage, Warning, TEXT("Saving object into cooked package %s which was created at cook time, Object Name %s, Full Path %s, Class %s, Outer %s, Outer class %s"), SaveContext.GetFilename(), *NameInUse.ToString(), *Export.Obj->GetFullName(), *Export.Obj->GetClass()->GetName(), Outer ? *Outer->GetName() : TEXT("None"), Outer ? *Outer->GetClass()->GetName() : TEXT("None"));
				}
			}
		}
	}
#endif

	// If this is a map package, make sure there is a world or level in the export map.
	if (SaveContext.GetPackage()->ContainsMap())
	{
		bool bContainsMap = false;
		for (const FTaggedExport& Export : SaveContext.GetExports())
		{
			UObject* Object = Export.Obj;
			// Consider redirectors to world/levels as map packages too.
			if (UObjectRedirector* Redirector = Cast<UObjectRedirector>(Object))
			{
				Object = Redirector->DestinationObject;
			}
			if (Object)
			{
				FName ClassName = Object->GetClass()->GetFName();
				bContainsMap |= (ClassName == SavePackageUtilities::NAME_World || ClassName == SavePackageUtilities::NAME_Level);
			}
		}
		if (!bContainsMap)
		{
			ensureMsgf(false, TEXT("Attempting to save a map package '%s' that does not contain a map object."), *SaveContext.GetPackage()->GetName());
			UE_LOG(LogSavePackage, Error, TEXT("Attempting to save a map package '%s' that does not contain a map object."), *SaveContext.GetPackage()->GetName());

			if (SaveContext.IsGenerateSaveError())
			{
				SaveContext.GetError()->Logf(ELogVerbosity::Warning, TEXT("%s"), *FText::Format(NSLOCTEXT("Core", "SavePackageNoMap", "Attempting to save a map asset '{0}' that does not contain a map object"), FText::FromString(SaveContext.GetFilename())).ToString());
			}
			return ESavePackageResult::Error;
		}
	}

	// Cooking checks
	if (SaveContext.IsCooking())
	{
		// Add the exports for the cook checker
		// This needs to be done before validating NativeCodeGenReplacement which can exit early and will exist anyway but in compiled form
		if (FEDLCookChecker* EDLCookChecker = SaveContext.GetEDLCookChecker())
		{
			// the package isn't actually in the export map, but that is ok, we add it as export anyway for error checking
			EDLCookChecker->AddExport(SaveContext.GetPackage());
			for (const FTaggedExport& Export : SaveContext.GetExports())
			{
				EDLCookChecker->AddExport(Export.Obj);
			}
		}

		return ValidateBlueprintNativeCodeGenReplacement(SaveContext);
	}
	return ReturnSuccessOrCancel();
}

ESavePackageResult ValidateIllegalReferences(FSaveContext& SaveContext, TArray<UObject*>& PrivateObjects, TArray<UObject*>& ObjectsInOtherMaps)
{
	FFormatNamedArguments Args;

	// Illegal objects in other map warning
	if (ObjectsInOtherMaps.Num() > 0)
	{
		UObject* MostLikelyCulprit = nullptr;
		const FProperty* PropertyRef = nullptr;

		// construct a string containing up to the first 5 objects problem objects
		FString ObjectNames;
		int32 MaxNamesToDisplay = 5;
		bool DisplayIsLimited = true;

		if (ObjectsInOtherMaps.Num() < MaxNamesToDisplay)
		{
			MaxNamesToDisplay = ObjectsInOtherMaps.Num();
			DisplayIsLimited = false;
		}

		for (int32 Idx = 0; Idx < MaxNamesToDisplay; Idx++)
		{
			ObjectNames += ObjectsInOtherMaps[Idx]->GetName() + TEXT("\n");
		}

		// if there are more than 5 items we indicated this by adding "..." at the end of the list
		if (DisplayIsLimited)
		{
			ObjectNames += TEXT("...\n");
		}

		Args.Empty();
		Args.Add(TEXT("FileName"), FText::FromString(SaveContext.GetFilename()));
		Args.Add(TEXT("ObjectNames"), FText::FromString(ObjectNames));
		const FText Message = FText::Format(NSLOCTEXT("Core", "LinkedToObjectsInOtherMap_FindCulpritQ", "Can't save {FileName}: Graph is linked to object(s) in external map.\nExternal Object(s):\n{ObjectNames}  \nTry to find the chain of references to that object (may take some time)?"), Args);

		FString CulpritString = TEXT("Unknown");
		bool bFindCulprit = IsRunningCommandlet() || (FMessageDialog::Open(EAppMsgType::YesNo, Message) == EAppReturnType::Yes);
		if (bFindCulprit)
		{
			SavePackageUtilities::FindMostLikelyCulprit(ObjectsInOtherMaps, MostLikelyCulprit, PropertyRef);
			if (MostLikelyCulprit != nullptr && PropertyRef != nullptr)
			{
				CulpritString = FString::Printf(TEXT("%s (%s)"), *MostLikelyCulprit->GetFullName(), *PropertyRef->GetName());
			}
			else if (MostLikelyCulprit != nullptr)
			{
				CulpritString = FString::Printf(TEXT("%s (Unknown property)"), *MostLikelyCulprit->GetFullName());
			}
		}

		FString ErrorMessage = FString::Printf(TEXT("Can't save %s: Graph is linked to object %s in external map"), SaveContext.GetFilename(), *CulpritString);
		if (SaveContext.IsGenerateSaveError())
		{
			SaveContext.GetError()->Logf(ELogVerbosity::Warning, TEXT("%s"), *ErrorMessage);
		}
		else
		{
			UE_LOG(LogSavePackage, Error, TEXT("%s"), *ErrorMessage);
		}
		return ESavePackageResult::Error;
	}

	if (PrivateObjects.Num() > 0)
	{
		UObject* MostLikelyCulprit = nullptr;
		const FProperty* PropertyRef = nullptr;

		// construct a string containing up to the first 5 objects problem objects
		FString ObjectNames;
		int32 MaxNamesToDisplay = 5;
		bool DisplayIsLimited = true;

		if (PrivateObjects.Num() < MaxNamesToDisplay)
		{
			MaxNamesToDisplay = PrivateObjects.Num();
			DisplayIsLimited = false;
		}

		for (int32 Idx = 0; Idx < MaxNamesToDisplay; Idx++)
		{
			ObjectNames += PrivateObjects[Idx]->GetName() + TEXT("\n");
		}

		// if there are more than 5 items we indicated this by adding "..." at the end of the list
		if (DisplayIsLimited)
		{
			ObjectNames += TEXT("...\n");
		}

		Args.Empty();
		Args.Add(TEXT("FileName"), FText::FromString(SaveContext.GetFilename()));
		Args.Add(TEXT("ObjectNames"), FText::FromString(ObjectNames));
		const FText Message = FText::Format(NSLOCTEXT("Core", "LinkedToPrivateObjectsInOtherPackage_FindCulpritQ", "Can't save {FileName}: Graph is linked to private object(s) in an external package.\nExternal Object(s):\n{ObjectNames}  \nTry to find the chain of references to that object (may take some time)?"), Args);

		FString CulpritString = TEXT("Unknown");
		if (FMessageDialog::Open(EAppMsgType::YesNo, Message) == EAppReturnType::Yes)
		{
			SavePackageUtilities::FindMostLikelyCulprit(PrivateObjects, MostLikelyCulprit, PropertyRef);
			CulpritString = FString::Printf(TEXT("%s (%s)"),
				(MostLikelyCulprit != nullptr) ? *MostLikelyCulprit->GetFullName() : TEXT("(unknown culprit)"),
				(PropertyRef != nullptr) ? *PropertyRef->GetName() : TEXT("unknown property ref"));
		}

		if (SaveContext.IsGenerateSaveError())
		{
			SaveContext.GetError()->Logf(ELogVerbosity::Warning, TEXT("Can't save %s: Graph is linked to external private object %s"), SaveContext.GetFilename(), *CulpritString);
		}
		return ESavePackageResult::Error;
	}
	return ReturnSuccessOrCancel();
}

ESavePackageResult ValidateImports(FSaveContext& SaveContext)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_Save_ValidateImports);

	TArray<UObject*> TopLevelObjects;
	GetObjectsWithPackage(SaveContext.GetPackage(), TopLevelObjects, false);
	auto IsInAnyTopLevelObject = [&TopLevelObjects](UObject* InObject) -> bool
	{
		for (UObject* TopObject : TopLevelObjects)
		{
			if (InObject->IsInOuter(TopObject))
			{
				return true;
			}
		}
		return false;
	};
	auto AnyTopLevelObjectIsIn = [&TopLevelObjects](UObject* InObject) -> bool
	{
		for (UObject* TopObject : TopLevelObjects)
		{
			if (TopObject->IsInOuter(InObject))
			{
				return true;
			}
		}
		return false;
	};
	auto AnyTopLevelObjectHasSameOutermostObject = [&TopLevelObjects](UObject* InObject) -> bool
	{
		UObject* Outermost = InObject->GetOutermostObject();
		for (UObject* TopObject : TopLevelObjects)
		{
			if (TopObject->GetOutermostObject() == Outermost)
			{
				return true;
			}
		}
		return false;

	};

	// Warn for private objects & map object references
	TArray<UObject*> PrivateObjects;
	TArray<UObject*> ObjectsInOtherMaps;
	for (UObject* Import : SaveContext.GetImports())
	{
		UPackage* ImportPackage = Import->GetPackage();
		// All names should be properly harvested at this point
		ensureAlways(SaveContext.NameExists(Import->GetFName().GetComparisonIndex()));
		ensureAlways(SaveContext.NameExists(ImportPackage->GetFName().GetComparisonIndex()));
		ensureAlways(SaveContext.NameExists(Import->GetClass()->GetFName().GetComparisonIndex()));
		ensureAlways(SaveContext.NameExists(Import->GetClass()->GetOuter()->GetFName().GetComparisonIndex()));

		// if an import outer is an export and that import doesn't have a specific package set then, there's an error
		const bool bWrongImport = Import->GetOuter() && Import->GetOuter()->IsInPackage(SaveContext.GetPackage()) && Import->GetExternalPackage() == nullptr;
		if (bWrongImport)
		{
			if (!Import->HasAllFlags(RF_Transient) || !Import->IsNative())
			{
				UE_LOG(LogSavePackage, Warning, TEXT("Bad Object=%s"), *Import->GetFullName());
			}
			else
			{
				// if an object is marked RF_Transient and native, it is either an intrinsic class or
				// a property of an intrinsic class.  Only properties of intrinsic classes will have
				// an Outer that passes the check for "GetOuter()->IsInPackage(InOuter)" (thus ending up in this
				// block of code).  Just verify that the Outer for this property is also marked RF_Transient and Native
				check(Import->GetOuter()->HasAllFlags(RF_Transient) && Import->GetOuter()->IsNative());
			}
		}
		check(!bWrongImport || Import->HasAllFlags(RF_Transient) || Import->IsNative());

		// @todo FH: validate prestream packages still needed..
		if (SaveContext.GetPrestreamPackages().Contains(ImportPackage))
		{
			// These are not errors
			UE_LOG(LogSavePackage, Display, TEXT("Prestreaming package %s "), *ImportPackage->GetPathName()); //-V595
			continue;
		}

		// if this import shares a outer with top level object of this package then the reference is acceptable
		if (!SaveContext.IsCooking() && (IsInAnyTopLevelObject(Import) || AnyTopLevelObjectIsIn(Import) || AnyTopLevelObjectHasSameOutermostObject(Import)))
		{
			continue;
		}

		// See whether the object we are referencing is in another map package.
		if (ImportPackage->ContainsMap())
		{
			ObjectsInOtherMaps.Add(Import);
		}

		if (!Import->HasAnyFlags(RF_Public) && (!SaveContext.IsCooking() || !ImportPackage->HasAnyPackageFlags(PKG_CompiledIn)))
		{
			PrivateObjects.Add(Import);
		}
	}
	if (PrivateObjects.Num() > 0 || ObjectsInOtherMaps.Num() > 0)
	{
		return ValidateIllegalReferences(SaveContext, PrivateObjects, ObjectsInOtherMaps);
	}

	// Cooking checks
	if (SaveContext.IsCooking())
	{
		// Now that imports are validated add them to the cook checker if available
		if (FEDLCookChecker* EDLCookChecker = SaveContext.GetEDLCookChecker())
		{
			for (UObject* Import : SaveContext.GetImports())
			{
				check(Import);
				EDLCookChecker->AddImport(Import, SaveContext.GetPackage());
			}
		}
	}

	return ReturnSuccessOrCancel();
}

ESavePackageResult CreateLinker(FSaveContext& SaveContext)
{
	const FString BaseFilename = FPaths::GetBaseFilename(SaveContext.GetFilename());
	// Make temp file. CreateTempFilename guarantees unique, non-existing filename.
	// The temp file will be saved in the game save folder to not have to deal with potentially too long paths.
	// Since the temp filename may include a 32 character GUID as well, limit the user prefix to 32 characters.
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_Save_CreateLinkerSave);

		if (SaveContext.IsDiffing())
		{
			// Diffing is supported for cooking only 
			if (!SaveContext.IsCooking())
			{
				UE_LOG(LogSavePackage, Warning, TEXT("Diffing Package %s is supported only while cooking."), *SaveContext.GetPackage()->GetName());
				return ESavePackageResult::Error;
			}
			
			// The package asset should always be provided upstream
			check(SaveContext.GetAsset());

			// The entire package will be serialized to memory and then compared against package on disk.
			// Each difference will be log with its Serialize call stack trace if IsDiffCallstack is true
			FArchive* Saver = new FArchiveStackTrace(SaveContext.GetAsset(), *SaveContext.GetPackage()->FileName.ToString(), SaveContext.IsDiffCallstack(), SaveContext.GetDiffMapPtr());
			SaveContext.Linker = MakeUnique<FLinkerSave>(SaveContext.GetPackage(), Saver, SaveContext.IsForceByteSwapping(), SaveContext.IsSaveUnversioned());
		}
		else
		{
			if (SaveContext.IsSaveAsync())
			{
				// Allocate the linker with a memory writer, forcing byte swapping if wanted.
				SaveContext.Linker = MakeUnique<FLinkerSave>(SaveContext.GetPackage(), SaveContext.IsForceByteSwapping(), SaveContext.IsSaveUnversioned());
			}
			else
			{
				// Allocate the linker, forcing byte swapping if wanted.
				SaveContext.TempFilename = FPaths::CreateTempFilename(*FPaths::ProjectSavedDir(), *BaseFilename.Left(32));
				SaveContext.Linker = MakeUnique<FLinkerSave>(SaveContext.GetPackage(), *SaveContext.TempFilename.GetValue(), SaveContext.IsForceByteSwapping(), SaveContext.IsSaveUnversioned());
			}
		}

#if WITH_TEXT_ARCHIVE_SUPPORT
		if (SaveContext.IsTextFormat())
		{
			if (SaveContext.TempFilename.IsSet())
			{
				SaveContext.TextFormatTempFilename = SaveContext.TempFilename.GetValue() + FPackageName::GetTextAssetPackageExtension();
			}
			else
			{
				SaveContext.TextFormatTempFilename = FPaths::CreateTempFilename(*FPaths::ProjectSavedDir(), *BaseFilename.Left(32)) + FPackageName::GetTextAssetPackageExtension();
			}
			SaveContext.TextFormatArchive = TUniquePtr<FArchive>(IFileManager::Get().CreateFileWriter(*SaveContext.TextFormatTempFilename.GetValue()));
			TUniquePtr<FJsonArchiveOutputFormatter> OutputFormatter = MakeUnique<FJsonArchiveOutputFormatter>(*SaveContext.TextFormatArchive);
			OutputFormatter->SetObjectIndicesMap(&SaveContext.Linker->ObjectIndicesMap);
			SaveContext.Formatter = MoveTemp(OutputFormatter);
		}
		else
#endif
		{
			SaveContext.Formatter = MakeUnique<FBinaryArchiveFormatter>(*(FArchive*)SaveContext.Linker.Get());
		}
	}

	SaveContext.StructuredArchive = MakeUnique<FStructuredArchive>(*SaveContext.Formatter);
	return ReturnSuccessOrCancel();
}

struct FNameEntryIdSortHelper
{
private:
	/** the linker that we're sorting names for */
	friend struct TDereferenceWrapper<FNameEntryId, FNameEntryIdSortHelper>;

	/** Comparison function used by Sort */
	FORCEINLINE bool operator()(const FName& A, const FName& B) const
	{
		return A.Compare(B) < 0;
	}

	/** Comparison function used by Sort */
	FORCEINLINE bool operator()(FNameEntryId A, FNameEntryId B) const
	{
		//@todo Could be implemented without constructing FName but need a would new FNameEntry comparison API
		return A != B && operator()(FName::CreateFromDisplayId(A, 0), FName::CreateFromDisplayId(B, 0));
	}
};

struct FObjectResourceSortHelper
{
private:
	friend struct TDereferenceWrapper<FObjectImport, FObjectResourceSortHelper>;
	friend struct TDereferenceWrapper<FObjectExport, FObjectResourceSortHelper>;

	/** Comparison function used by Sort */
	FORCEINLINE bool operator()(const FObjectResource& A, const FObjectResource& B) const
	{
		return A.ObjectName.Compare(B.ObjectName) < 0;
	}
};

ESavePackageResult BuildLinker(FSaveContext& SaveContext)
{
	// Setup Linker 
	{
		// Use the custom versions we harvested from the dependency harvesting pass
		SaveContext.Linker->Summary.SetCustomVersionContainer(SaveContext.GetCustomVersions());

		SaveContext.Linker->SetPortFlags(SaveContext.GetPortFlags());
		SaveContext.Linker->SetFilterEditorOnly(SaveContext.IsFilterEditorOnly());
		SaveContext.Linker->SetCookingTarget(SaveContext.GetTargetPlatform());

		bool bUseUnversionedProperties = SaveContext.IsUsingUnversionedProperties();
		SaveContext.Linker->SetUseUnversionedPropertySerialization(bUseUnversionedProperties);
		SaveContext.Linker->Saver->SetUseUnversionedPropertySerialization(bUseUnversionedProperties);


#if WITH_EDITOR
		if (SaveContext.IsCooking())
		{
			SaveContext.Linker->SetDebugSerializationFlags(DSF_EnableCookerWarnings | SaveContext.Linker->GetDebugSerializationFlags());
		}
#endif
		// Make sure the package has the same version as the linker
		SaveContext.GetPackage()->LinkerPackageVersion = SaveContext.Linker->UE4Ver();
		SaveContext.GetPackage()->LinkerLicenseeVersion = SaveContext.Linker->LicenseeUE4Ver();
		SaveContext.GetPackage()->LinkerCustomVersion = SaveContext.Linker->GetCustomVersions();
	}
	
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	SaveContext.Linker->Summary.Guid = SaveContext.IsKeepGuid() ? SaveContext.GetPackage()->GetGuid() : SaveContext.GetPackage()->MakeNewGuid();
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
#if WITH_EDITORONLY_DATA
	SaveContext.Linker->Summary.PersistentGuid = SaveContext.GetPackage()->GetPersistentGuid();
#endif
	SaveContext.Linker->Summary.Generations = TArray<FGenerationInfo>{ FGenerationInfo(0, 0) };

	FLinkerSave* Linker = SaveContext.Linker.Get();

	// Build Name Map
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_Save_BuildNameMap);
		Linker->Summary.NameOffset = 0;
		Linker->Summary.NameCount = 0;
		Linker->NameMap.Append(SaveContext.GetReferencedNames().Array());
		Sort(&Linker->NameMap[0], Linker->NameMap.Num(), FNameEntryIdSortHelper());

		if (!SaveContext.IsTextFormat())
		{
			for (int32 Index = 0; Index < Linker->NameMap.Num(); ++Index)
			{
				Linker->NameIndices.Add(Linker->NameMap[Index], Index);
			}
		}
	}

	// Build GatherableText
	{
		Linker->Summary.GatherableTextDataOffset = 0;
		Linker->Summary.GatherableTextDataCount = 0;
		if (!SaveContext.IsFilterEditorOnly())
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_Save_BuildGatherableTextData);

			// Gathers from the given package
			SaveContext.GatherableTextResultFlags = EPropertyLocalizationGathererResultFlags::Empty;
			FPropertyLocalizationDataGatherer(Linker->GatherableTextDataMap, SaveContext.GetPackage(), SaveContext.GatherableTextResultFlags);
		}
	}
	
	// Build ImportMap
	TMap<UObject*, UObject*> ReplacedImportOuters;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_Save_BuildImportMap);
		for (UObject* Import : SaveContext.GetImports())
		{
			UClass* ImportClass = Import->GetClass();
			FName ReplacedName = NAME_None;
			
			if (SaveContext.IsCooking())
			{
				UObject* ReplacedOuter = nullptr;
				SavePackageUtilities::GetBlueprintNativeCodeGenReplacement(Import, ImportClass, ReplacedOuter, ReplacedName, SaveContext.GetTargetPlatform());
				if (ReplacedOuter)
				{
					ReplacedImportOuters.Add(Import, ReplacedOuter);
				}
			}
			FObjectImport& ObjectImport = Linker->ImportMap.Add_GetRef(FObjectImport(Import, ImportClass));

			//@todo FH: validate this
			if (SaveContext.GetPrestreamPackages().Contains((UPackage*)Import))
			{
				ObjectImport.ClassName = SavePackageUtilities::NAME_PrestreamPackage;
			}

			if (ReplacedName != NAME_None)
			{
				ObjectImport.ObjectName = ReplacedName;
			}
		}
		Sort(&Linker->ImportMap[0], Linker->ImportMap.Num(), FObjectResourceSortHelper());
		Linker->Summary.ImportCount = Linker->ImportMap.Num();
	}

	// Build ExportMap & Package Netplay data
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_Save_BuildExportMap);
		for (const FTaggedExport& TaggedExport : SaveContext.GetExports())
		{
			FObjectExport& Export = Linker->ExportMap.Add_GetRef(FObjectExport(TaggedExport.Obj, TaggedExport.bNotAlwaysLoadedForEditorGame));
			if (UPackage* Package = Cast<UPackage>(Export.Object))
			{
				Export.PackageFlags = Package->GetPackageFlags();
				if (!Package->HasAnyPackageFlags(PKG_ServerSideOnly))
				{
					PRAGMA_DISABLE_DEPRECATION_WARNINGS
					Export.PackageGuid = Package->GetGuid();
					PRAGMA_ENABLE_DEPRECATION_WARNINGS
				}
			}
		}
		//Sort(&Linker->ExportMap[0], Linker->ExportMap.Num(), FObjectResourceSortHelper());

		// @todo: To remove but for now object sort freaking matter in an incidental manner where it should be properly tracked with dependencies
		// for example where FAnimInstanceProxy PostLoad actually depends on  UAnimBlueprintGeneratedClass PostLoad to be properly initialized.
		FObjectExportSortHelper ExportSortHelper;
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_Save_SortExports);
			ExportSortHelper.SortExports(Linker, nullptr);
		}
		Linker->Summary.ExportCount = Linker->ExportMap.Num();
	}

	// Build Linker Reverse Mapping
	{
		for (int32 ExportIndex = 0; ExportIndex < Linker->ExportMap.Num(); ++ExportIndex)
		{
			UObject* Object = Linker->ExportMap[ExportIndex].Object;
			check(Object);
			Linker->ObjectIndicesMap.Add(Object, FPackageIndex::FromExport(ExportIndex));
		}
		for (int32 ImportIndex = 0; ImportIndex < Linker->ImportMap.Num(); ++ImportIndex)
		{
			UObject* Object = Linker->ImportMap[ImportIndex].XObject;
			check(Object);
			Linker->ObjectIndicesMap.Add(Object, FPackageIndex::FromImport(ImportIndex));
		}
	}

	// Build DependsMap
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_Save_BuildExportDependsMap);

		Linker->DependsMap.AddZeroed(Linker->ExportMap.Num());
		for (int32 ExpIndex = 0; ExpIndex < Linker->ExportMap.Num(); ++ExpIndex)
		{
			UObject* Object = Linker->ExportMap[ExpIndex].Object;
			TArray<FPackageIndex>& DependIndices = Linker->DependsMap[ExpIndex];
			const TSet<UObject*>* SrcDepends = SaveContext.GetObjectDependencies().Find(Object);
			checkf(SrcDepends, TEXT("Couldn't find dependency map for %s"), *Object->GetFullName());
			DependIndices.Reserve(SrcDepends->Num());

			for (UObject* DependentObject : *SrcDepends)
			{
				FPackageIndex DependencyIndex = Linker->ObjectIndicesMap.FindRef(DependentObject);

				//@todo FH: UnmarkExportTagFromDuplicates redirect??

				// if we didn't find it (FindRef returns 0 on failure, which is good in this case), then we are in trouble, something went wrong somewhere
				checkf(!DependencyIndex.IsNull(), TEXT("Failed to find dependency index for %s (%s)"), *DependentObject->GetFullName(), *Object->GetFullName());

				// add the import as an import for this export
				DependIndices.Add(DependencyIndex);
			}
		}
	}

	// Build Searchable Name Map
	{
		Linker->SoftPackageReferenceList = SaveContext.GetSoftPackageReferenceList();

		// Convert the searchable names map from UObject to packageindex
		for (TPair<UObject*, TArray<FName>>& SearchableNamePair : SaveContext.GetSearchableNamesObjectMap())
		{
			const FPackageIndex PackageIndex = Linker->MapObject(SearchableNamePair.Key);
			// This should always be in the imports already
			if (ensure(!PackageIndex.IsNull()))
			{
				Linker->SearchableNamesMap.FindOrAdd(PackageIndex) = MoveTemp(SearchableNamePair.Value);
			}
		}
		SaveContext.GetSearchableNamesObjectMap().Empty();
	}

	// Map Export Indices
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_Save_MapExportIndices);
		for (FObjectExport& Export : Linker->ExportMap)
		{
			// Set class index.
			// If this is *exactly* a UClass, store null instead; for anything else, including UClass-derived classes, map it
			UClass* ObjClass = Export.Object->GetClass();
			if (ObjClass != UClass::StaticClass())
			{
				Export.ClassIndex = Linker->MapObject(ObjClass);
				checkf(!Export.ClassIndex.IsNull(), TEXT("Export %s class is not mapped when saving %s"), *Export.Object->GetFullName(), *Linker->LinkerRoot->GetName());
			}
			else
			{
				Export.ClassIndex = FPackageIndex();
			}

			if (SaveContext.IsCooking())
			{
				UObject* Archetype = Export.Object->GetArchetype();
				check(Archetype);
				check(Archetype->IsA(Export.Object->HasAnyFlags(RF_ClassDefaultObject) ? ObjClass->GetSuperClass() : ObjClass));
				Export.TemplateIndex = Linker->MapObject(Archetype);
				UE_CLOG(Export.TemplateIndex.IsNull(), LogSavePackage, Fatal, TEXT("%s was an archetype of %s but returned a null index mapping the object."), *Archetype->GetFullName(), *Export.Object->GetFullName());
				check(!Export.TemplateIndex.IsNull());
			}

			// Set the parent index, if this export represents a UStruct-derived object
			UStruct* Struct = Cast<UStruct>(Export.Object);
			if (Struct && Struct->GetSuperStruct() != nullptr)
			{
				Export.SuperIndex = Linker->MapObject(Struct->GetSuperStruct());
				checkf(!Export.SuperIndex.IsNull(),
					TEXT("Export Struct (%s) of type (%s) inheriting from (%s) of type (%s) has not mapped super struct."),
					*GetPathNameSafe(Struct),
					*(Struct->GetClass()->GetName()),
					*GetPathNameSafe(Struct->GetSuperStruct()),
					*(Struct->GetSuperStruct()->GetClass()->GetName())
				);
			}
			else
			{
				Export.SuperIndex = FPackageIndex();
			}

			// Set FPackageIndex for this export's Outer. If the export's Outer
			// is the UPackage corresponding to this package's LinkerRoot, leave it null
			Export.OuterIndex = Export.Object->GetOuter() != SaveContext.GetPackage() ? Linker->MapObject(Export.Object->GetOuter()) : FPackageIndex();

			// Only packages or object having the currently saved package as outer are allowed to have no outer
			ensureMsgf(Export.OuterIndex != FPackageIndex() || Export.Object->IsA(UPackage::StaticClass()) || Export.Object->GetOuter() == SaveContext.GetPackage(), TEXT("Export %s has no valid outer!"), *Export.Object->GetPathName());
		}

		for (FObjectImport& Import : Linker->ImportMap)
		{
			if (Import.XObject)
			{
				// Set the package index.
				if (Import.XObject->GetOuter())
				{
					UObject** ReplacedOuter = ReplacedImportOuters.Find(Import.XObject);
					if (ReplacedOuter && *ReplacedOuter)
					{
						Import.OuterIndex = Linker->MapObject(*ReplacedOuter);
						ensure(Import.OuterIndex != FPackageIndex());
					}
					else
					{
						Import.OuterIndex = Linker->MapObject(Import.XObject->GetOuter());
					}

					// if the import has a package set, set it up
					if (UPackage* ImportPackage = Import.XObject->GetExternalPackage())
					{
						Import.SetPackageName(ImportPackage->GetFName());
					}

					if (SaveContext.IsCooking())
					{
						// Only package imports are allowed to have no outer
						ensureMsgf(Import.OuterIndex != FPackageIndex() || Import.ClassName == NAME_Package, TEXT("Import %s has no valid outer when cooking!"), *Import.XObject->GetPathName());
					}
				}
			}
			else
			{
				checkf(false, TEXT("NULL XObject for import - Object: %s Class: %s"), *Import.ObjectName.ToString(), *Import.ClassName.ToString());
			}
		}
	}
	return ReturnSuccessOrCancel();
}

void SavePreloadDependencies(FStructuredArchive::FRecord& StructuredArchiveRoot, FSaveContext& SaveContext)
{
	FLinkerSave* Linker = SaveContext.Linker.Get();
	auto IncludeObjectAsDependency = [Linker, &SaveContext](int32 CallSite, TSet<FPackageIndex>& AddTo, UObject* ToTest, UObject* ForObj, bool bMandatory, bool bOnlyIfInLinkerTable)
	{
		// Skip transient, editor only, and excluded client/server objects
		if (ToTest)
		{
			UPackage* Outermost = ToTest->GetOutermost();
			check(Outermost);
			if (Outermost->GetFName() == GLongCoreUObjectPackageName)
			{
				return; // We assume nothing in coreuobject ever loads assets in a constructor
			}
			FPackageIndex Index = Linker->MapObject(ToTest);
			if (Index.IsNull() && bOnlyIfInLinkerTable)
			{
				return;
			}
			if (!Index.IsNull() && (ToTest->HasAllFlags(RF_Transient) && !ToTest->IsNative()))
			{
				UE_LOG(LogSavePackage, Warning, TEXT("A dependency '%s' of '%s' is in the linker table, but is transient. We will keep the dependency anyway (%d)."), *ToTest->GetFullName(), *ForObj->GetFullName(), CallSite);
			}
			if (!Index.IsNull() && ToTest->IsPendingKill())
			{
				UE_LOG(LogSavePackage, Warning, TEXT("A dependency '%s' of '%s' is in the linker table, but is pending kill. We will keep the dependency anyway (%d)."), *ToTest->GetFullName(), *ForObj->GetFullName(), CallSite);
			}
			bool bNotFiltered = !SaveContext.IsExcluded(ToTest);
			if (bMandatory && !bNotFiltered)
			{
				UE_LOG(LogSavePackage, Warning, TEXT("A dependency '%s' of '%s' was filtered, but is mandatory. This indicates a problem with editor only stripping. We will keep the dependency anyway (%d)."), *ToTest->GetFullName(), *ForObj->GetFullName(), CallSite);
				bNotFiltered = true;
			}
			if (bNotFiltered)
			{
				if (!Index.IsNull())
				{
					AddTo.Add(Index);
					return;
				}
				else if (!ToTest->HasAnyFlags(RF_Transient))
				{
					UE_CLOG(Outermost->HasAnyPackageFlags(PKG_CompiledIn), LogSavePackage, Verbose, TEXT("A compiled in dependency '%s' of '%s' was not actually in the linker tables and so will be ignored (%d)."), *ToTest->GetFullName(), *ForObj->GetFullName(), CallSite);
					UE_CLOG(!Outermost->HasAnyPackageFlags(PKG_CompiledIn), LogSavePackage, Fatal, TEXT("A dependency '%s' of '%s' was not actually in the linker tables and so will be ignored (%d)."), *ToTest->GetFullName(), *ForObj->GetFullName(), CallSite);
				}
			}
			check(!bMandatory);
		}
	};

	auto IncludeIndexAsDependency = [Linker](TSet<FPackageIndex>& AddTo, FPackageIndex Dep)
	{
		if (!Dep.IsNull())
		{
			UObject* ToTest = Dep.IsExport() ? Linker->Exp(Dep).Object : Linker->Imp(Dep).XObject;
			if (ToTest)
			{
				UPackage* Outermost = ToTest->GetOutermost();
				if (Outermost && Outermost->GetFName() != GLongCoreUObjectPackageName) // We assume nothing in coreuobject ever loads assets in a constructor
				{
					AddTo.Add(Dep);
				}
			}
		}
	};

	Linker->Summary.PreloadDependencyOffset = Linker->Tell();
	Linker->Summary.PreloadDependencyCount = -1;

	if (SaveContext.IsCooking())
	{
		Linker->Summary.PreloadDependencyCount = 0;

		FStructuredArchive::FStream DepedenciesStream = StructuredArchiveRoot.EnterStream(SA_FIELD_NAME(TEXT("PreloadDependencies")));

		TArray<UObject*> Subobjects;
		TArray<UObject*> Deps;
		TSet<FPackageIndex> SerializationBeforeCreateDependencies;
		TSet<FPackageIndex> SerializationBeforeSerializationDependencies;
		TSet<FPackageIndex> CreateBeforeSerializationDependencies;
		TSet<FPackageIndex> CreateBeforeCreateDependencies;

		for (int32 ExportIndex = 0; ExportIndex < Linker->ExportMap.Num(); ++ExportIndex)
		{
			FObjectExport& Export = Linker->ExportMap[ExportIndex];
			check(Export.Object);
			{
				SerializationBeforeCreateDependencies.Reset();
				IncludeIndexAsDependency(SerializationBeforeCreateDependencies, Export.ClassIndex);
				UObject* CDO = Export.Object->GetArchetype();
				IncludeObjectAsDependency(1, SerializationBeforeCreateDependencies, CDO, Export.Object, true, false);
				Subobjects.Reset();
				GetObjectsWithOuter(CDO, Subobjects);
				for (UObject* SubObj : Subobjects)
				{
					// Only include subobject archetypes
					if (SubObj->HasAnyFlags(RF_DefaultSubObject | RF_ArchetypeObject))
					{
						while (SubObj->HasAnyFlags(RF_Transient)) // transient components are stripped by the ICH, so find the one it will really use at runtime
						{
							UObject* SubObjArch = SubObj->GetArchetype();
							if (SubObjArch->GetClass()->HasAnyClassFlags(CLASS_Native | CLASS_Intrinsic))
							{
								break;
							}
							SubObj = SubObjArch;
						}
						if (!SubObj->IsPendingKill())
						{
							IncludeObjectAsDependency(2, SerializationBeforeCreateDependencies, SubObj, Export.Object, false, false);
						}
					}
				}
			}
			{
				SerializationBeforeSerializationDependencies.Reset();
				Deps.Reset();
				Export.Object->GetPreloadDependencies(Deps);

				for (UObject* Obj : Deps)
				{
					IncludeObjectAsDependency(3, SerializationBeforeSerializationDependencies, Obj, Export.Object, false, true);
				}
				if (Export.Object->HasAnyFlags(RF_ArchetypeObject | RF_ClassDefaultObject))
				{
					UObject* Outer = Export.Object->GetOuter();
					if (!Outer->IsA(UPackage::StaticClass()))
					{
						IncludeObjectAsDependency(4, SerializationBeforeSerializationDependencies, Outer, Export.Object, true, false);
					}
				}
				if (Export.Object->IsA(UClass::StaticClass()))
				{
					// we need to load archetypes of our subobjects before we load the class
					UObject* CDO = CastChecked<UClass>(Export.Object)->GetDefaultObject();
					Subobjects.Reset();
					GetObjectsWithOuter(CDO, Subobjects);
					for (UObject* SubObj : Subobjects)
					{
						// Only include subobject archetypes
						if (SubObj->HasAnyFlags(RF_DefaultSubObject | RF_ArchetypeObject))
						{
							SubObj = SubObj->GetArchetype();
							while (SubObj->HasAnyFlags(RF_Transient)) // transient components are stripped by the ICH, so find the one it will really use at runtime
							{
								UObject* SubObjArch = SubObj->GetArchetype();
								if (SubObjArch->GetClass()->HasAnyClassFlags(CLASS_Native | CLASS_Intrinsic))
								{
									break;
								}
								SubObj = SubObjArch;
							}
							if (!SubObj->IsPendingKill())
							{
								IncludeObjectAsDependency(5, SerializationBeforeSerializationDependencies, SubObj, Export.Object, false, false);
							}
						}
					}
				}
			}
			{
				CreateBeforeSerializationDependencies.Reset();
				UClass* Class = Cast<UClass>(Export.Object);
				UObject* ClassCDO = Class ? Class->GetDefaultObject() : nullptr;
				{
					TArray<FPackageIndex>& Depends = Linker->DependsMap[ExportIndex];
					for (FPackageIndex Dep : Depends)
					{
						UObject* ToTest = Dep.IsExport() ? Linker->Exp(Dep).Object : Linker->Imp(Dep).XObject;
						if (ToTest != ClassCDO)
						{
							IncludeIndexAsDependency(CreateBeforeSerializationDependencies, Dep);
						}
					}
				}
				{
					const TSet<UObject*>& NativeDeps = SaveContext.GetNativeObjectDependencies()[Export.Object];
					for (UObject* ToTest : NativeDeps)
					{
						if (ToTest != ClassCDO)
						{
							IncludeObjectAsDependency(6, CreateBeforeSerializationDependencies, ToTest, Export.Object, false, true);
						}
					}
				}
			}
			{
				CreateBeforeCreateDependencies.Reset();
				IncludeIndexAsDependency(CreateBeforeCreateDependencies, Export.OuterIndex);
				IncludeIndexAsDependency(CreateBeforeCreateDependencies, Export.SuperIndex);
			}
			FEDLCookChecker* EDLCookChecker = SaveContext.GetEDLCookChecker();
			check(EDLCookChecker);
			auto AddArcForDepChecking = [&Linker, &Export, EDLCookChecker](bool bExportIsSerialize, FPackageIndex Dep, bool bDepIsSerialize)
			{
				check(Export.Object);
				check(!Dep.IsNull());
				UObject* DepObject = Dep.IsExport() ? Linker->Exp(Dep).Object : Linker->Imp(Dep).XObject;
				check(DepObject);

				Linker->DepListForErrorChecking.Add(Dep);
				EDLCookChecker->AddArc(DepObject, bDepIsSerialize, Export.Object, bExportIsSerialize);
			};

			for (FPackageIndex Index : SerializationBeforeSerializationDependencies)
			{
				if (SerializationBeforeCreateDependencies.Contains(Index))
				{
					continue; // if the other thing must be serialized before we create, then this is a redundant dep
				}
				if (Export.FirstExportDependency == -1)
				{
					Export.FirstExportDependency = Linker->Summary.PreloadDependencyCount;
					check(Export.SerializationBeforeSerializationDependencies == 0 && Export.CreateBeforeSerializationDependencies == 0 && Export.SerializationBeforeCreateDependencies == 0 && Export.CreateBeforeCreateDependencies == 0);
				}
				Linker->Summary.PreloadDependencyCount++;
				Export.SerializationBeforeSerializationDependencies++;
				DepedenciesStream.EnterElement() << Index;
				AddArcForDepChecking(true, Index, true);
			}
			for (FPackageIndex Index : CreateBeforeSerializationDependencies)
			{
				if (SerializationBeforeCreateDependencies.Contains(Index))
				{
					continue; // if the other thing must be serialized before we create, then this is a redundant dep
				}
				if (SerializationBeforeSerializationDependencies.Contains(Index))
				{
					continue; // if the other thing must be serialized before we serialize, then this is a redundant dep
				}
				if (CreateBeforeCreateDependencies.Contains(Index))
				{
					continue; // if the other thing must be created before we are created, then this is a redundant dep
				}
				if (Export.FirstExportDependency == -1)
				{
					Export.FirstExportDependency = Linker->Summary.PreloadDependencyCount;
					check(Export.SerializationBeforeSerializationDependencies == 0 && Export.CreateBeforeSerializationDependencies == 0 && Export.SerializationBeforeCreateDependencies == 0 && Export.CreateBeforeCreateDependencies == 0);
				}
				Linker->Summary.PreloadDependencyCount++;
				Export.CreateBeforeSerializationDependencies++;
				DepedenciesStream.EnterElement() << Index;
				AddArcForDepChecking(true, Index, false);
			}
			for (FPackageIndex Index : SerializationBeforeCreateDependencies)
			{
				if (Export.FirstExportDependency == -1)
				{
					Export.FirstExportDependency = Linker->Summary.PreloadDependencyCount;
					check(Export.SerializationBeforeSerializationDependencies == 0 && Export.CreateBeforeSerializationDependencies == 0 && Export.SerializationBeforeCreateDependencies == 0 && Export.CreateBeforeCreateDependencies == 0);
				}
				Linker->Summary.PreloadDependencyCount++;
				Export.SerializationBeforeCreateDependencies++;
				DepedenciesStream.EnterElement() << Index;
				AddArcForDepChecking(false, Index, true);
			}
			for (FPackageIndex Index : CreateBeforeCreateDependencies)
			{
				if (Export.FirstExportDependency == -1)
				{
					Export.FirstExportDependency = Linker->Summary.PreloadDependencyCount;
					check(Export.SerializationBeforeSerializationDependencies == 0 && Export.CreateBeforeSerializationDependencies == 0 && Export.SerializationBeforeCreateDependencies == 0 && Export.CreateBeforeCreateDependencies == 0);
				}
				Linker->Summary.PreloadDependencyCount++;
				Export.CreateBeforeCreateDependencies++;
				DepedenciesStream.EnterElement() << Index;
				AddArcForDepChecking(false, Index, false);
			}
		}
		UE_LOG(LogSavePackage, Verbose, TEXT("Saved %d dependencies for %d exports."), Linker->Summary.PreloadDependencyCount, Linker->ExportMap.Num());
	}
}

void WriteGatherableText(FStructuredArchive::FRecord& StructuredArchiveRoot, FSaveContext& SaveContext)
{
	FStructuredArchive::FStream Stream = StructuredArchiveRoot.EnterStream(SA_FIELD_NAME(TEXT("GatherableTextData")));
	if (!SaveContext.IsFilterEditorOnly()
		// We can only cache packages that:
		//	1) Don't contain script data, as script data is very volatile and can only be safely gathered after it's been compiled (which happens automatically on asset load).
		//	2) Don't contain text keyed with an incorrect package localization ID, as these keys will be changed later during save.
		&& !EnumHasAnyFlags(SaveContext.GatherableTextResultFlags, EPropertyLocalizationGathererResultFlags::HasScript | EPropertyLocalizationGathererResultFlags::HasTextWithInvalidPackageLocalizationID))
	{
		FLinkerSave* Linker = SaveContext.GetLinker();

		// The Editor version is used as part of the check to see if a package is too old to use the gather cache, so we always have to add it if we have gathered loc for this asset
		// Note that using custom version here only works because we already added it to the export tagger before the package summary was serialized
		Linker->UsingCustomVersion(FEditorObjectVersion::GUID);

		Linker->Summary.GatherableTextDataOffset = Linker->Tell();
		Linker->Summary.GatherableTextDataCount = Linker->GatherableTextDataMap.Num();
		for (FGatherableTextData& GatherableTextData : Linker->GatherableTextDataMap)
		{
			Stream.EnterElement() << GatherableTextData;
		}
	}
}

ESavePackageResult WritePackageHeader(FStructuredArchive::FRecord& StructuredArchiveRoot, FSaveContext& SaveContext)
{
	FLinkerSave* Linker = SaveContext.Linker.Get();
#if WITH_EDITOR
	FArchiveStackTraceIgnoreScope IgnoreDiffScope(SaveContext.IsIgnoringHeaderDiff());
#endif

	// Write Dummy Summary
	{
		StructuredArchiveRoot.GetUnderlyingArchive() << Linker->Summary;
	}
	SaveContext.OffsetAfterPackageFileSummary = (int32)Linker->Tell();

	// Write Name Map
	Linker->Summary.NameOffset = SaveContext.OffsetAfterPackageFileSummary;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_Save_BuildNameMap);
		Linker->Summary.NameCount = Linker->NameMap.Num();
		for (const FNameEntryId NameEntryId : Linker->NameMap)
		{
			FName::GetEntry(NameEntryId)->Write(*Linker);
		}
	}

	// Write GatherableText
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_Save_WriteGatherableTextData);
		WriteGatherableText(StructuredArchiveRoot, SaveContext);
	}

	// Save Dummy Import Map, overwritten later.
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_Save_WriteDummyImportMap);
		Linker->Summary.ImportOffset = Linker->Tell();
		for (FObjectImport& Import : Linker->ImportMap)
		{
			StructuredArchiveRoot.GetUnderlyingArchive() << Import;
		}
	}
	SaveContext.OffsetAfterImportMap = Linker->Tell();

	// Save Dummy Export Map, overwritten later.
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_Save_WriteDummyExportMap);
		Linker->Summary.ExportOffset = Linker->Tell();
		for (FObjectExport& Export : Linker->ExportMap)
		{
			*Linker << Export;
		}
	}
	SaveContext.OffsetAfterExportMap = Linker->Tell();

	// Save Depend Map
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_Save_WriteDependsMap);

		FStructuredArchive::FStream DependsStream = StructuredArchiveRoot.EnterStream(SA_FIELD_NAME(TEXT("DependsMap")));
		Linker->Summary.DependsOffset = Linker->Tell();
		if (SaveContext.IsCooking())
		{
			//@todo optimization, this should just be stripped entirely from cooked packages
			TArray<FPackageIndex> Depends; // empty array
			for (int32 ExportIndex = 0; ExportIndex < Linker->ExportMap.Num(); ++ExportIndex)
			{
				DependsStream.EnterElement() << Depends;
			}
		}
		else
		{
			// save depends map (no need for later patching)
			check(Linker->DependsMap.Num() == Linker->ExportMap.Num());
			for (TArray<FPackageIndex>& Depends : Linker->DependsMap)
			{
				DependsStream.EnterElement() << Depends;
			}
		}
	}

	// Write Soft Package references & Searchable Names
	if (!SaveContext.IsFilterEditorOnly())
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_Save_SaveSoftPackagesAndSearchableNames);

		// Save soft package references
		Linker->Summary.SoftPackageReferencesOffset = Linker->Tell();
		Linker->Summary.SoftPackageReferencesCount = Linker->SoftPackageReferenceList.Num();
		{
			FStructuredArchive::FStream SoftReferenceStream = StructuredArchiveRoot.EnterStream(SA_FIELD_NAME(TEXT("SoftReferences")));
			for (FName& SoftPackageName : Linker->SoftPackageReferenceList)
			{
				SoftReferenceStream.EnterElement() << SoftPackageName;
			}

			// Save searchable names map
			Linker->Summary.SearchableNamesOffset = Linker->Tell();
			Linker->SerializeSearchableNamesMap(StructuredArchiveRoot.EnterField(SA_FIELD_NAME(TEXT("SearchableNames"))));
		}
	}
	else
	{
		Linker->Summary.SoftPackageReferencesCount = 0;
		Linker->Summary.SoftPackageReferencesOffset = 0;
		Linker->Summary.SearchableNamesOffset = 0;
	}

	// Save thumbnails
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_Save_SaveThumbnails);
		SavePackageUtilities::SaveThumbnails(SaveContext.GetPackage(), Linker, StructuredArchiveRoot.EnterField(SA_FIELD_NAME(TEXT("Thumbnails"))));
	}
	{
		// Save asset registry data so the editor can search for information about assets in this package
		TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_Save_SaveAssetRegistryData);
		UE::AssetRegistry::WritePackageData(StructuredArchiveRoot, SaveContext.IsCooking(), SaveContext.GetPackage(), Linker, SaveContext.GetImportsUsedInGame(), SaveContext.GetSoftPackagesUsedInGame());
	}
	// Save level information used by World browser
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_Save_WorldLevelData);
		SavePackageUtilities::SaveWorldLevelInfo(SaveContext.GetPackage(), Linker, StructuredArchiveRoot);
	}

	// Write Preload Dependencies
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_Save_PreloadDependencies);
		SavePreloadDependencies(StructuredArchiveRoot, SaveContext);
	}
	Linker->Summary.TotalHeaderSize = Linker->Tell();
	return ReturnSuccessOrCancel();
}

ESavePackageResult WritePackageTextHeader(FStructuredArchive::FRecord& StructuredArchiveRoot, FSaveContext& SaveContext)
{
	FLinkerSave* Linker = SaveContext.GetLinker();

	// Write GatherableText
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_Save_WriteGatherableTextData);
		WriteGatherableText(StructuredArchiveRoot, SaveContext);
	}

	// Save thumbnails
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_Save_SaveThumbnails);
		SavePackageUtilities::SaveThumbnails(SaveContext.GetPackage(), Linker, StructuredArchiveRoot.EnterField(SA_FIELD_NAME(TEXT("Thumbnails"))));
	}
	// Save level information used by World browser
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_Save_WorldLevelData);
		SavePackageUtilities::SaveWorldLevelInfo(SaveContext.GetPackage(), Linker, StructuredArchiveRoot);
	}

	return ReturnSuccessOrCancel();
}

ESavePackageResult WriteExports(FStructuredArchive::FRecord& StructuredArchiveRoot, FSaveContext& SaveContext)
{
	//COOK_STAT(FScopedDurationTimer SaveTimer(FSavePackageStats::SerializeExportsTimeSec));
	TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_Save_SaveExports);
	FLinkerSave* Linker = SaveContext.Linker.Get();
	FScopedSlowTask SlowTask(Linker->ExportMap.Num(), FText(), SaveContext.IsUsingSlowTask());

	FStructuredArchive::FRecord ExportsRecord = StructuredArchiveRoot.EnterRecord(SA_FIELD_NAME(TEXT("Exports")));

	// Save exports.
	int32 LastExportSaveStep = 0;
	for (int32 i = 0; i < Linker->ExportMap.Num(); i++)
	{
		if (GWarn->ReceivedUserCancel())
		{
			return ESavePackageResult::Canceled;
		}
		SlowTask.EnterProgressFrame();

		FObjectExport& Export = Linker->ExportMap[i];
		if (Export.Object)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_Save_SaveExport);

			// Save the object data.
			Export.SerialOffset = Linker->Tell();
			Linker->CurrentlySavingExport = FPackageIndex::FromExport(i);

			FString ObjectName = Export.Object->GetPathName(SaveContext.GetPackage());
			FStructuredArchive::FSlot ExportSlot = ExportsRecord.EnterField(SA_FIELD_NAME(*ObjectName));

			if (SaveContext.IsTextFormat())
			{
				FObjectTextExport ObjectTextExport(Export, SaveContext.GetPackage());
				ExportSlot << ObjectTextExport;
			}

#if WITH_EDITOR
			bool bSupportsText = UClass::IsSafeToSerializeToStructuredArchives(Export.Object->GetClass());
#else
			bool bSupportsText = false;
#endif

			if (Export.Object->HasAnyFlags(RF_ClassDefaultObject))
			{
				if (bSupportsText)
				{
					Export.Object->GetClass()->SerializeDefaultObject(Export.Object, ExportSlot);
				}
				else
				{
					FArchiveUObjectFromStructuredArchive Adapter(ExportSlot);
					Export.Object->GetClass()->SerializeDefaultObject(Export.Object, Adapter.GetArchive());
					Adapter.Close();
				}
			}
			else
			{
				TGuardValue<UObject*> GuardSerializedObject(SaveContext.GetSerializeContext()->SerializedObject, Export.Object);

				if (bSupportsText)
				{
					FStructuredArchive::FRecord ExportRecord = ExportSlot.EnterRecord();
					Export.Object->Serialize(ExportRecord);
				}
				else
				{
					FArchiveUObjectFromStructuredArchive Adapter(ExportSlot);
					Export.Object->Serialize(Adapter.GetArchive());
					Adapter.Close();
				}

#if WITH_EDITOR
				if (Linker->IsCooking())
				{
					Export.Object->CookAdditionalFiles(SaveContext.GetFilename(), SaveContext.GetTargetPlatform(),
						[&SaveContext](const TCHAR* Filename, void* Data, int64 Size)
						{
							FLargeMemoryWriter& Writer = SaveContext.AdditionalFilesFromExports.Emplace_GetRef(0, true, Filename);
							Writer.Serialize(Data, Size);
						});
				}
#endif
			}
			Linker->CurrentlySavingExport = FPackageIndex();
			Export.SerialSize = Linker->Tell() - Export.SerialOffset;
		}
	}
	return ReturnSuccessOrCancel();
}

ESavePackageResult WriteAdditionalExportFiles(FSaveContext& SaveContext)
{
	if (SaveContext.IsCooking() && SaveContext.AdditionalFilesFromExports.Num() > 0)
	{
		const bool bWriteFileToDisk = !SaveContext.IsDiffing();
		const bool bComputeHash = SaveContext.IsComputeHash();
		for (FLargeMemoryWriter& Writer : SaveContext.AdditionalFilesFromExports)
		{
			const int64 Size = Writer.TotalSize();
			SaveContext.TotalPackageSizeUncompressed += Size;

			if (bComputeHash || bWriteFileToDisk)
			{
				FLargeMemoryPtr DataPtr(Writer.ReleaseOwnership());

				EAsyncWriteOptions WriteOptions(EAsyncWriteOptions::None);
				if (bComputeHash)
				{
					WriteOptions |= EAsyncWriteOptions::ComputeHash;
				}
				if (bWriteFileToDisk)
				{
					WriteOptions |= EAsyncWriteOptions::WriteFileToDisk;
				}
				SavePackageUtilities::AsyncWriteFile(SaveContext.AsyncWriteAndHashSequence, MoveTemp(DataPtr), Size, *Writer.GetArchiveName(), WriteOptions, {});
			}
		}
		SaveContext.AdditionalFilesFromExports.Empty();
	}
	return ReturnSuccessOrCancel();
}

ESavePackageResult UpdatePackageHeader(FStructuredArchive::FRecord& StructuredArchiveRoot, FSaveContext& SaveContext)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_Save_UpdatePackageHeader);

	FLinkerSave* Linker = SaveContext.Linker.Get();
#if WITH_EDITOR
	FArchiveStackTraceIgnoreScope IgnoreDiffScope(SaveContext.IsIgnoringHeaderDiff());
#endif

	// Write Real Import Map
	if (!SaveContext.IsTextFormat())
	{
		Linker->Seek(Linker->Summary.ImportOffset);
		FStructuredArchive::FStream ImportTableStream = StructuredArchiveRoot.EnterStream(SA_FIELD_NAME(TEXT("ImportTable")));
		for (FObjectImport& Import : Linker->ImportMap)
		{
			ImportTableStream.EnterElement() << Import;
		}
	}
	// Write Real Export Map
	if (!SaveContext.IsTextFormat())
	{
		check(Linker->Tell() == SaveContext.OffsetAfterImportMap);
		Linker->Seek(Linker->Summary.ExportOffset);
		FStructuredArchive::FStream ExportTableStream = StructuredArchiveRoot.EnterStream(SA_FIELD_NAME(TEXT("ExportTable")));

		for (FObjectExport& Export : Linker->ExportMap)
		{
			ExportTableStream.EnterElement() << Export;
		}
		check(Linker->Tell() == SaveContext.OffsetAfterExportMap);
	}

	// Update Summary
	// Write Real Summary
	{
		//@todo: remove ExportCount and NameCount - no longer used
		Linker->Summary.Generations.Last().ExportCount = Linker->Summary.ExportCount;
		Linker->Summary.Generations.Last().NameCount = Linker->Summary.NameCount;

		// create the package source (based on developer or user created)
#if (UE_BUILD_SHIPPING && WITH_EDITOR)
		Linker->Summary.PackageSource = FMath::Rand() * FMath::Rand();
#else
		Linker->Summary.PackageSource = FCrc::StrCrc_DEPRECATED(*FPaths::GetBaseFilename(SaveContext.GetFilename()).ToUpper());
#endif

		// Flag package as requiring localization gather if the archive requires localization gathering.
		Linker->LinkerRoot->ThisRequiresLocalizationGather(Linker->RequiresLocalizationGather());

		// Update package flags from package, in case serialization has modified package flags.
		Linker->Summary.PackageFlags = Linker->LinkerRoot->GetPackageFlags() & ~PKG_NewlyCreated;

		// @todo: custom versions: when can this be checked?
		{
			// Verify that the final serialization pass hasn't added any new custom versions. Otherwise this will result in crashes when loading the package.
			bool bNewCustomVersionsUsed = false;
			for (const FCustomVersion& LinkerCustomVer : Linker->GetCustomVersions().GetAllVersions())
			{
				if (Linker->Summary.GetCustomVersionContainer().GetVersion(LinkerCustomVer.Key) == nullptr)
				{
					UE_LOG(LogSavePackage, Error,
						TEXT("Unexpected custom version \"%s\" found when saving %s. This usually happens when export tagging and final serialization paths differ. Package will not be saved."),
						*LinkerCustomVer.GetFriendlyName().ToString(), *Linker->LinkerRoot->GetName());
					bNewCustomVersionsUsed = true;
				}
			}
			if (bNewCustomVersionsUsed)
			{
				return ESavePackageResult::Error;
			}
		}

		if (!SaveContext.IsTextFormat())
		{
			Linker->Seek(0);
		}
		{
			StructuredArchiveRoot.EnterField(SA_FIELD_NAME(TEXT("Summary"))) << Linker->Summary;
		}

		if (!SaveContext.IsTextFormat())
		{
			check(Linker->Tell() == SaveContext.OffsetAfterPackageFileSummary);
		}
	}
	return ReturnSuccessOrCancel();
}

ESavePackageResult FinalizeFile(FStructuredArchive::FRecord& StructuredArchiveRoot, FSaveContext& SaveContext)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_Save_FinalizeFile);

	// In the concurrent case, it is called right after routing presave so it can be done in batch before going concurrent
	if (!SaveContext.IsConcurrent())
	{
		// If we're writing to the existing file call ResetLoaders on the Package so that we drop the handle to the file on disk and can write to it
		//COOK_STAT(FScopedDurationTimer SaveTimer(FSavePackageStats::ResetLoadersTimeSec));
		ResetLoadersForSave(SaveContext.GetPackage(), SaveContext.GetFilename());
	}

	if (SaveContext.IsSaveAsync())
	{
		FString PathToSave = SaveContext.GetFilename();
		FLinkerSave* Linker = SaveContext.GetLinker();

		if (SaveContext.IsDiffCallstack())
		{
			const TCHAR* CutoffString = TEXT("UEditorEngine::Save()");
			FArchiveStackTrace* Writer = static_cast<FArchiveStackTrace*>(Linker->Saver);
			TMap<FName, FArchiveDiffStats> PackageDiffStats;
			Writer->CompareWith(*PathToSave, SaveContext.IsCooking() ? Linker->Summary.TotalHeaderSize : 0, CutoffString, SaveContext.GetMaxDiffsToLog(), PackageDiffStats);
			SaveContext.TotalPackageSizeUncompressed += Writer->TotalSize();

			//COOK_STAT(FSavePackageStats::NumberOfDifferentPackages++);
			//COOK_STAT(FSavePackageStats::MergeStats(PackageDiffStats));

			if (SaveContext.IsSavingForDiff())
			{
				PathToSave = FPaths::Combine(FPaths::GetPath(PathToSave), FPaths::GetBaseFilename(PathToSave) + TEXT("_ForDiff") + FPaths::GetExtension(PathToSave, true));
			}
		}
		else if (SaveContext.IsDiffOnly())
		{
			FArchiveStackTrace* Writer = (FArchiveStackTrace*)(Linker->Saver);
			FArchiveDiffMap OutDiffMap;
			SaveContext.bDiffOnlyIdentical = Writer->GenerateDiffMap(*PathToSave, IsEventDrivenLoaderEnabledInCookedBuilds() ? Linker->Summary.TotalHeaderSize : 0, SaveContext.GetMaxDiffsToLog(), OutDiffMap);
			SaveContext.TotalPackageSizeUncompressed += Writer->TotalSize();
			if (FArchiveDiffMap* OutDiffMapPtr = SaveContext.GetDiffMapPtr())
			{
				*OutDiffMapPtr = MoveTemp(OutDiffMap);
			}
		}

		if (!SaveContext.IsDiffing() || SaveContext.IsSavingForDiff())
		{
			UE_LOG(LogSavePackage, Verbose, TEXT("Async saving from memory to '%s'"), *PathToSave);
			FLargeMemoryWriter* Writer = static_cast<FLargeMemoryWriter*>(Linker->Saver);
			const int64 DataSize = Writer->TotalSize();

			if (SaveContext.IsDiffCallstack())  // Avoid double counting the package size if SAVE_DiffCallstack flag is set and DiffSettings.bSaveForDiff == true.
			{
				SaveContext.TotalPackageSizeUncompressed += DataSize;
			}

			FSavePackageContext* SavePackageContext = SaveContext.GetSavePackageContext();
			if (SavePackageContext != nullptr && SavePackageContext->PackageStoreWriter != nullptr && SaveContext.IsCooking())
			{
				FIoBuffer IoBuffer(FIoBuffer::AssumeOwnership, Writer->ReleaseOwnership(), DataSize);

				if (SaveContext.IsComputeHash())
				{
					FIoBuffer InnerBuffer(IoBuffer.Data(), IoBuffer.DataSize(), IoBuffer);
					SavePackageUtilities::IncrementOutstandingAsyncWrites();
					SaveContext.AsyncWriteAndHashSequence.AddWork([InnerBuffer = MoveTemp(InnerBuffer)](FMD5& State)
					{
						State.Update(InnerBuffer.Data(), InnerBuffer.DataSize());
						SavePackageUtilities::DecrementOutstandingAsyncWrites();
					});
				}

				FPackageStoreWriter::HeaderInfo HeaderInfo;
				FPackageStoreWriter::ExportsInfo ExportsInfo;

				ExportsInfo.PackageName = HeaderInfo.PackageName = SaveContext.GetPackage()->GetFName();
				ExportsInfo.LooseFilePath = HeaderInfo.LooseFilePath = SaveContext.GetFilename();

				const int32 HeaderSize = Linker->Summary.TotalHeaderSize;
				SavePackageContext->PackageStoreWriter->WriteHeader(HeaderInfo, FIoBuffer(IoBuffer.Data(), HeaderSize, IoBuffer));

				const uint8* ExportsData = IoBuffer.Data() + HeaderSize;
				const int32 ExportCount = Linker->ExportMap.Num();

				ExportsInfo.Exports.Reserve(ExportCount);
				ExportsInfo.RegionsOffset = HeaderSize;

				for (const FObjectExport& Export : Linker->ExportMap)
				{
					ExportsInfo.Exports.Add(FIoBuffer(IoBuffer.Data() + Export.SerialOffset, Export.SerialSize, IoBuffer));
				}
				SavePackageContext->PackageStoreWriter->WriteExports(ExportsInfo, FIoBuffer(ExportsData, DataSize - HeaderSize, IoBuffer), Linker->FileRegions);
			}
			else
			{
				EAsyncWriteOptions WriteOptions(EAsyncWriteOptions::WriteFileToDisk);
				if (SaveContext.IsComputeHash())
				{
					WriteOptions |= EAsyncWriteOptions::ComputeHash;
				}
				if (SaveContext.IsCooking())
				{
					SavePackageUtilities::AsyncWriteFileWithSplitExports(SaveContext.AsyncWriteAndHashSequence, FLargeMemoryPtr(Writer->ReleaseOwnership()), DataSize, Linker->Summary.TotalHeaderSize, *PathToSave, WriteOptions, Linker->FileRegions);
				}
				else
				{
					SavePackageUtilities::AsyncWriteFile(SaveContext.AsyncWriteAndHashSequence, FLargeMemoryPtr(Writer->ReleaseOwnership()), DataSize, *PathToSave, WriteOptions, Linker->FileRegions);
				}
			}
			SaveContext.CloseLinkerArchives();
		}
	}
	else
	{
		// Destroy archives used for saving, closing file handle.
		bool bSuccess = SaveContext.CloseLinkerArchives();

		if (!bSuccess)
		{
			UE_LOG(LogSavePackage, Error, TEXT("Error writing temp file '%s' for '%s'"),
				SaveContext.TempFilename.IsSet() ? *SaveContext.TempFilename.GetValue() : TEXT("UNKNOWN"), SaveContext.GetFilename());
			return ESavePackageResult::Error;
		}

		// Move file to its real destination
		check(SaveContext.TempFilename.IsSet());
		if (SaveContext.IsTextFormat())
		{
			check(SaveContext.TextFormatTempFilename.IsSet());
			IFileManager::Get().Delete(*SaveContext.TempFilename.GetValue());
			SaveContext.TempFilename = SaveContext.TextFormatTempFilename;
			SaveContext.TextFormatTempFilename.Reset();
		}

		UE_LOG(LogSavePackage, Log, TEXT("Moving '%s' to '%s'"), SaveContext.TempFilename.IsSet() ? *SaveContext.TempFilename.GetValue() : TEXT("UNKNOWN"), SaveContext.GetFilename());
		bSuccess = IFileManager::Get().Move(SaveContext.GetFilename(), *SaveContext.TempFilename.GetValue());
		SaveContext.TempFilename.Reset();

		if (!bSuccess)
		{
			if (SaveContext.IsGenerateSaveError())
			{
				UE_LOG(LogSavePackage, Warning, TEXT("%s"), *FString::Printf(TEXT("Error saving '%s'"), SaveContext.GetFilename()));
			}
			else
			{
				UE_LOG(LogSavePackage, Error, TEXT("%s"), *FString::Printf(TEXT("Error saving '%s'"), SaveContext.GetFilename()));
				SaveContext.GetError()->Logf(ELogVerbosity::Warning, TEXT("%s"), *FText::Format(NSLOCTEXT("Core", "SaveWarning", "Error saving '{0}'"), FText::FromString(FString(SaveContext.GetFilename()))).ToString());
			}
			return ESavePackageResult::Error;
		}

		if (SaveContext.GetFinalTimestamp() != FDateTime::MinValue())
		{
			IFileManager::Get().SetTimeStamp(SaveContext.GetFilename(), SaveContext.GetFinalTimestamp());
		}

		if (SaveContext.IsComputeHash())
		{
			SavePackageUtilities::IncrementOutstandingAsyncWrites();
			SaveContext.AsyncWriteAndHashSequence.AddWork([NewPath = FString(SaveContext.GetFilename())](FMD5& State)
			{
				SavePackageUtilities::AddFileToHash(NewPath, State);
				SavePackageUtilities::DecrementOutstandingAsyncWrites();
			});
		}
	}

	return ESavePackageResult::Success;
}

void BeginCachePlatformCookedData(FSaveContext& SaveContext)
{
#if WITH_EDITOR
	// Cache platform cooked data
	if (SaveContext.IsCooking() && !SaveContext.IsConcurrent())
	{
		for (FTaggedExport& Export : SaveContext.GetExports())
		{
			Export.Obj->BeginCacheForCookedPlatformData(SaveContext.GetTargetPlatform());
		}
	}
#endif
}

void ClearCachedPlatformCookedData(FSaveContext& SaveContext)
{
#if WITH_EDITOR
	if (SaveContext.IsCooking() && !SaveContext.IsConcurrent())
	{
		for (FTaggedExport& Export : SaveContext.GetExports())
		{
			Export.Obj->ClearCachedCookedPlatformData(SaveContext.GetTargetPlatform());

		}
	}
#endif
}

/**
 * InnerSave is the portion of Save that can be safely run concurrently
 */
ESavePackageResult InnerSave(FSaveContext& SaveContext)
{
	TRefCountPtr<FUObjectSerializeContext> SerializeContext(FUObjectThreadContext::Get().GetSerializeContext());
	SaveContext.SetSerializeContext(SerializeContext);
	SaveContext.SetEDLCookChecker(&FEDLCookChecker::Get());

	// Create slow task dialog if needed
	const int32 TotalSaveSteps = 12;
	FScopedSlowTask SlowTask(TotalSaveSteps, FText(), SaveContext.IsUsingSlowTask());
	SlowTask.MakeDialog(SaveContext.IsFromAutoSave());

	// Harvest Package
	SlowTask.EnterProgressFrame();
	SaveContext.Result = HarvestPackage(SaveContext);
	if (SaveContext.Result != ESavePackageResult::Success)
	{
		return SaveContext.Result;
	}

	// Validate Exports
	SlowTask.EnterProgressFrame();
	SaveContext.Result = ValidateExports(SaveContext);
	if (SaveContext.Result != ESavePackageResult::Success)
	{
		return SaveContext.Result;
	}

	// Validate Imports
	SlowTask.EnterProgressFrame();
	SaveContext.Result = ValidateImports(SaveContext);
	if (SaveContext.Result != ESavePackageResult::Success)
	{
		return SaveContext.Result;
	}

	// Trigger platform cooked data caching
	BeginCachePlatformCookedData(SaveContext);

	// Create Linker
	SlowTask.EnterProgressFrame();
	SaveContext.Result = CreateLinker(SaveContext);
	if (SaveContext.Result != ESavePackageResult::Success)
	{
		return SaveContext.Result;
	}

	// Build Linker
	SlowTask.EnterProgressFrame();
	SaveContext.Result = BuildLinker(SaveContext);
	if (SaveContext.Result != ESavePackageResult::Success)
	{
		return SaveContext.Result;
	}

	FStructuredArchive::FRecord StructuredArchiveRoot = SaveContext.StructuredArchive->Open().EnterRecord();
	StructuredArchiveRoot.GetUnderlyingArchive().SetSerializeContext(SaveContext.GetSerializeContext());

	// Write Header
	SlowTask.EnterProgressFrame();
	SaveContext.Result = !SaveContext.IsTextFormat() ? WritePackageHeader(StructuredArchiveRoot, SaveContext) : WritePackageTextHeader(StructuredArchiveRoot, SaveContext);
	if (SaveContext.Result != ESavePackageResult::Success)
	{
		return SaveContext.Result;
	}

	// SHA Generation
	TArray<uint8>* ScriptSHABytes = nullptr;
	{
		// look for this package in the list of packages to generate script SHA for 
		ScriptSHABytes = FLinkerSave::PackagesToScriptSHAMap.Find(*FPaths::GetBaseFilename(SaveContext.GetFilename()));
		// if we want to generate the SHA key, start tracking script writes
		if (ScriptSHABytes)
		{
			SaveContext.Linker->StartScriptSHAGeneration();
		}
	}

	// Write Exports
	SlowTask.EnterProgressFrame();
	SaveContext.Result = WriteExports(StructuredArchiveRoot, SaveContext);
	if (SaveContext.Result != ESavePackageResult::Success)
	{
		return SaveContext.Result;
	}
	// Get SHA Key
	{
		// if we want to generate the SHA key, get it out now that the package has finished saving
		if (ScriptSHABytes && SaveContext.Linker->ContainsCode())
		{
			// make space for the 20 byte key
			ScriptSHABytes->Empty(20);
			ScriptSHABytes->AddUninitialized(20);

			// retrieve it
			SaveContext.Linker->GetScriptSHAKey(ScriptSHABytes->GetData());
		}
	}

	// Save Bulk Data
	SlowTask.EnterProgressFrame();
	SavePackageUtilities::SaveBulkData(SaveContext.Linker.Get(), SaveContext.GetPackage(), SaveContext.GetFilename(), SaveContext.GetTargetPlatform(), SaveContext.GetSavePackageContext(), SaveContext.IsTextFormat(), SaveContext.IsDiffing(),
		SaveContext.IsComputeHash(), SaveContext.AsyncWriteAndHashSequence, SaveContext.TotalPackageSizeUncompressed);

	// Write Additional files from export
	SlowTask.EnterProgressFrame();
	SaveContext.Result = WriteAdditionalExportFiles(SaveContext);
	if (SaveContext.Result != ESavePackageResult::Success)
	{
		return SaveContext.Result;
	}
	
	// Write Package Post Tag
	if (!SaveContext.IsTextFormat())
	{
		uint32 Tag = PACKAGE_FILE_TAG;
		StructuredArchiveRoot.GetUnderlyingArchive() << Tag;
	}

	// Capture Package Size
	int32 PackageSize = SaveContext.Linker->Tell();
	SaveContext.TotalPackageSizeUncompressed += PackageSize;

	// Update Package Header
	SlowTask.EnterProgressFrame();
	SaveContext.Result = UpdatePackageHeader(StructuredArchiveRoot, SaveContext);
	if (SaveContext.Result != ESavePackageResult::Success)
	{
		return SaveContext.Result;
	}

	// Finalize File Write
	SlowTask.EnterProgressFrame();
	SaveContext.Result = FinalizeFile(StructuredArchiveRoot, SaveContext);
	if (SaveContext.Result != ESavePackageResult::Success)
	{
		return SaveContext.Result;
	}

	//COOK_STAT(FSavePackageStats::MBWritten += ((double)SaveContext.TotalPackageSizeUncompressed) / 1024.0 / 1024.0);

	// Mark Exports & Package RF_Loaded
	SlowTask.EnterProgressFrame();
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_Save_MarkExportLoaded);
		FLinkerSave* Linker = SaveContext.Linker.Get();
		// Mark exports and the package as RF_Loaded after they've been serialized
		// This is to ensue that newly created packages are properly marked as loaded (since they now exist on disk and 
		// in memory in the exact same state).

 		// Nobody should be touching those objects beside us while we are saving them here as this can potentially be executed from another thread
		for (auto& Export : Linker->ExportMap)
		{
			if (Export.Object)
			{
				Export.Object->SetFlags(RF_WasLoaded | RF_LoadCompleted);
			}
		}
		if (Linker->LinkerRoot)
		{
			// And finally set the flag on the package itself.
			Linker->LinkerRoot->SetFlags(RF_WasLoaded | RF_LoadCompleted);
		}

		// Clear dirty flag if desired
		if (!SaveContext.IsKeepDirty())
		{
			SaveContext.GetPackage()->SetDirtyFlag(false);
		}

		// Update package FileSize value
		SaveContext.GetPackage()->FileSize = PackageSize;
	}
	return SaveContext.Result;
}

FText GetSlowTaskStatusMessage(const FSaveContext& SaveContext)
{
	FString CleanFilename = FPaths::GetCleanFilename(SaveContext.GetFilename());
	FFormatNamedArguments Args;
	Args.Add(TEXT("CleanFilename"), FText::FromString(CleanFilename));
	return FText::Format(NSLOCTEXT("Core", "SavingFile", "Saving file: {CleanFilename}..."), Args);
}

FSavePackageResultStruct UPackage::Save2(UPackage* InPackage, UObject* InAsset, const TCHAR* InFilename, FSavePackageArgs& SaveArgs)
{
	//COOK_STAT(FScopedDurationTimer FuncSaveTimer(FSavePackageStats::SavePackageTimeSec));
	//COOK_STAT(FSavePackageStats::NumPackagesSaved++);
	TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_Save2);
	FSaveContext SaveContext(InPackage, InAsset, InFilename, SaveArgs);

	// Create the slow task dialog if needed
	const int32 TotalSaveSteps = 7;
	FScopedSlowTask SlowTask(TotalSaveSteps, GetSlowTaskStatusMessage(SaveContext), SaveContext.IsUsingSlowTask());
	SlowTask.MakeDialog(SaveContext.IsFromAutoSave());

	SlowTask.EnterProgressFrame();
	SaveContext.Result = ValidatePackage(SaveContext);
	if (SaveContext.Result != ESavePackageResult::Success)
	{
		return SaveContext.Result;
	}

	// Ensures
	SlowTask.EnterProgressFrame();
	EnsurePackageLocalization(SaveContext.GetPackage());
	{
		// FullyLoad the package's Loader, so that anything we need to serialize (bulkdata, thumbnails) is available
		//COOK_STAT(FScopedDurationTimer SaveTimer(FSavePackageStats::FullyLoadLoadersTimeSec));
		EnsureLoadingComplete(SaveContext.GetPackage());

		if (!SaveContext.IsConcurrent())
		{
			// We need to fulfill all pending streaming and async loading requests to then allow us to lock the global IO manager. 
			// The latter implies flushing all file handles which is a pre-requisite of saving a package. The code basically needs 
			// to be sure that we are not reading from a file that is about to be overwritten and that there is no way we might 
			// start reading from the file till we are done overwriting it.
			FlushAsyncLoading();
		}
		(*GFlushStreamingFunc)();
	}

	// PreSave Asset
	SlowTask.EnterProgressFrame();
	if (InAsset)
	{
		SaveContext.SetPreSaveCleanup(InAsset->PreSaveRoot(InFilename));
	}

	// Route Presave only if not calling concurrently or diffing, in those case they should be handled separately
	SlowTask.EnterProgressFrame();
	if (!SaveContext.IsConcurrent() && !SaveContext.IsDiffing())
	{
		SaveContext.Result = RoutePresave(SaveContext);
		if (SaveContext.Result != ESavePackageResult::Success)
		{
			return SaveContext.Result;
		}
	}

	SlowTask.EnterProgressFrame();
	{
		FScopedSavingFlag IsSavingFlag(SaveContext.IsConcurrent());
		SaveContext.Result = InnerSave(SaveContext);
		if (SaveContext.Result != ESavePackageResult::Success)
		{
			return SaveContext.Result;
		}
	}

	// PostSave Asset
	SlowTask.EnterProgressFrame();
	if (InAsset)
	{
		InAsset->PostSaveRoot(SaveContext.GetPreSaveCleanup());
		SaveContext.SetPreSaveCleanup(false);
	}

	ClearCachedPlatformCookedData(SaveContext);

	// Package Saved event
	SlowTask.EnterProgressFrame();
	{
		// Package has been save, so unmark NewlyCreated flag.
		InPackage->ClearPackageFlags(PKG_NewlyCreated);

		// send a message that the package was saved
		UPackage::PackageSavedEvent.Broadcast(InFilename, InPackage);
	}
	return SaveContext.GetFinalResult();
}


ESavePackageResult UPackage::SaveConcurrent(TArrayView<FPackageSaveInfo> InPackages, FSavePackageArgs& SaveArgs, TArray<FSavePackageResultStruct>& OutResults)
{
	auto GetPackageAsset = [](FPackageSaveInfo& PackageSaveInfo) -> UObject*
	{
		UObject* Asset = nullptr;
		ForEachObjectWithPackage(PackageSaveInfo.Package, [&Asset](UObject* Object)
			{
				if (Object->IsAsset())
				{
					Asset = Object;
					return false;
				}
				return true;
			}, /*bIncludeNestedObjects*/ false);
		return Asset;
	};

	const int32 TotalSaveSteps = 4;
	FScopedSlowTask SlowTask(TotalSaveSteps, NSLOCTEXT("Core", "SavingFiles", "Saving files..."), SaveArgs.bSlowTask);
	SlowTask.MakeDialog(!!(SaveArgs.SaveFlags & SAVE_FromAutosave));

	// Create all the package save context and run pre save
	SlowTask.EnterProgressFrame();
	TArray<FSaveContext> PackageSaveContexts;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_SaveConcurrent_PreSave);
		for (FPackageSaveInfo& PackageSaveInfo : InPackages)
		{
			FSaveContext& SaveContext = PackageSaveContexts.Emplace_GetRef(PackageSaveInfo.Package, GetPackageAsset(PackageSaveInfo), *PackageSaveInfo.Filename, SaveArgs, nullptr);

			// Validation
			SaveContext.Result = ValidatePackage(SaveContext);
			if (SaveContext.Result != ESavePackageResult::Success)
			{
				continue;
			}

			// Ensures
			EnsurePackageLocalization(SaveContext.GetPackage());
			EnsureLoadingComplete(SaveContext.GetPackage()); //@todo: needed??

			// PreSave Asset
			if (SaveContext.GetAsset())
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_SaveConcurrent_PreSaveRoot);
				SaveContext.SetPreSaveCleanup(SaveContext.GetAsset()->PreSaveRoot(SaveContext.GetFilename()));
			}

			// Route Presave
			SaveContext.Result = RoutePresave(SaveContext);
			if (SaveContext.Result != ESavePackageResult::Success)
			{
				continue;
			}
		}
	}

	SlowTask.EnterProgressFrame();
	{
		// Flush async loading and reset loaders
		TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_SaveConcurrent_ResetLoadersForSave);
		ResetLoadersForSave(InPackages);
	}

	SlowTask.EnterProgressFrame();
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_SaveConcurrent);

		// Use concurrent new save only if new save is enabled, otherwise use old save
		static const IConsoleVariable* EnableNewSave = IConsoleManager::Get().FindConsoleVariable(TEXT("SavePackage.EnableNewSave"));
		if (EnableNewSave->GetInt())
		{
			// Passing in false here so that GIsSavingPackage is set to true on top of locking the GC
			FScopedSavingFlag IsSavingFlag(false);

			// Concurrent Part
			ParallelFor(PackageSaveContexts.Num(), [&PackageSaveContexts](int32 PackageIdx)
				{
					InnerSave(PackageSaveContexts[PackageIdx]);
				});
		}
		else
		{
			GIsSavingPackage = true;
			ParallelFor(PackageSaveContexts.Num(), [&PackageSaveContexts](int32 PackageIdx)
				{
					FSaveContext& SaveContext = PackageSaveContexts[PackageIdx];
					const FSavePackageArgs& SaveArgs = SaveContext.GetSaveArgs();
					Save(SaveContext.GetPackage(), SaveContext.GetAsset(), SaveArgs.TopLevelFlags, SaveContext.GetFilename(),
						SaveArgs.Error, nullptr, SaveArgs.bForceByteSwapping, SaveArgs.bWarnOfLongFilename, SaveArgs.SaveFlags|SAVE_Concurrent,
						SaveArgs.TargetPlatform, SaveArgs.FinalTimeStamp, false, nullptr, nullptr);
				});
			GIsSavingPackage = false;
		}
	}

	// Run Post Concurrent Save
	SlowTask.EnterProgressFrame();
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(UPackage_SaveConcurrent_PostSave);
		for (FSaveContext& SaveContext : PackageSaveContexts)
		{
			// PostSave Asset
			if (SaveContext.GetAsset())
			{
				SaveContext.GetAsset()->PostSaveRoot(SaveContext.GetPreSaveCleanup());
				SaveContext.SetPreSaveCleanup(false);
			}

			ClearCachedPlatformCookedData(SaveContext);

			// Package Saved event
			if (SaveContext.Result == ESavePackageResult::Success)
			{
				// Package has been save, so unmark NewlyCreated flag.
				SaveContext.GetPackage()->ClearPackageFlags(PKG_NewlyCreated);

				// send a message that the package was saved
				UPackage::PackageSavedEvent.Broadcast(SaveContext.GetFilename(), SaveContext.GetPackage());
			}
			OutResults.Add(SaveContext.GetFinalResult());
		}
	}
	
	return ESavePackageResult::Success;
}

#endif // UE_WITH_SAVEPACKAGE
