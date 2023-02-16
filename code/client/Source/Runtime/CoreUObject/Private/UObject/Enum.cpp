// Copyright Epic Games, Inc. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/ErrorException.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/MetaData.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/UObjectThreadContext.h"
#include "UObject/DevObjectVersion.h"
#include "UObject/CoreObjectVersion.h"
#include "UObject/CoreRedirects.h"
#include "UObject/LinkerLoad.h"

DEFINE_LOG_CATEGORY_STATIC(LogEnum, Log, All);

/*-----------------------------------------------------------------------------
	UEnum implementation.
-----------------------------------------------------------------------------*/

TMap<FName, UEnum*> UEnum::AllEnumNames;

UEnum::UEnum(const FObjectInitializer& ObjectInitializer)
	: UField(ObjectInitializer)
	, CppType()
	, CppForm(ECppForm::Regular)
	, EnumDisplayNameFn( nullptr )
{
}

void UEnum::Serialize( FArchive& Ar )
{
	Ar.UsingCustomVersion(FCoreObjectVersion::GUID);

	Super::Serialize(Ar);
	if (Ar.IsLoading())
	{
		if (Ar.UE4Ver() < VER_UE4_TIGHTLY_PACKED_ENUMS)
		{
			TArray<FName> TempNames;
			Ar << TempNames;
			int64 Value = 0;
			for (const FName& TempName : TempNames)
			{
				Names.Emplace(TempName, Value++);
			}
		}
		else if (Ar.CustomVer(FCoreObjectVersion::GUID) < FCoreObjectVersion::EnumProperties)
		{
			TArray<TPair<FName, uint8>> OldNames;
			Ar << OldNames;
			Names.Reset(OldNames.Num());
			for (const TPair<FName, uint8>& OldName : OldNames)
			{
				Names.Emplace(OldName.Key, OldName.Value);
			}
		}
		else
		{
			Ar << Names;
		}
	}
	else
	{
		Ar << Names;
	}

	if (Ar.UE4Ver() < VER_UE4_ENUM_CLASS_SUPPORT)
	{
		bool bIsNamespace;
		Ar << bIsNamespace;
		CppForm = bIsNamespace ? ECppForm::Namespaced : ECppForm::Regular;
	}
	else
	{
		uint8 EnumTypeByte = (uint8)CppForm;
		Ar << EnumTypeByte;
		CppForm = (ECppForm)EnumTypeByte;
	}

	if (Ar.IsLoading() || Ar.IsSaving())
	{
		// We're duplicating this enum.
		if ((Ar.GetPortFlags() & PPF_Duplicate)
			// and we're loading it from already serialized base.
			&& Ar.IsLoading())
		{
			// Rename enum names to reflect new class.
			RenameNamesAfterDuplication();
		}
		AddNamesToMasterList();
	}
}

void UEnum::BeginDestroy()
{
	RemoveNamesFromMasterList();

	Super::BeginDestroy();
}

FString UEnum::GetBaseEnumNameOnDuplication() const
{
	// Last name is always fully qualified, in form EnumName::Prefix_MAX.
	FString BaseEnumName = GetNameByIndex(Names.Num() - 1).ToString();

	// Double check we have a fully qualified name.
	const int32 DoubleColonPos = BaseEnumName.Find(TEXT("::"), ESearchCase::CaseSensitive);
	check(DoubleColonPos != INDEX_NONE);

	// Get actual base name.
	BaseEnumName.LeftChopInline(BaseEnumName.Len() - DoubleColonPos, false);

	return BaseEnumName;
}

void UEnum::RenameNamesAfterDuplication()
{
	if (Names.Num() != 0)
	{
		// Get name of base enum, from which we're duplicating.
		FString BaseEnumName = GetBaseEnumNameOnDuplication();

		// Get name of duplicated enum.
		FString ThisName = GetName();

		// Replace all usages of base class name to the duplicated one.
		for (TPair<FName, int64>& Kvp : Names)
		{
			FString NameString = Kvp.Key.ToString();
			NameString.ReplaceInline(*BaseEnumName, *ThisName);
			Kvp.Key = FName(*NameString);
		}
	}
}

int64 UEnum::ResolveEnumerator(FArchive& Ar, int64 EnumeratorIndex) const
{
	return EnumeratorIndex;
}

FString UEnum::GenerateFullEnumName(const TCHAR* InEnumName) const
{
	if (GetCppForm() == ECppForm::Regular || IsFullEnumName(InEnumName))
	{
		return InEnumName;
	}

	return FString::Printf(TEXT("%s::%s"), *GetName(), InEnumName);
}

FName UEnum::GetNameByIndex(int32 Index) const
{
	if (Names.IsValidIndex(Index))
	{
		return Names[Index].Key;
	}

	return NAME_None;
}

FName UEnum::GetNameByValue(int64 InValue) const
{
	for (const TPair<FName, int64>& Kvp : Names)
	{
		if (Kvp.Value == InValue)
		{
			return Kvp.Key;
		}
	}

	return NAME_None;
}

int32 UEnum::GetIndexByName(const FName InName, EGetByNameFlags Flags ) const
{
	ENameCase ComparisonMethod = !!(Flags & EGetByNameFlags::CaseSensitive) ? ENameCase::CaseSensitive : ENameCase::IgnoreCase;

	// First try the fast path
	const int32 Count = Names.Num();
	for (int32 Counter = 0; Counter < Count; ++Counter)
	{
		if (Names[Counter].Key.IsEqual(InName, ComparisonMethod))
		{
			return Counter;
		}
	}

	// Otherwise see if it is in the redirect table
	int32 Result = GetIndexByNameString(InName.ToString(), Flags);

	return Result;
}

int64 UEnum::GetValueByName(FName InName, EGetByNameFlags Flags) const
{
	// This handles redirects
	const int32 EnumIndex = GetIndexByName(InName, Flags);

	if (EnumIndex != INDEX_NONE)
	{
		return GetValueByIndex(EnumIndex);
	}

	return INDEX_NONE;
}

int64 UEnum::GetMaxEnumValue() const
{
	int32 NamesNum = Names.Num();
	if (NamesNum == 0)
	{
		return 0;
	}

	int64 MaxValue = Names[0].Value;
	for (int32 i = 0; i < NamesNum; ++i)
	{
		int64 CurrentValue = Names[i].Value;
		if (CurrentValue > MaxValue)
		{
			MaxValue = CurrentValue;
		}
	}

	return MaxValue;
}

bool UEnum::IsValidEnumValue(int64 InValue) const
{
	int32 NamesNum = Names.Num();
	for (int32 i = 0; i < NamesNum; ++i)
	{
		int64 CurrentValue = Names[i].Value;
		if (CurrentValue == InValue)
		{
			return true;
		}
	}

	return false;
}

bool UEnum::IsValidEnumName(FName InName) const
{
	int32 NamesNum = Names.Num();
	for (int32 i = 0; i < NamesNum; ++i)
	{
		FName CurrentName = Names[i].Key;
		if (CurrentName == InName)
		{
			return true;
		}
	}

	return false;
}

void UEnum::AddNamesToMasterList()
{
	for (TPair<FName, int64> Kvp : Names)
	{
		UEnum* Enum = AllEnumNames.FindRef(Kvp.Key);
		if (Enum == nullptr || Enum->HasAnyFlags(RF_NewerVersionExists))
		{
			AllEnumNames.Add(Kvp.Key, this);
		}
		else if (Enum != this && Enum->GetOutermost() != GetTransientPackage())
		{
			UE_LOG(LogEnum, Warning, TEXT("Enum name collision: '%s' is in both '%s' and '%s'"), *Kvp.Key.ToString(), *GetPathName(), *Enum->GetPathName());
		}
	}
}

void UEnum::RemoveNamesFromMasterList()
{
	for (TPair<FName, int64> Kvp : Names)
	{
		UEnum* Enum = AllEnumNames.FindRef(Kvp.Key);
		if (Enum == this)
		{
			AllEnumNames.Remove(Kvp.Key);
		}
	}
}

FString UEnum::GenerateEnumPrefix() const
{
	FString Prefix;
	if (Names.Num() > 0)
	{
		Names[0].Key.ToString(Prefix);

		// For each item in the enumeration, trim the prefix as much as necessary to keep it a prefix.
		// This ensures that once all items have been processed, a common prefix will have been constructed.
		// This will be the longest common prefix since as little as possible is trimmed at each step.
		for (int32 NameIdx = 1; NameIdx < Names.Num(); NameIdx++)
		{
			FString EnumItemName = Names[NameIdx].Key.ToString();

			// Find the length of the longest common prefix of Prefix and EnumItemName.
			int32 PrefixIdx = 0;
			while (PrefixIdx < Prefix.Len() && PrefixIdx < EnumItemName.Len() && Prefix[PrefixIdx] == EnumItemName[PrefixIdx])
			{
				PrefixIdx++;
			}

			// Trim the prefix to the length of the common prefix.
			Prefix.LeftInline(PrefixIdx, false);
		}

		// Find the index of the rightmost underscore in the prefix.
		int32 UnderscoreIdx = Prefix.Find(TEXT("_"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);

		// If an underscore was found, trim the prefix so only the part before the rightmost underscore is included.
		if (UnderscoreIdx > 0)
		{
			Prefix.LeftInline(UnderscoreIdx, false);
		}
		else
		{
			// no underscores in the common prefix - this probably indicates that the names
			// for this enum are not using Epic's notation, so just empty the prefix so that
			// the max item will use the full name of the enum
			Prefix.Empty();
		}
	}

	// If no common prefix was found, or if the enum does not contain any entries,
	// use the name of the enumeration instead.
	if (Prefix.Len() == 0)
	{
		Prefix = GetName();
	}
	return Prefix;
}

FString UEnum::GetNameStringByIndex(int32 InIndex) const
{
	if (Names.IsValidIndex(InIndex))
	{
		FName EnumEntryName = GetNameByIndex(InIndex);
		if (CppForm == ECppForm::Regular)
		{
			return EnumEntryName.ToString();
		}

		// Strip the namespace from the name.
		FString EnumNameString(EnumEntryName.ToString());
		int32 ScopeIndex = EnumNameString.Find(TEXT("::"), ESearchCase::CaseSensitive);
		if (ScopeIndex != INDEX_NONE)
		{
			return EnumNameString.Mid(ScopeIndex + 2);
		}
	}
	return FString();
}

FString UEnum::GetNameStringByValue(int64 Value) const
{
	int32 Index = GetIndexByValue(Value);
	return GetNameStringByIndex(Index);
}

bool UEnum::FindNameStringByValue(FString& Out, int64 InValue) const
{
	int32 Index = GetIndexByValue(InValue);
	if (Index == INDEX_NONE)
	{
		return false;
	}

	Out = GetNameStringByIndex(Index);
	return true;
}

FText UEnum::GetDisplayNameTextByIndex(int32 NameIndex) const
{
	FString RawName = GetNameStringByIndex(NameIndex);

	if (RawName.IsEmpty())
	{
		return FText::GetEmpty();
	}

#if WITH_EDITOR
	FText LocalizedDisplayName;
	// In the editor, use metadata and localization to look up names
	static const FString Namespace = TEXT("UObjectDisplayNames");
	const FString Key = GetFullGroupName(false) + TEXT(".") + RawName;

	FString NativeDisplayName;
	if (HasMetaData(TEXT("DisplayName"), NameIndex))
	{
		NativeDisplayName = GetMetaData(TEXT("DisplayName"), NameIndex);
	}
	else
	{
		NativeDisplayName = FName::NameToDisplayString(RawName, false);
	}

	if (!(FText::FindText(Namespace, Key, /*OUT*/LocalizedDisplayName, &NativeDisplayName)))
	{
		LocalizedDisplayName = FText::FromString(NativeDisplayName);
	}

	if (!LocalizedDisplayName.IsEmpty())
	{
		return LocalizedDisplayName;
	}
#endif

	if (EnumDisplayNameFn)
	{
		return (*EnumDisplayNameFn)(NameIndex);
	}

	return FText::FromString(GetNameStringByIndex(NameIndex));
}

FText UEnum::GetDisplayNameTextByValue(int64 Value) const
{
	int32 Index = GetIndexByValue(Value);
	return GetDisplayNameTextByIndex(Index);
}

bool UEnum::FindDisplayNameTextByValue(FText& Out, int64 Value) const
{
	int32 Index = GetIndexByValue(Value);
	if (Index == INDEX_NONE)
	{
		return false;
	}

	Out = GetDisplayNameTextByIndex(Index);
	return true;
}

FString UEnum::GetAuthoredNameStringByIndex(int32 InIndex) const
{
	return GetNameStringByIndex(InIndex);
}

FString UEnum::GetAuthoredNameStringByValue(int64 Value) const
{
	int32 Index = GetIndexByValue(Value);
	return GetAuthoredNameStringByIndex(Index);
}

bool UEnum::FindAuthoredNameStringByValue(FString& Out, int64 Value) const
{
	int32 Index = GetIndexByValue(Value);
	if (Index == INDEX_NONE)
	{
		return false;
	}

	Out = GetAuthoredNameStringByIndex(Index);
	return true;
}

int32 UEnum::GetIndexByNameString(const FString& InSearchString, EGetByNameFlags Flags) const
{
	ENameCase         NameComparisonMethod   = !!(Flags & EGetByNameFlags::CaseSensitive) ? ENameCase  ::CaseSensitive : ENameCase  ::IgnoreCase;
	ESearchCase::Type StringComparisonMethod = !!(Flags & EGetByNameFlags::CaseSensitive) ? ESearchCase::CaseSensitive : ESearchCase::IgnoreCase;

	FString SearchEnumEntryString = InSearchString;
	FString ModifiedEnumEntryString;

	// Strip or add the namespace
	int32 DoubleColonIndex = SearchEnumEntryString.Find(TEXT("::"), ESearchCase::CaseSensitive);
	if (DoubleColonIndex == INDEX_NONE)
	{
		ModifiedEnumEntryString = GenerateFullEnumName(*SearchEnumEntryString);
	}
	else
	{
		ModifiedEnumEntryString = SearchEnumEntryString.RightChop(DoubleColonIndex + 2);
	}

	const TMap<FString, FString>* ValueChanges = FCoreRedirects::GetValueRedirects(ECoreRedirectFlags::Type_Enum, this);

	if (ValueChanges)
	{
		const FString* FoundNewEnumEntry = ValueChanges->Find(SearchEnumEntryString);
		if (FoundNewEnumEntry == nullptr)
		{
			FoundNewEnumEntry = ValueChanges->Find(ModifiedEnumEntryString);
		}

		if (FoundNewEnumEntry)
		{
			SearchEnumEntryString = **FoundNewEnumEntry;

			// Recompute modified name
			int32 NewDoubleColonIndex = SearchEnumEntryString.Find(TEXT("::"), ESearchCase::CaseSensitive);
			if (NewDoubleColonIndex == INDEX_NONE)
			{
				ModifiedEnumEntryString = GenerateFullEnumName(*SearchEnumEntryString);
			}
			else
			{
				ModifiedEnumEntryString = SearchEnumEntryString.RightChop(NewDoubleColonIndex + 2);
			}
		}
	}
	else if (DoubleColonIndex != INDEX_NONE)
	{
		// If we didn't find a value redirect and our original string was namespaced, we need to fix the namespace now as it may have changed due to enum type redirect
		SearchEnumEntryString = GenerateFullEnumName(*ModifiedEnumEntryString);
	}

	// Search for names both with and without namespace
	FName SearchName = FName(*SearchEnumEntryString, FNAME_Find);
	FName ModifiedName = FName(*ModifiedEnumEntryString, FNAME_Find);

	const int32 Count = Names.Num();
	for (int32 Counter = 0; Counter < Count; ++Counter)
	{
		if (Names[Counter].Key.IsEqual(SearchName, NameComparisonMethod) || Names[Counter].Key.IsEqual(ModifiedName, NameComparisonMethod))
		{
			return Counter;
		}
	}

	// Check authored name, but only if this is a subclass of Enum that might have implemented it 
	// and we've ascertained that there are no entries that match on the cheaper FName checks
	const bool bCheckAuthoredName = !!(Flags & EGetByNameFlags::CheckAuthoredName) && (GetClass() != UEnum::StaticClass());

	if (bCheckAuthoredName)
	{
		for (int32 Counter = 0; Counter < Count; ++Counter)
		{
			FString AuthoredName = GetAuthoredNameStringByIndex(Counter);

			if (AuthoredName.Equals(SearchEnumEntryString, StringComparisonMethod) || AuthoredName.Equals(ModifiedEnumEntryString, StringComparisonMethod))
			{
				return Counter;
			}
		}
	}

	if (!InSearchString.Equals(SearchEnumEntryString, StringComparisonMethod))
	{
		// There was an actual redirect, and we didn't find it
		UE_LOG(LogEnum, Warning, TEXT("EnumRedirect for enum %s maps '%s' to invalid value '%s'!"), *GetName(), *InSearchString, *SearchEnumEntryString);
	}
	else if (!!(Flags & EGetByNameFlags::ErrorIfNotFound) && !InSearchString.IsEmpty() && !InSearchString.Equals(FName().ToString(), StringComparisonMethod))
	{
		// None is passed in by blueprints at various points, isn't an error. Any other failed resolve should be fixed
		UObject* SerializedObject = nullptr;
		if (FLinkerLoad* Linker = GetLinker())
		{
			if (FUObjectSerializeContext* LoadContext = Linker->GetSerializeContext())
			{
				SerializedObject = LoadContext->SerializedObject;
			}
		}
		UE_LOG(LogEnum, Warning, TEXT("In asset '%s', there is an enum property of type '%s' with an invalid value of '%s'"), *GetPathNameSafe(SerializedObject ? SerializedObject : FUObjectThreadContext::Get().ConstructedObject), *GetName(), *InSearchString);
	}

	return INDEX_NONE;
}

int64 UEnum::GetValueByNameString(const FString& SearchString, EGetByNameFlags Flags) const
{
	int32 Index = GetIndexByNameString(SearchString, Flags);

	if (Index != INDEX_NONE)
	{
		return GetValueByIndex(Index);
	}

	return INDEX_NONE;
}

bool UEnum::ContainsExistingMax() const
{
	if (GetIndexByName(*GenerateFullEnumName(TEXT("MAX")), EGetByNameFlags::CaseSensitive) != INDEX_NONE)
	{
		return true;
	}

	FName MaxEnumItem = *GenerateFullEnumName(*(GenerateEnumPrefix() + TEXT("_MAX")));
	if (GetIndexByName(MaxEnumItem, EGetByNameFlags::CaseSensitive) != INDEX_NONE)
	{
		return true;
	}

	return false;
}

bool UEnum::SetEnums(TArray<TPair<FName, int64>>& InNames, UEnum::ECppForm InCppForm, EEnumFlags InFlags, bool bAddMaxKeyIfMissing)
{
	if (Names.Num() > 0)
	{
		RemoveNamesFromMasterList();
	}
	Names     = InNames;
	CppForm   = InCppForm;
	EnumFlags = InFlags;

	if (bAddMaxKeyIfMissing)
	{
		if (!ContainsExistingMax())
		{
			FName MaxEnumItem = *GenerateFullEnumName(*(GenerateEnumPrefix() + TEXT("_MAX")));
			if (LookupEnumName(MaxEnumItem) != INDEX_NONE)
			{
				// the MAX identifier is already being used by another enum
				return false;
			}

			Names.Emplace(MaxEnumItem, GetMaxEnumValue() + 1);
		}
	}
	AddNamesToMasterList();

	return true;
}

#if WITH_EDITOR

FText UEnum::GetToolTipTextByIndex(int32 NameIndex) const
{
	FText LocalizedToolTip;
	FString NativeToolTip = GetMetaData( TEXT("ToolTip"), NameIndex );

	static const FString Namespace = TEXT("UObjectToolTips");
	FString Key = GetFullGroupName(false) + TEXT(".") + GetNameStringByIndex(NameIndex);
		
	if ( !FText::FindText( Namespace, Key, /*OUT*/LocalizedToolTip, &NativeToolTip ) )
	{
		static const FString DoxygenSee(TEXT("@see"));
		static const FString TooltipSee(TEXT("See:"));
		if (NativeToolTip.ReplaceInline(*DoxygenSee, *TooltipSee) > 0)
		{
			NativeToolTip.TrimEndInline();
		}

		LocalizedToolTip = FText::FromString(NativeToolTip);
	}

	return LocalizedToolTip;
}

#endif

#if WITH_EDITORONLY_DATA

bool UEnum::HasMetaData( const TCHAR* Key, int32 NameIndex/*=INDEX_NONE*/ ) const
{
	bool bResult = false;

	UPackage* Package = GetOutermost();
	check(Package);

	UMetaData* MetaData = Package->GetMetaData();
	check(MetaData);

	FString KeyString;

	// If an index was specified, search for metadata linked to a specified value
	if ( NameIndex != INDEX_NONE )
	{
		KeyString = GetNameStringByIndex(NameIndex);
		KeyString.AppendChar(TEXT('.'));
		KeyString.Append(Key);
	}
	// If no index was specified, search for metadata for the enum itself
	else
	{
		KeyString = Key;
	}
	bResult = MetaData->HasValue( this, *KeyString );
	
	return bResult;
}

FString UEnum::GetMetaData( const TCHAR* Key, int32 NameIndex/*=INDEX_NONE*/, bool bAllowRemap/*=true*/ ) const
{
	UPackage* Package = GetOutermost();
	check(Package);

	UMetaData* MetaData = Package->GetMetaData();
	check(MetaData);

	FString KeyString;

	// If an index was specified, search for metadata linked to a specified value
	if ( NameIndex != INDEX_NONE )
	{
		check(Names.IsValidIndex(NameIndex));
		KeyString = GetNameStringByIndex(NameIndex) + TEXT(".") + Key;
	}
	// If no index was specified, search for metadata for the enum itself
	else
	{
		KeyString = Key;
	}

	FString ResultString = MetaData->GetValue( this, *KeyString );
	
	// look in the engine ini, in a section named after the metadata key we are looking for, and the enum's name (KeyString)
	if (bAllowRemap && ResultString.StartsWith(TEXT("ini:")))
	{
		if (!GConfig->GetString(TEXT("EnumRemap"), *KeyString, ResultString, GEngineIni))
		{
			// if this fails, then use what's after the ini:
			ResultString.MidInline(4, MAX_int32, false);
		}
	}

	return ResultString;
}

void UEnum::SetMetaData( const TCHAR* Key, const TCHAR* InValue, int32 NameIndex ) const
{
	UPackage* Package = GetOutermost();
	check(Package);

	UMetaData* MetaData = Package->GetMetaData();
	check(MetaData);

	FString KeyString;

	// If an index was specified, search for metadata linked to a specified value
	if ( NameIndex != INDEX_NONE )
	{
		check(Names.IsValidIndex(NameIndex));
		KeyString = GetNameStringByIndex(NameIndex) + TEXT(".") + Key;
	}
	// If no index was specified, search for metadata for the enum itself
	else
	{
		KeyString = Key;
	}

	MetaData->SetValue( this, *KeyString, InValue );
}

void UEnum::RemoveMetaData( const TCHAR* Key, int32 NameIndex/*=INDEX_NONE*/) const
{
	UPackage* Package = GetOutermost();
	check(Package);

	UMetaData* MetaData = Package->GetMetaData();
	check(MetaData);

	FString KeyString;

	// If an index was specified, search for metadata linked to a specified value
	if ( NameIndex != INDEX_NONE )
	{
		check(Names.IsValidIndex(NameIndex));
		KeyString = GetNameStringByIndex(NameIndex) + TEXT(".") + Key;
	}
	// If no index was specified, search for metadata for the enum itself
	else
	{
		KeyString = Key;
	}

	MetaData->RemoveValue( this, *KeyString );
}

#endif

int64 UEnum::ParseEnum(const TCHAR*& Str)
{
	FString Token;
	const TCHAR* ParsedStr = Str;
	if (FParse::AlnumToken(ParsedStr, Token))
	{
		FName TheName = FName(*Token, FNAME_Find);
		int64 Result = LookupEnumName(TheName);
		if (Result != INDEX_NONE)
		{
			Str = ParsedStr;
		}
		return Result;
	}
	else
	{
		return 0;
	}
}

IMPLEMENT_CORE_INTRINSIC_CLASS(UEnum, UField,
	{
	}
);
