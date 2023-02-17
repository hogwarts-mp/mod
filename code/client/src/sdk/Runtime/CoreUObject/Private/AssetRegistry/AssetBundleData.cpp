// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetRegistry/AssetBundleData.h"
#include "AssetRegistry/AssetData.h"
#include "UObject/PropertyPortFlags.h"

IMPLEMENT_STRUCT(AssetBundleData);


namespace UE { namespace AssetBundleEntry { namespace Private
{

const FStringView BundleNamePrefix(TEXT("(BundleName=\""));
const FStringView BundleAssetsPrefix(TEXT(",BundleAssets=("));
const FStringView BundleAssetsSuffix(TEXT("))"));
const FStringView BundlesPrefix(TEXT("(Bundles=("));
const FStringView BundlesSuffix(TEXT("))"));
const FStringView EmptyBundles(TEXT("(Bundles=)"));

static bool SkipPrefix(const TCHAR*& InOutIt, FStringView Prefix)
{
	const bool bOk = FStringView(InOutIt, Prefix.Len()) == Prefix;
	if (bOk)
	{
		InOutIt += Prefix.Len();
	}
	return bOk;
}

}}}

bool FAssetBundleEntry::ExportTextItem(FString& ValueStr, const FAssetBundleEntry& DefaultValue, UObject* Parent, int32 PortFlags, UObject* ExportRootScope) const
{	
	if (DefaultValue.IsValid())
	{
		// This path does not handle default values, fall back to normal export path
		return false;
	}

	using namespace UE::AssetBundleEntry::Private;

	const uint32 OriginalLen = ValueStr.Len();

	ValueStr += BundleNamePrefix;
	BundleName.AppendString(ValueStr);
	ValueStr += '\"';
	ValueStr += BundleAssetsPrefix;

	const FSoftObjectPath EmptyPath;
	for (const FSoftObjectPath& Path : BundleAssets)
	{
		if (!Path.ExportTextItem(ValueStr, EmptyPath, Parent, PortFlags, ExportRootScope))
		{
			ValueStr.LeftInline(OriginalLen);
			return false;
		}
		
		ValueStr.AppendChar(',');
	}

	// Remove last comma
	ValueStr.LeftChopInline(1, /* shrink */ false);

	ValueStr += BundleAssetsSuffix;

	return true;
}

bool FAssetBundleEntry::ImportTextItem(const TCHAR*& Buffer, int32 PortFlags, UObject* Parent, FOutputDevice* ErrorText)
{
	using namespace UE::AssetBundleEntry::Private;

	const TCHAR* BundleNameBegin = Buffer;
	if (SkipPrefix(/* in-out */ BundleNameBegin, BundleNamePrefix))
	{
		if (const TCHAR* BundleNameEnd = FCString::Strchr(BundleNameBegin, TCHAR('"')))
		{
			FName Name(BundleNameEnd - BundleNameBegin, BundleNameBegin);

			const TCHAR* PathIt = BundleNameEnd + 1;
			if (SkipPrefix(/* in-out */ PathIt, BundleAssetsPrefix))
			{
				TArray<FSoftObjectPath> Paths;
				while (Paths.Emplace_GetRef().ImportTextItem(/* in-out */ PathIt, PortFlags, Parent, ErrorText))
				{
					if (*PathIt == ',')
					{
						++PathIt;
					}
					else if (SkipPrefix(/* in-out */ PathIt, BundleAssetsSuffix))
					{
						BundleName = Name;
						BundleAssets = MoveTemp(Paths);
						Buffer = PathIt;

						return true;
					}
					else
					{
						return false;
					}
				}
			}
		}
	}

	return false;
}

FAssetBundleEntry* FAssetBundleData::FindEntry(FName SearchName)
{
	for (FAssetBundleEntry& Entry : Bundles)
	{
		if (Entry.BundleName == SearchName)
		{
			return &Entry;
		}
	}
	return nullptr;
}

void FAssetBundleData::AddBundleAsset(FName BundleName, const FSoftObjectPath& AssetPath)
{
	if (!AssetPath.IsValid())
	{
		return;
	}

	FAssetBundleEntry* FoundEntry = FindEntry(BundleName);

	if (!FoundEntry)
	{
		FoundEntry = new(Bundles) FAssetBundleEntry(BundleName);
	}

	FoundEntry->BundleAssets.AddUnique(AssetPath);
}

void FAssetBundleData::AddBundleAssets(FName BundleName, const TArray<FSoftObjectPath>& AssetPaths)
{
	FAssetBundleEntry* FoundEntry = FindEntry(BundleName);

	for (const FSoftObjectPath& Path : AssetPaths)
	{
		if (Path.IsValid())
		{
			// Only create if required
			if (!FoundEntry)
			{
				FoundEntry = new(Bundles) FAssetBundleEntry(BundleName);
			}

			FoundEntry->BundleAssets.AddUnique(Path);
		}
	}
}

void FAssetBundleData::SetBundleAssets(FName BundleName, TArray<FSoftObjectPath>&& AssetPaths)
{
	FAssetBundleEntry* FoundEntry = FindEntry(BundleName);

	if (!FoundEntry)
	{
		FoundEntry = new(Bundles) FAssetBundleEntry(BundleName);
	}

	FoundEntry->BundleAssets = AssetPaths;
}

void FAssetBundleData::Reset()
{
	Bundles.Reset();
}

bool FAssetBundleData::ExportTextItem(FString& ValueStr, FAssetBundleData const& DefaultValue, UObject* Parent, int32 PortFlags, UObject* ExportRootScope) const
{
	if (Bundles.Num() == 0)
	{
		// Empty, don't write anything to avoid it cluttering the asset registry tags
		return true;
	}
	else if (DefaultValue.Bundles.Num() != 0)
	{
		// This path does not handle default values, fall back to normal export path
		return false;
	}
	
	using namespace UE::AssetBundleEntry::Private;

	const uint32 OriginalLen = ValueStr.Len();

	ValueStr += BundlesPrefix;

	const FAssetBundleEntry EmptyEntry;
	for (const FAssetBundleEntry& Entry : Bundles)
	{
		if (!Entry.ExportTextItem(ValueStr, EmptyEntry, Parent, PortFlags, ExportRootScope))
		{
			ValueStr.LeftInline(OriginalLen);
			return false;
		}
		ValueStr.AppendChar(',');
	}

	// Remove last comma
	ValueStr.LeftChopInline(1, /* shrink */ false);

	ValueStr += BundlesSuffix;

	return true;
}

bool FAssetBundleData::ImportTextItem(const TCHAR*& Buffer, int32 PortFlags, UObject* Parent, FOutputDevice* ErrorText)
{
	if (*Buffer != TEXT('('))
	{
		// Empty, don't read/write anything
		return true;
	}

	using namespace UE::AssetBundleEntry::Private;
	
	const TCHAR* It = Buffer;
	if (SkipPrefix(/* in-out */ It, BundlesPrefix))
	{
		TArray<FAssetBundleEntry> Entries;
		while (Entries.Emplace_GetRef().ImportTextItem(/* in-out */ It, PortFlags, Parent, ErrorText))
		{
			if (*It == ',')
			{
				++It;
			}
			else if (SkipPrefix(/* in-out */ It, BundlesSuffix))
			{
				Bundles = MoveTemp(Entries);
				Buffer = It;
				
				return true;
			}
			else
			{
				return false;
			}
		}
	}

	return SkipPrefix(/* in-out */ Buffer, EmptyBundles);
}

FString FAssetBundleData::ToDebugString() const
{
	TStringBuilder<220> Result;

	bool bFirstLine = true;
	for (const FAssetBundleEntry& Data : Bundles)
	{
		if (!bFirstLine)
		{
			Result.Append(TEXT("\n"));
		}

		Result.Appendf(TEXT("%s -> (%s)"),
			*Data.BundleName.ToString(),
			*FString::JoinBy(Data.BundleAssets, TEXT(", "), [](const FSoftObjectPath& LoadBundle) { return LoadBundle.ToString(); }));

		bFirstLine = false;
	}

	return Result.ToString();
}
