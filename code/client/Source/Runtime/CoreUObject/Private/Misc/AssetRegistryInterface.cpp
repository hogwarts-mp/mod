// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/AssetRegistryInterface.h"

#include "Containers/Set.h"
#include "CoreGlobals.h"
#include "Misc/CommandLine.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Templates/Casts.h"
#include "UObject/Class.h"
#include "UObject/NameTypes.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectIterator.h"

IAssetRegistryInterface* IAssetRegistryInterface::Default = nullptr;
IAssetRegistryInterface* IAssetRegistryInterface::GetPtr()
{
	return Default;
}

namespace UE
{
namespace AssetRegistry
{
namespace Private
{
	IAssetRegistry* IAssetRegistrySingleton::Singleton = nullptr;
}

#if WITH_ENGINE && WITH_EDITOR
	TSet<FName> SkipUncookedClasses;
	TSet<FName> SkipCookedClasses;
	bool bInitializedSkipClasses = false;
#endif //if WITH_ENGINE && WITH_EDITOR

	// TODO: replace with a function in CoreGlobals
	static bool IsRunningCookCommandlet()
	{
		FString Commandline = FCommandLine::Get();
		const bool bIsCookCommandlet = IsRunningCommandlet() && Commandline.Contains(TEXT("run=cook"));
		return bIsCookCommandlet;
	}

	bool FFiltering::ShouldSkipAsset(FName AssetClass, uint32 PackageFlags)
	{
#if WITH_ENGINE && WITH_EDITOR

		TRACE_CPUPROFILER_EVENT_SCOPE(AssetRegistry::FFiltering::ShouldSkipAsset)

		// We do not yet support having UBlueprintGeneratedClasses be assets when the UBlueprint is also
		// an asset; the content browser does not handle the multiple assets correctly and displays this
		// class asset as if it is in a separate package. Revisit when we have removed the UBlueprint as an asset
		// or when we support multiple assets.
		if (!bInitializedSkipClasses)
		{
			// Since we only collect these the first on-demand time, it is possible we will miss subclasses
			// from plugins that load later. This flaw is a rare edge case, though, and this solution will
			// be replaced eventually, so leaving it for now.
			if (GIsEditor && (!IsRunningCommandlet() || IsRunningCookCommandlet()))
			{
				static const FName NAME_EnginePackage("/Script/Engine");
				UPackage* EnginePackage = Cast<UPackage>(StaticFindObjectFast(UPackage::StaticClass(), nullptr, NAME_EnginePackage));

				{
					SkipUncookedClasses.Reset();

					static const FName NAME_BlueprintGeneratedClass("BlueprintGeneratedClass");
					UClass* BlueprintGeneratedClass = nullptr;
					if (EnginePackage)
					{
						BlueprintGeneratedClass = Cast<UClass>(StaticFindObjectFast(UClass::StaticClass(), EnginePackage, NAME_BlueprintGeneratedClass));
					}
					if (!BlueprintGeneratedClass)
					{
						UE_LOG(LogCore, Warning, TEXT("Could not find BlueprintGeneratedClass; will not be able to filter uncooked BPGC"));
					}
					else
					{
						SkipUncookedClasses.Add(NAME_BlueprintGeneratedClass);
						for (TObjectIterator<UClass> It; It; ++It)
						{
							if (It->IsChildOf(BlueprintGeneratedClass) && !It->HasAnyClassFlags(CLASS_Abstract))
							{
								SkipUncookedClasses.Add(It->GetFName());
							}
						}
					}
				}
				{
					SkipCookedClasses.Reset();

					static const FName NAME_Blueprint("Blueprint");
					UClass* BlueprintClass = nullptr;
					if (EnginePackage)
					{
						BlueprintClass = Cast<UClass>(StaticFindObjectFast(UClass::StaticClass(), EnginePackage, NAME_Blueprint));
					}
					if (!BlueprintClass)
					{
						UE_LOG(LogCore, Warning, TEXT("Could not find BlueprintClass; will not be able to filter cooked BP"));
					}
					else
					{
						SkipCookedClasses.Add(NAME_Blueprint);
						for (TObjectIterator<UClass> It; It; ++It)
						{
							if (It->IsChildOf(BlueprintClass) && !It->HasAnyClassFlags(CLASS_Abstract))
							{
								SkipCookedClasses.Add(It->GetFName());
							}
						}
					}
				}
			}

			bInitializedSkipClasses = true;
		}

		if (PackageFlags & PKG_ContainsNoAsset)
		{
			return true;
		}

		const bool bIsCooked = (PackageFlags & PKG_FilterEditorOnly);

		if ((bIsCooked && SkipCookedClasses.Contains(AssetClass)) ||
			(!bIsCooked && SkipUncookedClasses.Contains(AssetClass)))
		{
			return true;
		}
#endif //if WITH_ENGINE && WITH_EDITOR

		return false;
	}

	bool FFiltering::ShouldSkipAsset(const UObject* InAsset)
	{
		if (!InAsset)
		{
			return false;
		}
		UPackage* Package = InAsset->GetPackage();
		if (!Package)
		{
			return false;
		}
		return ShouldSkipAsset(InAsset->GetClass()->GetFName(), Package->GetPackageFlags());
	}

	void FFiltering::MarkDirty()
	{
#if WITH_ENGINE && WITH_EDITOR
		bInitializedSkipClasses = false;
#endif
	}
}
}
