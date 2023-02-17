// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/PackageId.h"
#include "Serialization/StructuredArchive.h"

FPackageId FPackageId::FromName(const FName& Name)
{
	TCHAR NameStr[FName::StringBufferSize];
	uint32 NameLen = Name.ToString(NameStr);
	for (uint32 I = 0; I < NameLen; ++I)
	{
		NameStr[I] = TChar<TCHAR>::ToLower(NameStr[I]);
	}
	uint64 Hash = CityHash64(reinterpret_cast<const char*>(NameStr), NameLen * sizeof(TCHAR));
	checkf(Hash != InvalidId, TEXT("Package name hash collision \"%s\" and InvalidId"), NameStr);
	return FPackageId(Hash);
}

FArchive& operator<<(FArchive& Ar, FPackageId& Value)
{
	FStructuredArchiveFromArchive(Ar).GetSlot() << Value;
	return Ar;
}

void operator<<(FStructuredArchiveSlot Slot, FPackageId& Value)
{
	Slot << Value.Id;
}
