// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/ARFilter.h"
#include "Serialization/CustomVersion.h"
#include "String/Find.h"
#include "UObject/PropertyPortFlags.h"

DEFINE_LOG_CATEGORY(LogAssetData);

IMPLEMENT_STRUCT(ARFilter);
IMPLEMENT_STRUCT(AssetData);

// Register Asset Registry version
const FGuid FAssetRegistryVersion::GUID(0x717F9EE7, 0xE9B0493A, 0x88B39132, 0x1B388107);
FCustomVersionRegistration GRegisterAssetRegistryVersion(FAssetRegistryVersion::GUID, FAssetRegistryVersion::LatestVersion, TEXT("AssetRegistry"));

namespace UE { namespace AssetData { namespace Private {

const FName GAssetBundleDataName("AssetBundleData");

static TSharedPtr<FAssetBundleData, ESPMode::ThreadSafe> ParseAssetBundles(const TCHAR* Text, const FAssetData& Context)
{
	// Register that we're reading string assets for a specific package
	FSoftObjectPathSerializationScope SerializationScope(Context.PackageName, GAssetBundleDataName, ESoftObjectPathCollectType::NeverCollect, ESoftObjectPathSerializeType::AlwaysSerialize);

	FAssetBundleData Temp;
	if (!Temp.ImportTextItem(Text, PPF_None, nullptr, (FOutputDevice*)GWarn))
	{
		// Native UScriptStruct isn't available during early cooked asset registry preloading.
		// Preloading should not require this fallback.
		
		UScriptStruct& Struct = *TBaseStructure<FAssetBundleData>::Get();
		Struct.ImportText(Text, &Temp, nullptr, PPF_None, (FOutputDevice*)GWarn, 
							[&]() { return Context.AssetName.ToString(); });
	}
	
	if (Temp.Bundles.Num() > 0)
	{
		return TSharedPtr<FAssetBundleData, ESPMode::ThreadSafe>(new FAssetBundleData(MoveTemp(Temp)));
	}

	return TSharedPtr<FAssetBundleData, ESPMode::ThreadSafe>();
}

}}} // end namespace UE::AssetData::Private

FAssetData::FAssetData(FName InPackageName, FName InPackagePath, FName InAssetName, FName InAssetClass, FAssetDataTagMap InTags, TArrayView<const int32> InChunkIDs, uint32 InPackageFlags)
	: PackageName(InPackageName)
	, PackagePath(InPackagePath)
	, AssetName(InAssetName)
	, AssetClass(InAssetClass)
	, ChunkIDs(MoveTemp(InChunkIDs))
	, PackageFlags(InPackageFlags)
{
	SetTagsAndAssetBundles(MoveTemp(InTags));

	TStringBuilder<FName::StringBufferSize> ObjectPathStr;
	PackageName.AppendString(ObjectPathStr);
	ObjectPathStr << TEXT('.');
	AssetName.AppendString(ObjectPathStr);
	ObjectPath = FName(FStringView(ObjectPathStr));
}

FAssetData::FAssetData(const FString& InLongPackageName, const FString& InObjectPath, FName InAssetClass, FAssetDataTagMap InTags, TArrayView<const int32> InChunkIDs, uint32 InPackageFlags)
	: ObjectPath(*InObjectPath)
	, PackageName(*InLongPackageName)
	, AssetClass(InAssetClass)
	, ChunkIDs(MoveTemp(InChunkIDs))
	, PackageFlags(InPackageFlags)
{
	SetTagsAndAssetBundles(MoveTemp(InTags));

	PackagePath = FName(*FPackageName::GetLongPackagePath(InLongPackageName));

	// Find the object name from the path, FPackageName::ObjectPathToObjectName(InObjectPath)) doesn't provide what we want here
	int32 CharPos = InObjectPath.FindLastCharByPredicate([](TCHAR Char)
	{
		return Char == ':' || Char == '.';
	});
	AssetName = FName(*InObjectPath.Mid(CharPos + 1));
}

FAssetData::FAssetData(const UObject* InAsset, bool bAllowBlueprintClass)
{
	if (InAsset != nullptr)
	{
		const UClass* InClass = Cast<UClass>(InAsset);
		if (InClass && InClass->ClassGeneratedBy && !bAllowBlueprintClass)
		{
			// For Blueprints, the AssetData refers to the UBlueprint and not the UBlueprintGeneratedClass
			InAsset = InClass->ClassGeneratedBy;
		}

		const UPackage* Outermost = InAsset->GetOutermost();

		PackageName = Outermost->GetFName();
		PackagePath = FName(*FPackageName::GetLongPackagePath(Outermost->GetName()));
		AssetName = InAsset->GetFName();
		AssetClass = InAsset->GetClass()->GetFName();
		ObjectPath = FName(*InAsset->GetPathName());

		InAsset->GetAssetRegistryTags(*this);

		ChunkIDs = Outermost->GetChunkIDs();
		PackageFlags = Outermost->GetPackageFlags();
	}
}

bool FAssetData::IsUAsset(UObject* InAsset)
{
	if (InAsset == nullptr)
	{
		return false;
	}

	const UPackage* Package = InAsset->GetPackage();

	TStringBuilder<FName::StringBufferSize> AssetNameStrBuilder;
	InAsset->GetPathName(Package, AssetNameStrBuilder);

	TStringBuilder<FName::StringBufferSize> PackageNameStrBuilder;
	Package->GetFName().AppendString(PackageNameStrBuilder);

	return DetectIsUAssetByNames(PackageNameStrBuilder, AssetNameStrBuilder);
}

void FAssetData::SetTagsAndAssetBundles(FAssetDataTagMap&& Tags)
{
	using namespace UE::AssetData::Private;

	for (TPair<FName, FString>& Tag : Tags)
	{
		check(!Tag.Key.IsNone() && !Tag.Value.IsEmpty());
	}

	FString AssetBundles;
	if (Tags.RemoveAndCopyValue(GAssetBundleDataName, AssetBundles))
	{
		TaggedAssetBundles = ParseAssetBundles(*AssetBundles, *this);
	}
	else
	{
		TaggedAssetBundles.Reset();
	}

	TagsAndValues = Tags.Num() > 0 ? FAssetDataTagMapSharedView(MoveTemp(Tags)) : FAssetDataTagMapSharedView();
}

FPrimaryAssetId FAssetData::GetPrimaryAssetId() const
{
	FName PrimaryAssetType = GetTagValueRef<FName>(FPrimaryAssetId::PrimaryAssetTypeTag);
	FName PrimaryAssetName = GetTagValueRef<FName>(FPrimaryAssetId::PrimaryAssetNameTag);

	if (!PrimaryAssetType.IsNone() && !PrimaryAssetName.IsNone())
	{
		return FPrimaryAssetId(PrimaryAssetType, PrimaryAssetName);
	}

	return FPrimaryAssetId();
}

bool FAssetRegistryVersion::SerializeVersion(FArchive& Ar, FAssetRegistryVersion::Type& Version)
{
	FGuid Guid = FAssetRegistryVersion::GUID;

	if (Ar.IsLoading())
	{
		Version = FAssetRegistryVersion::PreVersioning;
	}

	Ar << Guid;

	if (Ar.IsError())
	{
		return false;
	}

	if (Guid == FAssetRegistryVersion::GUID)
	{
		int32 VersionInt = Version;
		Ar << VersionInt;
		Version = (FAssetRegistryVersion::Type)VersionInt;

		Ar.SetCustomVersion(Guid, VersionInt, TEXT("AssetRegistry"));
	}
	else
	{
		return false;
	}

	return !Ar.IsError();
}
