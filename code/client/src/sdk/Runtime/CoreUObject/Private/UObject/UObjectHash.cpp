// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	UObjectHash.cpp: Unreal object name hashes
=============================================================================*/

#include "UObject/UObjectHash.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "Misc/AsciiSet.h"
#include "Misc/PackageName.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogUObjectHash, Log, All);

DECLARE_CYCLE_STAT( TEXT( "GetObjectsOfClass" ), STAT_Hash_GetObjectsOfClass, STATGROUP_UObjectHash );
DECLARE_CYCLE_STAT( TEXT( "HashObject" ), STAT_Hash_HashObject, STATGROUP_UObjectHash );
DECLARE_CYCLE_STAT( TEXT( "UnhashObject" ), STAT_Hash_UnhashObject, STATGROUP_UObjectHash );

#if UE_GC_TRACK_OBJ_AVAILABLE
DEFINE_STAT( STAT_Hash_NumObjects );
#endif

// Global UObject array instance
FUObjectArray GUObjectArray;

/**
 * This implementation will use more space than the UE3 implementation. The goal was to make UObjects smaller to save L2 cache space. 
 * The hash is rarely used at runtime. A more space-efficient implementation is possible.
 */


/*
 * Special hash bucket to conserve memory.
 * Contains a pointer to head element and an optional list of items if more than one element exists in the bucket.
 * The item list is only allocated if needed.
 */
struct FHashBucket
{
	friend struct FHashBucketIterator;

	/** This always empty set is used to get an iterator if the bucket doesn't use a TSet (has only 1 element) */
	static TSet<UObjectBase*> EmptyBucket;

	/*
	* If these are both null, this bucket is empty
	* If the first one is null, but the second one is non-null, then the second one is a TSet pointer
	* If the first one is not null, then it is a uobject ptr, and the second ptr is either null or a second element
	*/
	void *ElementsOrSetPtr[2];

#if !UE_BUILD_SHIPPING
	/** If true this bucket is being iterated over and no Add or Remove operations are allowed */
	int32 ReadOnlyLock;

	FORCEINLINE void Lock()
	{
		ReadOnlyLock++;
	}

	FORCEINLINE void Unlock()
	{
		ReadOnlyLock--;
		check(ReadOnlyLock >= 0);
	}
#endif // !UE_BUILD_SHIPPING

	FORCEINLINE TSet<UObjectBase*>* GetSet()
	{
		if (ElementsOrSetPtr[1] && !ElementsOrSetPtr[0])
		{
			return (TSet<UObjectBase*>*)ElementsOrSetPtr[1];
		}
		return nullptr;
	}

	FORCEINLINE const TSet<UObjectBase*>* GetSet() const
	{
		if (ElementsOrSetPtr[1] && !ElementsOrSetPtr[0])
		{
			return (TSet<UObjectBase*>*)ElementsOrSetPtr[1];
		}
		return nullptr;
	}

	/** Constructor */
	FORCEINLINE FHashBucket()
	{
		ElementsOrSetPtr[0] = nullptr;
		ElementsOrSetPtr[1] = nullptr;
#if !UE_BUILD_SHIPPING
		ReadOnlyLock = 0;
#endif // !UE_BUILD_SHIPPING
	}
	FORCEINLINE ~FHashBucket()
	{
		delete GetSet();
	}
	/** Adds an Object to the bucket */
	FORCEINLINE void Add(UObjectBase* Object)
	{
#if !UE_BUILD_SHIPPING
		UE_CLOG(ReadOnlyLock != 0, LogObj, Fatal, TEXT("Trying to add %s to a hash bucket that is currently being iterated over which is not allowed and may lead to undefined behavior!"),
			*static_cast<UObject*>(Object)->GetFullName());
#endif // !UE_BUILD_SHIPPING

		TSet<UObjectBase*>* Items = GetSet();
		if (Items)
		{
			Items->Add(Object);
		}
		else if (ElementsOrSetPtr[0] && ElementsOrSetPtr[1])
		{
			Items = new TSet<UObjectBase*>();
			Items->Add((UObjectBase*)ElementsOrSetPtr[0]);
			Items->Add((UObjectBase*)ElementsOrSetPtr[1]);
			Items->Add(Object);
			ElementsOrSetPtr[0] = nullptr;
			ElementsOrSetPtr[1] = Items;
		}
		else if (ElementsOrSetPtr[0])
		{
			ElementsOrSetPtr[1] = Object;
		}
		else
		{
			ElementsOrSetPtr[0] = Object;
			checkSlow(!ElementsOrSetPtr[1]);
		}
	}
	/** Removes an Object from the bucket */
	FORCEINLINE int32 Remove(UObjectBase* Object)
	{
#if !UE_BUILD_SHIPPING
		UE_CLOG(ReadOnlyLock != 0, LogObj, Fatal, TEXT("Trying to remove %s from a hash bucket that is currently being iterated over which is not allowed and may lead to undefined behavior!"),
			*static_cast<UObject*>(Object)->GetFullName());
#endif // !UE_BUILD_SHIPPING

		int32 Result = 0;
		TSet<UObjectBase*>* Items = GetSet();
		if (Items)
		{
			Result = Items->Remove(Object);
			if (Items->Num() <= 2)
			{
				auto It = TSet<UObjectBase*>::TIterator(*Items);
				ElementsOrSetPtr[0] = *It;
				checkSlow((bool)It);
				++It;
				ElementsOrSetPtr[1] = *It;
				delete Items;
			}
		}
		else if (Object == ElementsOrSetPtr[1])
		{
			Result = 1;
			ElementsOrSetPtr[1] = nullptr;
		}
		else if (Object == ElementsOrSetPtr[0])
		{
			Result = 1;
			ElementsOrSetPtr[0] = ElementsOrSetPtr[1];
			ElementsOrSetPtr[1] = nullptr;
		}
		return Result;
	}
	/** Checks if an Object exists in this bucket */
	FORCEINLINE bool Contains(UObjectBase* Object) const
	{
		const TSet<UObjectBase*>* Items = GetSet();
		if (Items)
		{
			return Items->Contains(Object);
		}
		return Object == ElementsOrSetPtr[0] || Object == ElementsOrSetPtr[1];
	}
	/** Returns the number of Objects in this bucket */
	FORCEINLINE int32 Num() const
	{
		const TSet<UObjectBase*>* Items = GetSet();
		if (Items)
		{
			return Items->Num();
		}
		return !!ElementsOrSetPtr[0] + !!ElementsOrSetPtr[1];
	}
	/** Returns the amount of memory allocated for and by Items TSet */
	FORCEINLINE uint32 GetItemsSize() const
	{
		const TSet<UObjectBase*>* Items = GetSet();
		if (Items)
		{
			return (uint32)sizeof(*Items) + Items->GetAllocatedSize();
		}
		return 0;
	}
	void Compact()
	{
		TSet<UObjectBase*>* Items = GetSet();
		if (Items)
		{
			Items->Compact();
		}
	}
private:
	/** Gets an iterator for the TSet in this bucket or for the EmptyBucker if Items is null */
	FORCEINLINE TSet<UObjectBase*>::TIterator GetIteratorForSet()
	{
		TSet<UObjectBase*>* Items = GetSet();
		return Items ? Items->CreateIterator() : EmptyBucket.CreateIterator();
	}
};
TSet<UObjectBase*> FHashBucket::EmptyBucket;

/** Hash Bucket Iterator. Iterates over all Objects in the bucket */
struct FHashBucketIterator
{
	FHashBucket& Bucket;
	TSet<UObjectBase*>::TIterator SetIterator;
	bool bItems;
	bool bReachedEndNoItems;
	bool bSecondItem;

	FORCEINLINE FHashBucketIterator(FHashBucket& InBucket)
		: Bucket(InBucket)
		, SetIterator(InBucket.GetIteratorForSet())
		, bItems(!!InBucket.GetSet())
		, bReachedEndNoItems(!InBucket.ElementsOrSetPtr[0] && !InBucket.ElementsOrSetPtr[1])
		, bSecondItem(false)
	{
	}
	/** Advances the iterator to the next element. */
	FORCEINLINE FHashBucketIterator& operator++()
	{
		if (bItems)
		{
			++SetIterator;
		}
		else
		{
			bReachedEndNoItems = bSecondItem || !Bucket.ElementsOrSetPtr[1];
			bSecondItem = true;
		}
		return *this;
	}
	/** conversion to "bool" returning true if the iterator is valid. */
	FORCEINLINE explicit operator bool() const
	{
		if (bItems)
		{
			return (bool)SetIterator;
		}
		else
		{
			return !bReachedEndNoItems;
		}
	}
	/** inverse of the "bool" operator */
	FORCEINLINE bool operator !() const
	{
		return !(bool)*this;
	}
	FORCEINLINE UObjectBase*& operator*()
	{
		if (bItems)
		{
			return *SetIterator;
		}
		else
		{
			return (UObjectBase*&)Bucket.ElementsOrSetPtr[!!bSecondItem];
		}
	}
};

class FUObjectHashTables
{
	/** Critical section that guards against concurrent adds from multiple threads */
	FCriticalSection CriticalSection;

public:

	/** Hash sets */
	TMap<int32, FHashBucket> Hash;
	TMultiMap<int32, class UObjectBase*> HashOuter;

	/** Map of object to their outers, used to avoid an object iterator to find such things. **/
	TMap<UObjectBase*, FHashBucket> ObjectOuterMap;
	TMap<UClass*, FHashBucket> ClassToObjectListMap;
	TMap<UClass*, TSet<UClass*>> ClassToChildListMap;
	TAtomic<uint64> ClassToChildListMapVersion;

	/** Map of package to the object their contain. */
	TMap<UPackage*, FHashBucket> PackageToObjectListMap;
	/** Map of object to their external package. */
	TMap<UObjectBase*, UPackage*> ObjectToPackageMap;

	FUObjectHashTables()
		: ClassToChildListMapVersion(0)
	{
	}

	void ShrinkMaps()
	{
		double StartTime = FPlatformTime::Seconds();
		Hash.Compact();
		for (auto& Pair : Hash)
		{
			Pair.Value.Compact();
		}
		HashOuter.Compact();
		ObjectOuterMap.Compact();
		for (auto& Pair : ObjectOuterMap)
		{
			Pair.Value.Compact();
		}
		ClassToObjectListMap.Compact();
		for (auto& Pair : ClassToObjectListMap)
		{
			Pair.Value.Compact();
		}
		ClassToChildListMap.Compact();
		for (auto& Pair : ClassToChildListMap)
		{
			Pair.Value.Compact();
		}
		PackageToObjectListMap.Compact();
		for (auto& Pair : PackageToObjectListMap)
		{
			Pair.Value.Compact();
		}
		ObjectToPackageMap.Compact();
		UE_LOG(LogUObjectHash, Log, TEXT("Compacting FUObjectHashTables data took %6.2fms"), 1000.0f * float(FPlatformTime::Seconds() - StartTime));
	}

	/** Checks if the Hash/Object pair exists in the FName hash table */
	FORCEINLINE bool PairExistsInHash(int32 InHash, UObjectBase* Object)
	{
		bool bResult = false;
		FHashBucket* Bucket = Hash.Find(InHash);
		if (Bucket)
		{
			bResult = Bucket->Contains(Object);
		}
		return bResult;
	}
	/** Adds the Hash/Object pair to the FName hash table */
	FORCEINLINE void AddToHash(int32 InHash, UObjectBase* Object)
	{
		FHashBucket& Bucket = Hash.FindOrAdd(InHash);
		Bucket.Add(Object);
	}
	/** Removes the Hash/Object pair from the FName hash table */
	FORCEINLINE int32 RemoveFromHash(int32 InHash, UObjectBase* Object)
	{
		int32 NumRemoved = 0;
		FHashBucket* Bucket = Hash.Find(InHash);
		if (Bucket)
		{
			NumRemoved = Bucket->Remove(Object);
			if (Bucket->Num() == 0)
			{
				Hash.Remove(InHash);
			}
		}
		return NumRemoved;
	}

	FORCEINLINE void Lock()
	{
		CriticalSection.Lock();
	}

	FORCEINLINE void Unlock()
	{
		CriticalSection.Unlock();
	}

	static FUObjectHashTables& Get()
	{
		static FUObjectHashTables Singleton;
		return Singleton;
	}
};

class FHashTableLock
{
#if THREADSAFE_UOBJECTS
	FUObjectHashTables* Tables;
#endif
public:
	FORCEINLINE FHashTableLock(FUObjectHashTables& InTables)
	{
#if THREADSAFE_UOBJECTS
		if (!(IsGarbageCollecting() && IsInGameThread()))
		{
			Tables = &InTables;
			InTables.Lock();
		}
		else
		{
			Tables = nullptr;
		}
#else
		check(IsInGameThread());
#endif
	}
	FORCEINLINE ~FHashTableLock()
	{
#if THREADSAFE_UOBJECTS
		if (Tables)
		{
			Tables->Unlock();
		}
#endif
	}
};

/**
 * Calculates the object's hash just using the object's name index
 *
 * @param ObjName the object's name to use the index of
 */
static FORCEINLINE int32 GetObjectHash(FName ObjName)
{
	return GetTypeHash(ObjName);
}

/**
 * Calculates the object's hash just using the object's name index
 * XORed with the outer. Yields much better spread in the hash
 * buckets, but requires knowledge of the outer, which isn't available
 * in all cases.
 *
 * @param ObjName the object's name to use the index of
 * @param Outer the object's outer pointer treated as an int32
 */
static FORCEINLINE int32 GetObjectOuterHash(FName ObjName,PTRINT Outer)
{
	return GetTypeHash(ObjName) + (Outer >> 6);
}

UObject* StaticFindObjectFastExplicitThreadSafe(FUObjectHashTables& ThreadHash, const UClass* ObjectClass, FName ObjectName, const FString& ObjectPathName, bool bExactClass, EObjectFlags ExcludeFlags/*=0*/)
{
	const EInternalObjectFlags ExclusiveInternalFlags = EInternalObjectFlags::Unreachable;

	// Find an object with the specified name and (optional) class, in any package; if bAnyPackage is false, only matches top-level packages
	int32 Hash = GetObjectHash(ObjectName);
	FHashTableLock HashLock(ThreadHash);
	FHashBucket* Bucket = ThreadHash.Hash.Find(Hash);
	if (Bucket)
	{
		for (FHashBucketIterator It(*Bucket); It; ++It)
		{
			UObject* Object = (UObject*)*It;
			if
				((Object->GetFName() == ObjectName)

				/* Don't return objects that have any of the exclusive flags set */
				&& !Object->HasAnyFlags(ExcludeFlags)

				&& !Object->HasAnyInternalFlags(ExclusiveInternalFlags)

				/** If a class was specified, check that the object is of the correct class */
				&& (ObjectClass == nullptr || (bExactClass ? Object->GetClass() == ObjectClass : Object->IsA(ObjectClass)))
				)

			{
				FString ObjectPath = Object->GetPathName();
				/** Finally check the explicit path */
				if (ObjectPath == ObjectPathName)
				{
					checkf(!Object->IsUnreachable(), TEXT("%s"), *Object->GetFullName());
					return Object;
				}
			}
		}
	}

	return nullptr;
}

/**
 * Variation of StaticFindObjectFast that uses explicit path.
 *
 * @param	ObjectClass		The to be found object's class
 * @param	ObjectName		The to be found object's class
 * @param	ObjectPathName	Full path name for the object to search for
 * @param	ExactClass		Whether to require an exact match with the passed in class
 * @param	ExclusiveFlags	Ignores objects that contain any of the specified exclusive flags
 * @return	Returns a pointer to the found object or nullptr if none could be found
 */
UObject* StaticFindObjectFastExplicit( const UClass* ObjectClass, FName ObjectName, const FString& ObjectPathName, bool bExactClass, EObjectFlags ExcludeFlags/*=0*/ )
{
	checkSlow(FPackageName::IsShortPackageName(ObjectName)); //@Package name transition, we aren't checking the name here because we know this is only used for texture

	// Find an object with the specified name and (optional) class, in any package; if bAnyPackage is false, only matches top-level packages
	const int32 Hash = GetObjectHash( ObjectName );
	FUObjectHashTables& ThreadHash = FUObjectHashTables::Get();
	UObject* Result = StaticFindObjectFastExplicitThreadSafe( ThreadHash, ObjectClass, ObjectName, ObjectPathName, bExactClass, ExcludeFlags );

	return Result;
}

static bool NameEndsWith(FName Name, FName Suffix)
{
	if (Name == Suffix)
	{
		return true;
	}

	if (Name.GetNumber() != Suffix.GetNumber())
	{
		return false;
	}
		
	TCHAR PlainName[NAME_SIZE];
	TCHAR PlainSuffix[NAME_SIZE];
	uint32 NameLen = Name.GetPlainNameString(PlainName);
	uint32 SuffixLen = Suffix.GetPlainNameString(PlainSuffix);

	return NameLen >= SuffixLen && FCString::Strnicmp(PlainName + NameLen - SuffixLen, PlainSuffix, SuffixLen) == 0;
}

// Splits an object path into FNames representing an outer chain.
//
// Input path examples: "Object", "Package.Object", "Object:Subobject", "Object:Subobject.Nested", "Package.Object:Subobject", "Package.Object:Subobject.NestedSubobject"
struct FObjectSearchPath
{
	FName Inner;
	TArray<FName, TInlineAllocator<8>> Outers;

	explicit FObjectSearchPath(FName InPath)
	{
		TCHAR Buffer[NAME_SIZE];
		InPath.GetPlainNameString(Buffer);

		constexpr FAsciiSet DotColon(".:");
		const TCHAR* Begin = Buffer;
		const TCHAR* End = FAsciiSet::FindFirstOrEnd(Begin, DotColon);
		while (*End != '\0')
		{
			Outers.Add(FName(End - Begin, Begin));

			Begin = End + 1;
			End = FAsciiSet::FindFirstOrEnd(Begin, DotColon);
		}

		Inner = Outers.Num() == 0 ? InPath : FName(FStringView(Begin, End - Begin), InPath.GetNumber());
	}

	bool MatchOuterNames(UObject* Outer) const
	{
		if (Outers.Num() == 0)
		{
			return true;
		}

		for (int32 Idx = Outers.Num() - 1; Idx > 0; --Idx)
		{
			if (!Outer || Outer->GetFName() != Outers[Idx])
			{
				return false;
			}

			Outer = Outer->GetOuter();
		}

		return Outer && NameEndsWith(Outer->GetFName(), Outers[0]);
	}
};

UObject* StaticFindObjectInPackageInternal(FUObjectHashTables& ThreadHash, const UClass* ObjectClass, const UPackage* ObjectPackage, FName ObjectName, bool bExactClass, EObjectFlags ExcludeFlags, EInternalObjectFlags ExclusiveInternalFlags)
{
	ExclusiveInternalFlags |= EInternalObjectFlags::Unreachable;
	UObject* Result = nullptr;
	if (FHashBucket* Inners = ThreadHash.PackageToObjectListMap.Find(ObjectPackage))
	{
#if !UE_BUILD_SHIPPING
		Inners->Lock();
#endif // !UE_BUILD_SHIPPING
		for (FHashBucketIterator It(*Inners); It; ++It)
		{
			UObject* Object = static_cast<UObject*>(*It);
			if
				/* check that the name matches the name we're searching for */
				((Object->GetFName() == ObjectName)

					/* Don't return objects that have any of the exclusive flags set */
					&& !Object->HasAnyFlags(ExcludeFlags)

					/** Do not return ourselves (Packages currently have themselves as their package. )*/
					&& Object != ObjectPackage

					/** If a class was specified, check that the object is of the correct class */
					&& (ObjectClass == nullptr || (bExactClass ? Object->GetClass() == ObjectClass : Object->IsA(ObjectClass)))

					/** Include (or not) pending kill objects */
					&& !Object->HasAnyInternalFlags(ExclusiveInternalFlags))
			{
				checkf(!Object->IsUnreachable(), TEXT("%s"), *Object->GetFullName());
				Result = Object;
				break;
			}
		}
#if !UE_BUILD_SHIPPING
		Inners->Unlock();
#endif // !UE_BUILD_SHIPPING
	}
	return Result;
}

UObject* StaticFindObjectFastInternalThreadSafe(FUObjectHashTables& ThreadHash, const UClass* ObjectClass, const UObject* ObjectPackage, FName ObjectName, bool bExactClass, bool bAnyPackage, EObjectFlags ExcludeFlags, EInternalObjectFlags ExclusiveInternalFlags)
{
	ExclusiveInternalFlags |= EInternalObjectFlags::Unreachable;

	// If they specified an outer use that during the hashing
	UObject* Result = nullptr;
	if (ObjectPackage != nullptr)
	{
		int32 Hash = GetObjectOuterHash(ObjectName, (PTRINT)ObjectPackage);
		FHashTableLock HashLock(ThreadHash);
		for (TMultiMap<int32, class UObjectBase*>::TConstKeyIterator HashIt(ThreadHash.HashOuter, Hash); HashIt; ++HashIt)
		{
			UObject *Object = (UObject *)HashIt.Value();
			if
				/* check that the name matches the name we're searching for */
				((Object->GetFName() == ObjectName)

				/* Don't return objects that have any of the exclusive flags set */
				&& !Object->HasAnyFlags(ExcludeFlags)

				/* check that the object has the correct Outer */
				&& Object->GetOuter() == ObjectPackage

				/** If a class was specified, check that the object is of the correct class */
				&& (ObjectClass == nullptr || (bExactClass ? Object->GetClass() == ObjectClass : Object->IsA(ObjectClass)))
				
				/** Include (or not) pending kill objects */
				&& !Object->HasAnyInternalFlags(ExclusiveInternalFlags))
			{
				checkf(!Object->IsUnreachable(), TEXT("%s"), *Object->GetFullName());
				if (Result)
				{
					UE_LOG(LogUObjectHash, Warning, TEXT("Ambiguous search, could be %s or %s"), *GetFullNameSafe(Result), *GetFullNameSafe(Object));
				}
				else
				{
					Result = Object;
				}
#if (UE_BUILD_SHIPPING || UE_BUILD_TEST)
				break;
#endif
			}
		}

#if WITH_EDITOR
		// if the search fail and the OuterPackage is a UPackage, lookup potential external package
		if (Result == nullptr && ObjectPackage->IsA(UPackage::StaticClass()))
		{
			Result = StaticFindObjectInPackageInternal(ThreadHash, ObjectClass, static_cast<const UPackage*>(ObjectPackage), ObjectName, bExactClass, ExcludeFlags, ExclusiveInternalFlags);
		}
#endif
	}
	else
	{
		FObjectSearchPath SearchPath(ObjectName);

		const int32 Hash = GetObjectHash(SearchPath.Inner);
		FHashTableLock HashLock(ThreadHash);

		FHashBucket* Bucket = ThreadHash.Hash.Find(Hash);
		if (Bucket)
		{
			for (FHashBucketIterator It(*Bucket); It; ++It)
			{
				UObject* Object = (UObject*)*It;

				if
					((Object->GetFName() == SearchPath.Inner)

					/* Don't return objects that have any of the exclusive flags set */
					&& !Object->HasAnyFlags(ExcludeFlags)

					/*If there is no package (no InObjectPackage specified, and InName's package is "")
					and the caller specified any_package, then accept it, regardless of its package.
					Or, if the object is a top-level package then accept it immediately.*/
					&& (bAnyPackage || !Object->GetOuter())

					/** If a class was specified, check that the object is of the correct class */
					&& (ObjectClass == nullptr || (bExactClass ? Object->GetClass() == ObjectClass : Object->IsA(ObjectClass)))

					/** Include (or not) pending kill objects */
					&& !Object->HasAnyInternalFlags(ExclusiveInternalFlags)

					/** Ensure that the partial path provided matches the object found */
					&& SearchPath.MatchOuterNames(Object->GetOuter()))
				{
					checkf(!Object->IsUnreachable(), TEXT("%s"), *Object->GetFullName());
					if (Result)
					{
						UE_LOG(LogUObjectHash, Warning, TEXT("Ambiguous path search, could be %s or %s"), *GetFullNameSafe(Result), *GetFullNameSafe(Object));
					}
					else
					{
						Result = Object;
					}
#if (UE_BUILD_SHIPPING || UE_BUILD_TEST)
					break;
#endif
				}
			}
		}
	}
	// Not found.
	return Result;
}

UObject* StaticFindObjectFastInternal(const UClass* ObjectClass, const UObject* ObjectPackage, FName ObjectName, bool bExactClass, bool bAnyPackage, EObjectFlags ExcludeFlags, EInternalObjectFlags ExclusiveInternalFlags)
{
	INC_DWORD_STAT(STAT_FindObjectFast);

	check(ObjectPackage != ANY_PACKAGE); // this could never have returned anything but nullptr
	// If they specified an outer use that during the hashing
	FUObjectHashTables& ThreadHash = FUObjectHashTables::Get();
	UObject* Result = StaticFindObjectFastInternalThreadSafe(ThreadHash, ObjectClass, ObjectPackage, ObjectName, bExactClass, bAnyPackage, ExcludeFlags | RF_NewerVersionExists, ExclusiveInternalFlags);
	return Result;
}

// Assumes that ThreadHash's critical is already locked
FORCEINLINE static void AddToOuterMap(FUObjectHashTables& ThreadHash, UObjectBase* Object)
{
	FHashBucket& Bucket = ThreadHash.ObjectOuterMap.FindOrAdd(Object->GetOuter());
	checkSlow(!Bucket.Contains(Object)); // if it already exists, something is wrong with the external code
	Bucket.Add(Object);
}

// Assumes that ThreadHash's critical is already locked
FORCEINLINE static void AddToClassMap(FUObjectHashTables& ThreadHash, UObjectBase* Object)
{
	{
		check(Object->GetClass());
		FHashBucket& ObjectList = ThreadHash.ClassToObjectListMap.FindOrAdd(Object->GetClass());
		ObjectList.Add(Object);
	}

	UObjectBaseUtility* ObjectWithUtility = static_cast<UObjectBaseUtility*>(Object);
	if ( ObjectWithUtility->IsA(UClass::StaticClass()) )
	{
		UClass* Class = static_cast<UClass*>(ObjectWithUtility);
		UClass* SuperClass = Class->GetSuperClass();
		if ( SuperClass )
		{
			TSet<UClass*>& ChildList = ThreadHash.ClassToChildListMap.FindOrAdd(SuperClass);
			bool bIsAlreadyInSetPtr = false;
			ChildList.Add(Class, &bIsAlreadyInSetPtr);
			ThreadHash.ClassToChildListMapVersion++;
			check(!bIsAlreadyInSetPtr); // if it already exists, something is wrong with the external code
		}
	}
}

// Assumes that ThreadHash's critical is already locked
FORCEINLINE static void AddToPackageMap(FUObjectHashTables& ThreadHash, UObjectBase* Object, UPackage* Package)
{
	check(Package != nullptr);
	FHashBucket& Bucket = ThreadHash.PackageToObjectListMap.FindOrAdd(Package);
	checkSlow(!Bucket.Contains(Object)); // if it already exists, something is wrong with the external code
	Bucket.Add(Object);
}

// Assumes that ThreadHash's critical is already locked
FORCEINLINE static UPackage* AssignExternalPackageToObject(FUObjectHashTables& ThreadHash, UObjectBase* Object, UPackage* Package)
{
	UPackage*& ExternalPackageRef = ThreadHash.ObjectToPackageMap.FindOrAdd(Object);
	UPackage* OldPackage = ExternalPackageRef;
	ExternalPackageRef = Package;
	return OldPackage;
}


// Assumes that ThreadHash's critical is already locked
FORCEINLINE static void RemoveFromOuterMap(FUObjectHashTables& ThreadHash, UObjectBase* Object)
{
	FHashBucket& Bucket = ThreadHash.ObjectOuterMap.FindOrAdd(Object->GetOuter());
	int32 NumRemoved = Bucket.Remove(Object);

	UE_CLOG(NumRemoved != 1, LogUObjectHash, Fatal, TEXT("Internal Error: RemoveFromOuterMap NumRemoved = %d  for %s"), NumRemoved, *GetFullNameSafe((UObjectBaseUtility*)Object));

	if (!Bucket.Num())
	{
		ThreadHash.ObjectOuterMap.Remove(Object->GetOuter());
	}
}

// Assumes that ThreadHash's critical is already locked
FORCEINLINE static void RemoveFromClassMap(FUObjectHashTables& ThreadHash, UObjectBase* Object)
{
	UObjectBaseUtility* ObjectWithUtility = static_cast<UObjectBaseUtility*>(Object);

	{
		FHashBucket& ObjectList = ThreadHash.ClassToObjectListMap.FindOrAdd(Object->GetClass());
		int32 NumRemoved = ObjectList.Remove(Object);

		// must have existed, else something is wrong with the external code
		UE_CLOG(NumRemoved != 1, LogUObjectHash, Fatal, TEXT("Internal Error: RemoveFromClassMap NumRemoved = %d  for %s"), NumRemoved, *GetFullNameSafe(ObjectWithUtility));

		if (!ObjectList.Num())
		{
			ThreadHash.ClassToObjectListMap.Remove(Object->GetClass());
		}
	}

	if ( ObjectWithUtility->IsA(UClass::StaticClass()) )
	{
		UClass* Class = static_cast<UClass*>(ObjectWithUtility);
		UClass* SuperClass = Class->GetSuperClass();
		if ( SuperClass )
		{
			// Remove the class from the SuperClass' child list
			TSet<UClass*>& ChildList = ThreadHash.ClassToChildListMap.FindOrAdd(SuperClass);
			int32 NumRemoved = ChildList.Remove(Class);

			// must have existed, else something is wrong with the external code
			UE_CLOG(NumRemoved != 1, LogUObjectHash, Fatal, TEXT("Internal Error: RemoveFromClassMap NumRemoved = %d  for %s"), NumRemoved, *GetFullNameSafe(ObjectWithUtility));

			if (!ChildList.Num())
			{
				ThreadHash.ClassToChildListMap.Remove(SuperClass);
			}
			ThreadHash.ClassToChildListMapVersion++;
		}
	}
}

// Assumes that ThreadHash's critical is already locked
FORCEINLINE static void RemoveFromPackageMap(FUObjectHashTables& ThreadHash, UObjectBase* Object, UPackage* Package)
{
	check(Package != nullptr);
	FHashBucket& Bucket = ThreadHash.PackageToObjectListMap.FindOrAdd(Package);
	int32 NumRemoved = Bucket.Remove(Object);

	UE_CLOG(NumRemoved != 1, LogUObjectHash, Fatal, TEXT("Internal Error: RemoveFromPackageMap NumRemoved = %d  for %s"), NumRemoved, *GetFullNameSafe((UObjectBaseUtility*)Object));

	if (!Bucket.Num())
	{
		ThreadHash.PackageToObjectListMap.Remove(Package);
	}
}

// Assumes that ThreadHash's critical is already locked
FORCEINLINE static UPackage* UnassignExternalPackageFromObject(FUObjectHashTables& ThreadHash, UObjectBase* Object)
{
	UPackage* OldPackage = nullptr;
	ThreadHash.ObjectToPackageMap.RemoveAndCopyValue(Object, OldPackage);
	return OldPackage;
}

void ShrinkUObjectHashTables()
{
	FUObjectHashTables& ThreadHash = FUObjectHashTables::Get();
	FHashTableLock HashLock(ThreadHash);
	ThreadHash.ShrinkMaps();
}

uint64 GetRegisteredClassesVersionNumber()
{
	FUObjectHashTables& ThreadHash = FUObjectHashTables::Get();
	return ThreadHash.ClassToChildListMapVersion;
}

static void ShrinkUObjectHashTablesDel(const TArray<FString>& Args)
{
	ShrinkUObjectHashTables();
}

static FAutoConsoleCommand ShrinkUObjectHashTablesCmd(
	TEXT("ShrinkUObjectHashTables"),
	TEXT("Shrinks all of the UObject hash tables."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&ShrinkUObjectHashTablesDel)
);

void GetObjectsWithOuter(const class UObjectBase* Outer, TArray<UObject *>& Results, bool bIncludeNestedObjects, EObjectFlags ExclusionFlags, EInternalObjectFlags ExclusionInternalFlags)
{
	checkf(Outer != nullptr, TEXT("Getting objects with a null outer is no longer supported. If you want to get all packages you might consider using GetObjectsOfClass instead."));

#if WITH_EDITOR
	// uncomment ensure to more easily find place where GetObjectsWithOuter should be replaced with GetObjectsWithPackage
	if (!/*ensure*/(!Outer->GetClass()->IsChildOf(UPackage::StaticClass())))
	{
		GetObjectsWithPackage((UPackage*)Outer, Results, bIncludeNestedObjects, ExclusionFlags, ExclusionInternalFlags);
		return;
	}
#endif

	// We don't want to return any objects that are currently being background loaded unless we're using the object iterator during async loading.
	ExclusionInternalFlags |= EInternalObjectFlags::Unreachable;
	if (!IsInAsyncLoadingThread())
	{
		ExclusionInternalFlags |= EInternalObjectFlags::AsyncLoading;
	}
	int32 StartNum = Results.Num();
	FUObjectHashTables& ThreadHash = FUObjectHashTables::Get();
	FHashTableLock HashLock(ThreadHash);
	FHashBucket* Inners = ThreadHash.ObjectOuterMap.Find(Outer);
	if (Inners)
	{
		for (FHashBucketIterator It(*Inners); It; ++It)
		{
			UObject *Object = static_cast<UObject *>(*It);
			if (!Object->HasAnyFlags(ExclusionFlags) && !Object->HasAnyInternalFlags(ExclusionInternalFlags))
			{
				Results.Add(Object);
			}
		}
		int32 MaxResults = GUObjectArray.GetObjectArrayNum();
		while (StartNum != Results.Num() && bIncludeNestedObjects)
		{
			int32 RangeStart = StartNum;
			int32 RangeEnd = Results.Num();
			StartNum = RangeEnd;
			for (int32 Index = RangeStart; Index < RangeEnd; Index++)
			{
				FHashBucket* InnerInners = ThreadHash.ObjectOuterMap.Find(Results[Index]);
				if (InnerInners)
				{
					for (FHashBucketIterator It(*InnerInners); It; ++It)
					{
						UObject *Object = static_cast<UObject *>(*It);
						if (!Object->HasAnyFlags(ExclusionFlags) && !Object->HasAnyInternalFlags(ExclusionInternalFlags))
						{
							Results.Add(Object);
						}
					}
				}
			}
			check(Results.Num() <= MaxResults); // otherwise we have a cycle in the outer chain, which should not be possible
		}
	}
}

void ForEachObjectWithOuter(const class UObjectBase* Outer, TFunctionRef<void(UObject*)> Operation, bool bIncludeNestedObjects, EObjectFlags ExclusionFlags, EInternalObjectFlags ExclusionInternalFlags)
{
	checkf(Outer != nullptr, TEXT("Getting objects with a null outer is no longer supported. If you want to get all packages you might consider using GetObjectsOfClass instead."));
	
#if WITH_EDITOR
	// uncomment ensure to more easily find place where ForEachObjectWithOuter should be replaced with ForEachObjectWithPackage
	if (!/*ensure*/(!Outer->GetClass()->IsChildOf(UPackage::StaticClass())))
	{
		ForEachObjectWithPackage((UPackage*)Outer, [Operation](UObject* InObject) { Operation(InObject); return true; }, bIncludeNestedObjects, ExclusionFlags, ExclusionInternalFlags);
		return;
	}
#endif

	// We don't want to return any objects that are currently being background loaded unless we're using the object iterator during async loading.
	ExclusionInternalFlags |= EInternalObjectFlags::Unreachable;
	if (!IsInAsyncLoadingThread())
	{
		ExclusionInternalFlags |= EInternalObjectFlags::AsyncLoading;
	}
	FUObjectHashTables& ThreadHash = FUObjectHashTables::Get();
	FHashTableLock HashLock(ThreadHash);
	TArray<FHashBucket*, TInlineAllocator<1> > AllInners;

	if (FHashBucket* Inners = ThreadHash.ObjectOuterMap.Find(Outer))
	{
		AllInners.Add(Inners);
	}
	while (AllInners.Num())
	{
		FHashBucket* Inners = AllInners.Pop();
#if !UE_BUILD_SHIPPING
		Inners->Lock();
#endif // !UE_BUILD_SHIPPING
		for (FHashBucketIterator It(*Inners); It; ++It)
		{
			UObject *Object = static_cast<UObject*>(*It);
			if (!Object->HasAnyFlags(ExclusionFlags) && !Object->HasAnyInternalFlags(ExclusionInternalFlags))
			{
				Operation(Object);
			}
			if (bIncludeNestedObjects)
			{
				if (FHashBucket* ObjectInners = ThreadHash.ObjectOuterMap.Find(Object))
				{
					AllInners.Add(ObjectInners);
				}
			}
		}
#if !UE_BUILD_SHIPPING
		Inners->Unlock();
#endif // !UE_BUILD_SHIPPING
	}
}

UObjectBase* FindObjectWithOuter(const class UObjectBase* Outer, const class UClass* ClassToLookFor, FName NameToLookFor)
{
	UObject* Result = nullptr;
	check( Outer );
	// We don't want to return any objects that are currently being background loaded unless we're using the object iterator during async loading.
	EInternalObjectFlags ExclusionInternalFlags = EInternalObjectFlags::Unreachable;
	if (!IsInAsyncLoadingThread())
	{
		ExclusionInternalFlags = EInternalObjectFlags::AsyncLoading;
	}

	if( NameToLookFor != NAME_None )
	{
		Result = StaticFindObjectFastInternal(ClassToLookFor, static_cast<const UObject*>(Outer), NameToLookFor, false, false, RF_NoFlags, ExclusionInternalFlags);
	}
	else
	{
		FUObjectHashTables& ThreadHash = FUObjectHashTables::Get();
		FHashTableLock HashLock(ThreadHash);
		FHashBucket* Inners = ThreadHash.ObjectOuterMap.Find(Outer);
		if (Inners)
		{
			for (FHashBucketIterator It(*Inners); It; ++It)
			{
				UObject *Object = static_cast<UObject *>(*It);
				if (Object->HasAnyInternalFlags(ExclusionInternalFlags))
				{
					continue;
				}
				if (ClassToLookFor && !Object->IsA(ClassToLookFor))
				{
					continue;
				}
				Result = Object;
				break;
			}
		}
	}
	return Result;
}

void GetObjectsWithPackage(const class UPackage* Package, TArray<UObject *>& Results, bool bIncludeNestedObjects, EObjectFlags ExclusionFlags, EInternalObjectFlags ExclusionInternalFlags)
{
	ForEachObjectWithPackage(Package, [&Results](UObject* Object)
	{
		Results.Add(Object);
		return true;
	}, bIncludeNestedObjects, ExclusionFlags, ExclusionInternalFlags);
}

void ForEachObjectWithPackage(const class UPackage* Package, TFunctionRef<bool(UObject*)> Operation, bool bIncludeNestedObjects, EObjectFlags ExclusionFlags, EInternalObjectFlags ExclusionInternalFlags)
{
	check(Package != nullptr);

	// We don't want to return any objects that are currently being background loaded unless we're using the object iterator during async loading.
	ExclusionInternalFlags |= EInternalObjectFlags::Unreachable;
	if (!IsInAsyncLoadingThread())
	{
		ExclusionInternalFlags |= EInternalObjectFlags::AsyncLoading;
	}
	FUObjectHashTables& ThreadHash = FUObjectHashTables::Get();
	FHashTableLock HashLock(ThreadHash);
	TArray<FHashBucket*, TInlineAllocator<1> > AllInners;

	// Add the object bucket that have this package as an external package
	if (FHashBucket* Inners = ThreadHash.PackageToObjectListMap.Find(Package))
	{
		AllInners.Add(Inners);
	}
	// Add the object bucket that have this package as an outer
	if (FHashBucket* ObjectInners = ThreadHash.ObjectOuterMap.Find(Package))
	{
		AllInners.Add(ObjectInners);
	}
	while (AllInners.Num())
	{
		FHashBucket* Inners = AllInners.Pop();
#if !UE_BUILD_SHIPPING
		Inners->Lock();
#endif // !UE_BUILD_SHIPPING
		for (FHashBucketIterator It(*Inners); It; ++It)
		{
			UObject *Object = static_cast<UObject*>(*It);

			UPackage* ObjectPackage = Object->GetExternalPackageInternal();
			bool bIsInPackage = ObjectPackage == Package || ObjectPackage == nullptr;
			
			if (!Object->HasAnyFlags(ExclusionFlags) && 
				!Object->HasAnyInternalFlags(ExclusionInternalFlags) &&
				bIsInPackage)
			{
				if (!Operation(Object))
				{
					AllInners.Empty(AllInners.Max());
					break;
				}
			}
			if (bIncludeNestedObjects && bIsInPackage)
			{
				if (FHashBucket* ObjectInners = ThreadHash.ObjectOuterMap.Find(Object))
				{
					AllInners.Add(ObjectInners);
				}
			}
		}
#if !UE_BUILD_SHIPPING
		Inners->Unlock();
#endif // !UE_BUILD_SHIPPING
	}
}

/** Helper function that returns all the children of the specified class recursively */
template<typename ClassType, typename ArrayAllocator>
static void RecursivelyPopulateDerivedClasses(FUObjectHashTables& ThreadHash, const UClass* ParentClass, TArray<ClassType, ArrayAllocator>& OutAllDerivedClass)
{
	// Start search with the parent class at virtual index Num-1, then continue searching from index Num as things are added
	int32 SearchIndex = OutAllDerivedClass.Num() - 1;
	const UClass* SearchClass = ParentClass;

	while (1)
	{
		TSet<UClass*>* ChildSet = ThreadHash.ClassToChildListMap.Find(SearchClass);
		if (ChildSet)
		{
			for (UClass* ChildClass : *ChildSet)
			{
				OutAllDerivedClass.Add(ChildClass);
			}
		}

		// Now search at next index, if it has been filled in by code above
		SearchIndex++;

		if (SearchIndex < OutAllDerivedClass.Num())
		{
			SearchClass = OutAllDerivedClass[SearchIndex];			
		}
		else
		{
			return;
		}
	}
}

void GetObjectsOfClass(const UClass* ClassToLookFor, TArray<UObject *>& Results, bool bIncludeDerivedClasses, EObjectFlags ExclusionFlags, EInternalObjectFlags ExclusionInternalFlags)
{
	SCOPE_CYCLE_COUNTER(STAT_Hash_GetObjectsOfClass);

	ForEachObjectOfClass(ClassToLookFor,
		[&Results](UObject* Object)
		{
			Results.Add(Object);
		}
	, bIncludeDerivedClasses, ExclusionFlags, ExclusionInternalFlags);

	check(Results.Num() <= GUObjectArray.GetObjectArrayNum()); // otherwise we have a cycle in the outer chain, which should not be possible
}

FORCEINLINE void ForEachObjectOfClasses_Implementation(FUObjectHashTables& ThreadHash, TArrayView<const UClass*> ClassesToLookFor, TFunctionRef<void(UObject*)> Operation, EObjectFlags ExcludeFlags /*= RF_ClassDefaultObject*/, EInternalObjectFlags ExclusionInternalFlags /*= EInternalObjectFlags::None*/)
{
	// We don't want to return any objects that are currently being background loaded unless we're using the object iterator during async loading.
	ExclusionInternalFlags |= EInternalObjectFlags::Unreachable;
	if (!IsInAsyncLoadingThread())
	{
		ExclusionInternalFlags |= EInternalObjectFlags::AsyncLoading;
	}

	for (const UClass* SearchClass : ClassesToLookFor)
	{
		FHashBucket* List = ThreadHash.ClassToObjectListMap.Find(SearchClass);
		if (List)
		{
			for (FHashBucketIterator ObjectIt(*List); ObjectIt; ++ObjectIt)
			{
				UObject* Object = static_cast<UObject*>(*ObjectIt);
				if (!Object->HasAnyFlags(ExcludeFlags) && !Object->HasAnyInternalFlags(ExclusionInternalFlags))
				{
					Operation(Object);
				}
			}
		}
	}
}

void ForEachObjectOfClass(const UClass* ClassToLookFor, TFunctionRef<void(UObject*)> Operation, bool bIncludeDerivedClasses, EObjectFlags ExclusionFlags, EInternalObjectFlags ExclusionInternalFlags)
{
	// Most classes searched for have around 10 subclasses, some have hundreds
	TArray<const UClass*, TInlineAllocator<16>> ClassesToSearch;
	ClassesToSearch.Add(ClassToLookFor);

	FUObjectHashTables& ThreadHash = FUObjectHashTables::Get();
	FHashTableLock HashLock(ThreadHash);

	if (bIncludeDerivedClasses)
	{
		RecursivelyPopulateDerivedClasses(ThreadHash, ClassToLookFor, ClassesToSearch);
	}

	ForEachObjectOfClasses_Implementation(ThreadHash, ClassesToSearch, Operation, ExclusionFlags, ExclusionInternalFlags);
}

void ForEachObjectOfClasses(TArrayView<const UClass*> ClassesToLookFor, TFunctionRef<void(UObject*)> Operation, EObjectFlags ExcludeFlags /*= RF_ClassDefaultObject*/, EInternalObjectFlags ExclusionInternalFlags /*= EInternalObjectFlags::None*/)
{
	FUObjectHashTables& ThreadHash = FUObjectHashTables::Get();
	FHashTableLock HashLock(ThreadHash);

	ForEachObjectOfClasses_Implementation(ThreadHash, ClassesToLookFor, Operation, ExcludeFlags, ExclusionInternalFlags);
}

void GetDerivedClasses(const UClass* ClassToLookFor, TArray<UClass*>& Results, bool bRecursive)
{
	FUObjectHashTables& ThreadHash = FUObjectHashTables::Get();
	FHashTableLock HashLock(ThreadHash);

	if (bRecursive)
	{
		RecursivelyPopulateDerivedClasses(ThreadHash, ClassToLookFor, Results);
	}
	else
	{
		TSet<UClass*>* DerivedClasses = ThreadHash.ClassToChildListMap.Find(ClassToLookFor);
		if ( DerivedClasses )
		{
			Results.Append( DerivedClasses->Array() );
		}
	}
}

bool ClassHasInstancesAsyncLoading(const UClass* ClassToLookFor)
{
	TArray<const UClass*> ClassesToSearch;
	ClassesToSearch.Add(ClassToLookFor);

	FUObjectHashTables& ThreadHash = FUObjectHashTables::Get();
	FHashTableLock HashLock(ThreadHash);

	RecursivelyPopulateDerivedClasses(ThreadHash, ClassToLookFor, ClassesToSearch);

	for (const UClass* SearchClass : ClassesToSearch)
	{
		FHashBucket* List = ThreadHash.ClassToObjectListMap.Find(SearchClass);
		if (List)
		{
			for (FHashBucketIterator ObjectIt(*List); ObjectIt; ++ObjectIt)
			{
				UObject *Object = static_cast<UObject*>(*ObjectIt);
				if (Object->HasAnyInternalFlags(EInternalObjectFlags::AsyncLoading))
				{
					return true;
				}
			}
		}
	}

	return false;
}

void HashObject(UObjectBase* Object)
{
	SCOPE_CYCLE_COUNTER( STAT_Hash_HashObject );

	FName Name = Object->GetFName();
	if (Name != NAME_None)
	{
		int32 Hash = 0;

		FUObjectHashTables& ThreadHash = FUObjectHashTables::Get();
		FHashTableLock HashLock(ThreadHash);

		Hash = GetObjectHash(Name);
#if !UE_BUILD_TEST && !UE_BUILD_SHIPPING
		// if it already exists, something is wrong with the external code
		UE_CLOG(ThreadHash.PairExistsInHash(Hash, Object), LogUObjectHash, Fatal, TEXT("%s already exists in UObject hash!"), *GetFullNameSafe((UObjectBaseUtility*)Object));
#endif
		ThreadHash.AddToHash(Hash, Object);

		if (PTRINT Outer = (PTRINT)Object->GetOuter())
		{
			Hash = GetObjectOuterHash(Name, Outer);
			checkSlow(!ThreadHash.HashOuter.FindPair(Hash, Object));  
#if !UE_BUILD_TEST && !UE_BUILD_SHIPPING
			// if it already exists, something is wrong with the external code
			UE_CLOG(ThreadHash.HashOuter.FindPair(Hash, Object), LogUObjectHash, Fatal, TEXT("%s already exists in UObject Outer hash!"), *GetFullNameSafe((UObjectBaseUtility*)Object));
#endif
			ThreadHash.HashOuter.Add(Hash, Object);

			AddToOuterMap(ThreadHash, Object);
		}

		AddToClassMap( ThreadHash, Object );
	}
}

/**
 * Remove an object to the name hash tables
 *
 * @param	Object		Object to remove from the hash tables
 */
void UnhashObject(UObjectBase* Object)
{
	SCOPE_CYCLE_COUNTER(STAT_Hash_UnhashObject);

	FName Name = Object->GetFName();
	if (Name != NAME_None)
	{
		int32 Hash = 0;
		int32 NumRemoved = 0;

		FUObjectHashTables& ThreadHash = FUObjectHashTables::Get();
		FHashTableLock LockHash(ThreadHash);

		Hash = GetObjectHash(Name);
		NumRemoved = ThreadHash.RemoveFromHash(Hash, Object);

		// must have existed, else something is wrong with the external code
		UE_CLOG(NumRemoved != 1, LogUObjectHash, Fatal, TEXT("Internal Error: RemoveFromHash NumRemoved = %d  for %s"), NumRemoved, *GetFullNameSafe((UObjectBaseUtility*)Object));

		if (PTRINT Outer = (PTRINT)Object->GetOuter())
		{
			Hash = GetObjectOuterHash(Name, Outer);
			NumRemoved = ThreadHash.HashOuter.RemoveSingle(Hash, Object);

			// must have existed, else something is wrong with the external code
			UE_CLOG(NumRemoved != 1, LogUObjectHash, Fatal, TEXT("Internal Error: Remove from HashOuter NumRemoved = %d  for %s"), NumRemoved, *GetFullNameSafe((UObjectBaseUtility*)Object));

			RemoveFromOuterMap(ThreadHash, Object);
		}

		RemoveFromClassMap( ThreadHash, Object );
	}
}

void HashObjectExternalPackage(UObjectBase* Object, UPackage* Package)
{
	if (Package)
	{
		FUObjectHashTables& ThreadHash = FUObjectHashTables::Get();
		FHashTableLock LockHash(ThreadHash);
		UPackage* OldPackage = AssignExternalPackageToObject(ThreadHash, Object, Package);
		if (OldPackage != Package)
		{
			if (OldPackage)
			{
				RemoveFromPackageMap(ThreadHash, Object, OldPackage);
			}
			AddToPackageMap(ThreadHash, Object, Package);
		}
	}
	else
	{
		UnhashObjectExternalPackage(Object);
	}
}

void UnhashObjectExternalPackage(class UObjectBase* Object)
{
	FUObjectHashTables& ThreadHash = FUObjectHashTables::Get();
	FHashTableLock LockHash(ThreadHash);
	UPackage* Package = UnassignExternalPackageFromObject(ThreadHash, Object);
	if (Package)
	{
		RemoveFromPackageMap(ThreadHash, Object, Package);
	}
}

UPackage* GetObjectExternalPackageThreadSafe(const UObjectBase* Object)
{
	FUObjectHashTables& ThreadHash = FUObjectHashTables::Get();
	FHashTableLock LockHash(ThreadHash);
	return ThreadHash.ObjectToPackageMap.FindRef(Object);
}

UPackage* GetObjectExternalPackageInternal(const UObjectBase* Object)
{
	FUObjectHashTables& ThreadHash = FUObjectHashTables::Get();
	return ThreadHash.ObjectToPackageMap.FindRef(Object);
}

/**
 * Prevents any other threads from finding/adding UObjects (e.g. while GC is running)
*/
void LockUObjectHashTables()
{
#if THREADSAFE_UOBJECTS
	FUObjectHashTables::Get().Lock();
#else
	check(IsInGameThread());
#endif
}

/**
 * Releases UObject hash tables lock (e.g. after GC has finished running)
 */
void UnlockUObjectHashTables()
{
#if THREADSAFE_UOBJECTS
	FUObjectHashTables::Get().Unlock();
#else
	check(IsInGameThread());
#endif
}

void LogHashStatisticsInternal(TMultiMap<int32, UObjectBase*>& Hash, FOutputDevice& Ar, const bool bShowHashBucketCollisionInfo)
{
	TArray<int32> HashBuckets;
	// Get the set of keys in use, which is the number of hash buckets
	int32 SlotsInUse = Hash.GetKeys(HashBuckets);

	int32 TotalCollisions = 0;
	int32 MinCollisions = MAX_int32;
	int32 MaxCollisions = 0;
	int32 MaxBin = 0;

	// Dump how many slots are in use
	Ar.Logf(TEXT("Slots in use %d"), SlotsInUse);

	// Work through each slot and figure out how many collisions
	for (auto HashBucket : HashBuckets)
	{
		int32 Collisions = 0;

		for (TMultiMap<int32, UObjectBase*>::TConstKeyIterator HashIt(Hash, HashBucket); HashIt; ++HashIt)
		{
			// There's one collision per object in a given bucket
			Collisions++;
		}

		// Keep the global stats
		TotalCollisions += Collisions;
		if (Collisions > MaxCollisions)
		{
			MaxBin = HashBucket;
		}
		MaxCollisions = FMath::Max<int32>(Collisions, MaxCollisions);
		MinCollisions = FMath::Min<int32>(Collisions, MinCollisions);

		if (bShowHashBucketCollisionInfo)
		{
			// Now log the output
			Ar.Logf(TEXT("\tSlot %d has %d collisions"), HashBucket, Collisions);
		}
	}
	Ar.Logf(TEXT(""));

	// Dump the first 30 objects in the worst bin for inspection
	Ar.Logf(TEXT("Worst hash bucket contains:"));
	int32 Count = 0;
	for (TMultiMap<int32, UObjectBase*>::TConstKeyIterator HashIt(Hash, MaxBin); HashIt && Count < 30; ++HashIt)
	{
		UObject* Object = (UObject*)HashIt.Value();
		Ar.Logf(TEXT("\tObject is %s (%s)"), *Object->GetName(), *Object->GetFullName());
		Count++;
	}
	Ar.Logf(TEXT(""));

	// Now dump how efficient the hash is
	Ar.Logf(TEXT("Collision Stats: Best Case (%d), Average Case (%d), Worst Case (%d)"),
		MinCollisions,
		FMath::FloorToInt(((float)TotalCollisions / (float)SlotsInUse)),
		MaxCollisions);

	// Calculate Hashtable size
	const uint32 HashtableAllocatedSize = Hash.GetAllocatedSize();
	Ar.Logf(TEXT("Total memory allocated for Object Outer Hash: %u bytes."), HashtableAllocatedSize);
}

void LogHashStatisticsInternal(TMap<int32, FHashBucket>& Hash, FOutputDevice& Ar, const bool bShowHashBucketCollisionInfo)
{
	TArray<int32> HashBuckets;
	// Get the set of keys in use, which is the number of hash buckets
	int32 SlotsInUse = Hash.Num();

	int32 TotalCollisions = 0;
	int32 MinCollisions = MAX_int32;
	int32 MaxCollisions = 0;
	int32 MaxBin = 0;
	int32 NumBucketsWithMoreThanOneItem = 0;

	// Dump how many slots are in use
	Ar.Logf(TEXT("Slots in use %d"), SlotsInUse);

	// Work through each slot and figure out how many collisions
	for (auto& HashPair : Hash)
	{
		int32 Collisions = HashPair.Value.Num();
		check(Collisions >= 0);
		if (Collisions > 1)
		{
			NumBucketsWithMoreThanOneItem++;
		}

		// Keep the global stats
		TotalCollisions += Collisions;
		if (Collisions > MaxCollisions)
		{
			MaxBin = HashPair.Key;
		}
		MaxCollisions = FMath::Max<int32>(Collisions, MaxCollisions);
		MinCollisions = FMath::Min<int32>(Collisions, MinCollisions);

		if (bShowHashBucketCollisionInfo)
		{
			// Now log the output
			Ar.Logf(TEXT("\tSlot %d has %d collisions"), HashPair.Key, Collisions);
		}
	}
	Ar.Logf(TEXT(""));

	// Dump the first 30 objects in the worst bin for inspection
	Ar.Logf(TEXT("Worst hash bucket contains:"));
	int32 Count = 0;
	FHashBucket& WorstBucket = Hash.FindChecked(MaxBin);
	for (FHashBucketIterator It(WorstBucket); It; ++It)
	{
		UObject* Object = (UObject*)*It;
		Ar.Logf(TEXT("\tObject is %s (%s)"), *Object->GetName(), *Object->GetFullName());
		Count++;
	}
	Ar.Logf(TEXT(""));

	// Now dump how efficient the hash is
	Ar.Logf(TEXT("Collision Stats: Best Case (%d), Average Case (%d), Worst Case (%d), Number of buckets with more than one item (%d/%d)"),
		MinCollisions,
		FMath::FloorToInt(((float)TotalCollisions / (float)SlotsInUse)),
		MaxCollisions,
		NumBucketsWithMoreThanOneItem,
		SlotsInUse);

	// Calculate Hashtable size
	uint32 HashtableAllocatedSize = Hash.GetAllocatedSize();
	// Calculate the size of a all Allocations inside of the buckets (TSet Items)
	for (auto& Pair : Hash)
	{
		HashtableAllocatedSize += Pair.Value.GetItemsSize();
	}
	Ar.Logf(TEXT("Total memory allocated for and by Object Hash: %u bytes."), HashtableAllocatedSize);
}

void LogHashStatistics(FOutputDevice& Ar, const bool bShowHashBucketCollisionInfo)
{
	Ar.Logf(TEXT("Hash efficiency statistics for the Object Hash"));
	Ar.Logf(TEXT("-------------------------------------------------"));
	Ar.Logf(TEXT(""));
	FHashTableLock HashLock(FUObjectHashTables::Get());
	LogHashStatisticsInternal(FUObjectHashTables::Get().Hash, Ar, bShowHashBucketCollisionInfo);
	Ar.Logf(TEXT(""));
}

void LogHashOuterStatistics(FOutputDevice& Ar, const bool bShowHashBucketCollisionInfo)
{
	Ar.Logf(TEXT("Hash efficiency statistics for the Outer Object Hash"));
	Ar.Logf(TEXT("-------------------------------------------------"));
	Ar.Logf(TEXT(""));
	FHashTableLock HashLock(FUObjectHashTables::Get());
	LogHashStatisticsInternal(FUObjectHashTables::Get().HashOuter, Ar, bShowHashBucketCollisionInfo);
	Ar.Logf(TEXT(""));

	uint32 HashOuterMapSize = 0;
	for (TPair<UObjectBase*, FHashBucket>& OuterMapEntry : FUObjectHashTables::Get().ObjectOuterMap)
	{
		HashOuterMapSize += OuterMapEntry.Value.GetItemsSize();
	}
	Ar.Logf(TEXT("Total memory allocated for Object Outer Map: %u bytes."), HashOuterMapSize);
	Ar.Logf(TEXT(""));
}

void LogHashMemoryOverheadStatistics(FOutputDevice& Ar, const bool bShowIndividualStats)
{
	Ar.Logf(TEXT("UObject Hash Tables and Maps memory overhead"));
	Ar.Logf(TEXT("-------------------------------------------------"));

	FUObjectHashTables& HashTables = FUObjectHashTables::Get();
	FHashTableLock HashLock(HashTables);

	int64 TotalSize = 0;
	
	{
		int64 Size = HashTables.Hash.GetAllocatedSize();
		for (const TPair<int32, FHashBucket>& Pair : HashTables.Hash)
		{
			Size += Pair.Value.GetItemsSize();
		}
		if (bShowIndividualStats)
		{
			Ar.Logf(TEXT("Memory used by UObject Hash: %lld bytes."), Size);
		}
		TotalSize += Size;
	}

	{
		int64 Size = HashTables.HashOuter.GetAllocatedSize();
		if (bShowIndividualStats)
		{
			Ar.Logf(TEXT("Memory used by UObject Outer Hash: %lld bytes."), Size);
		}
		TotalSize += Size;
	}

	{
		int64 Size = HashTables.ObjectOuterMap.GetAllocatedSize();
		for (const TPair<UObjectBase*, FHashBucket>& Pair : HashTables.ObjectOuterMap)
		{
			Size += Pair.Value.GetItemsSize();
		}
		if (bShowIndividualStats)
		{
			Ar.Logf(TEXT("Memory used by UObject Outer Map: %lld bytes."), Size);
		}
		TotalSize += Size;
	}

	{
		int64 Size = HashTables.ClassToObjectListMap.GetAllocatedSize();
		for (const TPair<UClass*, FHashBucket>& Pair : HashTables.ClassToObjectListMap)
		{
			Size += Pair.Value.GetItemsSize();
		}
		if (bShowIndividualStats)
		{
			Ar.Logf(TEXT("Memory used by UClass To UObject List Map: %lld bytes."), Size);
		}
		TotalSize += Size;
	}

	{
		int64 Size = HashTables.ClassToChildListMap.GetAllocatedSize();
		for (const TPair<UClass*, TSet<UClass*> >& Pair : HashTables.ClassToChildListMap)
		{
			Size += Pair.Value.GetAllocatedSize();
		}
		if (bShowIndividualStats)
		{
			Ar.Logf(TEXT("Memory used by UClass To Child UClass List Map: %lld bytes."), Size);
		}
		TotalSize += Size;
	}

	{
		int64 Size = HashTables.PackageToObjectListMap.GetAllocatedSize();
		for (const TPair<UPackage*, FHashBucket>& Pair : HashTables.PackageToObjectListMap)
		{
			Size += Pair.Value.GetItemsSize();
		}
		if (bShowIndividualStats)
		{
			Ar.Logf(TEXT("Memory used by UPackage To UObject List Map: %lld bytes."), Size);
		}
		TotalSize += Size;
	}

	{
		int64 Size = HashTables.ObjectToPackageMap.GetAllocatedSize();
		if (bShowIndividualStats)
		{
			Ar.Logf(TEXT("Memory used by UObject To External Package Map: %lld bytes."), Size);
		}
		TotalSize += Size;
	}

    {
        int64 Size = GUObjectArray.GetAllocatedSize();
        if (bShowIndividualStats)
        {
            Ar.Logf(TEXT("Memory used by UObjectArray: %lld bytes."), Size);
        }
        TotalSize += Size;
    }
    
	Ar.Logf(TEXT("Total memory allocated by Object hash tables and maps: %lld bytes (%.2f MB)."), TotalSize, (double)TotalSize / 1024.0 / 1024.0);
	Ar.Logf(TEXT(""));
}
