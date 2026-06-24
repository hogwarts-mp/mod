// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Templates/UnrealTemplate.h"

// Single-ownership smart pointer similar to TUniquePtr but with a few differences which make it
// particularly useful for (but not limited to) implementing the pimpl pattern:
//
// https://en.cppreference.com/w/cpp/language/pimpl
//
// Some of the features:
//
// Like TUniquePtr:
// - Unique ownership - no reference counting.
// - Move-only, no copying.
// - Has the same static footprint as a pointer.
//
// Like TSharedPtr:
// - The deleter is determined at binding time and type-erased, allowing the object to be deleted without access to the definition of the type.
// - Has additional heap footprint (but smaller than TSharedPtr).
//
// Unlike both:
// - No custom deleter support.
// - No derived->base pointer conversion support (impossible to implement in C++ in a good way with multiple inheritance, and not typically needed for pimpls).
// - The pointed-to object must be created with its Make function - it cannot take ownership of an existing pointer.
// - No array support.
//
// The main benefits of this class which make it useful for pimpls:
// - Has single-ownership semantics.
// - Has the same performance and footprint as a pointer to the object, and minimal overhead on construction and destruction.
// - Can be added as a class member with a forward-declared type without having to worry about the proper definition of constructors and other special member functions.

namespace UE4PimplPtr_Private
{
	constexpr SIZE_T RequiredAlignment = 16;

	template <typename T>
	struct TPimplHeapObjectImpl;

	template <typename T>
	void DeleterFunc(void* Ptr)
	{
		// We never pass a null pointer to this function, but the compiler emits delete code
		// which handles nulls - we don't need this extra branching code, so assume it's not.
		UE_ASSUME(Ptr);
		delete (TPimplHeapObjectImpl<T>*)Ptr;
	}

	template <typename T>
	struct TPimplHeapObjectImpl
	{
		template <typename... ArgTypes>
		explicit TPimplHeapObjectImpl(ArgTypes&&... Args)
			: Deleter(&DeleterFunc<T>)
			, Val(Forward<ArgTypes>(Args)...)
		{
			// This should never fire, unless a compiler has laid out this struct in an unexpected way
			static_assert(STRUCT_OFFSET(TPimplHeapObjectImpl, Val) == RequiredAlignment, "Unexpected alignment of T within the pimpl object");
		}

		void(*Deleter)(void*);
		char Padding[RequiredAlignment - sizeof(void(*)(void*))];
		alignas(RequiredAlignment) T Val;
	};

	FORCEINLINE void CallDeleter(void* Ptr)
	{
		void* ThunkedPtr = (char*)Ptr - RequiredAlignment;

		// 'noexcept' as part of a function signature is a C++17 feature, but its use here
		// can tidy up the codegen a bit.  As we're likely to build with exceptions disabled
		// anyway, this is not something we need a well-engineered solution for right now,
		// so it's simply left commented out until we can rely on it everywhere.
		(*(void(**)(void*) /*noexcept*/)ThunkedPtr)(ThunkedPtr);
	}
}

template <typename T>
struct TPimplPtr
{
private:
	template <typename U, typename... ArgTypes>
	friend TPimplPtr<U> MakePimpl(ArgTypes&&...);

	explicit TPimplPtr(UE4PimplPtr_Private::TPimplHeapObjectImpl<T>* Impl)
		: Ptr(&Impl->Val)
	{
	}

public:
	TPimplPtr() = default;

	TPimplPtr(TYPE_OF_NULLPTR)
	{
	}

	~TPimplPtr()
	{
		if (Ptr)
		{
			UE4PimplPtr_Private::CallDeleter(this->Ptr);
		}
	}

	// Non-copyable
	TPimplPtr(const TPimplPtr&) = delete;
	TPimplPtr& operator=(const TPimplPtr&) = delete;

	// Movable
	TPimplPtr(TPimplPtr&& Other)
		: Ptr(Other.Ptr)
	{
		Other.Ptr = nullptr;
	}

	TPimplPtr& operator=(TPimplPtr&& Other)
	{
		if (&Other != this)
		{
			T* LocalPtr = this->Ptr;
			this->Ptr = Other.Ptr;
			Other.Ptr = nullptr;
			if (LocalPtr)
			{
				UE4PimplPtr_Private::CallDeleter(LocalPtr);
			}
		}
		return *this;
	}

	TPimplPtr& operator=(TYPE_OF_NULLPTR)
	{
		Reset();
		return *this;
	}

	bool IsValid() const
	{
		return !!this->Ptr;
	}

	explicit operator bool() const
	{
		return !!this->Ptr;
	}

	T* operator->() const
	{
		return this->Ptr;
	}

	T* Get() const
	{
		return this->Ptr;
	}

	T& operator*() const
	{
		return *this->Ptr;
	}

	void Reset()
	{
		if (T* LocalPtr = this->Ptr)
		{
			this->Ptr = nullptr;
			UE4PimplPtr_Private::CallDeleter(LocalPtr);
		}
	}

private:
	T* Ptr = nullptr;
};

template <typename T> FORCEINLINE bool operator==(const TPimplPtr<T>& Ptr, TYPE_OF_NULLPTR) { return !Ptr.IsValid(); }
template <typename T> FORCEINLINE bool operator==(TYPE_OF_NULLPTR, const TPimplPtr<T>& Ptr) { return !Ptr.IsValid(); }
template <typename T> FORCEINLINE bool operator!=(const TPimplPtr<T>& Ptr, TYPE_OF_NULLPTR) { return  Ptr.IsValid(); }
template <typename T> FORCEINLINE bool operator!=(TYPE_OF_NULLPTR, const TPimplPtr<T>& Ptr) { return  Ptr.IsValid(); }

/**
 * Heap-allocates an instance of T with the given arguments and returns it as a TPimplPtr.
 *
 * Usage: TPimplPtr<FMyType> MyPtr = MakePimpl<FMyType>(...arguments...);
 */
template <typename T, typename... ArgTypes>
FORCEINLINE TPimplPtr<T> MakePimpl(ArgTypes&&... Args)
{
	static_assert(sizeof(T) > 0, "T must be a complete type");
	static_assert(alignof(T) <= UE4PimplPtr_Private::RequiredAlignment, "T cannot be aligned more than 16 bytes");
	return TPimplPtr<T>(new UE4PimplPtr_Private::TPimplHeapObjectImpl<T>(Forward<ArgTypes>(Args)...));
}
