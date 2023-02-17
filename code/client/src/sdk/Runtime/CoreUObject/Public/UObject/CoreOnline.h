// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Hash/CityHash.h"
#include "UObject/CoreOnlineFwd.h"
#include "UObject/ObjectMacros.h"

#define GameSessionName NAME_GameSession
#define PartySessionName NAME_PartySession
#define GamePort NAME_GamePort
#define BeaconPort NAME_BeaconPort

#if !CPP
// Circular dependency on Core vs UHT means we have to noexport these structs so tools can build
USTRUCT(noexport)
struct FJoinabilitySettings
{
	//GENERATED_BODY()

	/** Name of session these settings affect */
	UPROPERTY()
	FName SessionName;
	/** Is this session now publicly searchable */
	UPROPERTY()
	bool bPublicSearchable;
	/** Does this session allow invites */
	UPROPERTY()
	bool bAllowInvites;
	/** Does this session allow public join via presence */
	UPROPERTY()
	bool bJoinViaPresence;
	/** Does this session allow friends to join via presence */
	UPROPERTY()
	bool bJoinViaPresenceFriendsOnly;
	/** Current max players in this session */
	UPROPERTY()
	int32 MaxPlayers;
	/** Current max party size in this session */
	UPROPERTY()
	int32 MaxPartySize;
};

USTRUCT(noexport)
struct FUniqueNetIdWrapper
{
	//GENERATED_BODY()
};

#endif

struct FJoinabilitySettings
{
	/** Name of session these settings affect */
	FName SessionName;
	/** Is this session now publicly searchable */
	bool bPublicSearchable;
	/** Does this session allow invites */
	bool bAllowInvites;
	/** Does this session allow public join via presence */
	bool bJoinViaPresence;
	/** Does this session allow friends to join via presence */
	bool bJoinViaPresenceFriendsOnly;
	/** Current max players in this session */
	int32 MaxPlayers;
	/** Current max party size in this session */
	int32 MaxPartySize;

	FJoinabilitySettings() :
		SessionName(NAME_None),
		bPublicSearchable(false),
		bAllowInvites(false),
		bJoinViaPresence(false),
		bJoinViaPresenceFriendsOnly(false),
		MaxPlayers(0),
		MaxPartySize(0)
	{
	}

	bool operator==(const FJoinabilitySettings& Other) const
	{
		return SessionName == Other.SessionName &&
			bPublicSearchable == Other.bPublicSearchable &&
			bAllowInvites == Other.bAllowInvites &&
			bJoinViaPresence == Other.bJoinViaPresence &&
			bJoinViaPresenceFriendsOnly == Other.bJoinViaPresenceFriendsOnly &&
			MaxPlayers == Other.MaxPlayers &&
			MaxPartySize == Other.MaxPartySize;
	}

	bool operator!=(const FJoinabilitySettings& Other) const
	{
		return !(FJoinabilitySettings::operator==(Other));
	}
};

/**
 * Abstraction of a profile service online Id
 * The class is meant to be opaque
 */
class FUniqueNetId : public TSharedFromThis<FUniqueNetId, UNIQUENETID_ESPMODE>
{
protected:

	/** Only constructible by derived type */
	FUniqueNetId() = default;

	FUniqueNetId(const FUniqueNetId& Src) = default;
	FUniqueNetId& operator=(const FUniqueNetId& Src) = default;

	virtual bool Compare(const FUniqueNetId& Other) const
	{
		return (GetSize() == Other.GetSize()) &&
			(FMemory::Memcmp(GetBytes(), Other.GetBytes(), GetSize()) == 0);
	}

public:

	virtual ~FUniqueNetId() = default;

	/**
	 *	Comparison operator
	 */
	friend bool operator==(const FUniqueNetId& Lhs, const FUniqueNetId& Rhs)
	{
		return Lhs.Compare(Rhs);
	}

	friend bool operator!=(const FUniqueNetId& Lhs, const FUniqueNetId& Rhs)
	{
		return !Lhs.Compare(Rhs);
	}

	/**
	 * Get the type token for this opaque data
	 * This is useful for inferring UniqueId subclasses and knowing which OSS it "goes with"
	 *
	 * @return FName representing the Type
	 */
	virtual FName GetType() const { return NAME_None; /* This should be pure virtual, however, older versions of the OSS plugins cannot handle that */ }

	/** 
	 * Get the raw byte representation of this opaque data
	 * This data is platform dependent and shouldn't be manipulated directly
	 *
	 * @return byte array of size GetSize()
	 */
	virtual const uint8* GetBytes() const = 0;

	/** 
	 * Get the size of the opaque data
	 *
	 * @return size in bytes of the data representation
	 */
	virtual int32 GetSize() const = 0;

	/**
	 * Check the validity of the opaque data
	 *
	 * @return true if this is well formed data, false otherwise
	 */
	virtual bool IsValid() const = 0;

	/**
	 * Platform specific conversion to string representation of data
	 *
	 * @return data in string form
	 */
	virtual FString ToString() const = 0;

	/**
	 * Get a human readable representation of the opaque data
	 * Shouldn't be used for anything other than logging/debugging
	 *
	 * @return data in string form
	 */
	virtual FString ToDebugString() const = 0;

	/**
	 * @return hex encoded string representation of unique id
	 */
	FString GetHexEncodedString() const
	{
		if (GetSize() > 0 && GetBytes() != NULL)
		{
			return BytesToHex(GetBytes(), GetSize());
		}
		return FString();
	}

	friend inline uint32 GetTypeHash(const FUniqueNetId& Value)
	{
		return CityHash32(reinterpret_cast<const char*>(Value.GetBytes()), Value.GetSize());
	}
};

struct FUniqueNetIdWrapper
{
public:

	FUniqueNetIdWrapper() = default;
	virtual ~FUniqueNetIdWrapper() = default;

	// copy operators generated by compiler

	FUniqueNetIdWrapper(const FUniqueNetIdRef& InUniqueNetId)
		: UniqueNetId(InUniqueNetId)
	{
	}

	FUniqueNetIdWrapper(const FUniqueNetIdPtr& InUniqueNetId)
		: UniqueNetId(InUniqueNetId)
	{
	}

	// temporarily restored implicit conversion from FUniqueNetId
	FUniqueNetIdWrapper(const FUniqueNetId& InUniqueNetId)
		: UniqueNetId(InUniqueNetId.AsShared())
	{
	}

	FName GetType() const
	{
		return IsValid() ? UniqueNetId->GetType() : NAME_None;
	}
	
	/** Convert this value to a string */
	FString ToString() const
	{
		return IsValid() ? UniqueNetId->ToString() : TEXT("INVALID");
	}

	/** Convert this value to a string with additional information */
	FString ToDebugString() const
	{
		return IsValid() ? FString::Printf(TEXT("%s:%s"), *UniqueNetId->GetType().ToString(), *UniqueNetId->ToDebugString()) : TEXT("INVALID");
	}

	/** Is the FUniqueNetId wrapped in this object valid */
	bool IsValid() const
	{
		return UniqueNetId.IsValid() && UniqueNetId->IsValid();
	}
	
	/** 
	 * Assign a unique id to this wrapper object
	 *
	 * @param InUniqueNetId id to associate
	 */
	virtual void SetUniqueNetId(const FUniqueNetIdPtr& InUniqueNetId)
	{
		UniqueNetId = InUniqueNetId;
	}

	/** @return unique id associated with this wrapper object */
	const FUniqueNetIdPtr& GetUniqueNetId() const
	{
		return UniqueNetId;
	}

	/**
	 * Dereference operator returns a reference to the FUniqueNetId
	 */
	const FUniqueNetId& operator*() const
	{
		return *UniqueNetId;
	}

	/**
	 * Arrow operator returns a pointer to this FUniqueNetId
	 */
	const FUniqueNetId* operator->() const
	{
		return UniqueNetId.Get();
	}

	/**
	* Friend function for using FUniqueNetIdWrapper as a hashable key
	*/
	friend inline uint32 GetTypeHash(FUniqueNetIdWrapper const& Value)
	{
		if (Value.IsValid())
		{
			return GetTypeHash(*Value);
		}
		else
		{
			// If we hit this, something went wrong and we have received an unhashable wrapper.
			return INDEX_NONE;
		}
	}

	static FUniqueNetIdWrapper Invalid()
	{
		static FUniqueNetIdWrapper InvalidId(nullptr);
		return InvalidId;
	}

	friend bool operator==(const FUniqueNetIdWrapper& Lhs, const FUniqueNetIdWrapper& Rhs)
	{
		bool bLhsValid = Lhs.IsValid();
		return bLhsValid == Rhs.IsValid() && (!bLhsValid || *Lhs == *Rhs);
	}

	friend bool operator!=(const FUniqueNetIdWrapper& Lhs, const FUniqueNetIdWrapper& Rhs)
	{
		return !(Lhs == Rhs);
	}

	friend bool operator==(const FUniqueNetIdWrapper& Lhs, const FUniqueNetId& Rhs)
	{
		bool bLhsValid = Lhs.IsValid();
		return bLhsValid == Rhs.IsValid() && (!bLhsValid || *Lhs == Rhs);
	}

	friend bool operator!=(const FUniqueNetIdWrapper& Lhs, const FUniqueNetId& Rhs)
	{
		return !(Lhs == Rhs);
	}

	friend bool operator==(const FUniqueNetId& Lhs, const FUniqueNetIdWrapper& Rhs)
	{
		return Rhs == Lhs;
	}

	friend bool operator!=(const FUniqueNetId& Lhs, const FUniqueNetIdWrapper& Rhs)
	{
		return Rhs != Lhs;
	}

	// comparison with nullptr (alternative to IsValid)
	friend bool operator==(const FUniqueNetIdWrapper& NetIdWrapper, TYPE_OF_NULLPTR)
	{
		return !NetIdWrapper.IsValid();
	}

	friend bool operator!=(const FUniqueNetIdWrapper& NetIdWrapper, TYPE_OF_NULLPTR)
	{
		return NetIdWrapper.IsValid();
	}

	friend bool operator==(TYPE_OF_NULLPTR, const FUniqueNetIdWrapper& NetIdWrapper)
	{
		return !NetIdWrapper.IsValid();
	}

	friend bool operator!=(TYPE_OF_NULLPTR, const FUniqueNetIdWrapper& NetIdWrapper)
	{
		return NetIdWrapper.IsValid();
	}

protected:

	// Actual unique id
	FUniqueNetIdPtr UniqueNetId;
};

template <typename ValueType>
struct TUniqueNetIdMapKeyFuncs : public TDefaultMapKeyFuncs<FUniqueNetIdRef, ValueType, false>
{
	static FORCEINLINE FUniqueNetIdRef GetSetKey(TPair<FUniqueNetIdRef, ValueType> const& Element) { return Element.Key; }
	static FORCEINLINE uint32 GetKeyHash(FUniqueNetIdRef const& Key) { return GetTypeHash(*Key); }
	static FORCEINLINE bool Matches(FUniqueNetIdRef const& A, FUniqueNetIdRef const& B) { return (A == B) || (*A == *B); }
};

template <typename ValueType>
using TUniqueNetIdMap = TMap<FUniqueNetIdRef, ValueType, FDefaultSetAllocator, TUniqueNetIdMapKeyFuncs<ValueType>>;

struct FUniqueNetIdKeyFuncs : public DefaultKeyFuncs<FUniqueNetIdRef>
{
	static FORCEINLINE FUniqueNetIdRef GetSetKey(FUniqueNetIdRef const& Element) { return Element; }
	static FORCEINLINE uint32 GetKeyHash(FUniqueNetIdRef const& Key) { return GetTypeHash(*Key); }
	static FORCEINLINE bool Matches(FUniqueNetIdRef const& A, FUniqueNetIdRef const& B) { return (A == B) || (*A == *B); }
};

using FUniqueNetIdSet = TSet<FUniqueNetIdRef, FUniqueNetIdKeyFuncs>;
