// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	WeakObjectPtr.h: Weak pointer to UObject
=============================================================================*/

#pragma once

#include "CoreMinimal.h"
#include "UObject/UObjectArray.h"
#include "UObject/FastReferenceCollectorOptions.h"

/**
 * FWeakObjectPtr is a weak pointer to a UObject. 
 * It can return nullptr later if the object is garbage collected.
 * It has no impact on if the object is garbage collected or not.
 * It can't be directly used across a network.
 *
 * Most often it is used when you explicitly do NOT want to prevent something from being garbage collected.
 */
struct FWeakObjectPtr
{
public:

	template <typename ReferenceProcessorType, typename CollectorType, typename ArrayPoolType, EFastReferenceCollectorOptions Options>
	friend class TFastReferenceCollector;
	friend struct FFieldPath;

	/** Null constructor **/
	FORCEINLINE FWeakObjectPtr()
	{
		Reset();
	}
	/**  
	 * Construct from an object pointer
	 * @param Object object to create a weak pointer to
	 */
	FORCEINLINE FWeakObjectPtr(const class UObject* Object)
	{
		(*this)=Object;
	}

	/**  
	 * Construct from another weak pointer
	 * @param Other weak pointer to copy from
	 */
	FWeakObjectPtr(const FWeakObjectPtr& Other) = default;

	/**
	 * Reset the weak pointer back to the null state
	 */
	FORCEINLINE void Reset()
	{
		ObjectIndex = INDEX_NONE;
		ObjectSerialNumber = 0;
	}

	/**  
	 * Copy from an object pointer
	 * @param Object object to create a weak pointer to
	 */
	COREUOBJECT_API void operator=(const class UObject* Object);

	/**  
	 * Construct from another weak pointer
	 * @param Other weak pointer to copy from
	 */
	FWeakObjectPtr& operator=(const FWeakObjectPtr& Other) = default;

	/**  
	 * Compare weak pointers for equality.
	 * If both pointers would return nullptr from Get() they count as equal even if they were not initialized to the same object.
	 * @param Other weak pointer to compare to
	 */
	FORCEINLINE bool operator==(const FWeakObjectPtr& Other) const
	{
		return 
			(ObjectIndex == Other.ObjectIndex && ObjectSerialNumber == Other.ObjectSerialNumber) ||
			(!IsValid() && !Other.IsValid());
	}

	/**  
	 * Compare weak pointers for inequality
	 * @param Other weak pointer to compare to
	 */
	FORCEINLINE bool operator!=(const FWeakObjectPtr& Other) const
	{
		return 
			(ObjectIndex != Other.ObjectIndex || ObjectSerialNumber != Other.ObjectSerialNumber) &&
			(IsValid() || Other.IsValid());
	}

	/**
	 * Returns true if two weak pointers were originally set to the same object, even if they are now stale
	 * @param Other weak pointer to compare to
	 */
	FORCEINLINE bool HasSameIndexAndSerialNumber(const FWeakObjectPtr& Other) const
	{
		return ObjectIndex == Other.ObjectIndex && ObjectSerialNumber == Other.ObjectSerialNumber;
	}

	/**  
	 * Dereference the weak pointer.
	 * @param bEvenIfPendingKill if this is true, pendingkill objects are considered valid
	 * @return nullptr if this object is gone or the weak pointer is explicitly null, otherwise a valid uobject pointer
	 */
	COREUOBJECT_API class UObject* Get(bool bEvenIfPendingKill) const;

	/**  
	 * Dereference the weak pointer. This is an optimized version implying bEvenIfPendingKill=false.
	 * @return nullptr if this object is gone or the weak pointer is explicitly null, otherwise a valid uobject pointer
	 */
	COREUOBJECT_API class UObject* Get(/*bool bEvenIfPendingKill = false*/) const;

	/** Dereference the weak pointer even if it is RF_PendingKill or RF_Unreachable */
	COREUOBJECT_API class UObject* GetEvenIfUnreachable() const;

	/**  
	 * Test if this points to a live UObject
	 * @param bEvenIfPendingKill if this is true, pendingkill are not considered invalid
	 * @param bThreadsafeTest if true then function will just give you information whether referenced
	 *							UObject is gone forever (return false) or if it is still there (return true, no object flags checked).
	 * @return true if Get() would return a valid non-null pointer
	 */
	COREUOBJECT_API bool IsValid(bool bEvenIfPendingKill, bool bThreadsafeTest = false) const;

	/**
	 * Test if this points to a live UObject. This is an optimized version implying bEvenIfPendingKill=false, bThreadsafeTest=false.
	 * @return true if Get() would return a valid non-null pointer.
	 */
	COREUOBJECT_API bool IsValid(/*bool bEvenIfPendingKill = false, bool bThreadsafeTest = false*/) const;

	/**  
	 * Slightly different than !IsValid(), returns true if this used to point to a UObject, but doesn't any more and has not been assigned or reset in the mean time.
	 * @param bIncludingIfPendingKill if this is false, pendingkill objects are not considered stale
	 * @param bThreadsafeTest set it to true when testing outside of Game Thread. Results in false if WeakObjPtr point to an existing object (no flags checked)
	 * @return true if this used to point at a real object but no longer does.
	 */
	COREUOBJECT_API bool IsStale(bool bIncludingIfPendingKill = true, bool bThreadsafeTest = false) const;

	/**
	 * Returns true if this pointer was explicitly assigned to null, was reset, or was never initialized.
	 * If this returns true, IsValid() and IsStale() will both return false.
	 */
	FORCEINLINE bool IsExplicitlyNull() const
	{
		return ObjectIndex == INDEX_NONE;
	}

	/** Hash function. */
	friend uint32 GetTypeHash(const FWeakObjectPtr& WeakObjectPtr)
	{
		return uint32(WeakObjectPtr.ObjectIndex ^ WeakObjectPtr.ObjectSerialNumber);
	}

	/**
	 * Weak object pointer serialization.  Weak object pointers only have weak references to objects and
	 * won't serialize the object when gathering references for garbage collection.  So in many cases, you
	 * don't need to bother serializing weak object pointers.  However, serialization is required if you
	 * want to load and save your object.
	 */
	COREUOBJECT_API void Serialize(FArchive& Ar);

protected:

	FORCEINLINE int32 GetObjectIndex() const
	{
		return ObjectIndex;
	}

private:
	friend struct FObjectKey;
	
	/**  
	 * internal function to test for serial number matches
	 * @return true if the serial number in this matches the central table
	 */
	FORCEINLINE_DEBUGGABLE bool SerialNumbersMatch() const
	{
		checkSlow(ObjectSerialNumber > FUObjectArray::START_SERIAL_NUMBER && ObjectIndex >= 0); // otherwise this is a corrupted weak pointer
		int32 ActualSerialNumber = GUObjectArray.GetSerialNumber(ObjectIndex);
		checkSlow(!ActualSerialNumber || ActualSerialNumber >= ObjectSerialNumber); // serial numbers should never shrink
		return ActualSerialNumber == ObjectSerialNumber;
	}

	FORCEINLINE_DEBUGGABLE bool SerialNumbersMatch(FUObjectItem* ObjectItem) const
	{
		checkSlow(ObjectSerialNumber > FUObjectArray::START_SERIAL_NUMBER && ObjectIndex >= 0); // otherwise this is a corrupted weak pointer
		const int32 ActualSerialNumber = ObjectItem->GetSerialNumber();
		checkSlow(!ActualSerialNumber || ActualSerialNumber >= ObjectSerialNumber); // serial numbers should never shrink
		return ActualSerialNumber == ObjectSerialNumber;
	}

	FORCEINLINE FUObjectItem* Internal_GetObjectItem() const
	{
		if (ObjectSerialNumber == 0)
		{
			checkSlow(ObjectIndex == 0 || ObjectIndex == -1); // otherwise this is a corrupted weak pointer
			return nullptr;
		}
		if (ObjectIndex < 0)
		{
			return nullptr;
		}
		FUObjectItem* const ObjectItem = GUObjectArray.IndexToObject(ObjectIndex);
		if (!ObjectItem)
		{
			return nullptr;
		}
		if (!SerialNumbersMatch(ObjectItem))
		{
			return nullptr;
		}
		return ObjectItem;
	}

	/** Private (inlined) version for internal use only. */
	FORCEINLINE_DEBUGGABLE bool Internal_IsValid(bool bEvenIfPendingKill, bool bThreadsafeTest) const
	{
		FUObjectItem* const ObjectItem = Internal_GetObjectItem();
		if (bThreadsafeTest)
		{
			return (ObjectItem != nullptr);
		}
		else
		{
			return (ObjectItem != nullptr) && GUObjectArray.IsValid(ObjectItem, bEvenIfPendingKill);
		}
	}

	/** Private (inlined) version for internal use only. */
	FORCEINLINE_DEBUGGABLE UObject* Internal_Get(bool bEvenIfPendingKill) const
	{
		FUObjectItem* const ObjectItem = Internal_GetObjectItem();
		return ((ObjectItem != nullptr) && GUObjectArray.IsValid(ObjectItem, bEvenIfPendingKill)) ? (UObject*)ObjectItem->Object : nullptr;
	}

	int32		ObjectIndex;
	int32		ObjectSerialNumber;
};

template<> struct TIsPODType<FWeakObjectPtr> { enum { Value = true }; };
template<> struct TIsZeroConstructType<FWeakObjectPtr> { enum { Value = true }; };
template<> struct TIsWeakPointerType<FWeakObjectPtr> { enum { Value = true }; };

// Typedef script delegates for convenience.
typedef TScriptDelegate<> FScriptDelegate;
typedef TMulticastScriptDelegate<> FMulticastScriptDelegate;
