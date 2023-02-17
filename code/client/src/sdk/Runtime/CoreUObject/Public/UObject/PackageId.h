// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FArchive;
class FStructuredArchiveSlot;

class FPackageId
{
	static constexpr uint64 InvalidId = 0;
	uint64 Id = InvalidId;

	inline explicit FPackageId(uint64 InId): Id(InId) {}

public:
	FPackageId() = default;

	COREUOBJECT_API static FPackageId FromName(const FName& Name);

	inline bool IsValid() const
	{
		return Id != InvalidId;
	}

	inline uint64 Value() const
	{
		check(Id != InvalidId);
		return Id;
	}

	inline uint64 ValueForDebugging() const
	{
		return Id;
	}

	inline bool operator<(FPackageId Other) const
	{
		return Id < Other.Id;
	}

	inline bool operator==(FPackageId Other) const
	{
		return Id == Other.Id;
	}
	
	inline bool operator!=(FPackageId Other) const
	{
		return Id != Other.Id;
	}

	inline friend uint32 GetTypeHash(const FPackageId& In)
	{
		return uint32(In.Id);
	}

	COREUOBJECT_API friend FArchive& operator<<(FArchive& Ar, FPackageId& Value);

	COREUOBJECT_API friend void operator<<(FStructuredArchiveSlot Slot, FPackageId& Value);
};

