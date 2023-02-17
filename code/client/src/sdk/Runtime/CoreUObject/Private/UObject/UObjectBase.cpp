// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	UObjectBase.cpp: Unreal UObject base class
=============================================================================*/

#include "UObject/UObjectBase.h"
#include "Misc/MessageDialog.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/IConsoleManager.h"
#include "HAL/LowLevelMemTracker.h"
#include "Misc/FeedbackContext.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectAllocator.h"
#include "UObject/UObjectHash.h"
#include "UObject/Class.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Package.h"
#include "Templates/Casts.h"
#include "UObject/GCObject.h"
#include "UObject/LinkerLoad.h"
#include "Misc/CommandLine.h"
#include "Interfaces/IPluginManager.h"
#include "Serialization/LoadTimeTrace.h"

DEFINE_LOG_CATEGORY_STATIC(LogUObjectBase, Log, All);
DEFINE_STAT(STAT_UObjectsStatGroupTester);

DECLARE_CYCLE_STAT(TEXT("CreateStatID"), STAT_CreateStatID, STATGROUP_StatSystem);

DEFINE_LOG_CATEGORY_STATIC(LogUObjectBootstrap, Display, Display);


/** Whether uobject system is initialized.												*/
namespace Internal
{
	static bool& GetUObjectSubsystemInitialised()
	{
		static bool ObjInitialized = false;
		return ObjInitialized;
	}
};
bool UObjectInitialized()
{
	return Internal::GetUObjectSubsystemInitialised();
}

/** Objects to automatically register once the object system is ready.					*/
struct FPendingRegistrantInfo
{
	const TCHAR*	Name;
	const TCHAR*	PackageName;
	FPendingRegistrantInfo(const TCHAR* InName,const TCHAR* InPackageName)
		:	Name(InName)
		,	PackageName(InPackageName)
	{}
	static TMap<UObjectBase*, FPendingRegistrantInfo>& GetMap()
	{
		static TMap<UObjectBase*, FPendingRegistrantInfo> PendingRegistrantInfo;
		return PendingRegistrantInfo;
	}
};


/** Objects to automatically register once the object system is ready.					*/
struct FPendingRegistrant
{
	UObjectBase*	Object;
	FPendingRegistrant*	NextAutoRegister;
	FPendingRegistrant(UObjectBase* InObject)
	:	Object(InObject)
	,	NextAutoRegister(NULL)
	{}
};
static FPendingRegistrant* GFirstPendingRegistrant = NULL;
static FPendingRegistrant* GLastPendingRegistrant = NULL;

#if USE_PER_MODULE_UOBJECT_BOOTSTRAP
static TMap<FName, TArray<FPendingRegistrant*>>& GetPerModuleBootstrapMap()
{
	static TMap<FName, TArray<FPendingRegistrant*>> PendingRegistrantInfo;
	return PendingRegistrantInfo;
}

#endif


/**
 * Constructor used for bootstrapping
 * @param	InClass			possibly NULL, this gives the class of the new object, if known at this time
 * @param	InFlags			RF_Flags to assign
 */
UObjectBase::UObjectBase( EObjectFlags InFlags )
:	ObjectFlags			(InFlags)
,	InternalIndex		(INDEX_NONE)
,	ClassPrivate		(nullptr)
,	OuterPrivate		(nullptr)
{}

/**
 * Constructor used by StaticAllocateObject
 * @param	InClass				non NULL, this gives the class of the new object, if known at this time
 * @param	InFlags				RF_Flags to assign
 * @param	InOuter				outer for this object
 * @param	InName				name of the new object
 * @param	InObjectArchetype	archetype to assign
 */
UObjectBase::UObjectBase(UClass* InClass, EObjectFlags InFlags, EInternalObjectFlags InInternalFlags, UObject *InOuter, FName InName)
:	ObjectFlags			(InFlags)
,	InternalIndex		(INDEX_NONE)
,	ClassPrivate		(InClass)
,	OuterPrivate		(InOuter)
{
	check(ClassPrivate);
	// Add to global table.
	AddObject(InName, InInternalFlags);
}


/**
 * Final destructor, removes the object from the object array, and indirectly, from any annotations
 **/
UObjectBase::~UObjectBase()
{
	// If not initialized, skip out.
	if( UObjectInitialized() && ClassPrivate && !GIsCriticalError )
	{
		// Validate it.
		check(IsValidLowLevel());
		check(GetFName() == NAME_None);
		GUObjectArray.FreeUObjectIndex(this);
	}
}




/**
 * Convert a boot-strap registered class into a real one, add to uobject array, etc
 *
 * @param UClassStaticClass Now that it is known, fill in UClass::StaticClass() as the class
 */
void UObjectBase::DeferredRegister(UClass *UClassStaticClass,const TCHAR* PackageName,const TCHAR* InName)
{
	check(UObjectInitialized());
	// Set object properties.
	UPackage* Package = CreatePackage(PackageName);
	check(Package);
	Package->SetPackageFlags(PKG_CompiledIn);
	OuterPrivate = Package;

	check(UClassStaticClass);
	check(!ClassPrivate);
	ClassPrivate = UClassStaticClass;

	// Add to the global object table.
	AddObject(FName(InName), EInternalObjectFlags::None);
	// At this point all compiled-in objects should have already been fully constructed so it's safe to remove the NotFullyConstructed flag
	// which was set in FUObjectArray::AllocateUObjectIndex (called from AddObject)
	GUObjectArray.IndexToObject(InternalIndex)->ClearFlags(EInternalObjectFlags::PendingConstruction);

	// Make sure that objects disregarded for GC are part of root set.
	check(!GUObjectArray.IsDisregardForGC(this) || GUObjectArray.IndexToObject(InternalIndex)->IsRootSet());

	UE_LOG(LogUObjectBootstrap, Verbose, TEXT("UObjectBase::DeferredRegister %s %s"), PackageName, InName);
}

/**
 * Add a newly created object to the name hash tables and the object array
 *
 * @param Name name to assign to this uobject
 */
void UObjectBase::AddObject(FName InName, EInternalObjectFlags InSetInternalFlags)
{
	NamePrivate = InName;
	EInternalObjectFlags InternalFlagsToSet = InSetInternalFlags;
	if (!IsInGameThread())
	{
		InternalFlagsToSet |= EInternalObjectFlags::Async;
	}
	if (ObjectFlags & RF_MarkAsRootSet)
	{		
		InternalFlagsToSet |= EInternalObjectFlags::RootSet;
		ObjectFlags &= ~RF_MarkAsRootSet;
	}
	if (ObjectFlags & RF_MarkAsNative)
	{
		InternalFlagsToSet |= EInternalObjectFlags::Native;
		ObjectFlags &= ~RF_MarkAsNative;
	}
	GUObjectArray.AllocateUObjectIndex(this);
	check(InName != NAME_None && InternalIndex >= 0);
	if (InternalFlagsToSet != EInternalObjectFlags::None)
	{
		GUObjectArray.IndexToObject(InternalIndex)->SetFlags(InternalFlagsToSet);
	
	}	
	HashObject(this);
	check(IsValidLowLevel());
}

/**
 * Just change the FName and Outer and rehash into name hash tables. For use by higher level rename functions.
 *
 * @param NewName	new name for this object
 * @param NewOuter	new outer for this object, if NULL, outer will be unchanged
 */
void UObjectBase::LowLevelRename(FName NewName,UObject *NewOuter)
{
#if STATS || ENABLE_STATNAMEDEVENTS_UOBJECT
	((UObject*)this)->ResetStatID(); // reset the stat id since this thing now has a different name
#endif
	UnhashObject(this);
	check(InternalIndex >= 0);
	NamePrivate = NewName;
	if (NewOuter)
	{
		OuterPrivate = NewOuter;
	}
	HashObject(this);
}

UPackage* UObjectBase::GetExternalPackage() const
{
	// if we have no outer, consider this a package, packages returns themselves as their external package
	if (OuterPrivate == nullptr)
	{
		return CastChecked<UPackage>((UObject*)(this));
	}
	UPackage* ExternalPackage = nullptr;
	if ((GetFlags() & RF_HasExternalPackage) != 0)
	{
		ExternalPackage = GetObjectExternalPackageThreadSafe(this);
		// if the flag is set there should be an override set.
		ensure(ExternalPackage);
	}
	return ExternalPackage;
}

UPackage* UObjectBase::GetExternalPackageInternal() const
{
	// if we have no outer, consider this a package, packages returns themselves as their external package
	if (OuterPrivate == nullptr)
	{
		return CastChecked<UPackage>((UObject*)(this));
	}
	return (GetFlags() & RF_HasExternalPackage) != 0 ? GetObjectExternalPackageInternal(this) : nullptr;
}

void UObjectBase::SetExternalPackage(UPackage* InPackage)
{
	HashObjectExternalPackage(this, InPackage);
	if (InPackage)
	{
		SetFlagsTo(GetFlags() | RF_HasExternalPackage);
	}
	else
	{
		SetFlagsTo(GetFlags() & ~RF_HasExternalPackage);
	}
}

void UObjectBase::SetClass(UClass* NewClass)
{
#if STATS || ENABLE_STATNAMEDEVENTS_UOBJECT
	((UObject*)this)->ResetStatID(); // reset the stat id since this thing now has a different name
#endif

	UnhashObject(this);
#if USE_UBER_GRAPH_PERSISTENT_FRAME
	UClass* OldClass = ClassPrivate;
	ClassPrivate->DestroyPersistentUberGraphFrame((UObject*)this);
#endif
	ClassPrivate = NewClass;
#if USE_UBER_GRAPH_PERSISTENT_FRAME
	ClassPrivate->CreatePersistentUberGraphFrame((UObject*)this, /*bCreateOnlyIfEmpty =*/false, /*bSkipSuperClass =*/false, OldClass);
#endif
	HashObject(this);
}


/**
 * Checks to see if the object appears to be valid
 * @return true if this appears to be a valid object
 */
bool UObjectBase::IsValidLowLevel() const
{
	if( this == nullptr )
	{
		UE_LOG(LogUObjectBase, Warning, TEXT("NULL object") );
		return false;
	}
	if( !ClassPrivate )
	{
		UE_LOG(LogUObjectBase, Warning, TEXT("Object is not registered") );
		return false;
	}
	return GUObjectArray.IsValid(this);
}

bool UObjectBase::IsValidLowLevelFast(bool bRecursive /*= true*/) const
{
	// As DEFAULT_ALIGNMENT is defined to 0 now, I changed that to the original numerical value here
	const int32 AlignmentCheck = MIN_ALIGNMENT - 1;

	// Check 'this' pointer before trying to access any of the Object's members
	if ((this == nullptr) || (UPTRINT)this < 0x100)
	{
		UE_LOG(LogUObjectBase, Error, TEXT("\'this\' pointer is invalid."));
		return false;
	}
	if ((UPTRINT)this & AlignmentCheck)
	{
		UE_LOG(LogUObjectBase, Error, TEXT("\'this\' pointer is misaligned."));
		return false;
	}
	if (*(void**)this == nullptr)
	{
		UE_LOG(LogUObjectBase, Error, TEXT("Virtual functions table is invalid."));
		return false;
	}

	// These should all be 0.
	const UPTRINT CheckZero = (ObjectFlags & ~RF_AllFlags) | ((UPTRINT)ClassPrivate & AlignmentCheck) | ((UPTRINT)OuterPrivate & AlignmentCheck);
	if (!!CheckZero)
	{
		UE_LOG(LogUObjectBase, Error, TEXT("Object flags are invalid or either Class or Outer is misaligned"));
		return false;
	}
	// These should all be non-NULL (except CDO-alignment check which should be 0)
	if (ClassPrivate == nullptr || ClassPrivate->ClassDefaultObject == nullptr || ((UPTRINT)ClassPrivate->ClassDefaultObject & AlignmentCheck) != 0)
	{
		UE_LOG(LogUObjectBase, Error, TEXT("Class pointer is invalid or CDO is invalid."));
		return false;
	}
	// Avoid infinite recursion so call IsValidLowLevelFast on the class object with bRecirsive = false.
	if (bRecursive && !ClassPrivate->IsValidLowLevelFast(false))
	{
		UE_LOG(LogUObjectBase, Error, TEXT("Class object failed IsValidLowLevelFast test."));
		return false;
	}
	// Lightweight versions of index checks.
	if (!GUObjectArray.IsValidIndex(this) || !NamePrivate.IsValidIndexFast())
	{
		UE_LOG(LogUObjectBase, Error, TEXT("Object array index or name index is invalid."));
		return false;
	}
	return true;
}

void UObjectBase::EmitBaseReferences(UClass *RootClass)
{
	static const FName ClassPropertyName(TEXT("Class"));
	static const FName OuterPropertyName(TEXT("Outer"));
	// Mark UObject class reference as persistent object reference so that it (ClassPrivate) doesn't get nulled when a class
	// is marked as pending kill. Nulling ClassPrivate may leave the object in a broken state if it doesn't get GC'd in the same
	// GC call as its class. And even if it gets GC'd in the same call as its class it may break inside of GC (for example when traversing TMap references)
	RootClass->EmitObjectReference(STRUCT_OFFSET(UObjectBase, ClassPrivate), ClassPropertyName, GCRT_PersistentObject);
	RootClass->EmitObjectReference(STRUCT_OFFSET(UObjectBase, OuterPrivate), OuterPropertyName, GCRT_PersistentObject);
	RootClass->EmitExternalPackageReference();
}

#if USE_PER_MODULE_UOBJECT_BOOTSTRAP
static void UObjectReleaseModuleRegistrants(FName Module)
{
	TMap<FName, TArray<FPendingRegistrant*>>& PerModuleMap = GetPerModuleBootstrapMap();

	FName Package = IPluginManager::Get().PackageNameFromModuleName(Module);

	FName ScriptName = *(FString(TEXT("/Script/")) + Package.ToString());

	TArray<FPendingRegistrant*>* Array = PerModuleMap.Find(ScriptName);
	if (Array)
	{
		SCOPED_BOOT_TIMING("UObjectReleaseModuleRegistrants");
		for (FPendingRegistrant* PendingRegistration : *Array)
		{
			if (GLastPendingRegistrant)
			{
				GLastPendingRegistrant->NextAutoRegister = PendingRegistration;
			}
			else
			{
				check(!GFirstPendingRegistrant);
				GFirstPendingRegistrant = PendingRegistration;
			}
			GLastPendingRegistrant = PendingRegistration;
		}
		UE_LOG(LogUObjectBootstrap, Verbose, TEXT("UObjectReleaseModuleRegistrants %d items in %s"), Array->Num(), *ScriptName.ToString());
		PerModuleMap.Remove(ScriptName);
	}
	else
	{
		UE_LOG(LogUObjectBootstrap, Verbose, TEXT("UObjectReleaseModuleRegistrants no items in %s"), *ScriptName.ToString());
	}
}

void UObjectReleaseAllModuleRegistrants()
{
	SCOPED_BOOT_TIMING("UObjectReleaseAllModuleRegistrants");
	TMap<FName, TArray<FPendingRegistrant*>>& PerModuleMap = GetPerModuleBootstrapMap();
	for (auto& Pair : PerModuleMap)
	{
		for (FPendingRegistrant* PendingRegistration : Pair.Value)
		{
			if (GLastPendingRegistrant)
			{
				GLastPendingRegistrant->NextAutoRegister = PendingRegistration;
			}
			else
			{
				check(!GFirstPendingRegistrant);
				GFirstPendingRegistrant = PendingRegistration;
			}
			GLastPendingRegistrant = PendingRegistration;
		}
		UE_LOG(LogUObjectBootstrap, Verbose, TEXT("UObjectReleaseAllModuleRegistrants %d items in %s"), Pair.Value.Num(), *Pair.Key.ToString());
	}
	PerModuleMap.Empty();
	ProcessNewlyLoadedUObjects();
}

static void DumpPendingUObjectModules(const TArray<FString>& Args)
{
	TMap<FName, TArray<FPendingRegistrant*>>& PerModuleMap = GetPerModuleBootstrapMap();
	for (auto& Pair : PerModuleMap)
	{
		UE_LOG(LogUObjectBootstrap, Display, TEXT("Not yet loaded: %d items in %s"), Pair.Value.Num(), *Pair.Key.ToString());
	}
}

static FAutoConsoleCommand DumpPendingUObjectModulesCmd(
	TEXT("DumpPendingUObjectModules"),
	TEXT("When doing per-module UObject bootstrapping, show the modules that are not yet loaded."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&DumpPendingUObjectModules)
);

#endif

/** Enqueue the registration for this object. */
void UObjectBase::Register(const TCHAR* PackageName,const TCHAR* InName)
{
	TMap<UObjectBase*, FPendingRegistrantInfo>& PendingRegistrants = FPendingRegistrantInfo::GetMap();

	FPendingRegistrant* PendingRegistration = new FPendingRegistrant(this);
	PendingRegistrants.Add(this, FPendingRegistrantInfo(InName, PackageName));

#if USE_PER_MODULE_UOBJECT_BOOTSTRAP
	if (FName(PackageName) != FName("/Script/CoreUObject"))
	{
		TMap<FName, TArray<FPendingRegistrant*>>& PerModuleMap = GetPerModuleBootstrapMap();

		PerModuleMap.FindOrAdd(FName(PackageName)).Add(PendingRegistration);
	}
	else
#endif
	{
		if (GLastPendingRegistrant)
		{
			GLastPendingRegistrant->NextAutoRegister = PendingRegistration;
		}
		else
		{
			check(!GFirstPendingRegistrant);
			GFirstPendingRegistrant = PendingRegistration;
		}
		GLastPendingRegistrant = PendingRegistration;
	}
}


/**
 * Dequeues registrants from the list of pending registrations into an array.
 * The contents of the array is preserved, and the new elements are appended.
 */
static void DequeuePendingAutoRegistrants(TArray<FPendingRegistrant>& OutPendingRegistrants)
{
	// We process registrations in the order they were enqueued, since each registrant ensures
	// its dependencies are enqueued before it enqueues itself.
	FPendingRegistrant* NextPendingRegistrant = GFirstPendingRegistrant;
	GFirstPendingRegistrant = NULL;
	GLastPendingRegistrant = NULL;
	while(NextPendingRegistrant)
	{
		FPendingRegistrant* PendingRegistrant = NextPendingRegistrant;
		OutPendingRegistrants.Add(*PendingRegistrant);
		NextPendingRegistrant = PendingRegistrant->NextAutoRegister;
		delete PendingRegistrant;
	};
}

/**
 * Process the auto register objects adding them to the UObject array
 */
static void UObjectProcessRegistrants()
{
	SCOPED_BOOT_TIMING("UObjectProcessRegistrants");

	check(UObjectInitialized());
	// Make list of all objects to be registered.
	TArray<FPendingRegistrant> PendingRegistrants;
	DequeuePendingAutoRegistrants(PendingRegistrants);

	for(int32 RegistrantIndex = 0;RegistrantIndex < PendingRegistrants.Num();++RegistrantIndex)
	{
		const FPendingRegistrant& PendingRegistrant = PendingRegistrants[RegistrantIndex];

		UObjectForceRegistration(PendingRegistrant.Object, false);

		check(PendingRegistrant.Object->GetClass()); // should have been set by DeferredRegister

		// Register may have resulted in new pending registrants being enqueued, so dequeue those.
		DequeuePendingAutoRegistrants(PendingRegistrants);
	}
}

void UObjectForceRegistration(UObjectBase* Object, bool bCheckForModuleRelease)
{
	TMap<UObjectBase*, FPendingRegistrantInfo>& PendingRegistrants = FPendingRegistrantInfo::GetMap();

	FPendingRegistrantInfo* Info = PendingRegistrants.Find(Object);
	if (Info)
	{
		const TCHAR* PackageName = Info->PackageName;
#if USE_PER_MODULE_UOBJECT_BOOTSTRAP
		if (bCheckForModuleRelease)
		{
			UObjectReleaseModuleRegistrants(FName(PackageName));
		}
#endif
		const TCHAR* Name = Info->Name;
		PendingRegistrants.Remove(Object);  // delete this first so that it doesn't try to do it twice
		Object->DeferredRegister(UClass::StaticClass(),PackageName,Name);
	}
}

/**
 * Struct containing the function pointer and package name of a UStruct to be registered with UObject system
 */
struct FPendingStructRegistrant
{	
	class UScriptStruct *(*RegisterFn)();
	const TCHAR* PackageName;

	FPendingStructRegistrant() {}
	FPendingStructRegistrant(class UScriptStruct *(*Fn)(), const TCHAR* InPackageName)
		: RegisterFn(Fn)
		, PackageName(InPackageName)
	{
	}
	FORCEINLINE bool operator==(const FPendingStructRegistrant& Other) const
	{
		return RegisterFn == Other.RegisterFn;
	}
};

static TArray<FPendingStructRegistrant>& GetDeferredCompiledInStructRegistration()
{
	static TArray<FPendingStructRegistrant> DeferredCompiledInRegistration;
	return DeferredCompiledInRegistration;
}

TMap<FName, UScriptStruct *(*)()>& GetDynamicStructMap()
{
	static TMap<FName, UScriptStruct *(*)()> DynamicStructMap;
	return DynamicStructMap;
}

void UObjectCompiledInDeferStruct(class UScriptStruct *(*InRegister)(), const TCHAR* PackageName, const TCHAR* ObjectName, bool bDynamic, const TCHAR* DynamicPathName)
{
	if (!bDynamic)
	{
		// we do reregister StaticStruct in hot reload
		FPendingStructRegistrant Registrant(InRegister, PackageName);
		checkSlow(!GetDeferredCompiledInStructRegistration().Contains(Registrant));
		GetDeferredCompiledInStructRegistration().Add(Registrant);
	}
	else
	{
		GetDynamicStructMap().Add(DynamicPathName, InRegister);
	}
	NotifyRegistrationEvent(PackageName, ObjectName, ENotifyRegistrationType::NRT_Struct, ENotifyRegistrationPhase::NRP_Added, (UObject *(*)())(InRegister), bDynamic);

}

class UScriptStruct *GetStaticStruct(class UScriptStruct *(*InRegister)(), UObject* StructOuter, const TCHAR* StructName, SIZE_T Size, uint32 Crc)
{
	NotifyRegistrationEvent(*StructOuter->GetOutermost()->GetName(), StructName, ENotifyRegistrationType::NRT_Struct, ENotifyRegistrationPhase::NRP_Started);
	UScriptStruct *Result = (*InRegister)();
	NotifyRegistrationEvent(*StructOuter->GetOutermost()->GetName(), StructName, ENotifyRegistrationType::NRT_Struct, ENotifyRegistrationPhase::NRP_Finished);
	return Result;
}

/**
 * Struct containing the function pointer and package name of a UEnum to be registered with UObject system
 */
struct FPendingEnumRegistrant
{
	class UEnum *(*RegisterFn)();
	const TCHAR* PackageName;

	FPendingEnumRegistrant() {}
	FPendingEnumRegistrant(class UEnum *(*Fn)(), const TCHAR* InPackageName)
		: RegisterFn(Fn)
		, PackageName(InPackageName)
	{
	}
	FORCEINLINE bool operator==(const FPendingEnumRegistrant& Other) const
	{
		return RegisterFn == Other.RegisterFn;
	}
};

// Same thing as GetDeferredCompiledInStructRegistration but for UEnums declared in header files without UClasses.
static TArray<FPendingEnumRegistrant>& GetDeferredCompiledInEnumRegistration()
{
	static TArray<FPendingEnumRegistrant> DeferredCompiledInRegistration;
	return DeferredCompiledInRegistration;
}

TMap<FName, UEnum *(*)()>& GetDynamicEnumMap()
{
	static TMap<FName, UEnum *(*)()> DynamicEnumMap;
	return DynamicEnumMap;
}

void UObjectCompiledInDeferEnum(class UEnum *(*InRegister)(), const TCHAR* PackageName, const TCHAR* ObjectName, bool bDynamic, const TCHAR* DynamicPathName)
{
	if (!bDynamic)
	{
		// we do reregister StaticStruct in hot reload
		FPendingEnumRegistrant Registrant(InRegister, PackageName);
		checkSlow(!GetDeferredCompiledInEnumRegistration().Contains(Registrant));
		GetDeferredCompiledInEnumRegistration().Add(Registrant);
	}
	else
	{
		GetDynamicEnumMap().Add(DynamicPathName, InRegister);
	}
	NotifyRegistrationEvent(PackageName, ObjectName, ENotifyRegistrationType::NRT_Enum, ENotifyRegistrationPhase::NRP_Added, (UObject *(*)())(InRegister), bDynamic);
}

class UEnum *GetStaticEnum(class UEnum *(*InRegister)(), UObject* EnumOuter, const TCHAR* EnumName)
{
	NotifyRegistrationEvent(*EnumOuter->GetOutermost()->GetName(), EnumName, ENotifyRegistrationType::NRT_Enum, ENotifyRegistrationPhase::NRP_Started);
	UEnum *Result = (*InRegister)();
	NotifyRegistrationEvent(*EnumOuter->GetOutermost()->GetName(), EnumName, ENotifyRegistrationType::NRT_Enum, ENotifyRegistrationPhase::NRP_Finished);
	return Result;
}

static TArray<class UClass *(*)()>& GetDeferredCompiledInRegistration()
{
	static TArray<class UClass *(*)()> DeferredCompiledInRegistration;
	return DeferredCompiledInRegistration;
}

/** Classes loaded with a module, deferred until we register them all in one go */
static TArray<FFieldCompiledInInfo*>& GetDeferredClassRegistration()
{
	static TArray<FFieldCompiledInInfo*> DeferredClassRegistration;
	return DeferredClassRegistration;
}

#if WITH_HOT_RELOAD
/** Map of deferred class registration info (including size and reflection info) */
static TMap<FName, FFieldCompiledInInfo*>& GetDeferRegisterClassMap()
{
	static TMap<FName, FFieldCompiledInInfo*> DeferRegisterClassMap;
	return DeferRegisterClassMap;
}

/** Classes that changed during hot-reload and need to be re-instanced */
static TArray<FFieldCompiledInInfo*>& GetHotReloadClasses()
{
	static TArray<FFieldCompiledInInfo*> HotReloadClasses;
	return HotReloadClasses;
}
#endif

/** Removes prefix from the native class name */
FString UObjectBase::RemoveClassPrefix(const TCHAR* ClassName)
{
	static const TCHAR* DeprecatedPrefix = TEXT("DEPRECATED_");
	FString NameWithoutPrefix(ClassName);
	NameWithoutPrefix.MidInline(1, MAX_int32, false);
	if (NameWithoutPrefix.StartsWith(DeprecatedPrefix))
	{
		NameWithoutPrefix.MidInline(FCString::Strlen(DeprecatedPrefix), MAX_int32, false);
	}
	return NameWithoutPrefix;
}

void UClassCompiledInDefer(FFieldCompiledInInfo* ClassInfo, const TCHAR* Name, SIZE_T ClassSize, uint32 Crc)
{
	const FName CPPClassName = Name;
#if WITH_HOT_RELOAD
	// Check for existing classes
	TMap<FName, FFieldCompiledInInfo*>& DeferMap = GetDeferRegisterClassMap();
	FFieldCompiledInInfo** ExistingClassInfo = DeferMap.Find(CPPClassName);
	ClassInfo->bHasChanged = !ExistingClassInfo || (*ExistingClassInfo)->Size != ClassInfo->Size || (*ExistingClassInfo)->Crc != ClassInfo->Crc;
	if (ExistingClassInfo)
	{
		// Class exists, this can only happen during hot-reload
		checkf(GIsHotReload, TEXT("Trying to recreate class '%s' outside of hot reload!"), *CPPClassName.ToString());

		// Get the native name
		FString NameWithoutPrefix = UObjectBase::RemoveClassPrefix(Name);
		UClass* ExistingClass = FindObjectChecked<UClass>(ANY_PACKAGE, *NameWithoutPrefix);

		if (ClassInfo->bHasChanged)
		{
			// Rename the old class and move it to transient package
			ExistingClass->RemoveFromRoot();
			ExistingClass->ClearFlags(RF_Standalone | RF_Public);
			ExistingClass->GetDefaultObject()->RemoveFromRoot();
			ExistingClass->GetDefaultObject()->ClearFlags(RF_Standalone | RF_Public);
			const FName OldClassRename = MakeUniqueObjectName(GetTransientPackage(), ExistingClass->GetClass(), *FString::Printf(TEXT("HOTRELOADED_%s"), *NameWithoutPrefix));
			ExistingClass->Rename(*OldClassRename.ToString(), GetTransientPackage());
			ExistingClass->SetFlags(RF_Transient);
			ExistingClass->AddToRoot();

			// Make sure enums de-register their names BEFORE we create the new class, otherwise there will be name conflicts
			TArray<UObject*> ClassSubobjects;
			GetObjectsWithOuter(ExistingClass, ClassSubobjects);
			for (auto ClassSubobject : ClassSubobjects)
			{
				if (auto Enum = dynamic_cast<UEnum*>(ClassSubobject))
				{
					Enum->RemoveNamesFromMasterList();
				}
			}
		}
		ClassInfo->OldClass = ExistingClass;
		GetHotReloadClasses().Add(ClassInfo);

		*ExistingClassInfo = ClassInfo;
	}
	else
	{
		DeferMap.Add(CPPClassName, ClassInfo);
	}
#endif
	// We will either create a new class or update the static class pointer of the existing one
	GetDeferredClassRegistration().Add(ClassInfo);
}

TMap<FName, FDynamicClassStaticData>& GetDynamicClassMap()
{
	static TMap<FName, FDynamicClassStaticData> DynamicClassMap;
	return DynamicClassMap;
}

void UObjectCompiledInDefer(UClass *(*InRegister)(), UClass *(*InStaticClass)(), const TCHAR* Name, const TCHAR* PackageName, bool bDynamic, const TCHAR* DynamicPathName, void (*InInitSearchableValues)(TMap<FName, FName>&))
{
	if (!bDynamic)
	{
#if WITH_HOT_RELOAD
		// Either add all classes if not hot-reloading, or those which have changed
		TMap<FName, FFieldCompiledInInfo*>& DeferMap = GetDeferRegisterClassMap();
		if (!GIsHotReload || DeferMap.FindChecked(Name)->bHasChanged)
#endif
		{
			FString NoPrefix(UObjectBase::RemoveClassPrefix(Name));
			NotifyRegistrationEvent(PackageName, *NoPrefix, ENotifyRegistrationType::NRT_Class, ENotifyRegistrationPhase::NRP_Added, (UObject *(*)())(InRegister), false);
			NotifyRegistrationEvent(PackageName, *(FString(DEFAULT_OBJECT_PREFIX) + NoPrefix), ENotifyRegistrationType::NRT_ClassCDO, ENotifyRegistrationPhase::NRP_Added, (UObject *(*)())(InRegister), false);

			TArray<UClass *(*)()>& DeferredCompiledInRegistration = GetDeferredCompiledInRegistration();
			checkSlow(!DeferredCompiledInRegistration.Contains(InRegister));
			DeferredCompiledInRegistration.Add(InRegister);
		}
	}
	else
	{
		FDynamicClassStaticData ClassFunctions;
		ClassFunctions.ZConstructFn = InRegister;
		ClassFunctions.StaticClassFn = InStaticClass;
		if (InInitSearchableValues)
		{
			InInitSearchableValues(ClassFunctions.SelectedSearchableValues);
		}
		GetDynamicClassMap().Add(FName(DynamicPathName), ClassFunctions);

		FString OriginalPackageName = DynamicPathName;
		check(OriginalPackageName.EndsWith(Name));
		OriginalPackageName.RemoveFromEnd(FString(Name));
		check(OriginalPackageName.EndsWith(TEXT(".")));
		OriginalPackageName.RemoveFromEnd(FString(TEXT(".")));

		NotifyRegistrationEvent(*OriginalPackageName, Name, ENotifyRegistrationType::NRT_Class, ENotifyRegistrationPhase::NRP_Added, (UObject *(*)())(InRegister), true);
		NotifyRegistrationEvent(*OriginalPackageName, *(FString(DEFAULT_OBJECT_PREFIX) + Name), ENotifyRegistrationType::NRT_ClassCDO, ENotifyRegistrationPhase::NRP_Added, (UObject *(*)())(InRegister), true);
	}
}

/** Register all loaded classes */
void UClassRegisterAllCompiledInClasses()
{
#if WITH_HOT_RELOAD
	TArray<UClass*> AddedClasses;
#endif
	SCOPED_BOOT_TIMING("UClassRegisterAllCompiledInClasses");

	TArray<FFieldCompiledInInfo*>& DeferredClassRegistration = GetDeferredClassRegistration();
	for (const FFieldCompiledInInfo* Class : DeferredClassRegistration)
	{
		UClass* RegisteredClass = Class->Register();
#if WITH_HOT_RELOAD
		if (GIsHotReload && Class->OldClass == nullptr)
		{
			AddedClasses.Add(RegisteredClass);
		}
#endif
	}
	DeferredClassRegistration.Empty();

#if WITH_HOT_RELOAD
	if (AddedClasses.Num() > 0)
	{
		FCoreUObjectDelegates::RegisterHotReloadAddedClassesDelegate.Broadcast(AddedClasses);
	}
#endif
}

#if WITH_HOT_RELOAD
/** Re-instance all existing classes that have changed during hot-reload */
void UClassReplaceHotReloadClasses()
{
	TArray<FFieldCompiledInInfo*>& HotReloadClasses = GetHotReloadClasses();

	if (FCoreUObjectDelegates::RegisterClassForHotReloadReinstancingDelegate.IsBound())
	{
		for (const FFieldCompiledInInfo* Class : HotReloadClasses)
		{
			check(Class->OldClass);

			UClass* RegisteredClass = nullptr;
			if (Class->bHasChanged)
			{
				RegisteredClass = Class->Register();
			}

			FCoreUObjectDelegates::RegisterClassForHotReloadReinstancingDelegate.Broadcast(Class->OldClass, RegisteredClass, Class->bHasChanged ? EHotReloadedClassFlags::Changed : EHotReloadedClassFlags::None);
		}
	}

	FCoreUObjectDelegates::ReinstanceHotReloadedClassesDelegate.Broadcast();
	HotReloadClasses.Empty();
}

#endif

/**
 * Load any outstanding compiled in default properties
 */
static void UObjectLoadAllCompiledInDefaultProperties()
{
	TRACE_LOADTIME_REQUEST_GROUP_SCOPE(TEXT("UObjectLoadAllCompiledInDefaultProperties"));

	static FName LongEnginePackageName(TEXT("/Script/Engine"));

	TArray<UClass *(*)()>& DeferredCompiledInRegistration = GetDeferredCompiledInRegistration();

	const bool bHaveRegistrants = DeferredCompiledInRegistration.Num() != 0;
	if( bHaveRegistrants )
	{
		SCOPED_BOOT_TIMING("UObjectLoadAllCompiledInDefaultProperties");
		TArray<UClass*> NewClasses;
		TArray<UClass*> NewClassesInCoreUObject;
		TArray<UClass*> NewClassesInEngine;
		TArray<UClass* (*)()> PendingRegistrants = MoveTemp(DeferredCompiledInRegistration);
		for (UClass* (*Registrant)() : PendingRegistrants)
		{
			UClass* Class = Registrant();
			UE_LOG(LogUObjectBootstrap, Verbose, TEXT("UObjectLoadAllCompiledInDefaultProperties After Registrant %s %s"), *Class->GetOutermost()->GetName(), *Class->GetName());
			if (Class->GetOutermost()->GetFName() == GLongCoreUObjectPackageName)
			{
				NewClassesInCoreUObject.Add(Class);
			}
			else if (Class->GetOutermost()->GetFName() == LongEnginePackageName)
			{
				NewClassesInEngine.Add(Class);
			}
			else
			{
				NewClasses.Add(Class);
			}
		}
		{
			SCOPED_BOOT_TIMING("CoreUObject Classes");
			for (UClass* Class : NewClassesInCoreUObject) // we do these first because we assume these never trigger loads
			{
				UE_LOG(LogUObjectBootstrap, Verbose, TEXT("GetDefaultObject Begin %s %s"), *Class->GetOutermost()->GetName(), *Class->GetName());
				Class->GetDefaultObject();
				UE_LOG(LogUObjectBootstrap, Verbose, TEXT("GetDefaultObject End %s %s"), *Class->GetOutermost()->GetName(), *Class->GetName());
			}
		}
		{
			SCOPED_BOOT_TIMING("Engine Classes");
			for (UClass* Class : NewClassesInEngine) // we do these second because we want to bring the engine up before the game
			{
				UE_LOG(LogUObjectBootstrap, Verbose, TEXT("GetDefaultObject Begin %s %s"), *Class->GetOutermost()->GetName(), *Class->GetName());
				Class->GetDefaultObject();
				UE_LOG(LogUObjectBootstrap, Verbose, TEXT("GetDefaultObject End %s %s"), *Class->GetOutermost()->GetName(), *Class->GetName());
			}
		}
		{
			SCOPED_BOOT_TIMING("Other Classes");
			for (UClass* Class : NewClasses)
			{
				UE_LOG(LogUObjectBootstrap, Verbose, TEXT("GetDefaultObject Begin %s %s"), *Class->GetOutermost()->GetName(), *Class->GetName());
				Class->GetDefaultObject();
				UE_LOG(LogUObjectBootstrap, Verbose, TEXT("GetDefaultObject End %s %s"), *Class->GetOutermost()->GetName(), *Class->GetName());
			}
		}
		FFeedbackContext& ErrorsFC = UClass::GetDefaultPropertiesFeedbackContext();
		if (ErrorsFC.GetNumErrors() || ErrorsFC.GetNumWarnings())
		{
			TArray<FString> AllErrorsAndWarnings;
			ErrorsFC.GetErrorsAndWarningsAndEmpty(AllErrorsAndWarnings);

			FString AllInOne;
			UE_LOG(LogUObjectBase, Warning, TEXT("-------------- Default Property warnings and errors:"));
			for (const FString& ErrorOrWarning : AllErrorsAndWarnings)
			{
				UE_LOG(LogUObjectBase, Warning, TEXT("%s"), *ErrorOrWarning);
				AllInOne += ErrorOrWarning;
				AllInOne += TEXT("\n");
			}
			FMessageDialog::Open(EAppMsgType::Ok, FText::Format( NSLOCTEXT("Core", "DefaultPropertyWarningAndErrors", "Default Property warnings and errors:\n{0}"), FText::FromString( AllInOne ) ) );
		}
	}
}

/**
 * Call StaticStruct for each struct...this sets up the internal singleton, and important works correctly with hot reload
 */
static void UObjectLoadAllCompiledInStructs()
{
	SCOPED_BOOT_TIMING("UObjectLoadAllCompiledInStructs");

	TArray<FPendingEnumRegistrant> PendingEnumRegistrants = MoveTemp(GetDeferredCompiledInEnumRegistration());
	TArray<FPendingStructRegistrant> PendingStructRegistrants = MoveTemp(GetDeferredCompiledInStructRegistration());

	{
		SCOPED_BOOT_TIMING("UObjectLoadAllCompiledInStructs -  CreatePackages (could be optimized!)");
		// Load Enums first
		for (const FPendingEnumRegistrant& EnumRegistrant : PendingEnumRegistrants)
		{
			// Make sure the package exists in case it does not contain any UObjects
			CreatePackage(EnumRegistrant.PackageName);
		}
		for (const FPendingStructRegistrant& StructRegistrant : PendingStructRegistrants)
		{
			// Make sure the package exists in case it does not contain any UObjects or UEnums
			CreatePackage(StructRegistrant.PackageName);
		}
	}

	// Load Structs

	for (const FPendingEnumRegistrant& EnumRegistrant : PendingEnumRegistrants)
	{
		EnumRegistrant.RegisterFn();
	}

	for (const FPendingStructRegistrant& StructRegistrant : PendingStructRegistrants)
	{
		StructRegistrant.RegisterFn();
	}
}

void ProcessNewlyLoadedUObjects(FName Package, bool bCanProcessNewlyLoadedObjects)
{
	SCOPED_BOOT_TIMING("ProcessNewlyLoadedUObjects");
#if USE_PER_MODULE_UOBJECT_BOOTSTRAP
	if (Package != NAME_None)
	{
		UObjectReleaseModuleRegistrants(Package);
	}
#endif
	if (!bCanProcessNewlyLoadedObjects)
	{
		return;
	}
	LLM_SCOPE(ELLMTag::UObject);
	DECLARE_SCOPE_CYCLE_COUNTER(TEXT("ProcessNewlyLoadedUObjects"), STAT_ProcessNewlyLoadedUObjects, STATGROUP_ObjectVerbose);

	UClassRegisterAllCompiledInClasses();

	const TArray<UClass* (*)()>& DeferredCompiledInRegistration = GetDeferredCompiledInRegistration();
	const TArray<FPendingStructRegistrant>& DeferredCompiledInStructRegistration = GetDeferredCompiledInStructRegistration();
	const TArray<FPendingEnumRegistrant>& DeferredCompiledInEnumRegistration = GetDeferredCompiledInEnumRegistration();

	bool bNewUObjects = false;
	while (GFirstPendingRegistrant || DeferredCompiledInRegistration.Num() || DeferredCompiledInStructRegistration.Num() || DeferredCompiledInEnumRegistration.Num())
	{
		bNewUObjects = true;
		UObjectProcessRegistrants();
		UObjectLoadAllCompiledInStructs();

		FCoreUObjectDelegates::CompiledInUObjectsRegisteredDelegate.Broadcast(Package);

		UObjectLoadAllCompiledInDefaultProperties();
	}
#if WITH_HOT_RELOAD
	UClassReplaceHotReloadClasses();
#endif

	if (bNewUObjects && !GIsInitialLoad)
	{
		UClass::AssembleReferenceTokenStreams();
	}
}

static int32 GVarMaxObjectsNotConsideredByGC;
static FAutoConsoleVariableRef CMaxObjectsNotConsideredByGC(
	TEXT("gc.MaxObjectsNotConsideredByGC"),
	GVarMaxObjectsNotConsideredByGC,
	TEXT("Placeholder console variable, currently not used in runtime."),
	ECVF_Default
	);

static int32 GSizeOfPermanentObjectPool;
static FAutoConsoleVariableRef CSizeOfPermanentObjectPool(
	TEXT("gc.SizeOfPermanentObjectPool"),
	GSizeOfPermanentObjectPool,
	TEXT("Placeholder console variable, currently not used in runtime."),
	ECVF_Default
	);

static int32 GMaxObjectsInEditor;
static FAutoConsoleVariableRef CMaxObjectsInEditor(
	TEXT("gc.MaxObjectsInEditor"),
	GMaxObjectsInEditor,
	TEXT("Placeholder console variable, currently not used in runtime."),
	ECVF_Default
	);

static int32 GMaxObjectsInGame;
static FAutoConsoleVariableRef CMaxObjectsInGame(
	TEXT("gc.MaxObjectsInGame"),
	GMaxObjectsInGame,
	TEXT("Placeholder console variable, currently not used in runtime."),
	ECVF_Default
	);


/**
 * Final phase of UObject initialization. all auto register objects are added to the main data structures.
 */
void UObjectBaseInit()
{
	SCOPED_BOOT_TIMING("UObjectBaseInit");

	// Zero initialize and later on get value from .ini so it is overridable per game/ platform...
	int32 MaxObjectsNotConsideredByGC = 0;
	int32 SizeOfPermanentObjectPool = 0;
	int32 MaxUObjects = 2 * 1024 * 1024; // Default to ~2M UObjects
	bool bPreAllocateUObjectArray = false;	

	// To properly set MaxObjectsNotConsideredByGC look for "Log: XXX objects as part of root set at end of initial load."
	// in your log file. This is being logged from LaunchEnglineLoop after objects have been added to the root set. 

	// Disregard for GC relies on seekfree loading for interaction with linkers. We also don't want to use it in the Editor, for which
	// FPlatformProperties::RequiresCookedData() will be false. Please note that GIsEditor and FApp::IsGame() are not valid at this point.
	if (FPlatformProperties::RequiresCookedData())
	{
		FString Value;
		bool bIsCookOnTheFly = FParse::Value(FCommandLine::Get(), TEXT("-filehostip="), Value);
		if (bIsCookOnTheFly)
		{
			GCreateGCClusters = false;
		}
		else
		{
			GConfig->GetInt(TEXT("/Script/Engine.GarbageCollectionSettings"), TEXT("gc.MaxObjectsNotConsideredByGC"), MaxObjectsNotConsideredByGC, GEngineIni);

			// Not used on PC as in-place creation inside bigger pool interacts with the exit purge and deleting UObject directly.
			GConfig->GetInt(TEXT("/Script/Engine.GarbageCollectionSettings"), TEXT("gc.SizeOfPermanentObjectPool"), SizeOfPermanentObjectPool, GEngineIni);
		}

		// Maximum number of UObjects in cooked game
		GConfig->GetInt(TEXT("/Script/Engine.GarbageCollectionSettings"), TEXT("gc.MaxObjectsInGame"), MaxUObjects, GEngineIni);

		// If true, the UObjectArray will pre-allocate all entries for UObject pointers
		GConfig->GetBool(TEXT("/Script/Engine.GarbageCollectionSettings"), TEXT("gc.PreAllocateUObjectArray"), bPreAllocateUObjectArray, GEngineIni);
	}
	else
	{
#if IS_PROGRAM
		// Maximum number of UObjects for programs can be low
		MaxUObjects = 100000; // Default to 100K for programs
		GConfig->GetInt(TEXT("/Script/Engine.GarbageCollectionSettings"), TEXT("gc.MaxObjectsInProgram"), MaxUObjects, GEngineIni);
#else
		// Maximum number of UObjects in the editor
		GConfig->GetInt(TEXT("/Script/Engine.GarbageCollectionSettings"), TEXT("gc.MaxObjectsInEditor"), MaxUObjects, GEngineIni);
#endif
	}

	if (MaxObjectsNotConsideredByGC <= 0 && SizeOfPermanentObjectPool > 0)
	{
		// If permanent object pool is enabled but disregard for GC is disabled, GC will mark permanent object pool objects
		// as unreachable and may destroy them so disable permanent object pool too.
		// An alternative would be to make GC not mark permanent object pool objects as unreachable but then they would have to
		// be considered as root set objects because they could be referencing objects from outside of permanent object pool.
		// This would be inconsistent and confusing and also counter productive (the more root set objects the more expensive MarkAsUnreachable phase is).
		SizeOfPermanentObjectPool = 0;
		UE_LOG(LogInit, Warning, TEXT("Disabling permanent object pool because disregard for GC is disabled (gc.MaxObjectsNotConsideredByGC=%d)."), MaxObjectsNotConsideredByGC);
	}

	// Log what we're doing to track down what really happens as log in LaunchEngineLoop doesn't report those settings in pristine form.
	UE_LOG(LogInit, Log, TEXT("%s for max %d objects, including %i objects not considered by GC, pre-allocating %i bytes for permanent pool."), 
		bPreAllocateUObjectArray ? TEXT("Pre-allocating") : TEXT("Presizing"),
		MaxUObjects, MaxObjectsNotConsideredByGC, SizeOfPermanentObjectPool);

	GUObjectAllocator.AllocatePermanentObjectPool(SizeOfPermanentObjectPool);
	GUObjectArray.AllocateObjectPool(MaxUObjects, MaxObjectsNotConsideredByGC, bPreAllocateUObjectArray);

	void InitAsyncThread();
	InitAsyncThread();

	// Note initialized.
	Internal::GetUObjectSubsystemInitialised() = true;

	UObjectProcessRegistrants();
}

/**
 * Final phase of UObject shutdown
 */
void UObjectBaseShutdown()
{
	void ShutdownAsyncThread();
	ShutdownAsyncThread();

	GUObjectArray.ShutdownUObjectArray();
	Internal::GetUObjectSubsystemInitialised() = false;
}

/**
 * Helper function that can be used inside the debuggers watch window. E.g. "DebugFName(Class)". 
 *
 * @param	Object	Object to look up the name for 
 * @return			Associated name
 */
const TCHAR* DebugFName(UObject* Object)
{
	if ( Object )
	{
		// Hardcoded static array. This function is only used inside the debugger so it should be fine to return it.
		static TCHAR TempName[256];
		FName Name = Object->GetFName();
		FCString::Strcpy(TempName, *FName::SafeString(Name.GetDisplayIndex(), Name.GetNumber()));
		return TempName;
	}
	else
	{
		return TEXT("NULL");
	}
}

/**
 * Helper function that can be used inside the debuggers watch window. E.g. "DebugFName(Object)". 
 *
 * @param	Object	Object to look up the name for 
 * @return			Fully qualified path name
 */
const TCHAR* DebugPathName(UObject* Object)
{
	if( Object )
	{
		// Hardcoded static array. This function is only used inside the debugger so it should be fine to return it.
		static TCHAR PathName[1024];
		PathName[0] = 0;

		// Keep track of how many outers we have as we need to print them in inverse order.
		UObject*	TempObject = Object;
		int32			OuterCount = 0;
		while( TempObject )
		{
			TempObject = TempObject->GetOuter();
			OuterCount++;
		}

		// Iterate over each outer + self in reverse oder and append name.
		for( int32 OuterIndex=OuterCount-1; OuterIndex>=0; OuterIndex-- )
		{
			// Move to outer name.
			TempObject = Object;
			for( int32 i=0; i<OuterIndex; i++ )
			{
				TempObject = TempObject->GetOuter();
			}

			// Dot separate entries.
			if( OuterIndex != OuterCount -1 )
			{
				FCString::Strcat( PathName, TEXT(".") );
			}
			// And app end the name.
			FCString::Strcat( PathName, DebugFName( TempObject ) );
		}

		return PathName;
	}
	else
	{
		return TEXT("None");
	}
}

/**
 * Helper function that can be used inside the debuggers watch window. E.g. "DebugFName(Object)". 
 *
 * @param	Object	Object to look up the name for 
 * @return			Fully qualified path name prepended by class name
 */
const TCHAR* DebugFullName(UObject* Object)
{
	if( Object )
	{
		// Hardcoded static array. This function is only used inside the debugger so it should be fine to return it.
		static TCHAR FullName[1024];
		FullName[0]=0;

		// Class Full.Path.Name
		FCString::Strcat( FullName, DebugFName(Object->GetClass()) );
		FCString::Strcat( FullName, TEXT(" "));
		FCString::Strcat( FullName, DebugPathName(Object) );

		return FullName;
	}
	else
	{
		return TEXT("None");
	}
}

#if WITH_HOT_RELOAD
namespace
{
	struct FObjectCompiledInfo
	{
		/** Registered struct info (including size and reflection info) */
		static TMap<TTuple<UObject*, FName>, FObjectCompiledInfo>& GetRegisteredInfo()
		{
			static TMap<TTuple<UObject*, FName>, FObjectCompiledInfo> StructOrEnumCompiledInfoMap;
			return StructOrEnumCompiledInfoMap;
		}

		FObjectCompiledInfo(SIZE_T InClassSize, uint32 InCrc)
			: Size(InClassSize)
			, Crc (InCrc)
		{
		}

		SIZE_T Size;
		uint32 Crc;
	};

	template <typename TType>
	TType* FindExistingObjectIfHotReload(UObject* Outer, const TCHAR* Name, SIZE_T Size, uint32 Crc)
	{
		TTuple<UObject*, FName> Key(Outer, Name);

		bool bChanged = true;
		if (FObjectCompiledInfo* Info = FObjectCompiledInfo::GetRegisteredInfo().Find(Key))
		{
			// Hot-reloaded struct
			bChanged = Info->Size != Size || Info->Crc != Crc;

			Info->Size = Size;
			Info->Crc  = Crc;
		}
		else
		{
			// New struct
			FObjectCompiledInfo::GetRegisteredInfo().Add(Key, FObjectCompiledInfo(Size, Crc));
		}

		if (!GIsHotReload)
		{
			return nullptr;
		}

		TType* Existing = FindObject<TType>(Outer, Name);
		if (!Existing)
		{
			// New type added during hot-reload
			UE_LOG(LogClass, Log, TEXT("Could not find existing type %s for HotReload. Assuming new"), Name);
			return nullptr;
		}

		// Existing type, make sure we destroy the old one if it has changed
		if (bChanged)
		{
			// Make sure the old struct is not used by anything
			Existing->ClearFlags(RF_Standalone | RF_Public);
			Existing->RemoveFromRoot();
			const FName OldRename = MakeUniqueObjectName(GetTransientPackage(), Existing->GetClass(), *FString::Printf(TEXT("HOTRELOADED_%s"), Name));
			Existing->Rename(*OldRename.ToString(), GetTransientPackage());
			return nullptr;
		}

		UE_LOG(LogClass, Log, TEXT("%s HotReload."), Name);
		return Existing;
	}
}
#endif // WITH_HOT_RELOAD

UScriptStruct* FindExistingStructIfHotReloadOrDynamic(UObject* Outer, const TCHAR* StructName, SIZE_T Size, uint32 Crc, bool bIsDynamic)
{
#if WITH_HOT_RELOAD
	UScriptStruct* Result = FindExistingObjectIfHotReload<UScriptStruct>(Outer, StructName, Size, Crc);
#else
	UScriptStruct* Result = nullptr;
#endif
	if (!Result && bIsDynamic)
	{
		Result = Cast<UScriptStruct>(StaticFindObjectFast(UScriptStruct::StaticClass(), Outer, StructName));
	}
	return Result;
}

UEnum* FindExistingEnumIfHotReloadOrDynamic(UObject* Outer, const TCHAR* EnumName, SIZE_T Size, uint32 Crc, bool bIsDynamic)
{
#if WITH_HOT_RELOAD
	UEnum* Result = FindExistingObjectIfHotReload<UEnum>(Outer, EnumName, Size, Crc);
#else
	UEnum* Result = nullptr;
#endif
	if (!Result && bIsDynamic)
	{
		Result = Cast<UEnum>(StaticFindObjectFast(UEnum::StaticClass(), Outer, EnumName));
	}
	return Result;
}

UObject* ConstructDynamicType(FName TypePathName, EConstructDynamicType ConstructionSpecifier)
{
	UObject* Result = nullptr;
	if (FDynamicClassStaticData* ClassConstructFn = GetDynamicClassMap().Find(TypePathName))
	{
		if (ConstructionSpecifier == EConstructDynamicType::CallZConstructor)
		{
			UClass* DynamicClass = ClassConstructFn->ZConstructFn();
			check(DynamicClass);
			DynamicClass->AssembleReferenceTokenStream();
			Result = DynamicClass;
		}
		else if (ConstructionSpecifier == EConstructDynamicType::OnlyAllocateClassObject)
		{
			Result = ClassConstructFn->StaticClassFn();
			check(Result);
		}
	}
	else if (UScriptStruct *(**StaticStructFNPtr)() = GetDynamicStructMap().Find(TypePathName))
	{
		Result = (*StaticStructFNPtr)();
	}
	else if (UEnum *(**StaticEnumFNPtr)() = GetDynamicEnumMap().Find(TypePathName))
	{
		Result = (*StaticEnumFNPtr)();
	}
	return Result;
}

FName GetDynamicTypeClassName(FName TypePathName)
{
	FName Result = NAME_None;
	if (GetDynamicClassMap().Find(TypePathName))
	{
		Result = UDynamicClass::StaticClass()->GetFName();
	}
	else if (GetDynamicStructMap().Find(TypePathName))
	{
		Result = UScriptStruct::StaticClass()->GetFName();
	}
	else if (GetDynamicEnumMap().Find(TypePathName))
	{
		Result = UEnum::StaticClass()->GetFName();
	}
	if (false && Result == NAME_None)
	{
		UE_LOG(LogUObjectBase, Warning, TEXT("GetDynamicTypeClassName %s not found."), *TypePathName.ToString());
		UE_LOG(LogUObjectBase, Warning, TEXT("---- classes"));
		for (auto& Pair : GetDynamicClassMap())
		{
			UE_LOG(LogUObjectBase, Warning, TEXT("    %s"), *Pair.Key.ToString());
		}
		UE_LOG(LogUObjectBase, Warning, TEXT("---- structs"));
		for (auto& Pair : GetDynamicStructMap())
		{
			UE_LOG(LogUObjectBase, Warning, TEXT("    %s"), *Pair.Key.ToString());
		}
		UE_LOG(LogUObjectBase, Warning, TEXT("---- enums"));
		for (auto& Pair : GetDynamicEnumMap())
		{
			UE_LOG(LogUObjectBase, Warning, TEXT("    %s"), *Pair.Key.ToString());
		}
		UE_LOG(LogUObjectBase, Fatal, TEXT("GetDynamicTypeClassName %s not found."), *TypePathName.ToString());
	}
	UE_CLOG(Result == NAME_None, LogUObjectBase, Warning, TEXT("GetDynamicTypeClassName %s not found."), *TypePathName.ToString());
	return Result;
}

UPackage* FindOrConstructDynamicTypePackage(const TCHAR* PackageName)
{
	UPackage* Package = Cast<UPackage>(StaticFindObjectFast(UPackage::StaticClass(), nullptr, PackageName));
	if (!Package)
	{
		Package = CreatePackage(PackageName);
		if (!GEventDrivenLoaderEnabled)
		{
			Package->SetPackageFlags(PKG_CompiledIn);
		}
	}
	check(Package);
	return Package;
}

TMap<FName, FName>& GetConvertedDynamicPackageNameToTypeName()
{
	static TMap<FName, FName> ConvertedDynamicPackageNameToTypeName;
	return ConvertedDynamicPackageNameToTypeName;
}
