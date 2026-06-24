// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Set.h"
#include "Containers/Queue.h"
#include "Serialization/ArchiveUObject.h"
#include "UObject/NameTypes.h"

class FSaveContext;
class UObject;
struct FWeakObjectPtr;
struct FLazyObjectPtr;
struct FSoftObjectPath;

/**
 * Helper class used to gather all package exports, imports and dependencies to build linker tables when saving packages
 * Gather Export, Imports, Referenced Names, SoftObjectPaths, Custom Object Versions
 */
class FPackageHarvester : public FArchiveUObject
{
public:
	class FExportScope
	{
	public:
		FExportScope(FPackageHarvester& InHarvester, UObject* InExport, bool bIsEditorOnlyObject)
			: Harvester(InHarvester)
		{
			check(Harvester.CurrentExportDependencies.CurrentExport == nullptr);
			Harvester.CurrentExportDependencies = { InExport };
			Harvester.bIsEditorOnlyExportOnStack = bIsEditorOnlyObject;
		}

		~FExportScope()
		{
			Harvester.AppendCurrentExportDependencies();
			Harvester.bIsEditorOnlyExportOnStack = false;
		}
	private:
		FPackageHarvester& Harvester;
	};

	class FIgnoreDependenciesScope
	{
	public:
		FIgnoreDependenciesScope(FPackageHarvester& InHarvester)
			: Harvester(InHarvester)
			, bPreviousValue(Harvester.CurrentExportDependencies.bIgnoreDependencies)
		{
			Harvester.CurrentExportDependencies.bIgnoreDependencies = true;
		}

		~FIgnoreDependenciesScope()
		{
			Harvester.CurrentExportDependencies.bIgnoreDependencies = bPreviousValue;
		}
	private:
		FPackageHarvester& Harvester;
		bool bPreviousValue;
	};

public:
	FPackageHarvester(FSaveContext& InContext);

	UObject* PopExportToProcess();

	void ProcessExport(UObject* InObject);
	void TryHarvestExport(UObject* InObject);
	void TryHarvestImport(UObject* InObject);

	void HarvestName(FName Name);
	void HarvestSearchableName(UObject* TypeObject, FName Name);
	void HarvestDependency(UObject* InObj, bool bIsNative);

	bool CurrentExportHasDependency(UObject* InObj) const;

	// FArchiveUObject implementation
	virtual FString GetArchiveName() const override;
	virtual void MarkSearchableName(const UObject* TypeObject, const FName& ValueName) const override;
	virtual FArchive& operator<<(UObject*& Obj) override;
	virtual FArchive& operator<<(struct FWeakObjectPtr& Value) override;
	virtual FArchive& operator<<(FLazyObjectPtr& LazyObjectPtr) override;
	virtual FArchive& operator<<(FSoftObjectPath& Value) override;
	virtual FArchive& operator<<(FName& Name) override;

private:

	void AppendCurrentExportDependencies();

	struct FExportDependencies
	{
		UObject* CurrentExport = nullptr;
		TSet<UObject*> ObjectReferences;
		TSet<UObject*> NativeObjectReferences;
		bool bIgnoreDependencies = false;
	};

	FSaveContext& SaveContext;
	TQueue<UObject*> ExportsToProcess;
	FExportDependencies CurrentExportDependencies;
	bool bIsEditorOnlyExportOnStack;
};