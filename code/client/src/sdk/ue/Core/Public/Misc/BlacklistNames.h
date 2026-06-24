// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/StringView.h"
#include "Templates/SharedPointer.h"
#include "Delegates/Delegate.h"

/** List of owner names that requested a specific item filtered, allows unregistering specific set of changes by a given plugin or system */
typedef TArray<FName> FBlacklistOwners;

class CORE_API FBlacklistNames : public TSharedFromThis<FBlacklistNames>
{
public:
	FBlacklistNames();
	~FBlacklistNames() {}

	/** Returns true if passes filter restrictions using exact match */
	bool PassesFilter(const FName Item) const;

	/** 
	 * Add item to blacklist, this specific item will be filtered out.
	 * @return whether the filters changed.
	 */
	bool AddBlacklistItem(const FName OwnerName, const FName Item);

	/**
	 * Add item to whitelist after which all items not in the whitelist will be filtered out.
	 * @return whether the filters changed.
	 */
	bool AddWhitelistItem(const FName OwnerName, const FName Item);

	/**
	 * Set to filter out all items.
	 * @return whether the filters changed.
	 */
	bool AddBlacklistAll(const FName OwnerName);
	
	/** True if has filters active */
	bool HasFiltering() const;

	/** Gathers the names of all the owners in this blacklist. */
	TArray<FName> GetOwnerNames() const;

	/** 
	* Removes all filtering changes associated with a specific owner name.
	 * @return whether the filters changed.
	 */
	bool UnregisterOwner(const FName OwnerName);

	/**
	 * Removes all filtering changes associated with the specified list of owner names.
	 * @return whether the filters changed.
	 */
	bool UnregisterOwners(const TArray<FName>& OwnerNames);

	/**
	 * Add the specified filters to this one.
	 * @return whether the filters changed.
	 */
	bool Append(const FBlacklistNames& Other);

	/**
	* Unregisters specified owners then adds specified filters in one operation (to avoid multiple filters changed events).
	* @return whether the filters changed.
	*/
	bool UnregisterOwnersAndAppend(const TArray<FName>& OwnerNamesToRemove, const FBlacklistNames& FiltersToAdd);

	/** Get raw blacklist */
	const TMap<FName, FBlacklistOwners>& GetBlacklist() const { return Blacklist; }
	
	/** Get raw whitelist */
	const TMap<FName, FBlacklistOwners>& GetWhitelist() const { return Whitelist; }

	/** Are all items set to be filtered out */
	bool IsBlacklistAll() const { return BlacklistAll.Num() > 0; }

	/** Triggered when filter changes */
	FSimpleMulticastDelegate& OnFilterChanged() { return OnFilterChangedDelegate; }

protected:

	/** List if items to filter out */
	TMap<FName, FBlacklistOwners> Blacklist;

	/** List of items to allow, if not empty all items will be filtered out unless they are in the list */
	TMap<FName, FBlacklistOwners> Whitelist;

	/** List of owner names that requested all items to be filtered out */
	FBlacklistOwners BlacklistAll;

	/** Triggered when filter changes */
	FSimpleMulticastDelegate OnFilterChangedDelegate;

	/** Temporarily prevent delegate from being triggered */
	bool bSuppressOnFilterChanged;
};

class CORE_API FBlacklistPaths : public TSharedFromThis<FBlacklistPaths>
{
public:
	FBlacklistPaths();
	~FBlacklistPaths() {}
	
	/** Returns true if passes filter restrictions using exact match */
	bool PassesFilter(const FStringView Item) const;

	/** Returns true if passes filter restrictions using exact match */
	bool PassesFilter(const FName Item) const;

	/** Returns true if passes filter restrictions using exact match */
	bool PassesFilter(const TCHAR* Item) const;

	/** Returns true if passes filter restrictions for path */
	bool PassesStartsWithFilter(const FStringView Item, const bool bAllowParentPaths = false) const;

	/** Returns true if passes filter restrictions for path */
	bool PassesStartsWithFilter(const FName Item, const bool bAllowParentPaths = false) const;

	/** Returns true if passes filter restrictions for path */
	bool PassesStartsWithFilter(const TCHAR* Item, const bool bAllowParentPaths = false) const;

	/** 
	 * Add item to blacklist, this specific item will be filtered out.
	 * @return whether the filters changed.
	 */
	bool AddBlacklistItem(const FName OwnerName, const FStringView Item);

	/**
	 * Add item to blacklist, this specific item will be filtered out.
	 * @return whether the filters changed.
	 */
	bool AddBlacklistItem(const FName OwnerName, const FName Item);

	/**
	 * Add item to blacklist, this specific item will be filtered out.
	 * @return whether the filters changed.
	 */
	bool AddBlacklistItem(const FName OwnerName, const TCHAR* Item);

	/**
	 * Add item to whitelist after which all items not in the whitelist will be filtered out.
	 * @return whether the filters changed.
	 */
	bool AddWhitelistItem(const FName OwnerName, const FStringView Item);

	/**
	 * Add item to whitelist after which all items not in the whitelist will be filtered out.
	 * @return whether the filters changed.
	 */
	bool AddWhitelistItem(const FName OwnerName, const FName Item);

	/**
	 * Add item to whitelist after which all items not in the whitelist will be filtered out.
	 * @return whether the filters changed.
	 */
	bool AddWhitelistItem(const FName OwnerName, const TCHAR* Item);

	/**
	 * Set to filter out all items.
	 * @return whether the filters changed.
	 */
	bool AddBlacklistAll(const FName OwnerName);
	
	/** True if has filters active */
	bool HasFiltering() const;

	/** Gathers the names of all the owners in this blacklist. */
	TArray<FName> GetOwnerNames() const;

	/**
	 * Removes all filtering changes associated with a specific owner name.
	 * @return whether the filters changed.
	 */
	bool UnregisterOwner(const FName OwnerName);

	/**
	 * Removes all filtering changes associated with the specified list of owner names.
	 * @return whether the filters changed.
	 */
	bool UnregisterOwners(const TArray<FName>& OwnerNames);
	
	/**
	 * Add the specified filters to this one.
	 * @return whether the filters changed.
	 */
	bool Append(const FBlacklistPaths& Other);

	/**
	* Unregisters specified owners then adds specified filters in one operation (to avoid multiple filters changed events).
	* @return whether the filters changed.
	*/
	bool UnregisterOwnersAndAppend(const TArray<FName>& OwnerNamesToRemove, const FBlacklistPaths& FiltersToAdd);

	/** Get raw blacklist */
	const TMap<FString, FBlacklistOwners>& GetBlacklist() const { return Blacklist; }
	
	/** Get raw whitelist */
	const TMap<FString, FBlacklistOwners>& GetWhitelist() const { return Whitelist; }

	/** Are all items set to be filtered out */
	bool IsBlacklistAll() const { return BlacklistAll.Num() > 0; }
	
	/** Triggered when filter changes */
	FSimpleMulticastDelegate& OnFilterChanged() { return OnFilterChangedDelegate; }

protected:

	/** List if items to filter out */
	TMap<FString, FBlacklistOwners> Blacklist;

	/** List of items to allow, if not empty all items will be filtered out unless they are in the list */
	TMap<FString, FBlacklistOwners> Whitelist;

	/** List of owner names that requested all items to be filtered out */
	FBlacklistOwners BlacklistAll;
	
	/** Triggered when filter changes */
	FSimpleMulticastDelegate OnFilterChangedDelegate;

	/** Temporarily prevent delegate from being triggered */
	bool bSuppressOnFilterChanged;
};
