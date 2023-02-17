// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/StructuredArchive.h"
#include "Containers/Set.h"
#include "Containers/UnrealString.h"

#if WITH_TEXT_ARCHIVE_SUPPORT

//////////// FStructuredArchive::FContainer ////////////

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS

struct FStructuredArchive::FContainer
{
	int  Index                   = 0;
	int  Count                   = 0;
	bool bAttributedValueWritten = false;

#if DO_STRUCTURED_ARCHIVE_UNIQUE_FIELD_NAME_CHECKS
	TSet<FString> KeyNames;
#endif

	explicit FContainer(int InCount)
		: Count(InCount)
	{
	}
};
#endif

FStructuredArchiveChildReader::FStructuredArchiveChildReader(FStructuredArchiveSlot InSlot)
	: OwnedFormatter(nullptr)
	, Archive(nullptr)
{
	FStructuredArchiveFormatter* Formatter = &InSlot.Ar.Formatter;
	if (InSlot.GetUnderlyingArchive().IsTextFormat())
	{
		Formatter = OwnedFormatter = InSlot.Ar.Formatter.CreateSubtreeReader();
	}

	Archive = new FStructuredArchive(*Formatter);
	Root.Emplace(Archive->Open());
	InSlot.EnterRecord();
}

FStructuredArchiveChildReader::~FStructuredArchiveChildReader()
{
	Root.Reset();
	Archive->Close();
	delete Archive;
	Archive = nullptr;

	// If this is a text archive, we'll have created a subtree reader that our contained archive is using as 
	// its formatter. We need to clean it up now.
	if (OwnedFormatter)
	{
		delete OwnedFormatter;
		OwnedFormatter = nullptr;
	}
}

//////////// FStructuredArchive ////////////

FStructuredArchive::FStructuredArchive(FArchiveFormatterType& InFormatter)
	: Formatter(InFormatter)
#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
	, bRequiresStructuralMetadata(true)
#else
	, bRequiresStructuralMetadata(InFormatter.HasDocumentTree())
#endif
{
	CurrentScope.Reserve(32);
#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
	CurrentContainer.Reserve(32);
#endif
}

FStructuredArchive::~FStructuredArchive()
{
	Close();
}

FStructuredArchiveSlot FStructuredArchive::Open()
{
	check(CurrentScope.Num() == 0);
	check(!RootElementId.IsValid());
	check(!CurrentSlotElementId.IsValid());

	RootElementId = ElementIdGenerator.Generate();
	CurrentScope.Emplace(RootElementId, UE4StructuredArchive_Private::EElementType::Root);

	CurrentSlotElementId = ElementIdGenerator.Generate();

	return FStructuredArchiveSlot(*this, 0, CurrentSlotElementId);
}

void FStructuredArchive::Close()
{
	SetScope(UE4StructuredArchive_Private::FSlotPosition(0, RootElementId));
}

void FStructuredArchive::EnterSlot(UE4StructuredArchive_Private::FSlotPosition Slot, bool bEnteringAttributedValue)
{
	int32                                    ParentDepth = Slot.Depth;
	UE4StructuredArchive_Private::FElementId ElementId   = Slot.ElementId;

	// If the slot being entered has attributes, enter the value slot first.
	if (ParentDepth + 1 < CurrentScope.Num() && CurrentScope[ParentDepth + 1].Id == ElementId && CurrentScope[ParentDepth + 1].Type == UE4StructuredArchive_Private::EElementType::AttributedValue)
	{
#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
		checkf(!CurrentSlotElementId.IsValid() && !CurrentContainer.Top()->bAttributedValueWritten, TEXT("Attempt to serialize data into an invalid slot"));
		CurrentContainer.Top()->bAttributedValueWritten = true;
#else
		checkf(!CurrentSlotElementId.IsValid(), TEXT("Attempt to serialize data into an invalid slot"));
#endif

		SetScope(UE4StructuredArchive_Private::FSlotPosition(ParentDepth + 1, ElementId));
		Formatter.EnterAttributedValueValue();
	}
	else if (!bEnteringAttributedValue && Formatter.TryEnterAttributedValueValue())
	{
		int32 NewDepth = EnterSlotAsType(UE4StructuredArchive_Private::FSlotPosition(ParentDepth, ElementId), UE4StructuredArchive_Private::EElementType::AttributedValue);
		check(NewDepth == ParentDepth + 1);
		UE4StructuredArchive_Private::FElementId AttributedValueId = CurrentScope[NewDepth].Id;
		SetScope(UE4StructuredArchive_Private::FSlotPosition(NewDepth, AttributedValueId));
#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
		CurrentContainer.Emplace(0);
#endif
	}
	else
	{
		checkf(ElementId == CurrentSlotElementId, TEXT("Attempt to serialize data into an invalid slot"));
		CurrentSlotElementId.Reset();
	}

	CurrentEnteringAttributeState = UE4StructuredArchive_Private::EEnteringAttributeState::NotEnteringAttribute;
}

int32 FStructuredArchive::EnterSlotAsType(UE4StructuredArchive_Private::FSlotPosition Slot, UE4StructuredArchive_Private::EElementType ElementType)
{
	EnterSlot(Slot, ElementType == UE4StructuredArchive_Private::EElementType::AttributedValue);

	int32 NewSlotDepth = Slot.Depth + 1;

	// If we're entering the value of an attributed slot, we need to return a depth one higher than usual, because we're
	// inside an attributed value container.
	//
	// We don't need to do adjust for attributes, because entering the attribute slot will bump the depth anyway.
	if (NewSlotDepth < CurrentScope.Num() &&
		CurrentScope[NewSlotDepth].Type == UE4StructuredArchive_Private::EElementType::AttributedValue &&
		CurrentEnteringAttributeState == UE4StructuredArchive_Private::EEnteringAttributeState::NotEnteringAttribute)
	{
		++NewSlotDepth;
	}

	CurrentScope.Emplace(Slot.ElementId, ElementType);
	return NewSlotDepth;
}

void FStructuredArchive::LeaveSlot()
{
	if (bRequiresStructuralMetadata)
	{
		switch (CurrentScope.Top().Type)
		{
		case UE4StructuredArchive_Private::EElementType::Record:
			Formatter.LeaveField();
			break;
		case UE4StructuredArchive_Private::EElementType::Array:
			Formatter.LeaveArrayElement();
#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
			CurrentContainer.Top()->Index++;
#endif
			break;
		case UE4StructuredArchive_Private::EElementType::Stream:
			Formatter.LeaveStreamElement();
			break;
		case UE4StructuredArchive_Private::EElementType::Map:
			Formatter.LeaveMapElement();
#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
			CurrentContainer.Top()->Index++;
#endif
			break;
		case UE4StructuredArchive_Private::EElementType::AttributedValue:
			Formatter.LeaveAttribute();
			break;
		}
	}
}

void FStructuredArchive::SetScope(UE4StructuredArchive_Private::FSlotPosition Slot)
{
	// Make sure the scope is valid
	checkf(Slot.Depth < CurrentScope.Num() && CurrentScope[Slot.Depth].Id == Slot.ElementId, TEXT("Invalid scope for writing to archive"));
	checkf(!CurrentSlotElementId.IsValid() || GetUnderlyingArchive().IsLoading(), TEXT("Cannot change scope until having written a value to the current slot"));

	// Roll back to the correct scope
	if (bRequiresStructuralMetadata)
	{
		for (int32 CurrentDepth = CurrentScope.Num() - 1; CurrentDepth > Slot.Depth; CurrentDepth--)
		{
			// Leave the current element
			const FElement& Element = CurrentScope[CurrentDepth];
			switch (Element.Type)
			{
			case UE4StructuredArchive_Private::EElementType::Record:
				Formatter.LeaveRecord();
#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
				CurrentContainer.Pop(false);
#endif
				break;
			case UE4StructuredArchive_Private::EElementType::Array:
#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
				checkf(GetUnderlyingArchive().IsLoading() || CurrentContainer.Top()->Index == CurrentContainer.Top()->Count, TEXT("Incorrect number of elements serialized in array"));
#endif
				Formatter.LeaveArray();
#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
				CurrentContainer.Pop(false);
#endif
				break;
			case UE4StructuredArchive_Private::EElementType::Stream:
				Formatter.LeaveStream();
				break;
			case UE4StructuredArchive_Private::EElementType::Map:
#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
				checkf(CurrentContainer.Top()->Index == CurrentContainer.Top()->Count, TEXT("Incorrect number of elements serialized in map"));
#endif
				Formatter.LeaveMap();
#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
				CurrentContainer.Pop(false);
#endif
				break;
			case UE4StructuredArchive_Private::EElementType::AttributedValue:
				Formatter.LeaveAttributedValue();
#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
				CurrentContainer.Pop(false);
#endif
				break;
			}

			// Remove the element from the stack
			CurrentScope.RemoveAt(CurrentDepth, 1, false);

			// Leave the slot containing it
			LeaveSlot();
		}
	}
	else
	{
		// Remove all the top elements from the stack
		CurrentScope.RemoveAt(Slot.Depth + 1, CurrentScope.Num() - (Slot.Depth + 1));
	}
}

//////////// FStructuredArchiveSlot ////////////

FStructuredArchiveRecord FStructuredArchiveSlot::EnterRecord()
{
	int32 NewDepth = Ar.EnterSlotAsType(*this, UE4StructuredArchive_Private::EElementType::Record);

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
	Ar.CurrentContainer.Emplace(0);
#endif

	Ar.Formatter.EnterRecord();

	return FStructuredArchiveRecord(Ar, NewDepth, ElementId);
}

FStructuredArchiveRecord FStructuredArchiveSlot::EnterRecord_TextOnly(TArray<FString>& OutFieldNames)
{
	int32 NewDepth = Ar.EnterSlotAsType(*this, UE4StructuredArchive_Private::EElementType::Record);

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
	Ar.CurrentContainer.Emplace(0);
#endif

	Ar.Formatter.EnterRecord_TextOnly(OutFieldNames);

	return FStructuredArchiveRecord(Ar, NewDepth, ElementId);
}

FStructuredArchiveArray FStructuredArchiveSlot::EnterArray(int32& Num)
{
	int32 NewDepth = Ar.EnterSlotAsType(*this, UE4StructuredArchive_Private::EElementType::Array);

	Ar.Formatter.EnterArray(Num);

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
	Ar.CurrentContainer.Emplace(Num);
#endif

	return FStructuredArchiveArray(Ar, NewDepth, ElementId);
}

FStructuredArchiveStream FStructuredArchiveSlot::EnterStream()
{
	int32 NewDepth = Ar.EnterSlotAsType(*this, UE4StructuredArchive_Private::EElementType::Stream);

	Ar.Formatter.EnterStream();

	return FStructuredArchiveStream(Ar, NewDepth, ElementId);
}

FStructuredArchiveStream FStructuredArchiveSlot::EnterStream_TextOnly(int32& OutNumElements)
{
	int32 NewDepth = Ar.EnterSlotAsType(*this, UE4StructuredArchive_Private::EElementType::Stream);

	Ar.Formatter.EnterStream_TextOnly(OutNumElements);

	return FStructuredArchiveStream(Ar, NewDepth, ElementId);
}

FStructuredArchiveMap FStructuredArchiveSlot::EnterMap(int32& Num)
{
	int32 NewDepth = Ar.EnterSlotAsType(*this, UE4StructuredArchive_Private::EElementType::Map);

	Ar.Formatter.EnterMap(Num);

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
	Ar.CurrentContainer.Emplace(Num);
#endif

	return FStructuredArchiveMap(Ar, NewDepth, ElementId);
}

FStructuredArchiveSlot FStructuredArchiveSlot::EnterAttribute(FArchiveFieldName AttributeName)
{
	check(Ar.CurrentScope.Num() > 0);

	int32 NewDepth = Depth + 1;
	if (NewDepth >= Ar.CurrentScope.Num() || Ar.CurrentScope[NewDepth].Id != ElementId || Ar.CurrentScope[NewDepth].Type != UE4StructuredArchive_Private::EElementType::AttributedValue)
	{
		int32 NewDepthCheck = Ar.EnterSlotAsType(*this, UE4StructuredArchive_Private::EElementType::AttributedValue);
		checkSlow(NewDepth == NewDepthCheck);

		Ar.Formatter.EnterAttributedValue();

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
		Ar.CurrentContainer.Emplace(0);
#endif
	}

	Ar.CurrentEnteringAttributeState = UE4StructuredArchive_Private::EEnteringAttributeState::NotEnteringAttribute;

	UE4StructuredArchive_Private::FElementId AttributedValueId = Ar.CurrentScope[NewDepth].Id;

	Ar.SetScope(UE4StructuredArchive_Private::FSlotPosition(NewDepth, AttributedValueId));

	Ar.CurrentSlotElementId = Ar.ElementIdGenerator.Generate();

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
#if DO_STRUCTURED_ARCHIVE_UNIQUE_FIELD_NAME_CHECKS
	if (!Ar.GetUnderlyingArchive().IsLoading())
	{
		FContainer& Container = *Ar.CurrentContainer.Top();
		checkf(!Container.KeyNames.Contains(Name.Name), TEXT("Multiple attributes called '%s' serialized into attributed value"), AttributeName.Name);
		Container.KeyNames.Add(Name.Name);
	}
#endif
#endif

	Ar.Formatter.EnterAttribute(AttributeName);

	return FStructuredArchiveSlot(Ar, NewDepth, Ar.CurrentSlotElementId);
}

TOptional<FStructuredArchiveSlot> FStructuredArchiveSlot::TryEnterAttribute(FArchiveFieldName AttributeName, bool bEnterWhenWriting)
{
	check(Ar.CurrentScope.Num() > 0);

	int32 NewDepth = Depth + 1;
	if (NewDepth >= Ar.CurrentScope.Num() || Ar.CurrentScope[NewDepth].Id != ElementId || Ar.CurrentScope[NewDepth].Type != UE4StructuredArchive_Private::EElementType::AttributedValue)
	{
		int32 NewDepthCheck = Ar.EnterSlotAsType(*this, UE4StructuredArchive_Private::EElementType::AttributedValue);
		checkSlow(NewDepth == NewDepthCheck);

		Ar.Formatter.EnterAttributedValue();

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
		Ar.CurrentContainer.Emplace(0);
#endif
	}

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
#if DO_STRUCTURED_ARCHIVE_UNIQUE_FIELD_NAME_CHECKS
	if (!Ar.GetUnderlyingArchive().IsLoading())
	{
		FContainer& Container = *Ar.CurrentContainer.Top();
		checkf(!Container.KeyNames.Contains(Name.Name), TEXT("Multiple attributes called '%s' serialized into attributed value"), AttributeName.Name);
		Container.KeyNames.Add(Name.Name);
	}
#endif
#endif

	UE4StructuredArchive_Private::FElementId AttributedValueId = Ar.CurrentScope[NewDepth].Id;

	Ar.SetScope(UE4StructuredArchive_Private::FSlotPosition(NewDepth, AttributedValueId));

	if (Ar.Formatter.TryEnterAttribute(AttributeName, bEnterWhenWriting))
	{
		Ar.CurrentEnteringAttributeState = UE4StructuredArchive_Private::EEnteringAttributeState::NotEnteringAttribute;

		Ar.CurrentSlotElementId = Ar.ElementIdGenerator.Generate();

		return FStructuredArchiveSlot(Ar, NewDepth, Ar.CurrentSlotElementId);
	}
	else
	{
		return {};
	}
}

void FStructuredArchiveSlot::operator<< (uint8& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (uint16& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (uint32& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (uint64& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (int8& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (int16& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (int32& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (int64& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (float& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (double& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (bool& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (FString& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (FName& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (UObject*& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (FText& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (FWeakObjectPtr& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (FLazyObjectPtr& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (FSoftObjectPtr& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::operator<< (FSoftObjectPath& Value)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Value);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::Serialize(TArray<uint8>& Data)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Data);
	Ar.LeaveSlot();
}

void FStructuredArchiveSlot::Serialize(void* Data, uint64 DataSize)
{
	Ar.EnterSlot(*this);
	Ar.Formatter.Serialize(Data, DataSize);
	Ar.LeaveSlot();
}

//////////// FStructuredArchiveRecord ////////////

FStructuredArchiveSlot FStructuredArchiveRecord::EnterField(FArchiveFieldName Name)
{
	Ar.SetScope(*this);

	Ar.CurrentSlotElementId = Ar.ElementIdGenerator.Generate();

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
#if DO_STRUCTURED_ARCHIVE_UNIQUE_FIELD_NAME_CHECKS
	if (!Ar.GetUnderlyingArchive().IsLoading())
	{
		FContainer& Container = *Ar.CurrentContainer.Top();
		checkf(!Container.KeyNames.Contains(Name.Name), TEXT("Multiple keys called '%s' serialized into record"), Name.Name);
		Container.KeyNames.Add(Name.Name);
	}
#endif
#endif

	Ar.Formatter.EnterField(Name);

	return FStructuredArchiveSlot(Ar, Depth, Ar.CurrentSlotElementId);
}

FStructuredArchiveRecord FStructuredArchiveRecord::EnterRecord(FArchiveFieldName Name)
{
	return EnterField(Name).EnterRecord();
}

FStructuredArchiveRecord FStructuredArchiveRecord::EnterRecord_TextOnly(FArchiveFieldName Name, TArray<FString>& OutFieldNames)
{
	return EnterField(Name).EnterRecord_TextOnly(OutFieldNames);
}

FStructuredArchiveArray FStructuredArchiveRecord::EnterArray(FArchiveFieldName Name, int32& Num)
{
	return EnterField(Name).EnterArray(Num);
}

FStructuredArchiveStream FStructuredArchiveRecord::EnterStream(FArchiveFieldName Name)
{
	return EnterField(Name).EnterStream();
}

FStructuredArchiveStream FStructuredArchiveRecord::EnterStream_TextOnly(FArchiveFieldName Name, int32& OutNumElements)
{
	return EnterField(Name).EnterStream_TextOnly(OutNumElements);
}

FStructuredArchiveMap FStructuredArchiveRecord::EnterMap(FArchiveFieldName Name, int32& Num)
{
	return EnterField(Name).EnterMap(Num);
}

TOptional<FStructuredArchiveSlot> FStructuredArchiveRecord::TryEnterField(FArchiveFieldName Name, bool bEnterWhenWriting)
{
	Ar.SetScope(*this);

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
#if DO_STRUCTURED_ARCHIVE_UNIQUE_FIELD_NAME_CHECKS
	if (!GetUnderlyingArchive().IsLoading())
	{
		FContainer& Container = *Ar.CurrentContainer.Top();
		checkf(!Container.KeyNames.Contains(Name.Name), TEXT("Multiple keys called '%s' serialized into record"), Name.Name);
		Container.KeyNames.Add(Name.Name);
	}
#endif
#endif

	if (Ar.Formatter.TryEnterField(Name, bEnterWhenWriting))
	{
		Ar.CurrentSlotElementId = Ar.ElementIdGenerator.Generate();
		return FStructuredArchiveSlot(Ar, Depth, Ar.CurrentSlotElementId);
	}
	else
	{
		return {};
	}
}

//////////// FStructuredArchiveArray ////////////

FStructuredArchiveSlot FStructuredArchiveArray::EnterElement()
{
	Ar.SetScope(*this);

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
	checkf(Ar.CurrentContainer.Top()->Index < Ar.CurrentContainer.Top()->Count, TEXT("Serialized too many array elements"));
#endif

	Ar.CurrentSlotElementId = Ar.ElementIdGenerator.Generate();

	Ar.Formatter.EnterArrayElement();

	return FStructuredArchiveSlot(Ar, Depth, Ar.CurrentSlotElementId);
}

FStructuredArchiveSlot FStructuredArchiveArray::EnterElement_TextOnly(EArchiveValueType& OutType)
{
	Ar.SetScope(*this);

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
	checkf(Ar.CurrentContainer.Top()->Index < Ar.CurrentContainer.Top()->Count, TEXT("Serialized too many array elements"));
#endif

	Ar.CurrentSlotElementId = Ar.ElementIdGenerator.Generate();

	Ar.Formatter.EnterArrayElement_TextOnly(OutType);

	return FStructuredArchiveSlot(Ar, Depth, Ar.CurrentSlotElementId);
}

//////////// FStructuredArchiveStream ////////////

FStructuredArchiveSlot FStructuredArchiveStream::EnterElement()
{
	Ar.SetScope(*this);

	Ar.CurrentSlotElementId = Ar.ElementIdGenerator.Generate();

	Ar.Formatter.EnterStreamElement();

	return FStructuredArchiveSlot(Ar, Depth, Ar.CurrentSlotElementId);
}

FStructuredArchiveSlot FStructuredArchiveStream::EnterElement_TextOnly(EArchiveValueType& OutType)
{
	Ar.SetScope(*this);

	Ar.CurrentSlotElementId = Ar.ElementIdGenerator.Generate();

	Ar.Formatter.EnterStreamElement_TextOnly(OutType);

	return FStructuredArchiveSlot(Ar, Depth, Ar.CurrentSlotElementId);
}

//////////// FStructuredArchiveMap ////////////

FStructuredArchiveSlot FStructuredArchiveMap::EnterElement(FString& Name)
{
	Ar.SetScope(*this);

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
	checkf(Ar.CurrentContainer.Top()->Index < Ar.CurrentContainer.Top()->Count, TEXT("Serialized too many map elements"));
#endif

	Ar.CurrentSlotElementId = Ar.ElementIdGenerator.Generate();

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
#if DO_STRUCTURED_ARCHIVE_UNIQUE_FIELD_NAME_CHECKS
	if(Ar.GetUnderlyingArchive().IsSaving())
	{
		FContainer& Container = *Ar.CurrentContainer.Top();
		checkf(!Container.KeyNames.Contains(Name), TEXT("Multiple keys called '%s' serialized into record"), *Name);
		Container.KeyNames.Add(Name);
	}
#endif
#endif

	Ar.Formatter.EnterMapElement(Name);

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
#if DO_STRUCTURED_ARCHIVE_UNIQUE_FIELD_NAME_CHECKS
	if(Ar.GetUnderlyingArchive().IsLoading())
	{
		FContainer& Container = *Ar.CurrentContainer.Top();
		checkf(!Container.KeyNames.Contains(Name), TEXT("Multiple keys called '%s' serialized into record"), *Name);
		Container.KeyNames.Add(Name);
	}
#endif
#endif

	return FStructuredArchiveSlot(Ar, Depth, Ar.CurrentSlotElementId);
}

FStructuredArchiveSlot FStructuredArchiveMap::EnterElement_TextOnly(FString& Name, EArchiveValueType& OutType)
{
	Ar.SetScope(*this);

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
	checkf(Ar.CurrentContainer.Top()->Index < Ar.CurrentContainer.Top()->Count, TEXT("Serialized too many map elements"));
#endif

	Ar.CurrentSlotElementId = Ar.ElementIdGenerator.Generate();

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
#if DO_STRUCTURED_ARCHIVE_UNIQUE_FIELD_NAME_CHECKS
	if(Ar.GetUnderlyingArchive().IsSaving())
	{
		FContainer& Container = *Ar.CurrentContainer.Top();
		checkf(!Container.KeyNames.Contains(Name), TEXT("Multiple keys called '%s' serialized into record"), *Name);
		Container.KeyNames.Add(Name);
	}
#endif
#endif

	Ar.Formatter.EnterMapElement_TextOnly(Name, OutType);

#if DO_STRUCTURED_ARCHIVE_CONTAINER_CHECKS
#if DO_STRUCTURED_ARCHIVE_UNIQUE_FIELD_NAME_CHECKS
	if(Ar.GetUnderlyingArchive().IsLoading())
	{
		FContainer& Container = *Ar.CurrentContainer.Top();
		checkf(!Container.KeyNames.Contains(Name), TEXT("Multiple keys called '%s' serialized into record"), *Name);
		Container.KeyNames.Add(Name);
	}
#endif
#endif

	return FStructuredArchiveSlot(Ar, Depth, Ar.CurrentSlotElementId);
}

#endif
