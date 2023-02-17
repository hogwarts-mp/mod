// Copyright Epic Games, Inc. All Rights Reserved.

#include "Internationalization/LegacyInternationalization.h"
#include "Internationalization/Cultures/LeetCulture.h"

#if !UE_ENABLE_ICU

#include "InvariantCulture.h"

FLegacyInternationalization::FLegacyInternationalization(FInternationalization* const InI18N)
	: I18N(InI18N)
{

}

bool FLegacyInternationalization::Initialize()
{
	I18N->InvariantCulture = FInvariantCulture::Create();
	I18N->DefaultLanguage = I18N->InvariantCulture;
	I18N->DefaultLocale = I18N->InvariantCulture;
	I18N->CurrentLanguage = I18N->InvariantCulture;
	I18N->CurrentLocale = I18N->InvariantCulture;

#if ENABLE_LOC_TESTING
	I18N->AddCustomCulture(MakeShared<FLeetCulture>(I18N->InvariantCulture.ToSharedRef()));
#endif

	return true;
}

void FLegacyInternationalization::Terminate()
{
}

void FLegacyInternationalization::LoadAllCultureData()
{
}

bool FLegacyInternationalization::IsCultureRemapped(const FString& Name, FString* OutMappedCulture)
{
	return false;
}

bool FLegacyInternationalization::IsCultureAllowed(const FString& Name)
{
	return true;
}

void FLegacyInternationalization::RefreshCultureDisplayNames(const TArray<FString>& InPrioritizedDisplayCultureNames)
{
}

void FLegacyInternationalization::RefreshCachedConfigData()
{
}

void FLegacyInternationalization::HandleLanguageChanged(const FCultureRef InNewLanguage)
{
}

void FLegacyInternationalization::GetCultureNames(TArray<FString>& CultureNames) const
{
	CultureNames.Reset(1 + I18N->CustomCultures.Num());
	CultureNames.Add(FString());
	for (const FCultureRef& CustomCulture : I18N->CustomCultures)
	{
		CultureNames.Add(CustomCulture->GetName());
	}
}

TArray<FString> FLegacyInternationalization::GetPrioritizedCultureNames(const FString& Name)
{
	TArray<FString> PrioritizedCultureNames;
	PrioritizedCultureNames.Add(Name);
	return PrioritizedCultureNames;
}

FCulturePtr FLegacyInternationalization::GetCulture(const FString& Name)
{
	FCulturePtr Culture = I18N->GetCustomCulture(Name);
	if (!Culture && Name.IsEmpty())
	{
		Culture = I18N->InvariantCulture;
	}
	return Culture;
}

#endif
