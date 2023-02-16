// Copyright Epic Games, Inc. All Rights Reserved.

#include "UObject/UnrealType.h"
#include "Serialization/ArchiveUObjectFromStructuredArchive.h"

DEFINE_LOG_CATEGORY(LogType);

bool FPropertyValueIterator::NextValue(EPropertyValueIteratorFlags InRecursionFlags)
{
	if (PropertyIteratorStack.Num() == 0)
	{
		// Stack is done, return
		return false;
	}

	FPropertyValueStackEntry& Entry = PropertyIteratorStack.Last();

	// If we have pending values, deal with them
	if (Entry.ValueIndex < Entry.ValueArray.Num())
	{
		// Look for recursion on current value first
		const FProperty* Property = Entry.ValueArray[Entry.ValueIndex].Key;
		const void* PropertyValue = Entry.ValueArray[Entry.ValueIndex].Value;

		// For containers, insert at next index ahead of others
		int32 InsertIndex = Entry.ValueIndex + 1;

		// Handle container properties
		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			if (InRecursionFlags == EPropertyValueIteratorFlags::FullRecursion)
			{
				FScriptArrayHelper Helper(ArrayProperty, PropertyValue);
				for (int32 DynamicIndex = 0; DynamicIndex < Helper.Num(); ++DynamicIndex)
				{
					Entry.ValueArray.EmplaceAt(InsertIndex++, ArrayProperty->Inner, Helper.GetRawPtr(DynamicIndex));
				}
			}
		}
		else if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
		{
			if (InRecursionFlags == EPropertyValueIteratorFlags::FullRecursion)
			{
				FScriptMapHelper Helper(MapProperty, PropertyValue);
				int32 Num = Helper.Num();
				for (int32 DynamicIndex = 0; Num; ++DynamicIndex)
				{
					if (Helper.IsValidIndex(DynamicIndex))
					{
						Entry.ValueArray.EmplaceAt(InsertIndex++, MapProperty->KeyProp, Helper.GetKeyPtr(DynamicIndex));
						Entry.ValueArray.EmplaceAt(InsertIndex++, MapProperty->ValueProp, Helper.GetValuePtr(DynamicIndex));

						--Num;
					}
				}
			}
		}
		else if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
		{
			if (InRecursionFlags == EPropertyValueIteratorFlags::FullRecursion)
			{
				FScriptSetHelper Helper(SetProperty, PropertyValue);
				int32 Num = Helper.Num();
				for (int32 DynamicIndex = 0; Num; ++DynamicIndex)
				{
					if (Helper.IsValidIndex(DynamicIndex))
					{
						Entry.ValueArray.EmplaceAt(InsertIndex++, SetProperty->ElementProp, Helper.GetElementPtr(DynamicIndex));

						--Num;
					}
				}
			}
		}
		else if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			if (InRecursionFlags == EPropertyValueIteratorFlags::FullRecursion)
			{
				// We don't need recursion on these - this will happen naturally as we process these values
				for (FPropertyValueIterator Iter(FProperty::StaticClass(), StructProperty->Struct, PropertyValue, EPropertyValueIteratorFlags::NoRecursion, DeprecatedPropertyFlags); Iter; ++Iter)
				{
					Entry.ValueArray.EmplaceAt(InsertIndex++, Iter->Key, Iter->Value);
				}
			}
		}

		// Else this is a normal property and has nothing to expand
		// We don't expand enum properties because EnumProperty handles value wrapping for us

		// Increment next value to check
		Entry.ValueIndex++;
	}

	// Out of pending values, try to add more
	if (Entry.ValueIndex == Entry.ValueArray.Num())
	{
		// If Field iterator is done, pop stack and run again as Entry is invalidated
		if (!Entry.FieldIterator)
		{
			PropertyIteratorStack.Pop();

			if (PropertyIteratorStack.Num() > 0)
			{
				// Increment value index as we delayed incrementing it when entering recursion
				PropertyIteratorStack.Last().ValueIndex++;

				return NextValue(InRecursionFlags);
			}

			return false;
		}

		// If nothing left in value array, add base properties for current field and increase field iterator
		const FProperty* Property = *Entry.FieldIterator;
		++Entry.FieldIterator;

		// Clear out existing value array
		Entry.ValueArray.Reset();
		Entry.ValueIndex = 0;

		// Handle static arrays 
		for (int32 StaticIndex = 0; StaticIndex != Property->ArrayDim; ++StaticIndex)
		{
			const void* PropertyValue = Property->ContainerPtrToValuePtr<void>(Entry.StructValue, StaticIndex);

			Entry.ValueArray.Emplace(Property, PropertyValue);
		}
		return true;
	}
	
	return true;
}

void FPropertyValueIterator::IterateToNext()
{
	EPropertyValueIteratorFlags LocalRecursionFlags = RecursionFlags;

	if (bSkipRecursionOnce)
	{
		LocalRecursionFlags = EPropertyValueIteratorFlags::NoRecursion;
		bSkipRecursionOnce = false;
	}

	while (NextValue(LocalRecursionFlags))
	{
		// If this property is valid type, stop iteration
		FPropertyValueStackEntry& Entry = PropertyIteratorStack.Last();
		if (Entry.GetPropertyValue().Key->IsA(PropertyClass))
		{
			return;
		}

		// Reset recursion override as we've skipped the first property
		LocalRecursionFlags = RecursionFlags;
	}
}

void FPropertyValueIterator::GetPropertyChain(TArray<const FProperty*>& PropertyChain) const
{
	// Iterate over UStruct nesting, starting at the inner most property
	for (int32 StackIndex = PropertyIteratorStack.Num() - 1; StackIndex >= 0; StackIndex--)
	{
		const FPropertyValueStackEntry& Entry = PropertyIteratorStack[StackIndex];

		// Index should always be valid
		const FProperty* Property = Entry.ValueArray[Entry.ValueIndex].Key;

		while (Property)
		{
			// This handles container property nesting
			PropertyChain.Add(Property);
			Property = Property->GetOwner<FProperty>();
		}
	}
}