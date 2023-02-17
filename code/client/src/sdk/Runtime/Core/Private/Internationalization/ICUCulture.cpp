// Copyright Epic Games, Inc. All Rights Reserved.

#include "Internationalization/ICUCulture.h"
#include "Internationalization/Cultures/LeetCulture.h"
#include "Internationalization/Internationalization.h"
#include "Misc/ScopeLock.h"
#include "Containers/SortedMap.h"
#include "Internationalization/FastDecimalFormat.h"

#if UE_ENABLE_ICU
#include "Internationalization/ICUUtilities.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<int32> CVarSpanishUsesRAENumberFormat(
	TEXT("Localization.SpanishUsesRAENumberFormat"),
	1,
	TEXT("0: Disabled (CLDR format), 1: Enabled (RAE format, default)."),
	ECVF_Default
	);

namespace
{
	TSharedRef<const icu::BreakIterator> CreateBreakIterator( const icu::Locale& ICULocale, const EBreakIteratorType Type)
	{
		UErrorCode ICUStatus = U_ZERO_ERROR;
		icu::BreakIterator* (*FactoryFunction)(const icu::Locale&, UErrorCode&) = nullptr;
		switch (Type)
		{
		case EBreakIteratorType::Grapheme:
			FactoryFunction = icu::BreakIterator::createCharacterInstance;
			break;
		case EBreakIteratorType::Word:
			FactoryFunction = icu::BreakIterator::createWordInstance;
			break;
		case EBreakIteratorType::Line:
			FactoryFunction = icu::BreakIterator::createLineInstance;
			break;
		case EBreakIteratorType::Sentence:
			FactoryFunction = icu::BreakIterator::createSentenceInstance;
			break;
		case EBreakIteratorType::Title:
			FactoryFunction = icu::BreakIterator::createTitleInstance;
			break;
		default:
			checkf(false, TEXT("Unhandled break iterator type"));
		}
		TSharedPtr<const icu::BreakIterator> Ptr = MakeShareable( FactoryFunction(ICULocale, ICUStatus) );
		checkf(Ptr.IsValid(), TEXT("Creating a break iterator object failed using locale %s. Perhaps this locale has no data."), StringCast<TCHAR>(ICULocale.getName()).Get());
		return Ptr.ToSharedRef();
	}

	TSharedRef<const icu::Collator, ESPMode::ThreadSafe> CreateCollator( const icu::Locale& ICULocale )
	{
		UErrorCode ICUStatus = U_ZERO_ERROR;
		TSharedPtr<const icu::Collator, ESPMode::ThreadSafe> Ptr = MakeShareable( icu::Collator::createInstance( ICULocale, ICUStatus ) );
		checkf(Ptr.IsValid(), TEXT("Creating a collator object failed using locale %s. Perhaps this locale has no data."), StringCast<TCHAR>(ICULocale.getName()).Get());
		return Ptr.ToSharedRef();
	}

	TSharedRef<const icu::DateFormat, ESPMode::ThreadSafe> CreateDateFormat( const icu::Locale& ICULocale )
	{
		UErrorCode ICUStatus = U_ZERO_ERROR;
		TSharedPtr<icu::DateFormat, ESPMode::ThreadSafe> Ptr = MakeShareable( icu::DateFormat::createDateInstance( icu::DateFormat::kDefault, ICULocale ) );
		checkf(Ptr.IsValid(), TEXT("Creating a date format object failed using locale %s. Perhaps this locale has no data."), StringCast<TCHAR>(ICULocale.getName()).Get());
		Ptr->adoptTimeZone( icu::TimeZone::createDefault() );
		return Ptr.ToSharedRef();
	}

	TSharedRef<const icu::DateFormat, ESPMode::ThreadSafe> CreateTimeFormat( const icu::Locale& ICULocale )
	{
		UErrorCode ICUStatus = U_ZERO_ERROR;
		TSharedPtr<icu::DateFormat, ESPMode::ThreadSafe> Ptr = MakeShareable( icu::DateFormat::createTimeInstance( icu::DateFormat::kDefault, ICULocale ) );
		checkf(Ptr.IsValid(), TEXT("Creating a time format object failed using locale %s. Perhaps this locale has no data."), StringCast<TCHAR>(ICULocale.getName()).Get());
		Ptr->adoptTimeZone( icu::TimeZone::createDefault() );
		return Ptr.ToSharedRef();
	}

	TSharedRef<const icu::DateFormat, ESPMode::ThreadSafe> CreateDateTimeFormat( const icu::Locale& ICULocale )
	{
		UErrorCode ICUStatus = U_ZERO_ERROR;
		TSharedPtr<icu::DateFormat, ESPMode::ThreadSafe> Ptr = MakeShareable( icu::DateFormat::createDateTimeInstance( icu::DateFormat::kDefault, icu::DateFormat::kDefault, ICULocale ) );
		checkf(Ptr.IsValid(), TEXT("Creating a date-time format object failed using locale %s. Perhaps this locale has no data."), StringCast<TCHAR>(ICULocale.getName()).Get());
		Ptr->adoptTimeZone( icu::TimeZone::createDefault() );
		return Ptr.ToSharedRef();
	}
}

ETextPluralForm ICUPluralFormToUE(const icu::UnicodeString& InICUTag)
{
	static const icu::UnicodeString ZeroStr("zero");
	static const icu::UnicodeString OneStr("one");
	static const icu::UnicodeString TwoStr("two");
	static const icu::UnicodeString FewStr("few");
	static const icu::UnicodeString ManyStr("many");
	static const icu::UnicodeString OtherStr("other");

	if (InICUTag == ZeroStr)
	{
		return ETextPluralForm::Zero;
	}
	if (InICUTag == OneStr)
	{
		return ETextPluralForm::One;
	}
	if (InICUTag == TwoStr)
	{
		return ETextPluralForm::Two;
	}
	if (InICUTag == FewStr)
	{
		return ETextPluralForm::Few;
	}
	if (InICUTag == ManyStr)
	{
		return ETextPluralForm::Many;
	}
	if (InICUTag == OtherStr)
	{
		return ETextPluralForm::Other;
	}

	ensureAlwaysMsgf(false, TEXT("Unknown ICU plural form tag! Returning 'other'."));
	return ETextPluralForm::Other;
}

TArray<ETextPluralForm> ICUPluralRulesToUEValidPluralForms(const icu::PluralRules* InICUPluralRules)
{
	check(InICUPluralRules);

	UErrorCode ICUStatus = U_ZERO_ERROR;
	icu::StringEnumeration* ICUAvailablePluralForms = InICUPluralRules->getKeywords(ICUStatus);

	TArray<ETextPluralForm> UEPluralForms;

	if (ICUAvailablePluralForms)
	{
		while (const icu::UnicodeString* ICUTag = ICUAvailablePluralForms->snext(ICUStatus))
		{
			UEPluralForms.Add(ICUPluralFormToUE(*ICUTag));
		}
		delete ICUAvailablePluralForms;
	}

	UEPluralForms.Sort();
	return UEPluralForms;
}

FICUCultureImplementation::FICUCultureImplementation(const FString& LocaleName)
	: ICULocale( TCHAR_TO_ANSI( *LocaleName ) )
{
	if (ICULocale.isBogus())
	{
		ICULocale = icu::Locale();
	}
	{
		UErrorCode ICUStatus = U_ZERO_ERROR;
		ICUCardinalPluralRules = icu::PluralRules::forLocale(ICULocale, UPLURAL_TYPE_CARDINAL, ICUStatus);
		checkf(U_SUCCESS(ICUStatus) && ICUCardinalPluralRules, TEXT("Creating a cardinal plural rules object failed using locale %s. Perhaps this locale has no data."), *LocaleName);
		UEAvailableCardinalPluralForms = ICUPluralRulesToUEValidPluralForms(ICUCardinalPluralRules);
	}
	{
		UErrorCode ICUStatus = U_ZERO_ERROR;
		ICUOrdinalPluralRules = icu::PluralRules::forLocale(ICULocale, UPLURAL_TYPE_ORDINAL, ICUStatus);
		checkf(U_SUCCESS(ICUStatus) && ICUOrdinalPluralRules, TEXT("Creating an ordinal plural rules object failed using locale %s. Perhaps this locale has no data."), *LocaleName);
		UEAvailableOrdinalPluralForms = ICUPluralRulesToUEValidPluralForms(ICUOrdinalPluralRules);
	}
}

FString FICUCultureImplementation::GetDisplayName() const
{
	icu::UnicodeString ICUResult;
	ICULocale.getDisplayName(ICUResult);
	return ICUUtilities::ConvertString(ICUResult);
}

FString FICUCultureImplementation::GetEnglishName() const
{
	icu::UnicodeString ICUResult;
	ICULocale.getDisplayName(icu::Locale("en"), ICUResult);
	return ICUUtilities::ConvertString(ICUResult);
}

int FICUCultureImplementation::GetKeyboardLayoutId() const
{
	return 0;
}

int FICUCultureImplementation::GetLCID() const
{
	return ICULocale.getLCID();
}

FString FICUCultureImplementation::GetCanonicalName(const FString& Name)
{
	auto IsLanguageCode = [](const FString& InCode)
	{
		// Language codes must be 2 or 3 letters, or our special "LEET" language
		return InCode.Len() == 2 
			|| InCode.Len() == 3 
#if ENABLE_LOC_TESTING
			|| InCode == FLeetCulture::StaticGetName()
#endif
			;
	};

	auto IsScriptCode = [](const FString& InCode)
	{
		// Script codes must be 4 letters
		return InCode.Len() == 4;
	};

	auto IsRegionCode = [](const FString& InCode)
	{
		// Region codes must be 2 or 3 letters
		return InCode.Len() == 2 || InCode.Len() == 3;
	};

	auto ConditionLanguageCode = [](FString& InOutCode)
	{
		// Language codes are lowercase
		InOutCode.ToLowerInline();
	};

	auto ConditionScriptCode = [](FString& InOutCode)
	{
		// Script codes are titlecase
		InOutCode.ToLowerInline();
		if (InOutCode.Len() > 0)
		{
			InOutCode[0] = FChar::ToUpper(InOutCode[0]);
		}
	};

	auto ConditionRegionCode = [](FString& InOutCode)
	{
		// Region codes are uppercase
		InOutCode.ToUpperInline();
	};

	auto ConditionVariant = [](FString& InOutVariant)
	{
		// Variants are uppercase
		InOutVariant.ToUpperInline();
	};

	auto ConditionKeywordArgKey = [](FString& InOutKey)
	{
		static const FString ValidKeywords[] = {
			TEXT("calendar"),
			TEXT("collation"),
			TEXT("currency"),
			TEXT("numbers"),
		};

		// Keyword argument keys are lowercase
		InOutKey.ToLowerInline();

		// Only certain argument keys are accepted
		for (const FString& ValidKeyword : ValidKeywords)
		{
			if (InOutKey.Equals(ValidKeyword, ESearchCase::CaseSensitive))
			{
				return;
			}
		}

		// Invalid key - clear it
		InOutKey.Reset();
	};

	enum class ENameTagType : uint8
	{
		Language,
		Script,
		Region,
		Variant,
	};

	struct FNameTag
	{
		FString Str;
		ENameTagType Type;
	};

	struct FCanonizedTagData
	{
		const TCHAR* CanonizedNameTag;
		const TCHAR* KeywordArgKey;
		const TCHAR* KeywordArgValue;
	};

	static const TSortedMap<FString, FCanonizedTagData> CanonizedTagMap = []()
	{
		TSortedMap<FString, FCanonizedTagData> TmpCanonizedTagMap;
		TmpCanonizedTagMap.Add(TEXT(""),				{ TEXT("en-US-POSIX"), nullptr, nullptr });
		TmpCanonizedTagMap.Add(TEXT("c"),				{ TEXT("en-US-POSIX"), nullptr, nullptr });
		TmpCanonizedTagMap.Add(TEXT("posix"),			{ TEXT("en-US-POSIX"), nullptr, nullptr });
		TmpCanonizedTagMap.Add(TEXT("ca-ES-PREEURO"),	{ TEXT("ca-ES"), TEXT("currency"), TEXT("ESP") });
		TmpCanonizedTagMap.Add(TEXT("de-AT-PREEURO"),	{ TEXT("de-AT"), TEXT("currency"), TEXT("ATS") });
		TmpCanonizedTagMap.Add(TEXT("de-DE-PREEURO"),	{ TEXT("de-DE"), TEXT("currency"), TEXT("DEM") });
		TmpCanonizedTagMap.Add(TEXT("de-LU-PREEURO"),	{ TEXT("de-LU"), TEXT("currency"), TEXT("LUF") });
		TmpCanonizedTagMap.Add(TEXT("el-GR-PREEURO"),	{ TEXT("el-GR"), TEXT("currency"), TEXT("GRD") });
		TmpCanonizedTagMap.Add(TEXT("en-BE-PREEURO"),	{ TEXT("en-BE"), TEXT("currency"), TEXT("BEF") });
		TmpCanonizedTagMap.Add(TEXT("en-IE-PREEURO"),	{ TEXT("en-IE"), TEXT("currency"), TEXT("IEP") });
		TmpCanonizedTagMap.Add(TEXT("es-ES-PREEURO"),	{ TEXT("es-ES"), TEXT("currency"), TEXT("ESP") });
		TmpCanonizedTagMap.Add(TEXT("eu-ES-PREEURO"),	{ TEXT("eu-ES"), TEXT("currency"), TEXT("ESP") });
		TmpCanonizedTagMap.Add(TEXT("fi-FI-PREEURO"),	{ TEXT("fi-FI"), TEXT("currency"), TEXT("FIM") });
		TmpCanonizedTagMap.Add(TEXT("fr-BE-PREEURO"),	{ TEXT("fr-BE"), TEXT("currency"), TEXT("BEF") });
		TmpCanonizedTagMap.Add(TEXT("fr-FR-PREEURO"),	{ TEXT("fr-FR"), TEXT("currency"), TEXT("FRF") });
		TmpCanonizedTagMap.Add(TEXT("fr-LU-PREEURO"),	{ TEXT("fr-LU"), TEXT("currency"), TEXT("LUF") });
		TmpCanonizedTagMap.Add(TEXT("ga-IE-PREEURO"),	{ TEXT("ga-IE"), TEXT("currency"), TEXT("IEP") });
		TmpCanonizedTagMap.Add(TEXT("gl-ES-PREEURO"),	{ TEXT("gl-ES"), TEXT("currency"), TEXT("ESP") });
		TmpCanonizedTagMap.Add(TEXT("it-IT-PREEURO"),	{ TEXT("it-IT"), TEXT("currency"), TEXT("ITL") });
		TmpCanonizedTagMap.Add(TEXT("nl-BE-PREEURO"),	{ TEXT("nl-BE"), TEXT("currency"), TEXT("BEF") });
		TmpCanonizedTagMap.Add(TEXT("nl-NL-PREEURO"),	{ TEXT("nl-NL"), TEXT("currency"), TEXT("NLG") });
		TmpCanonizedTagMap.Add(TEXT("pt-PT-PREEURO"),	{ TEXT("pt-PT"), TEXT("currency"), TEXT("PTE") });
		return TmpCanonizedTagMap;
	}();

	static const TSortedMap<FString, FCanonizedTagData> VariantMap = []()
	{
		TSortedMap<FString, FCanonizedTagData> TmpVariantMap;
		TmpVariantMap.Add(TEXT("EURO"), { nullptr, TEXT("currency"), TEXT("EUR") });
		return TmpVariantMap;
	}();

	// Sanitize any nastiness from the culture code
	const FString SanitizedName = ICUUtilities::SanitizeCultureCode(Name);

	// If the name matches a custom culture, then just accept it as-is
	if (FInternationalization::Get().GetCustomCulture(SanitizedName))
	{
		return SanitizedName;
	}

	// These will be populated as the string is processed and are used to re-build the canonized string
	TArray<FNameTag, TInlineAllocator<4>> ParsedNameTags;
	TSortedMap<FString, FString, TInlineAllocator<4>> ParsedKeywords;

	// Parse the string into its component parts
	{
		// 1) Split the string so that the keywords exist in a separate string (both halves need separate processing)
		FString NameTag;
		FString NameKeywords;
		{
			int32 NameKeywordsSplitIndex = INDEX_NONE;
			SanitizedName.FindChar(TEXT('@'), NameKeywordsSplitIndex);

			int32 EncodingSplitIndex = INDEX_NONE;
			SanitizedName.FindChar(TEXT('.'), EncodingSplitIndex);

			// The name tags part of the string ends at either the start of the keywords or encoding (whichever is smaller)
			const int32 NameTagEndIndex = FMath::Min(
				NameKeywordsSplitIndex == INDEX_NONE ? SanitizedName.Len() : NameKeywordsSplitIndex, 
				EncodingSplitIndex == INDEX_NONE ? SanitizedName.Len() : EncodingSplitIndex
				);

			NameTag = SanitizedName.Left(NameTagEndIndex);
			NameTag.ReplaceInline(TEXT("_"), TEXT("-"), ESearchCase::CaseSensitive);

			if (NameKeywordsSplitIndex != INDEX_NONE)
			{
				NameKeywords = SanitizedName.Mid(NameKeywordsSplitIndex + 1);
			}
		}

		// 2) Perform any wholesale substitution (which may also add keywords into ParsedKeywords)
		if (const FCanonizedTagData* CanonizedTagData = CanonizedTagMap.Find(NameTag))
		{
			NameTag = CanonizedTagData->CanonizedNameTag;
			if (CanonizedTagData->KeywordArgKey && CanonizedTagData->KeywordArgValue)
			{
				ParsedKeywords.Add(CanonizedTagData->KeywordArgKey, CanonizedTagData->KeywordArgValue);
			}
		}

		// 3) Split the name tag into its component parts (produces the initial set of ParsedNameTags)
		{
			int32 NameTagStartIndex = 0;
			int32 NameTagEndIndex = 0;
			do
			{
				// Walk to the next breaking point
				for (; NameTagEndIndex < NameTag.Len() && NameTag[NameTagEndIndex] != TEXT('-'); ++NameTagEndIndex) {}

				// Process the tag
				{
					FString NameTagStr = NameTag.Mid(NameTagStartIndex, NameTagEndIndex - NameTagStartIndex);
					const FCanonizedTagData* VariantTagData = nullptr;

					// What kind of tag is this?
					ENameTagType NameTagType = ENameTagType::Variant;
					if (ParsedNameTags.Num() == 0 && IsLanguageCode(NameTagStr))
					{
						// todo: map 3 letter language codes into 2 letter language codes like ICU would?
						NameTagType = ENameTagType::Language;
						ConditionLanguageCode(NameTagStr);
					}
					else if (ParsedNameTags.Num() == 1 && ParsedNameTags.Last().Type == ENameTagType::Language && IsScriptCode(NameTagStr))
					{
						NameTagType = ENameTagType::Script;
						ConditionScriptCode(NameTagStr);
					}
					else if (ParsedNameTags.Num() > 0 && ParsedNameTags.Num() <= 2 && (ParsedNameTags.Last().Type == ENameTagType::Language || ParsedNameTags.Last().Type == ENameTagType::Script) && IsRegionCode(NameTagStr))
					{
						// todo: map 3 letter region codes into 2 letter region codes like ICU would?
						NameTagType = ENameTagType::Region;
						ConditionRegionCode(NameTagStr);
					}
					else
					{
						ConditionVariant(NameTagStr);
						VariantTagData = VariantMap.Find(NameTagStr);
					}

					if (VariantTagData)
					{
						check(VariantTagData->KeywordArgKey && VariantTagData->KeywordArgValue);
						ParsedKeywords.Add(VariantTagData->KeywordArgKey, VariantTagData->KeywordArgValue);
					}
					else if (NameTagStr.Len() > 0)
					{
						ParsedNameTags.Add({ MoveTemp(NameTagStr), NameTagType });
					}
				}

				// Prepare for the next loop
				NameTagStartIndex = NameTagEndIndex + 1;
				NameTagEndIndex = NameTagStartIndex;
			}
			while (NameTagEndIndex < NameTag.Len());
		}

		// 4) Parse the keywords (this may produce both variants into ParsedNameTags, and keywords into ParsedKeywords)
		{
			TArray<FString> NameKeywordArgs;
			NameKeywords.ParseIntoArray(NameKeywordArgs, TEXT(";"));

			for (FString& NameKeywordArg : NameKeywordArgs)
			{
				int32 KeyValueSplitIndex = INDEX_NONE;
				NameKeywordArg.FindChar(TEXT('='), KeyValueSplitIndex);

				if (KeyValueSplitIndex == INDEX_NONE)
				{
					// Single values are treated as variants
					ConditionVariant(NameKeywordArg);
					if (NameKeywordArg.Len() > 0)
					{
						ParsedNameTags.Add({ MoveTemp(NameKeywordArg), ENameTagType::Variant });
					}
				}
				else
				{
					// Key->Value pairs are treated as keywords
					FString NameKeywordArgKey = NameKeywordArg.Left(KeyValueSplitIndex);
					ConditionKeywordArgKey(NameKeywordArgKey);
					FString NameKeywordArgValue = NameKeywordArg.Mid(KeyValueSplitIndex + 1);
					if (NameKeywordArgKey.Len() > 0 && NameKeywordArgValue.Len() > 0)
					{
						if (!ParsedKeywords.Contains(NameKeywordArgKey))
						{
							ParsedKeywords.Add(MoveTemp(NameKeywordArgKey), MoveTemp(NameKeywordArgValue));
						}
					}
				}
			}
		}
	}

	// Re-assemble the string into its canonized form
	FString CanonicalName;
	{
		// Assemble the name tags first
		// These *must* start with a language tag
		if (ParsedNameTags.Num() > 0 && ParsedNameTags[0].Type == ENameTagType::Language)
		{
			for (int32 NameTagIndex = 0; NameTagIndex < ParsedNameTags.Num(); ++NameTagIndex)
			{
				const FNameTag& NameTag = ParsedNameTags[NameTagIndex];

				switch (NameTag.Type)
				{
				case ENameTagType::Language:
					CanonicalName = NameTag.Str;
					break;

				case ENameTagType::Script:
				case ENameTagType::Region:
					CanonicalName += TEXT('-');
					CanonicalName += NameTag.Str;
					break;

				case ENameTagType::Variant:
					// If the previous tag was a language, we need to add an extra hyphen for non-empty variants since ICU would produce a double hyphen in this case
					if (ParsedNameTags.IsValidIndex(NameTagIndex - 1) && ParsedNameTags[NameTagIndex - 1].Type == ENameTagType::Language && !NameTag.Str.IsEmpty())
					{
						CanonicalName += TEXT('-');
					}
					CanonicalName += TEXT('-');
					CanonicalName += NameTag.Str;
					break;

				default:
					break;
				}
			}
		}

		// Now add the keywords
		if (CanonicalName.Len() > 0 && ParsedKeywords.Num() > 0)
		{
			TCHAR NextToken = TEXT('@');
			for (const auto& ParsedKeywordPair : ParsedKeywords)
			{
				CanonicalName += NextToken;
				NextToken = TEXT(';');

				CanonicalName += ParsedKeywordPair.Key;
				CanonicalName += TEXT('=');
				CanonicalName += ParsedKeywordPair.Value;
			}
		}

		// If we canonicalized to an empty string, just fallback to en-US-POSIX
		if (CanonicalName.IsEmpty())
		{
			CanonicalName = TEXT("en-US-POSIX");
		}
	}
	return CanonicalName;
}

FString FICUCultureImplementation::GetName() const
{
	FString Result = ICULocale.getName();
	Result.ReplaceInline(TEXT("_"), TEXT("-"), ESearchCase::IgnoreCase);
	return Result;
}

FString FICUCultureImplementation::GetNativeName() const
{
	icu::UnicodeString ICUResult;
	ICULocale.getDisplayName(ICULocale, ICUResult);
	return ICUUtilities::ConvertString(ICUResult);
}

FString FICUCultureImplementation::GetUnrealLegacyThreeLetterISOLanguageName() const
{
	FString Result( ICULocale.getISO3Language() );

	// Legacy Overrides (INT, JPN, KOR), also for new web localization (CHN)
	// and now for any other languages (FRA, DEU...) for correct redirection of documentation web links
	if (Result == TEXT("eng"))
	{
		Result = TEXT("INT");
	}
	else
	{
		Result = Result.ToUpper();
	}

	return Result;
}

FString FICUCultureImplementation::GetThreeLetterISOLanguageName() const
{
	return ICULocale.getISO3Language();
}

FString FICUCultureImplementation::GetTwoLetterISOLanguageName() const
{
	return ICULocale.getLanguage();
}

FString FICUCultureImplementation::GetNativeLanguage() const
{
	icu::UnicodeString ICUNativeLanguage;
	ICULocale.getDisplayLanguage(ICULocale, ICUNativeLanguage);
	FString NativeLanguage;
	ICUUtilities::ConvertString(ICUNativeLanguage, NativeLanguage);

	icu::UnicodeString ICUNativeScript;
	ICULocale.getDisplayScript(ICULocale, ICUNativeScript);
	FString NativeScript;
	ICUUtilities::ConvertString(ICUNativeScript, NativeScript);

	if ( !NativeScript.IsEmpty() )
	{
		return NativeLanguage + TEXT(" (") + NativeScript + TEXT(")");
	}
	return NativeLanguage;
}

FString FICUCultureImplementation::GetRegion() const
{
	return ICULocale.getCountry();
}

FString FICUCultureImplementation::GetNativeRegion() const
{
	icu::UnicodeString ICUNativeCountry;
	ICULocale.getDisplayCountry(ICULocale, ICUNativeCountry);
	FString NativeCountry;
	ICUUtilities::ConvertString(ICUNativeCountry, NativeCountry);

	icu::UnicodeString ICUNativeVariant;
	ICULocale.getDisplayVariant(ICULocale, ICUNativeVariant);
	FString NativeVariant;
	ICUUtilities::ConvertString(ICUNativeVariant, NativeVariant);

	if ( !NativeVariant.IsEmpty() )
	{
		return NativeCountry + TEXT(", ") + NativeVariant;
	}
	return NativeCountry;
}

FString FICUCultureImplementation::GetScript() const
{
	return ICULocale.getScript();
}

FString FICUCultureImplementation::GetVariant() const
{
	return ICULocale.getVariant();
}

bool FICUCultureImplementation::IsRightToLeft() const
{
#if WITH_ICU_V64
	return ICULocale.isRightToLeft() != 0;
#else
	return false;
#endif
}

TSharedRef<const icu::BreakIterator> FICUCultureImplementation::GetBreakIterator(const EBreakIteratorType Type)
{
	TSharedPtr<const icu::BreakIterator> Result;

	switch (Type)
	{
	case EBreakIteratorType::Grapheme:
		{
			Result = ICUGraphemeBreakIterator.IsValid() ? ICUGraphemeBreakIterator : ( ICUGraphemeBreakIterator = CreateBreakIterator(ICULocale, Type) );
		}
		break;
	case EBreakIteratorType::Word:
		{
			Result = ICUWordBreakIterator.IsValid() ? ICUWordBreakIterator : ( ICUWordBreakIterator = CreateBreakIterator(ICULocale, Type) );
		}
		break;
	case EBreakIteratorType::Line:
		{
			Result = ICULineBreakIterator.IsValid() ? ICULineBreakIterator : ( ICULineBreakIterator = CreateBreakIterator(ICULocale, Type) );
		}
		break;
	case EBreakIteratorType::Sentence:
		{
			Result = ICUSentenceBreakIterator.IsValid() ? ICUSentenceBreakIterator : ( ICUSentenceBreakIterator = CreateBreakIterator(ICULocale, Type) );
		}
		break;
	case EBreakIteratorType::Title:
		{
			Result = ICUTitleBreakIterator.IsValid() ? ICUTitleBreakIterator : ( ICUTitleBreakIterator = CreateBreakIterator(ICULocale, Type) );
		}
		break;
	}

	return Result.ToSharedRef();
}

TSharedRef<const icu::Collator, ESPMode::ThreadSafe> FICUCultureImplementation::GetCollator(const ETextComparisonLevel::Type ComparisonLevel)
{
	if (!ICUCollator.IsValid())
	{
		ICUCollator = CreateCollator( ICULocale );
	}

	UErrorCode ICUStatus = U_ZERO_ERROR;
	const bool bIsDefault = (ComparisonLevel == ETextComparisonLevel::Default);
	const TSharedRef<const icu::Collator, ESPMode::ThreadSafe> DefaultCollator( ICUCollator.ToSharedRef() );
	if(bIsDefault)
	{
		return DefaultCollator;
	}
	else
	{
		const TSharedRef<icu::Collator, ESPMode::ThreadSafe> Collator( DefaultCollator->clone() );
		Collator->setAttribute(UColAttribute::UCOL_STRENGTH, UEToICU(ComparisonLevel), ICUStatus);
		return Collator;
	}
}

TSharedRef<const icu::DateFormat, ESPMode::ThreadSafe> FICUCultureImplementation::GetDateFormatter(const EDateTimeStyle::Type DateStyle, const FString& TimeZone)
{
	if (!ICUDateFormat.IsValid())
	{
		ICUDateFormat = CreateDateFormat( ICULocale );
	}

	const FString SanitizedTimezoneCode = ICUUtilities::SanitizeTimezoneCode(TimeZone);

	icu::UnicodeString InputTimeZoneID;
	ICUUtilities::ConvertString(SanitizedTimezoneCode, InputTimeZoneID, false);

	const TSharedRef<const icu::DateFormat, ESPMode::ThreadSafe> DefaultFormatter( ICUDateFormat.ToSharedRef() );

	bool bIsDefaultTimeZone = SanitizedTimezoneCode.IsEmpty();
	if( !bIsDefaultTimeZone )
	{
		UErrorCode ICUStatus = U_ZERO_ERROR;

		icu::UnicodeString CanonicalInputTimeZoneID;
		icu::TimeZone::getCanonicalID(InputTimeZoneID, CanonicalInputTimeZoneID, ICUStatus);

		icu::UnicodeString DefaultTimeZoneID;
		DefaultFormatter->getTimeZone().getID(DefaultTimeZoneID);

		icu::UnicodeString CanonicalDefaultTimeZoneID;
		icu::TimeZone::getCanonicalID(DefaultTimeZoneID, CanonicalDefaultTimeZoneID, ICUStatus);

		bIsDefaultTimeZone = (CanonicalInputTimeZoneID == CanonicalDefaultTimeZoneID ? true : false);
	}

	const bool bIsDefault = 
		DateStyle == EDateTimeStyle::Default &&
		bIsDefaultTimeZone;

	if(bIsDefault)
	{
		return DefaultFormatter;
	}
	else
	{
		const TSharedRef<icu::DateFormat, ESPMode::ThreadSafe> Formatter( icu::DateFormat::createDateInstance( UEToICU(DateStyle), ICULocale ) );
		Formatter->adoptTimeZone( bIsDefaultTimeZone ? icu::TimeZone::createDefault() : icu::TimeZone::createTimeZone(InputTimeZoneID) );
		return Formatter;
	}
}

TSharedRef<const icu::DateFormat, ESPMode::ThreadSafe> FICUCultureImplementation::GetTimeFormatter(const EDateTimeStyle::Type TimeStyle, const FString& TimeZone)
{
	if (!ICUTimeFormat.IsValid())
	{
		ICUTimeFormat = CreateTimeFormat( ICULocale );
	}

	const FString SanitizedTimezoneCode = ICUUtilities::SanitizeTimezoneCode(TimeZone);

	icu::UnicodeString InputTimeZoneID;
	ICUUtilities::ConvertString(SanitizedTimezoneCode, InputTimeZoneID, false);

	const TSharedRef<const icu::DateFormat, ESPMode::ThreadSafe> DefaultFormatter( ICUTimeFormat.ToSharedRef() );

	bool bIsDefaultTimeZone = SanitizedTimezoneCode.IsEmpty();
	if( !bIsDefaultTimeZone )
	{
		UErrorCode ICUStatus = U_ZERO_ERROR;

		icu::UnicodeString CanonicalInputTimeZoneID;
		icu::TimeZone::getCanonicalID(InputTimeZoneID, CanonicalInputTimeZoneID, ICUStatus);

		icu::UnicodeString DefaultTimeZoneID;
		DefaultFormatter->getTimeZone().getID(DefaultTimeZoneID);

		icu::UnicodeString CanonicalDefaultTimeZoneID;
		icu::TimeZone::getCanonicalID(DefaultTimeZoneID, CanonicalDefaultTimeZoneID, ICUStatus);

		bIsDefaultTimeZone = (CanonicalInputTimeZoneID == CanonicalDefaultTimeZoneID ? true : false);
	}

	const bool bIsDefault = 
		TimeStyle == EDateTimeStyle::Default &&
		bIsDefaultTimeZone;

	if(bIsDefault)
	{
		return DefaultFormatter;
	}
	else
	{
		const TSharedRef<icu::DateFormat, ESPMode::ThreadSafe> Formatter( icu::DateFormat::createTimeInstance( UEToICU(TimeStyle), ICULocale ) );
		Formatter->adoptTimeZone( bIsDefaultTimeZone ? icu::TimeZone::createDefault() : icu::TimeZone::createTimeZone(InputTimeZoneID) );
		return Formatter;
	}
}

TSharedRef<const icu::DateFormat, ESPMode::ThreadSafe> FICUCultureImplementation::GetDateTimeFormatter(const EDateTimeStyle::Type DateStyle, const EDateTimeStyle::Type TimeStyle, const FString& TimeZone)
{
	if (!ICUDateTimeFormat.IsValid())
	{
		ICUDateTimeFormat = CreateDateTimeFormat( ICULocale );
	}

	const FString SanitizedTimezoneCode = ICUUtilities::SanitizeTimezoneCode(TimeZone);

	icu::UnicodeString InputTimeZoneID;
	ICUUtilities::ConvertString(SanitizedTimezoneCode, InputTimeZoneID, false);

	const TSharedRef<const icu::DateFormat, ESPMode::ThreadSafe> DefaultFormatter( ICUDateTimeFormat.ToSharedRef() );

	bool bIsDefaultTimeZone = SanitizedTimezoneCode.IsEmpty();
	if( !bIsDefaultTimeZone )
	{
		UErrorCode ICUStatus = U_ZERO_ERROR;

		icu::UnicodeString CanonicalInputTimeZoneID;
		icu::TimeZone::getCanonicalID(InputTimeZoneID, CanonicalInputTimeZoneID, ICUStatus);

		icu::UnicodeString DefaultTimeZoneID;
		DefaultFormatter->getTimeZone().getID(DefaultTimeZoneID);

		icu::UnicodeString CanonicalDefaultTimeZoneID;
		icu::TimeZone::getCanonicalID(DefaultTimeZoneID, CanonicalDefaultTimeZoneID, ICUStatus);

		bIsDefaultTimeZone = (CanonicalInputTimeZoneID == CanonicalDefaultTimeZoneID ? true : false);
	}

	const bool bIsDefault = 
		DateStyle == EDateTimeStyle::Default &&
		TimeStyle == EDateTimeStyle::Default &&
		bIsDefaultTimeZone;

	if(bIsDefault)
	{
		return DefaultFormatter;
	}
	else
	{
		const TSharedRef<icu::DateFormat, ESPMode::ThreadSafe> Formatter( icu::DateFormat::createDateTimeInstance( UEToICU(DateStyle), UEToICU(TimeStyle), ICULocale ) );
		Formatter->adoptTimeZone( bIsDefaultTimeZone ? icu::TimeZone::createDefault() : icu::TimeZone::createTimeZone(InputTimeZoneID) );
		return Formatter;
	}
}

namespace
{

FDecimalNumberFormattingRules ExtractNumberFormattingRulesFromICUDecimalFormatter(icu::Locale& InICULocale, icu::DecimalFormat& InICUDecimalFormat)
{
	FDecimalNumberFormattingRules NewUEDecimalNumberFormattingRules;

	// Extract the default formatting options before we mess around with the formatter object settings
	NewUEDecimalNumberFormattingRules.CultureDefaultFormattingOptions
		.SetUseGrouping(InICUDecimalFormat.isGroupingUsed() != 0)
		.SetRoundingMode(ICUToUE(InICUDecimalFormat.getRoundingMode()))
		.SetMinimumIntegralDigits(InICUDecimalFormat.getMinimumIntegerDigits())
		.SetMaximumIntegralDigits(InICUDecimalFormat.getMaximumIntegerDigits())
		.SetMinimumFractionalDigits(InICUDecimalFormat.getMinimumFractionDigits())
		.SetMaximumFractionalDigits(InICUDecimalFormat.getMaximumFractionDigits());

	// We force grouping to be on, even if a culture doesn't use it by default, so that we can extract meaningful grouping information
	// This allows us to use the correct groupings if we should ever force grouping for a number, rather than use the culture default
	InICUDecimalFormat.setGroupingUsed(true);

	auto ExtractFormattingSymbolAsCharacter = [&](icu::DecimalFormatSymbols::ENumberFormatSymbol InSymbolToExtract, const TCHAR InFallbackChar) -> TCHAR
	{
		const icu::UnicodeString& ICUSymbolString = InICUDecimalFormat.getDecimalFormatSymbols()->getConstSymbol(InSymbolToExtract);
		check(ICUSymbolString.length() <= 2);

		// Some cultures use characters outside of the BMP which present as a surrogate pair on platforms using UTF-16 TCHAR
		// We need to update this code to use FString or UTF32CHAR for these symbols (see UE-83143), but for now we just use the fallback if we find a surrogate pair
		return ICUSymbolString.length() == 1
			? static_cast<TCHAR>(ICUSymbolString.charAt(0))
			: InFallbackChar;
	};

	icu::UnicodeString ScratchICUString;

	// Extract the rules from the decimal formatter
	NewUEDecimalNumberFormattingRules.NaNString						= ICUUtilities::ConvertString(InICUDecimalFormat.getDecimalFormatSymbols()->getConstSymbol(icu::DecimalFormatSymbols::kNaNSymbol));
	NewUEDecimalNumberFormattingRules.NegativePrefixString			= ICUUtilities::ConvertString(InICUDecimalFormat.getNegativePrefix(ScratchICUString));
	NewUEDecimalNumberFormattingRules.NegativeSuffixString			= ICUUtilities::ConvertString(InICUDecimalFormat.getNegativeSuffix(ScratchICUString));
	NewUEDecimalNumberFormattingRules.PositivePrefixString			= ICUUtilities::ConvertString(InICUDecimalFormat.getPositivePrefix(ScratchICUString));
	NewUEDecimalNumberFormattingRules.PositiveSuffixString			= ICUUtilities::ConvertString(InICUDecimalFormat.getPositiveSuffix(ScratchICUString));
	NewUEDecimalNumberFormattingRules.PlusString					= ICUUtilities::ConvertString(InICUDecimalFormat.getDecimalFormatSymbols()->getConstSymbol(icu::DecimalFormatSymbols::kPlusSignSymbol));
	NewUEDecimalNumberFormattingRules.MinusString					= ICUUtilities::ConvertString(InICUDecimalFormat.getDecimalFormatSymbols()->getConstSymbol(icu::DecimalFormatSymbols::kMinusSignSymbol));
	NewUEDecimalNumberFormattingRules.GroupingSeparatorCharacter	= ExtractFormattingSymbolAsCharacter(icu::DecimalFormatSymbols::kGroupingSeparatorSymbol, TEXT(','));
	NewUEDecimalNumberFormattingRules.DecimalSeparatorCharacter		= ExtractFormattingSymbolAsCharacter(icu::DecimalFormatSymbols::kDecimalSeparatorSymbol,  TEXT('.'));
	NewUEDecimalNumberFormattingRules.PrimaryGroupingSize			= static_cast<uint8>(InICUDecimalFormat.getGroupingSize());
	NewUEDecimalNumberFormattingRules.SecondaryGroupingSize			= (InICUDecimalFormat.getSecondaryGroupingSize() < 1) 
																		? NewUEDecimalNumberFormattingRules.PrimaryGroupingSize 
																		: static_cast<uint8>(InICUDecimalFormat.getSecondaryGroupingSize());

	NewUEDecimalNumberFormattingRules.DigitCharacters[0]			= ExtractFormattingSymbolAsCharacter(icu::DecimalFormatSymbols::kZeroDigitSymbol,	TEXT('0'));
	NewUEDecimalNumberFormattingRules.DigitCharacters[1]			= ExtractFormattingSymbolAsCharacter(icu::DecimalFormatSymbols::kOneDigitSymbol,	TEXT('1'));
	NewUEDecimalNumberFormattingRules.DigitCharacters[2]			= ExtractFormattingSymbolAsCharacter(icu::DecimalFormatSymbols::kTwoDigitSymbol,	TEXT('2'));
	NewUEDecimalNumberFormattingRules.DigitCharacters[3]			= ExtractFormattingSymbolAsCharacter(icu::DecimalFormatSymbols::kThreeDigitSymbol,	TEXT('3'));
	NewUEDecimalNumberFormattingRules.DigitCharacters[4]			= ExtractFormattingSymbolAsCharacter(icu::DecimalFormatSymbols::kFourDigitSymbol,	TEXT('4'));
	NewUEDecimalNumberFormattingRules.DigitCharacters[5]			= ExtractFormattingSymbolAsCharacter(icu::DecimalFormatSymbols::kFiveDigitSymbol,	TEXT('5'));
	NewUEDecimalNumberFormattingRules.DigitCharacters[6]			= ExtractFormattingSymbolAsCharacter(icu::DecimalFormatSymbols::kSixDigitSymbol,	TEXT('6'));
	NewUEDecimalNumberFormattingRules.DigitCharacters[7]			= ExtractFormattingSymbolAsCharacter(icu::DecimalFormatSymbols::kSevenDigitSymbol,	TEXT('7'));
	NewUEDecimalNumberFormattingRules.DigitCharacters[8]			= ExtractFormattingSymbolAsCharacter(icu::DecimalFormatSymbols::kEightDigitSymbol,	TEXT('8'));
	NewUEDecimalNumberFormattingRules.DigitCharacters[9]			= ExtractFormattingSymbolAsCharacter(icu::DecimalFormatSymbols::kNineDigitSymbol,	TEXT('9'));

	// The CLDR uses a dot as the group separator for Spanish, however the RAE favor using a space: https://www.rae.es/dpd/n%C3%BAmeros
	if (FCStringAnsi::Strcmp(InICULocale.getLanguage(), "es") == 0 && CVarSpanishUsesRAENumberFormat.AsVariable()->GetInt())
	{
		NewUEDecimalNumberFormattingRules.GroupingSeparatorCharacter = TEXT('\u00A0'); // No-Break Space
	}

	return NewUEDecimalNumberFormattingRules;
}

} // anonymous namespace

const FDecimalNumberFormattingRules& FICUCultureImplementation::GetDecimalNumberFormattingRules()
{
	if (UEDecimalNumberFormattingRules.IsValid())
	{
		return *UEDecimalNumberFormattingRules;
	}

	// Create a culture decimal formatter
	TSharedPtr<icu::DecimalFormat> DecimalFormatterForCulture;
	{
		UErrorCode ICUStatus = U_ZERO_ERROR;
		DecimalFormatterForCulture = MakeShareable(static_cast<icu::DecimalFormat*>(icu::NumberFormat::createInstance(ICULocale, ICUStatus)));
		checkf(DecimalFormatterForCulture.IsValid(), TEXT("Creating a decimal format object failed using locale %s. Perhaps this locale has no data."), StringCast<TCHAR>(ICULocale.getName()).Get());
	}

	const FDecimalNumberFormattingRules NewUEDecimalNumberFormattingRules = ExtractNumberFormattingRulesFromICUDecimalFormatter(ICULocale, *DecimalFormatterForCulture);

	// Check the pointer again in case another thread beat us to it
	{
		FScopeLock PtrLock(&UEDecimalNumberFormattingRulesCS);

		if (!UEDecimalNumberFormattingRules.IsValid())
		{
			UEDecimalNumberFormattingRules = MakeShareable(new FDecimalNumberFormattingRules(NewUEDecimalNumberFormattingRules));
		}
	}

	return *UEDecimalNumberFormattingRules;
}

const FDecimalNumberFormattingRules& FICUCultureImplementation::GetPercentFormattingRules()
{
	if (UEPercentFormattingRules.IsValid())
	{
		return *UEPercentFormattingRules;
	}

	// Create a culture percent formatter (doesn't call CreatePercentFormat as we need a mutable instance)
	TSharedPtr<icu::DecimalFormat> PercentFormatterForCulture;
	{
		UErrorCode ICUStatus = U_ZERO_ERROR;
		PercentFormatterForCulture = MakeShareable(static_cast<icu::DecimalFormat*>(icu::NumberFormat::createPercentInstance(ICULocale, ICUStatus)));
		checkf(PercentFormatterForCulture.IsValid(), TEXT("Creating a percent format object failed using locale %s. Perhaps this locale has no data."), StringCast<TCHAR>(ICULocale.getName()).Get());
	}

	const FDecimalNumberFormattingRules NewUEPercentFormattingRules = ExtractNumberFormattingRulesFromICUDecimalFormatter(ICULocale, *PercentFormatterForCulture);

	// Check the pointer again in case another thread beat us to it
	{
		FScopeLock PtrLock(&UEPercentFormattingRulesCS);

		if (!UEPercentFormattingRules.IsValid())
		{
			UEPercentFormattingRules = MakeShareable(new FDecimalNumberFormattingRules(NewUEPercentFormattingRules));
		}
	}

	return *UEPercentFormattingRules;
}

const FDecimalNumberFormattingRules& FICUCultureImplementation::GetCurrencyFormattingRules(const FString& InCurrencyCode)
{
	const FString SanitizedCurrencyCode = ICUUtilities::SanitizeCurrencyCode(InCurrencyCode);
	const bool bUseDefaultFormattingRules = SanitizedCurrencyCode.IsEmpty();

	if (bUseDefaultFormattingRules)
	{
		if (UECurrencyFormattingRules.IsValid())
		{
			return *UECurrencyFormattingRules;
		}
	}
	else
	{
		FScopeLock MapLock(&UEAlternateCurrencyFormattingRulesCS);

		auto FoundUEAlternateCurrencyFormattingRules = UEAlternateCurrencyFormattingRules.FindRef(SanitizedCurrencyCode);
		if (FoundUEAlternateCurrencyFormattingRules.IsValid())
		{
			return *FoundUEAlternateCurrencyFormattingRules;
		}
	}

	// Create a currency specific formatter (doesn't call CreateCurrencyFormat as we need a mutable instance)
	TSharedPtr<icu::DecimalFormat> CurrencyFormatterForCulture;
	{
		UErrorCode ICUStatus = U_ZERO_ERROR;
		CurrencyFormatterForCulture = MakeShareable(static_cast<icu::DecimalFormat*>(icu::NumberFormat::createCurrencyInstance(ICULocale, ICUStatus)));
		checkf(CurrencyFormatterForCulture.IsValid(), TEXT("Creating a currency format object failed using locale %s. Perhaps this locale has no data."), StringCast<TCHAR>(ICULocale.getName()).Get());
	}

	if (!bUseDefaultFormattingRules)
	{
		// Set the custom currency before we extract the data from the formatter
		icu::UnicodeString ICUCurrencyCode = ICUUtilities::ConvertString(SanitizedCurrencyCode);
		CurrencyFormatterForCulture->setCurrency(ICUCurrencyCode.getBuffer());
	}

	const FDecimalNumberFormattingRules NewUECurrencyFormattingRules = ExtractNumberFormattingRulesFromICUDecimalFormatter(ICULocale, *CurrencyFormatterForCulture);

	if (bUseDefaultFormattingRules)
	{
		// Check the pointer again in case another thread beat us to it
		{
			FScopeLock PtrLock(&UECurrencyFormattingRulesCS);

			if (!UECurrencyFormattingRules.IsValid())
			{
				UECurrencyFormattingRules = MakeShareable(new FDecimalNumberFormattingRules(NewUECurrencyFormattingRules));
			}
		}

		return *UECurrencyFormattingRules;
	}
	else
	{
		FScopeLock MapLock(&UEAlternateCurrencyFormattingRulesCS);

		// Find again in case another thread beat us to it
		auto FoundUEAlternateCurrencyFormattingRules = UEAlternateCurrencyFormattingRules.FindRef(SanitizedCurrencyCode);
		if (FoundUEAlternateCurrencyFormattingRules.IsValid())
		{
			return *FoundUEAlternateCurrencyFormattingRules;
		}

		FoundUEAlternateCurrencyFormattingRules = MakeShareable(new FDecimalNumberFormattingRules(NewUECurrencyFormattingRules));
		UEAlternateCurrencyFormattingRules.Add(SanitizedCurrencyCode, FoundUEAlternateCurrencyFormattingRules);
		return *FoundUEAlternateCurrencyFormattingRules;
	}
}

ETextPluralForm FICUCultureImplementation::GetPluralForm(int32 Val, const ETextPluralType PluralType) const
{
	checkf(Val >= 0, TEXT("GetPluralFormImpl requires a positive value"));

	const icu::PluralRules* ICUPluralRules = (PluralType == ETextPluralType::Cardinal) ? ICUCardinalPluralRules : ICUOrdinalPluralRules;
	const icu::UnicodeString ICUPluralFormTag = ICUPluralRules->select(Val);

	return ICUPluralFormToUE(ICUPluralFormTag);
}

ETextPluralForm FICUCultureImplementation::GetPluralForm(double Val, const ETextPluralType PluralType) const
{
	checkf(!FMath::IsNegativeDouble(Val), TEXT("GetPluralFormImpl requires a positive value"));

	const icu::PluralRules* ICUPluralRules = (PluralType == ETextPluralType::Cardinal) ? ICUCardinalPluralRules : ICUOrdinalPluralRules;
	const icu::UnicodeString ICUPluralFormTag = ICUPluralRules->select(Val);

	return ICUPluralFormToUE(ICUPluralFormTag);
}

const TArray<ETextPluralForm>& FICUCultureImplementation::GetValidPluralForms(const ETextPluralType PluralType) const
{
	return (PluralType == ETextPluralType::Cardinal) ? UEAvailableCardinalPluralForms : UEAvailableOrdinalPluralForms;
}

#endif
