// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/RedirectCollector.h"
#include "Misc/CoreDelegates.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Object.h"
#include "UObject/Package.h"
#include "Templates/Casts.h"
#include "UObject/UnrealType.h"
#include "UObject/ObjectRedirector.h"
#include "Misc/PackageName.h"
#include "UObject/LinkerLoad.h"
#include "UObject/UObjectThreadContext.h"
#include "UObject/SoftObjectPath.h"

#if WITH_EDITOR

DEFINE_LOG_CATEGORY_STATIC(LogRedirectors, Log, All);

void FRedirectCollector::OnSoftObjectPathLoaded(const FSoftObjectPath& InPath, FArchive* InArchive)
{
	if (InPath.IsNull() || !GIsEditor)
	{
		// No need to track empty strings, or in standalone builds
		return;
	}

	FSoftObjectPathThreadContext& ThreadContext = FSoftObjectPathThreadContext::Get();

	FName PackageName, PropertyName;
	ESoftObjectPathCollectType CollectType = ESoftObjectPathCollectType::AlwaysCollect;
	ESoftObjectPathSerializeType SerializeType = ESoftObjectPathSerializeType::AlwaysSerialize;

	ThreadContext.GetSerializationOptions(PackageName, PropertyName, CollectType, SerializeType, InArchive);

	if (CollectType == ESoftObjectPathCollectType::NeverCollect)
	{
		// Do not track
		return;
	}

	const bool bReferencedByEditorOnlyProperty = (CollectType == ESoftObjectPathCollectType::EditorOnlyCollect);
	FSoftObjectPathProperty SoftObjectPathProperty(InPath.GetAssetPathName(), PropertyName, bReferencedByEditorOnlyProperty);

	FScopeLock ScopeLock(&CriticalSection);

	SoftObjectPathMap.FindOrAdd(PackageName).Add(SoftObjectPathProperty);
}

void FRedirectCollector::OnStringAssetReferenceLoaded(const FString& InString)
{
	FSoftObjectPath Path(InString);
	OnSoftObjectPathLoaded(Path, nullptr);
}

FString FRedirectCollector::OnStringAssetReferenceSaved(const FString& InString)
{
	FScopeLock ScopeLock(&CriticalSection);

	FName Found = GetAssetPathRedirection(FName(*InString));

	if (Found != NAME_None)
	{
		return Found.ToString();
	}
	return InString;
}

void FRedirectCollector::ResolveAllSoftObjectPaths(FName FilterPackage)
{	
	auto LoadSoftObjectPathLambda = [this](const FSoftObjectPathProperty& SoftObjectPathProperty)
	{
		const FName& ToLoadFName = SoftObjectPathProperty.GetAssetPathName();
		const FString ToLoad = ToLoadFName.ToString();

		if (ToLoad.Len() > 0 )
		{
			UE_LOG(LogRedirectors, Verbose, TEXT("Resolving Soft Object Path '%s'"), *ToLoad);
			UE_CLOG(SoftObjectPathProperty.GetPropertyName().ToString().Len(), LogRedirectors, Verbose, TEXT("    Referenced by '%s'"), *SoftObjectPathProperty.GetPropertyName().ToString());

			int32 DotIndex = ToLoad.Find(TEXT("."));
			FString PackageName = DotIndex != INDEX_NONE ? ToLoad.Left(DotIndex) : ToLoad;

			// If is known missing don't try
			if (FLinkerLoad::IsKnownMissingPackage(FName(*PackageName)))
			{
				return;
			}

			UObject *Loaded = LoadObject<UObject>(NULL, *ToLoad, NULL, SoftObjectPathProperty.GetReferencedByEditorOnlyProperty() ? LOAD_EditorOnly | LOAD_NoWarn : LOAD_NoWarn, NULL);

			if (Loaded)
			{
				FString Dest = Loaded->GetPathName();
				UE_LOG(LogRedirectors, Verbose, TEXT("    Resolved to '%s'"), *Dest);
				if (Dest != ToLoad)
				{
					AssetPathRedirectionMap.Add(ToLoadFName, FName(*Dest));
				}
			}
			else
			{
				const FString Referencer = SoftObjectPathProperty.GetPropertyName().ToString().Len() ? SoftObjectPathProperty.GetPropertyName().ToString() : TEXT("Unknown");
				UE_LOG(LogRedirectors, Warning, TEXT("Soft Object Path '%s' was not found when resolving paths! (Referencer '%s')"), *ToLoad, *Referencer);
			}
		}
	};

	FScopeLock ScopeLock(&CriticalSection);

	FSoftObjectPathMap KeepSoftObjectPathMap;
	KeepSoftObjectPathMap.Reserve(SoftObjectPathMap.Num());
	while (SoftObjectPathMap.Num())
	{
		FSoftObjectPathMap LocalSoftObjectPathMap;
		Swap(SoftObjectPathMap, LocalSoftObjectPathMap);

		for (TPair<FName, FSoftObjectPathPropertySet>& CurrentPackage : LocalSoftObjectPathMap)
		{
			const FName& CurrentPackageName = CurrentPackage.Key;
			FSoftObjectPathPropertySet& SoftObjectPathProperties = CurrentPackage.Value;

			if ((FilterPackage != NAME_None) && // not using a filter
				(FilterPackage != CurrentPackageName) && // this is the package we are looking for
				(CurrentPackageName != NAME_None) // if we have an empty package name then process it straight away
				)
			{
				// If we have a valid filter and it doesn't match, skip processing of this package and keep it
				KeepSoftObjectPathMap.FindOrAdd(CurrentPackageName).Append(MoveTemp(SoftObjectPathProperties));
				continue;
			}

			// This will call LoadObject which may trigger OnSoftObjectPathLoaded and add new soft object paths to the SoftObjectPathMap
			for (const FSoftObjectPathProperty& SoftObjecPathProperty : SoftObjectPathProperties)
			{
				LoadSoftObjectPathLambda(SoftObjecPathProperty);
			}
		}
	}

	check(SoftObjectPathMap.Num() == 0);
	// Add any non processed packages back into the global map for the next time this is called
	Swap(SoftObjectPathMap, KeepSoftObjectPathMap);
	// we shouldn't have any references left if we decided to resolve them all
	check((SoftObjectPathMap.Num() == 0) || (FilterPackage != NAME_None));
}

void FRedirectCollector::ProcessSoftObjectPathPackageList(FName FilterPackage, bool bGetEditorOnly, TSet<FName>& OutReferencedPackages)
{
	FScopeLock ScopeLock(&CriticalSection);

	TSet<FSoftObjectPathProperty>* SoftObjectPathProperties = SoftObjectPathMap.Find(FilterPackage);
	if (!SoftObjectPathProperties)
	{
		return;
	}

	// potentially add soft object path package names to OutReferencedPackages
	OutReferencedPackages.Reserve(SoftObjectPathProperties->Num());
	for (const FSoftObjectPathProperty& SoftObjectPathProperty : *SoftObjectPathProperties)
	{
		if (!SoftObjectPathProperty.GetReferencedByEditorOnlyProperty() || bGetEditorOnly)
		{
			const FName& ToLoadFName = SoftObjectPathProperty.GetAssetPathName();
			FString PackageNameString = FPackageName::ObjectPathToPackageName(ToLoadFName.ToString());
			OutReferencedPackages.Add(FName(*PackageNameString));
		}
	}

	// always remove all data for the processed FilterPackage
	SoftObjectPathMap.Remove(FilterPackage);
}

void FRedirectCollector::AddAssetPathRedirection(FName OriginalPath, FName RedirectedPath)
{
	FScopeLock ScopeLock(&CriticalSection);

	if (!ensureMsgf(OriginalPath != NAME_None, TEXT("Cannot add redirect from Name_None!")))
	{
		return;
	}

	FName FinalRedirection = GetAssetPathRedirection(RedirectedPath);
	if (FinalRedirection == OriginalPath)
	{
		// If RedirectedPath points back to OriginalPath, remove that to avoid a circular reference
		// This can happen when renaming assets in the editor but not actually dropping redirectors because it was new
		AssetPathRedirectionMap.Remove(RedirectedPath);
	}

	// This replaces an existing mapping, can happen in the editor if things are renamed twice
	AssetPathRedirectionMap.Add(OriginalPath, RedirectedPath);
}

void FRedirectCollector::RemoveAssetPathRedirection(FName OriginalPath)
{
	FScopeLock ScopeLock(&CriticalSection);

	FName* Found = AssetPathRedirectionMap.Find(OriginalPath);

	if (ensureMsgf(Found, TEXT("Cannot remove redirection from %s, it was not registered"), *OriginalPath.ToString()))
	{
		AssetPathRedirectionMap.Remove(OriginalPath);
	}
}

FName FRedirectCollector::GetAssetPathRedirection(FName OriginalPath)
{
	FScopeLock ScopeLock(&CriticalSection);
	TArray<FName> SeenPaths;

	// We need to follow the redirect chain recursively
	FName CurrentPath = OriginalPath;

	while (CurrentPath != NAME_None)
	{
		SeenPaths.Add(CurrentPath);
		FName NewPath = AssetPathRedirectionMap.FindRef(CurrentPath);

		if (NewPath != NAME_None)
		{
			if (!ensureMsgf(!SeenPaths.Contains(NewPath), TEXT("Found circular redirect from %s to %s! Returning None instead"), *CurrentPath.ToString(), *NewPath.ToString()))
			{
				return NAME_None;
			}

			// Continue trying to follow chain
			CurrentPath = NewPath;
		}
		else
		{
			// No more redirections
			break;
		}
	}

	if (CurrentPath != OriginalPath)
	{
		return CurrentPath;
	}
	return NAME_None;
}

FRedirectCollector GRedirectCollector;

#endif // WITH_EDITOR
