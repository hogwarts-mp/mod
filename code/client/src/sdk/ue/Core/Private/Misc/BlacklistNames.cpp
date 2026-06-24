// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/BlacklistNames.h"
#include "Misc/StringBuilder.h"

FBlacklistNames::FBlacklistNames() :
	bSuppressOnFilterChanged(false)
{}

bool FBlacklistNames::PassesFilter(const FName Item) const
{
	if (BlacklistAll.Num() > 0)
	{
		return false;
	}

	if (Whitelist.Num() > 0 && !Whitelist.Contains(Item))
	{
		return false;
	}

	if (Blacklist.Contains(Item))
	{
		return false;
	}

	return true;
}

bool FBlacklistNames::AddBlacklistItem(const FName OwnerName, const FName Item)
{
	const int32 OldNum = Blacklist.Num();
	Blacklist.FindOrAdd(Item).AddUnique(OwnerName);

	const bool bFilterChanged = OldNum != Blacklist.Num();
	if (bFilterChanged && !bSuppressOnFilterChanged)
	{
		OnFilterChanged().Broadcast();
	}

	return bFilterChanged;
}

bool FBlacklistNames::AddWhitelistItem(const FName OwnerName, const FName Item)
{
	const int32 OldNum = Whitelist.Num();
	Whitelist.FindOrAdd(Item).AddUnique(OwnerName);

	const bool bFilterChanged = OldNum != Whitelist.Num();
	if (bFilterChanged && !bSuppressOnFilterChanged)
	{
		OnFilterChanged().Broadcast();
	}

	return bFilterChanged;
}

bool FBlacklistNames::AddBlacklistAll(const FName OwnerName)
{
	const int32 OldNum = BlacklistAll.Num();
	BlacklistAll.AddUnique(OwnerName);

	const bool bFilterChanged = OldNum != BlacklistAll.Num();
	if (bFilterChanged && !bSuppressOnFilterChanged)
	{
		OnFilterChanged().Broadcast();
	}

	return bFilterChanged;
}

bool FBlacklistNames::HasFiltering() const
{
	return Blacklist.Num() > 0 || Whitelist.Num() > 0 || BlacklistAll.Num() > 0;
}

TArray<FName> FBlacklistNames::GetOwnerNames() const
{
	TArray<FName> OwnerNames;
	
	for (const auto& It : Blacklist)
	{
		for (const auto& OwnerName : It.Value)
		{
			OwnerNames.AddUnique(OwnerName);
		}
	}

	for (const auto& It : Whitelist)
	{
		for (const auto& OwnerName : It.Value)
		{
			OwnerNames.AddUnique(OwnerName);
		}
	}

	for (const auto& OwnerName : BlacklistAll)
	{
		OwnerNames.AddUnique(OwnerName);
	}

	return OwnerNames;
}

bool FBlacklistNames::UnregisterOwner(const FName OwnerName)
{
	bool bFilterChanged = false;

	for (auto It = Blacklist.CreateIterator(); It; ++It)
	{
		It->Value.Remove(OwnerName);
		if (It->Value.Num() == 0)
		{
			It.RemoveCurrent();
			bFilterChanged = true;
		}
	}

	for (auto It = Whitelist.CreateIterator(); It; ++It)
	{
		It->Value.Remove(OwnerName);
		if (It->Value.Num() == 0)
		{
			It.RemoveCurrent();
			bFilterChanged = true;
		}
	}

	bFilterChanged |= (BlacklistAll.Remove(OwnerName) > 0);

	if (bFilterChanged && !bSuppressOnFilterChanged)
	{
		OnFilterChanged().Broadcast();
	}

	return bFilterChanged;
}

bool FBlacklistNames::UnregisterOwners(const TArray<FName>& OwnerNames)
{
	bool bFilterChanged = false;
	{
		TGuardValue<bool> Guard(bSuppressOnFilterChanged, true);

		for (FName OwnerName : OwnerNames)
		{
			bFilterChanged |= UnregisterOwner(OwnerName);
		}
	}

	if (bFilterChanged && !bSuppressOnFilterChanged)
	{
		OnFilterChanged().Broadcast();
	}

	return bFilterChanged;
}

bool FBlacklistNames::Append(const FBlacklistNames& Other)
{
	bool bFilterChanged = false;
	{
		TGuardValue<bool> Guard(bSuppressOnFilterChanged, true);

		for (const auto& It : Other.Blacklist)
		{
			for (const auto& OwnerName : It.Value)
			{
				bFilterChanged |= AddBlacklistItem(OwnerName, It.Key);
			}
		}

		for (const auto& It : Other.Whitelist)
		{
			for (const auto& OwnerName : It.Value)
			{
				bFilterChanged |= AddWhitelistItem(OwnerName, It.Key);
			}
		}

		for (const auto& OwnerName : Other.BlacklistAll)
		{
			bFilterChanged |= AddBlacklistAll(OwnerName);
		}
	}

	if (bFilterChanged && !bSuppressOnFilterChanged)
	{
		OnFilterChanged().Broadcast();
	}

	return bFilterChanged;
}

bool FBlacklistNames::UnregisterOwnersAndAppend(const TArray<FName>& OwnerNamesToRemove, const FBlacklistNames& FiltersToAdd)
{
	bool bFilterChanged = false;
	{
		TGuardValue<bool> Guard(bSuppressOnFilterChanged, true);

		bFilterChanged |= UnregisterOwners(OwnerNamesToRemove);
		bFilterChanged |= Append(FiltersToAdd);
	}

	if (bFilterChanged && !bSuppressOnFilterChanged)
	{
		OnFilterChanged().Broadcast();
	}

	return bFilterChanged;
}

/** FBlacklistPaths */
FBlacklistPaths::FBlacklistPaths() :
	bSuppressOnFilterChanged(false)
{
}

bool FBlacklistPaths::PassesFilter(const FStringView Item) const
{
	if (BlacklistAll.Num() > 0)
	{
		return false;
	}

	if (Whitelist.Num() > 0 || Blacklist.Num() > 0)
	{
		const uint32 ItemHash = GetTypeHash(Item);

		if (Whitelist.Num() > 0 && !Whitelist.ContainsByHash(ItemHash, Item))
		{
			return false;
		}

		if (Blacklist.ContainsByHash(ItemHash, Item))
		{
			return false;
		}
	}

	return true;
}

bool FBlacklistPaths::PassesFilter(const FName Item) const
{
	TStringBuilder<FName::StringBufferSize> ItemStr;
	Item.ToString(ItemStr);
	return PassesFilter(FStringView(ItemStr));
}

bool FBlacklistPaths::PassesFilter(const TCHAR* Item) const
{
	return PassesFilter(FStringView(Item));
}

bool FBlacklistPaths::PassesStartsWithFilter(const FStringView Item, const bool bAllowParentPaths) const
{
	if (Whitelist.Num() > 0)
	{
		bool bPassedWhitelist = false;
		for (const auto& Other : Whitelist)
		{
			if (Item.StartsWith(Other.Key) && (Item.Len() <= Other.Key.Len() || Item[Other.Key.Len()] == TEXT('/')))
			{
				bPassedWhitelist = true;
				break;
			}

			if (bAllowParentPaths)
			{
				// If allowing parent paths (eg, when filtering folders), then we must also check if the item has a whitelisted child path
				if (FStringView(Other.Key).StartsWith(Item) && (Other.Key.Len() <= Item.Len() || Other.Key[Item.Len()] == TEXT('/')))
				{
					bPassedWhitelist = true;
					break;
				}
			}
		}

		if (!bPassedWhitelist)
		{
			return false;
		}
	}

	if (Blacklist.Num() > 0)
	{
		for (const auto& Other : Blacklist)
		{
			if (Item.StartsWith(Other.Key) && (Item.Len() <= Other.Key.Len() || Item[Other.Key.Len()] == TEXT('/')))
			{
				return false;
			}
		}
	}

	if (BlacklistAll.Num() > 0)
	{
		return false;
	}

	return true;
}

bool FBlacklistPaths::PassesStartsWithFilter(const FName Item, const bool bAllowParentPaths) const
{
	TStringBuilder<FName::StringBufferSize> ItemStr;
	Item.ToString(ItemStr);
	return PassesStartsWithFilter(FStringView(ItemStr), bAllowParentPaths);
}

bool FBlacklistPaths::PassesStartsWithFilter(const TCHAR* Item, const bool bAllowParentPaths) const
{
	return PassesStartsWithFilter(FStringView(Item), bAllowParentPaths);
}

bool FBlacklistPaths::AddBlacklistItem(const FName OwnerName, const FStringView Item)
{
	const uint32 ItemHash = GetTypeHash(Item);

	FBlacklistOwners* Owners = Blacklist.FindByHash(ItemHash, Item);
	const bool bFilterChanged = (Owners == nullptr);
	if (!Owners)
	{
		Owners = &Blacklist.AddByHash(ItemHash, FString(Item));
	}

	Owners->AddUnique(OwnerName);
	
	if (bFilterChanged && !bSuppressOnFilterChanged)
	{
		OnFilterChanged().Broadcast();
	}

	return bFilterChanged;
}

bool FBlacklistPaths::AddBlacklistItem(const FName OwnerName, const FName Item)
{
	TStringBuilder<FName::StringBufferSize> ItemStr;
	Item.ToString(ItemStr);
	return AddBlacklistItem(OwnerName, FStringView(ItemStr));
}

bool FBlacklistPaths::AddBlacklistItem(const FName OwnerName, const TCHAR* Item)
{
	return AddBlacklistItem(OwnerName, FStringView(Item));
}

bool FBlacklistPaths::AddWhitelistItem(const FName OwnerName, const FStringView Item)
{
	const uint32 ItemHash = GetTypeHash(Item);

	FBlacklistOwners* Owners = Whitelist.FindByHash(ItemHash, Item);
	const bool bFilterChanged = (Owners == nullptr);
	if (!Owners)
	{
		Owners = &Whitelist.AddByHash(ItemHash, FString(Item));
	}

	Owners->AddUnique(OwnerName);

	if (bFilterChanged && !bSuppressOnFilterChanged)
	{
		OnFilterChanged().Broadcast();
	}

	return bFilterChanged;
}

bool FBlacklistPaths::AddWhitelistItem(const FName OwnerName, const FName Item)
{
	TStringBuilder<FName::StringBufferSize> ItemStr;
	Item.ToString(ItemStr);
	return AddWhitelistItem(OwnerName, FStringView(ItemStr));
}

bool FBlacklistPaths::AddWhitelistItem(const FName OwnerName, const TCHAR* Item)
{
	return AddWhitelistItem(OwnerName, FStringView(Item));
}

bool FBlacklistPaths::AddBlacklistAll(const FName OwnerName)
{
	const int32 OldNum = BlacklistAll.Num();
	BlacklistAll.AddUnique(OwnerName);

	const bool bFilterChanged = OldNum != BlacklistAll.Num();
	if (bFilterChanged && !bSuppressOnFilterChanged)
	{
		OnFilterChanged().Broadcast();
	}

	return bFilterChanged;
}

bool FBlacklistPaths::HasFiltering() const
{
	return Blacklist.Num() > 0 || Whitelist.Num() > 0 || BlacklistAll.Num() > 0;
}

TArray<FName> FBlacklistPaths::GetOwnerNames() const
{
	TArray<FName> OwnerNames;

	for (const auto& It : Blacklist)
	{
		for (const auto& OwnerName : It.Value)
		{
			OwnerNames.AddUnique(OwnerName);
		}
	}

	for (const auto& It : Whitelist)
	{
		for (const auto& OwnerName : It.Value)
		{
			OwnerNames.AddUnique(OwnerName);
		}
	}

	for (const auto& OwnerName : BlacklistAll)
	{
		OwnerNames.AddUnique(OwnerName);
	}

	return OwnerNames;
}

bool FBlacklistPaths::UnregisterOwner(const FName OwnerName)
{
	bool bFilterChanged = false;

	for (auto It = Blacklist.CreateIterator(); It; ++It)
	{
		It->Value.Remove(OwnerName);
		if (It->Value.Num() == 0)
		{
			It.RemoveCurrent();
			bFilterChanged = true;
		}
	}

	for (auto It = Whitelist.CreateIterator(); It; ++It)
	{
		It->Value.Remove(OwnerName);
		if (It->Value.Num() == 0)
		{
			It.RemoveCurrent();
			bFilterChanged = true;
		}
	}

	bFilterChanged |= (BlacklistAll.Remove(OwnerName) > 0);

	if (bFilterChanged && !bSuppressOnFilterChanged)
	{
		OnFilterChanged().Broadcast();
	}

	return bFilterChanged;
}

bool FBlacklistPaths::UnregisterOwners(const TArray<FName>& OwnerNames)
{
	bool bFilterChanged = false;
	{
		TGuardValue<bool> Guard(bSuppressOnFilterChanged, true);

		for (FName OwnerName : OwnerNames)
		{
			bFilterChanged |= UnregisterOwner(OwnerName);
		}
	}

	if (bFilterChanged && !bSuppressOnFilterChanged)
	{
		OnFilterChanged().Broadcast();
	}

	return bFilterChanged;
}

bool FBlacklistPaths::Append(const FBlacklistPaths& Other)
{
	bool bFilterChanged = false;
	{
		TGuardValue<bool> Guard(bSuppressOnFilterChanged, true);

		for (const auto& It : Other.Blacklist)
		{
			for (const auto& OwnerName : It.Value)
			{
				bFilterChanged |= AddBlacklistItem(OwnerName, It.Key);
			}
		}

		for (const auto& It : Other.Whitelist)
		{
			for (const auto& OwnerName : It.Value)
			{
				bFilterChanged |= AddWhitelistItem(OwnerName, It.Key);
			}
		}

		for (const auto& OwnerName : Other.BlacklistAll)
		{
			bFilterChanged |= AddBlacklistAll(OwnerName);
		}
	}

	if (bFilterChanged && !bSuppressOnFilterChanged)
	{
		OnFilterChanged().Broadcast();
	}

	return bFilterChanged;
}

bool FBlacklistPaths::UnregisterOwnersAndAppend(const TArray<FName>& OwnerNamesToRemove, const FBlacklistPaths& FiltersToAdd)
{
	bool bFilterChanged = false;
	{
		TGuardValue<bool> Guard(bSuppressOnFilterChanged, true);

		bFilterChanged |= UnregisterOwners(OwnerNamesToRemove);
		bFilterChanged |= Append(FiltersToAdd);
	}

	if (bFilterChanged && !bSuppressOnFilterChanged)
	{
		OnFilterChanged().Broadcast();
	}

	return bFilterChanged;
}
