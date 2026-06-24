// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/ObjectResource.h"
#include "UObject/Class.h"
#include "Templates/Casts.h"

/*-----------------------------------------------------------------------------
	Helper functions.
-----------------------------------------------------------------------------*/
namespace
{
	FORCEINLINE bool IsCorePackage(const FName& PackageName)
	{
		return PackageName == NAME_Core || PackageName == GLongCorePackageName;
	}
}

/*-----------------------------------------------------------------------------
	FObjectResource
-----------------------------------------------------------------------------*/

FObjectResource::FObjectResource()
{}

FObjectResource::FObjectResource( UObject* InObject )
:	ObjectName		( InObject ? InObject->GetFName() : FName(NAME_None)		)
{
}

/*-----------------------------------------------------------------------------
	FObjectExport.
-----------------------------------------------------------------------------*/

FObjectExport::FObjectExport()
: FObjectResource()
, ObjectFlags(RF_NoFlags)
, SerialSize(0)
, SerialOffset(0)
, ScriptSerializationStartOffset(0)
, ScriptSerializationEndOffset(0)
, Object(NULL)
, HashNext(INDEX_NONE)
, bForcedExport(false)
, bNotForClient(false)
, bNotForServer(false)
, bNotAlwaysLoadedForEditorGame(true)
, bIsAsset(false)
, bExportLoadFailed(false)
, DynamicType(EDynamicType::NotDynamicExport)
, bWasFiltered(false)
, PackageFlags(0)
, FirstExportDependency(-1)
, SerializationBeforeSerializationDependencies(0)
, CreateBeforeSerializationDependencies(0)
, SerializationBeforeCreateDependencies(0)
, CreateBeforeCreateDependencies(0)

{}

FObjectExport::FObjectExport( UObject* InObject, bool bInNotAlwaysLoadedForEditorGame /*= true*/)
: FObjectResource(InObject)
, ObjectFlags(InObject ? InObject->GetMaskedFlags(RF_Load) : RF_NoFlags)
, SerialSize(0)
, SerialOffset(0)
, ScriptSerializationStartOffset(0)
, ScriptSerializationEndOffset(0)
, Object(InObject)
, HashNext(INDEX_NONE)
, bForcedExport(false)
, bNotForClient(false)
, bNotForServer(false)
, bNotAlwaysLoadedForEditorGame(bInNotAlwaysLoadedForEditorGame)
, bIsAsset(false)
, bExportLoadFailed(false)
, DynamicType(EDynamicType::NotDynamicExport)
, bWasFiltered(false)
, PackageFlags(0)
, FirstExportDependency(-1)
, SerializationBeforeSerializationDependencies(0)
, CreateBeforeSerializationDependencies(0)
, SerializationBeforeCreateDependencies(0)
, CreateBeforeCreateDependencies(0)
{
	if(Object)		
	{
		bNotForClient = !Object->NeedsLoadForClient();
		bNotForServer = !Object->NeedsLoadForServer();
		bIsAsset = Object->IsAsset();
	}
}

void FObjectExport::ResetObject()
{
	Object = nullptr;
	bExportLoadFailed = false;
	bWasFiltered = false;
}

FArchive& operator<<(FArchive& Ar, FObjectExport& E)
{
	FStructuredArchiveFromArchive(Ar).GetSlot() << E;
	return Ar;
}

void operator<<(FStructuredArchive::FSlot Slot, FObjectExport& E)
{
	FArchive& BaseArchive = Slot.GetUnderlyingArchive();
	FStructuredArchive::FRecord Record = Slot.EnterRecord();

	Record << SA_VALUE(TEXT("ClassIndex"), E.ClassIndex);
	Record << SA_VALUE(TEXT("SuperIndex"), E.SuperIndex);

	if (BaseArchive.UE4Ver() >= VER_UE4_TemplateIndex_IN_COOKED_EXPORTS)
	{
		Record << SA_VALUE(TEXT("TemplateIndex"), E.TemplateIndex);
	}

	Record << SA_VALUE(TEXT("OuterIndex"), E.OuterIndex);
	Record << SA_VALUE(TEXT("ObjectName"), E.ObjectName);

	uint32 Save = E.ObjectFlags & RF_Load;
	Record << SA_VALUE(TEXT("ObjectFlags"), Save);

	if (BaseArchive.IsLoading())
	{
		E.ObjectFlags = EObjectFlags(Save & RF_Load);
	}

	if (BaseArchive.UE4Ver() < VER_UE4_64BIT_EXPORTMAP_SERIALSIZES)
	{
		int32 SerialSize = E.SerialSize;
		Record << SA_VALUE(TEXT("SerialSize"), SerialSize);
		E.SerialSize = (int64)SerialSize;

		int32 SerialOffset = E.SerialOffset;
		Record << SA_VALUE(TEXT("SerialOffset"), SerialOffset);
		E.SerialOffset = SerialOffset;
	}
	else
	{
		Record << SA_VALUE(TEXT("SerialSize"), E.SerialSize);
		Record << SA_VALUE(TEXT("SerialOffset"), E.SerialOffset);
	}

	Record << SA_VALUE(TEXT("bForcedExport"), E.bForcedExport);
	Record << SA_VALUE(TEXT("bNotForClient"), E.bNotForClient);
	Record << SA_VALUE(TEXT("bNotForServer"), E.bNotForServer);

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	Record << SA_VALUE(TEXT("PackageGuid"), E.PackageGuid);
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	Record << SA_VALUE(TEXT("PackageFlags"), E.PackageFlags);

	if (BaseArchive.UE4Ver() >= VER_UE4_LOAD_FOR_EDITOR_GAME)
	{
		Record << SA_VALUE(TEXT("bNotAlwaysLoadedForEditorGame"), E.bNotAlwaysLoadedForEditorGame);
	}

	if (BaseArchive.UE4Ver() >= VER_UE4_COOKED_ASSETS_IN_EDITOR_SUPPORT)
	{
		Record << SA_VALUE(TEXT("bIsAsset"), E.bIsAsset);
	}

	if (BaseArchive.UE4Ver() >= VER_UE4_PRELOAD_DEPENDENCIES_IN_COOKED_EXPORTS)
	{
		Record << SA_VALUE(TEXT("FirstExportDependency"), E.FirstExportDependency);
		Record << SA_VALUE(TEXT("SerializationBeforeSerializationDependencies"), E.SerializationBeforeSerializationDependencies);
		Record << SA_VALUE(TEXT("CreateBeforeSerializationDependencies"), E.CreateBeforeSerializationDependencies);
		Record << SA_VALUE(TEXT("SerializationBeforeCreateDependencies"), E.SerializationBeforeCreateDependencies);
		Record << SA_VALUE(TEXT("CreateBeforeCreateDependencies"), E.CreateBeforeCreateDependencies);
	}	
}

/*-----------------------------------------------------------------------------
	FObjectTextExport.
-----------------------------------------------------------------------------*/

void operator<<(FStructuredArchive::FSlot Slot, FObjectTextExport& E)
{
	FArchive& BaseArchive = Slot.GetUnderlyingArchive();
	FString ClassName, OuterName, SuperStructName;
	check(BaseArchive.IsTextFormat());

	if (BaseArchive.IsSaving())
	{
		check(E.Export.Object);

		UClass* ObjClass = E.Export.Object->GetClass();
		if (ObjClass != UClass::StaticClass())
		{
			ClassName = ObjClass->GetFullName();
		}

		if (E.Export.Object->GetOuter() != E.Outer)
		{
			OuterName = E.Export.Object->GetOuter() ? E.Export.Object->GetOuter()->GetFullName() : FString();
		}

		if (UStruct* Struct = Cast<UStruct>(E.Export.Object))
		{
			if (Struct->GetSuperStruct() != nullptr)
			{
				SuperStructName = Struct->GetSuperStruct()->GetFullName();
			}
		}
	}

	Slot << SA_ATTRIBUTE(TEXT("Class"), ClassName);
	Slot << SA_OPTIONAL_ATTRIBUTE(TEXT("Outer"), OuterName, FString());
	Slot << SA_OPTIONAL_ATTRIBUTE(TEXT("SuperStruct"), SuperStructName, FString());

	if (BaseArchive.IsLoading())
	{
		E.ClassName = ClassName;
		E.OuterName = OuterName;
		E.SuperStructName = SuperStructName;
	}

	uint32 Save = E.Export.ObjectFlags & RF_Load;
	Slot << SA_OPTIONAL_ATTRIBUTE(TEXT("ObjectFlags"), Save, 0);
	if (BaseArchive.IsLoading())
	{
		E.Export.ObjectFlags = EObjectFlags(Save & RF_Load);
	}

	Slot << SA_OPTIONAL_ATTRIBUTE(TEXT("bForcedExport"), E.Export.bForcedExport, false);
	Slot << SA_OPTIONAL_ATTRIBUTE(TEXT("bNotForClient"), E.Export.bNotForClient, false);
	Slot << SA_OPTIONAL_ATTRIBUTE(TEXT("bNotForServer"), E.Export.bNotForServer, false);

	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	Slot << SA_OPTIONAL_ATTRIBUTE(TEXT("PackageGuid"), E.Export.PackageGuid, FGuid());
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	Slot << SA_OPTIONAL_ATTRIBUTE(TEXT("PackageFlags"), E.Export.PackageFlags, 0);

	Slot << SA_OPTIONAL_ATTRIBUTE(TEXT("bNotAlwaysLoadedForEditorGame"), E.Export.bNotAlwaysLoadedForEditorGame, false);
	Slot << SA_OPTIONAL_ATTRIBUTE(TEXT("bIsAsset"), E.Export.bIsAsset, false);
}

/*-----------------------------------------------------------------------------
	FObjectImport.
-----------------------------------------------------------------------------*/

FObjectImport::FObjectImport()
	: FObjectResource()
	, bImportPackageHandled(false)
	, bImportSearchedFor(false)
	, bImportFailed(false)
{
}

FObjectImport::FObjectImport(UObject* InObject)
	: FObjectResource(InObject)
	, ClassPackage(InObject ? InObject->GetClass()->GetOuter()->GetFName() : NAME_None)
	, ClassName(InObject ? InObject->GetClass()->GetFName() : NAME_None)
	, XObject(InObject)
	, SourceLinker(NULL)
	, SourceIndex(INDEX_NONE)
	, bImportPackageHandled(false)
	, bImportSearchedFor(false)
	, bImportFailed(false)
{
}

FObjectImport::FObjectImport(UObject* InObject, UClass* InClass)
	: FObjectResource(InObject)
	, ClassPackage((InObject && InClass) ? InClass->GetOuter()->GetFName() : NAME_None)
	, ClassName((InObject && InClass) ? InClass->GetFName() : NAME_None)
	, XObject(InObject)
	, SourceLinker(NULL)
	, SourceIndex(INDEX_NONE)
	, bImportPackageHandled(false)
	, bImportSearchedFor(false)
	, bImportFailed(false)
{
}

FArchive& operator<<( FArchive& Ar, FObjectImport& I )
{
	FStructuredArchiveFromArchive(Ar).GetSlot() << I;
	return Ar;
}

void operator<<(FStructuredArchive::FSlot Slot, FObjectImport& I)
{
	FStructuredArchive::FRecord Record = Slot.EnterRecord();

	Record << SA_VALUE(TEXT("ClassPackage"), I.ClassPackage);
	Record << SA_VALUE(TEXT("ClassName"), I.ClassName);
	Record << SA_VALUE(TEXT("OuterIndex"), I.OuterIndex);
	Record << SA_VALUE(TEXT("ObjectName"), I.ObjectName);

	//@todo: re-enable package override at runtime when ready
#if WITH_EDITORONLY_DATA
	if (Slot.GetUnderlyingArchive().UE4Ver() >= VER_UE4_NON_OUTER_PACKAGE_IMPORT && !Slot.GetUnderlyingArchive().IsFilterEditorOnly())
	{
		Record << SA_VALUE(TEXT("PackageName"), I.PackageName);
	}
#endif

	if (Slot.GetUnderlyingArchive().IsLoading())
	{
		I.SourceLinker = NULL;
		I.SourceIndex = INDEX_NONE;
		I.XObject = NULL;
	}
}
