// Copyright Epic Games, Inc. All Rights Reserved.


#include "UObject/SavePackage/PackageHarvester.h"

#include "UObject/SavePackage/SaveContext.h"
#include "UObject/SavePackage/SavePackageUtilities.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"

// bring the UObectGlobal declaration visible to non editor
bool IsEditorOnlyObject(const UObject* InObject, bool bCheckRecursive, bool bCheckMarks);

EObjectMark GenerateMarksForObject(const UObject* InObject, const ITargetPlatform* TargetPlatform)
{
	EObjectMark Marks = OBJECTMARK_NOMARKS;

	// CDOs must be included if their class are, so do not generate any marks for it here, defer exclusion to their outer and class
	if (InObject->HasAnyFlags(RF_ClassDefaultObject))
	{
		return Marks;
	}

	if (!InObject->NeedsLoadForClient())
	{
		Marks = (EObjectMark)(Marks | OBJECTMARK_NotForClient);
	}

	if (!InObject->NeedsLoadForServer())
	{
		Marks = (EObjectMark)(Marks | OBJECTMARK_NotForServer);
	}

	if ((!(Marks & OBJECTMARK_NotForServer) || !(Marks & OBJECTMARK_NotForClient)) && TargetPlatform && !InObject->NeedsLoadForTargetPlatform(TargetPlatform))
	{
		Marks = (EObjectMark)(Marks | OBJECTMARK_NotForClient | OBJECTMARK_NotForServer);
	}
	// CDOs must be included if their class is so only inherit marks, for everything else we check the native overrides as well
	if (IsEditorOnlyObject(InObject, false, false))
	{
		Marks = (EObjectMark)(Marks | OBJECTMARK_EditorOnly);
	}
	else
	// If NotForClient and NotForServer, it is implicitly editor only
	if ((Marks & OBJECTMARK_NotForClient) && (Marks & OBJECTMARK_NotForServer))
	{
		Marks = (EObjectMark)(Marks | OBJECTMARK_EditorOnly);
	}

	return Marks;
}

bool ConditionallyExcludeObjectForTarget(FSaveContext& SaveContext, UObject* Obj)
{
	if (!Obj || Obj->GetOutermost()->GetFName() == GLongCoreUObjectPackageName)
	{
		// No object or in CoreUObject, don't exclude
		return false;
	}

	bool bExcluded = false;
	if (SaveContext.IsExcluded(Obj))
	{
		return true;
	}
	else if (!SaveContext.IsIncluded(Obj))
	{
		const EObjectMark ExcludedObjectMarks = SaveContext.GetExcludedObjectMarks();
		const ITargetPlatform* TargetPlatform = SaveContext.GetTargetPlatform();
		EObjectMark ObjectMarks = GenerateMarksForObject(Obj, TargetPlatform);
		if (!(ObjectMarks & ExcludedObjectMarks))
		{
			UObject* ObjOuter = Obj->GetOuter();
			UClass* ObjClass = Obj->GetClass();

			if (TargetPlatform)
			{
				FName UnusedName;
				SavePackageUtilities::GetBlueprintNativeCodeGenReplacement(Obj, ObjClass, ObjOuter, UnusedName, TargetPlatform);
			}

			if (ConditionallyExcludeObjectForTarget(SaveContext, ObjClass))
			{
				// If the object class is excluded, the object must be excluded too
				bExcluded = true;
			}
			else if (ConditionallyExcludeObjectForTarget(SaveContext, ObjOuter))
			{
				// If the object outer is excluded, the object must be excluded too
				bExcluded = true;
			}

			// Check parent struct if we have one
			UStruct* ThisStruct = Cast<UStruct>(Obj);
			if (ThisStruct && ThisStruct->GetSuperStruct())
			{
				UObject* SuperStruct = ThisStruct->GetSuperStruct();
				if (ConditionallyExcludeObjectForTarget(SaveContext, SuperStruct))
				{
					bExcluded = true;
				}
			}

			// Check archetype, this may not have been covered in the case of components
			UObject* Archetype = Obj->GetArchetype();
			if (Archetype)
			{
				if (ConditionallyExcludeObjectForTarget(SaveContext, Archetype))
				{
					bExcluded = true;
				}
			}
		}
		else
		{
			bExcluded = true;
		}
		if (bExcluded)
		{
			SaveContext.AddExcluded(Obj);
		}
	}
	return bExcluded;
}

bool DoesObjectNeedLoadForEditorGame(UObject* InObject)
{
	check(InObject);
	bool bNeedsLoadForEditorGame = false;
	// NeedsLoadForEditor game is inherited to child objects, so check outer chain
	UObject* Outer = InObject;
	while (Outer && !bNeedsLoadForEditorGame)
	{
		bNeedsLoadForEditorGame = Outer->NeedsLoadForEditorGame();
		Outer = Outer->GetOuter();
	}

	if (InObject->HasAnyFlags(RF_ClassDefaultObject))
	{
		bNeedsLoadForEditorGame = bNeedsLoadForEditorGame || InObject->GetClass()->NeedsLoadForEditorGame();
	}
	return bNeedsLoadForEditorGame;
}

FPackageHarvester::FPackageHarvester(FSaveContext& InContext)
	: SaveContext(InContext)
	, bIsEditorOnlyExportOnStack(false)
{
	this->SetIsSaving(true);
	this->SetIsPersistent(true);
	ArIsObjectReferenceCollector = true;
	ArShouldSkipBulkData = true;

	this->SetPortFlags(SaveContext.GetPortFlags());
	this->SetFilterEditorOnly(SaveContext.IsFilterEditorOnly());
	this->SetCookingTarget(SaveContext.GetTargetPlatform());
	this->SetSerializeContext(SaveContext.GetSerializeContext());
}

UObject* FPackageHarvester::PopExportToProcess()
{
	UObject* Export = nullptr;
	ExportsToProcess.Dequeue(Export);
	return Export;
}

void FPackageHarvester::ProcessExport(UObject* InObject)
{
	check(SaveContext.IsExport(InObject));
	bool bReferencerIsEditorOnly = IsEditorOnlyObject(InObject, true /* bCheckRecursive */, true /* bCheckMarks */) && !InObject->HasNonEditorOnlyReferences();
	FExportScope HarvesterScope(*this, InObject, bReferencerIsEditorOnly);

	// Harvest its class 
	UClass* Class = InObject->GetClass();
	*this << Class;

	// Harvest the export outer
	if (UObject* Outer = InObject->GetOuter())
	{
		if (!Outer->IsInPackage(SaveContext.GetPackage()))
		{
			*this << Outer;
		}
		else
		{
			// Legacy behavior does not add an export outer as a preload dependency if that outer is also an export since those are handled already by the EDL
			FIgnoreDependenciesScope IgnoreDependencies(*this);
			*this << Outer;
		}
	}

	// Harvest its template, if any
	UObject* Template = InObject->GetArchetype();
	if (Template
		 && (Template != Class->GetDefaultObject() || SaveContext.IsCooking())
		)
	{
		*this << Template;
	}

	// Serialize the object or CDO
	if (InObject->HasAnyFlags(RF_ClassDefaultObject))
	{
		Class->SerializeDefaultObject(InObject, *this);
		//@ todo FH: I don't think recursing into the template subobject is necessary, serializing it should catch the necessary sub objects
		// GetCDOSubobjects??
	}
	else
	{
		// @todo FH: always serialize???
		// In the CDO case the above would serialize most of the references, including transient properties
		// but we still want to serialize the object using the normal path to collect all custom versions it might be using.
		InObject->Serialize(*this);
	}

	// Gather object preload dependencies
	if (SaveContext.IsCooking())
	{
		TArray<UObject*> Deps;
		{
			// We want to tag these as imports, but not as dependencies, here since they are handled separately to the the DependsMap as SerializationBeforeSerializationDependencies instead of CreateBeforeSerializationDependencies 
			FIgnoreDependenciesScope IgnoreDependencies(*this);

			InObject->GetPreloadDependencies(Deps);
			for (UObject* Dep : Deps)
			{
				// We assume nothing in coreuobject ever loads assets in a constructor
				if (Dep && Dep->GetOutermost()->GetFName() != GLongCoreUObjectPackageName)
				{
					*this << Dep;
				}
			}
		}

		//@todo FH: Is this even useful anymore!
		if (SaveContext.IsProcessingPrestreamingRequests())
		{
			Deps.Reset();
			InObject->GetPrestreamPackages(Deps);
			for (UObject* Dep : Deps)
			{
				if (Dep)
				{
					UPackage* Pkg = Dep->GetOutermost();
					if (ensureAlways(!Pkg->HasAnyPackageFlags(PKG_CompiledIn)))
					{
						SaveContext.AddPrestreamPackages(Pkg);
					}
				}
			}
		}
	}
}

void FPackageHarvester::TryHarvestExport(UObject* InObject)
{
	// Those should have been already validated
	check(InObject && InObject->IsInPackage(SaveContext.GetPackage()));
	if (!SaveContext.IsExport(InObject))
	{
		SaveContext.MarkUnsaveable(InObject);
		bool bExcluded = false;
		if (!InObject->HasAnyFlags(RF_Transient))
		{
			bExcluded = ConditionallyExcludeObjectForTarget(SaveContext, InObject);
		}
		if (!InObject->HasAnyFlags(RF_Transient) && !bExcluded)
		{
			// It passed filtering so mark as export
			SaveContext.AddExport(InObject, !DoesObjectNeedLoadForEditorGame(InObject));

			// Harvest the export name
			HarvestName(InObject->GetFName());

			ExportsToProcess.Enqueue(InObject);
		}
	}
}

void FPackageHarvester::TryHarvestImport(UObject* InObject)
{
	// Those should have been already validated
	check(InObject);
	check(!InObject->IsInPackage(SaveContext.GetPackage()));
	
	auto IsObjNative = [](UObject* InObj)
	{
		bool bIsNative = InObj->IsNative();
		UObject* Outer = InObj->GetOuter();
		while (!bIsNative && Outer)
		{
			bIsNative |= Cast<UClass>(Outer) != nullptr && Outer->IsNative();
			Outer = Outer->GetOuter();
		}
		return bIsNative;
	};

	bool bExcluded = ConditionallyExcludeObjectForTarget(SaveContext, InObject);
	bool bExcludePackageFromCook = InObject && FCoreUObjectDelegates::ShouldCookPackageForPlatform.IsBound() ? !FCoreUObjectDelegates::ShouldCookPackageForPlatform.Execute(InObject->GetOutermost(), CookingTarget()) : false;
	if (!bExcludePackageFromCook && !bExcluded && !SaveContext.IsUnsaveable(InObject))
	{
		bool bIsNative = IsObjNative(InObject);
		SaveContext.AddImport(InObject);
#if WITH_EDITORONLY_DATA
		if (!bIsEditorOnlyExportOnStack && !IsEditorOnlyPropertyOnTheStack())
#endif
		{
			SaveContext.ImportsUsedInGame.Add(InObject);
		}

		UObject* ObjOuter = InObject->GetOuter();
		UClass* ObjClass = InObject->GetClass();
		FName ObjName = InObject->GetFName();
		if (SaveContext.IsCooking())
		{
			// The ignore dependencies check is is necessary not to have infinite recursive calls
			if (!bIsNative && !CurrentExportDependencies.bIgnoreDependencies)
			{
				UClass* ClassObj = Cast<UClass>(InObject);
				UObject* CDO = ClassObj ? ClassObj->GetDefaultObject() : nullptr;
				if (CDO)
				{
					FIgnoreDependenciesScope IgnoreDependencies(*this);

					// Gets all subobjects defined in a class, including the CDO, CDO components and blueprint-created components
					TArray<UObject*> ObjectTemplates;
					ObjectTemplates.Add(CDO);
					SavePackageUtilities::GetCDOSubobjects(CDO, ObjectTemplates);
					for (UObject* ObjTemplate : ObjectTemplates)
					{
						// Recurse into templates
						*this << ObjTemplate;
					}
				}
			}
			
			// @todo FH: Why no code gen replacement here in the old save?
			UClass* DummyClassPtr = nullptr;
			SavePackageUtilities::GetBlueprintNativeCodeGenReplacement(InObject, DummyClassPtr, ObjOuter, ObjName, CookingTarget());
		}

		// Harvest the import name
		HarvestName(ObjName);

		// Recurse into outer, package override and non native class
		if (ObjOuter)
		{
			*this << ObjOuter;
		}
		UPackage* Package = InObject->GetExternalPackage();
		if (Package && Package != InObject)
		{
			*this << Package;
		}
		// For things with a BP-created class we need to recurse into that class so the import ClassPackage will load properly
		// We don't do this for native classes to avoid bloating the import table, but we need to harvest their name and outer (package) name
		if (!ObjClass->IsNative())
		{
			*this << ObjClass; 
		}	
		else
		{
			HarvestName(ObjClass->GetFName());
			HarvestName(ObjClass->GetOuter()->GetFName());
		}
	}
}

FString FPackageHarvester::GetArchiveName() const
{
	return FString::Printf(TEXT("PackageHarvester (%s)"), *SaveContext.GetPackage()->GetName());
}

void FPackageHarvester::MarkSearchableName(const UObject* TypeObject, const FName& ValueName) const
{
	if (TypeObject == nullptr)
	{
		return;
	}

	// Serialize object to make sure it ends up in import table
	// This is doing a const cast to avoid backward compatibility issues
	UObject* TempObject = const_cast<UObject*>(TypeObject);
	FPackageHarvester* MutableArchive = const_cast<FPackageHarvester*>(this);
	MutableArchive->HarvestSearchableName(TempObject, ValueName);
}

FArchive& FPackageHarvester::operator<<(UObject*& Obj)
{	
	// if the object is null or already marked excluded, we can skip the harvest
	if (!Obj || SaveContext.IsExcluded(Obj))
	{
		return *this;
	}

	// if the package we are saving is referenced, just harvest its name
	if (Obj == SaveContext.GetPackage())
	{
		HarvestName(Obj->GetFName());
		return *this;
	}

	// if the object is in the save context package, try to tag it as export
	if (Obj->IsInPackage(SaveContext.GetPackage()))
	{
		TryHarvestExport(Obj);
	}
	// Otherwise visit the import
	else
	{
		TryHarvestImport(Obj);
	}

	auto IsObjNative = [](UObject* InObj)
	{
		bool bIsNative = InObj->IsNative();
		UObject* Outer = InObj->GetOuter();
		while (!bIsNative && Outer)
		{
			bIsNative |= Cast<UClass>(Outer) != nullptr && Outer->IsNative();
			Outer = Outer->GetOuter();
		}
		return bIsNative;
	};

	if (SaveContext.IsIncluded(Obj))
	{
		HarvestDependency(Obj, IsObjNative(Obj));
	}

	return *this;
}

FArchive& FPackageHarvester::operator<<(struct FWeakObjectPtr& Value)
{
	// @todo FH: Should we really force weak import in cooked builds?
	if (IsCooking())
	{
		// Always serialize weak pointers for the purposes of object tagging
		UObject* Object = static_cast<UObject*>(Value.Get(true));
		*this << Object;
	}
	else
	{
		FArchiveUObject::SerializeWeakObjectPtr(*this, Value);
	}
	return *this;
}
FArchive& FPackageHarvester::operator<<(FLazyObjectPtr& LazyObjectPtr)
{
	// @todo FH: Does this really do anything as far as tagging goes?
	FUniqueObjectGuid ID;
	ID = LazyObjectPtr.GetUniqueID();
	return *this << ID;
}

FArchive& FPackageHarvester::operator<<(FSoftObjectPath& Value)
{
	if (Value.IsValid())
	{
		Value.SerializePath(*this);

		FSoftObjectPathThreadContext& ThreadContext = FSoftObjectPathThreadContext::Get();
		FName ReferencingPackageName, ReferencingPropertyName;
		ESoftObjectPathCollectType CollectType = ESoftObjectPathCollectType::AlwaysCollect;
		ESoftObjectPathSerializeType SerializeType = ESoftObjectPathSerializeType::AlwaysSerialize;

		ThreadContext.GetSerializationOptions(ReferencingPackageName, ReferencingPropertyName, CollectType, SerializeType, this);

		if (CollectType != ESoftObjectPathCollectType::NeverCollect)
		{
			// Don't track if this is a never collect path
			FString Path = Value.ToString();
			FName PackageName = FName(*FPackageName::ObjectPathToPackageName(Path));
			HarvestName(PackageName);
			SaveContext.SoftPackageReferenceList.AddUnique(PackageName);
#if WITH_EDITORONLY_DATA
			if (CollectType != ESoftObjectPathCollectType::EditorOnlyCollect && !bIsEditorOnlyExportOnStack)
#endif
			{
				SaveContext.SoftPackagesUsedInGame.Add(PackageName);
			}
		}
	}
	return *this;
}

FArchive& FPackageHarvester::operator<<(FName& Name)
{
	HarvestName(Name);
	return *this;
}

void FPackageHarvester::HarvestDependency(UObject* InObj, bool bIsNative)
{
	// if we aren't currently processing an export or the referenced object is a package, do not harvest the dependency
	if (CurrentExportDependencies.bIgnoreDependencies ||
		CurrentExportDependencies.CurrentExport == nullptr ||
		(InObj->GetOuter() == nullptr && InObj->GetClass()->GetFName() == NAME_Package))
	{
		return;
	}

	if (bIsNative)
	{
		CurrentExportDependencies.NativeObjectReferences.Add(InObj);
	}
	else
	{
		CurrentExportDependencies.ObjectReferences.Add(InObj);
	}
}

bool FPackageHarvester::CurrentExportHasDependency(UObject* InObj) const
{
	return SaveContext.ExportObjectDependencies.Contains(InObj) || SaveContext.ExportNativeObjectDependencies.Contains(InObj);
}

void FPackageHarvester::HarvestName(FName Name)
{
	SaveContext.ReferencedNames.Add(Name.GetDisplayIndex());
}

void FPackageHarvester::HarvestSearchableName(UObject* TypeObject, FName Name)
{
	// Make sure the object is tracked as a dependency
	if (!CurrentExportHasDependency(TypeObject))
	{
		(*this) << TypeObject;
	}

	HarvestName(Name);
	SaveContext.SearchableNamesObjectMap.FindOrAdd(TypeObject).AddUnique(Name);
}

void FPackageHarvester::AppendCurrentExportDependencies()
{
	check(CurrentExportDependencies.CurrentExport);
	SaveContext.ExportObjectDependencies.Add(CurrentExportDependencies.CurrentExport, MoveTemp(CurrentExportDependencies.ObjectReferences));
	SaveContext.ExportNativeObjectDependencies.Add(CurrentExportDependencies.CurrentExport, MoveTemp(CurrentExportDependencies.NativeObjectReferences));
	CurrentExportDependencies.CurrentExport = nullptr;
}