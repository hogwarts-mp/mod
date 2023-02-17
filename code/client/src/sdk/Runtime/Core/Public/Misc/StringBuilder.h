// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/StringFwd.h"
#include "Containers/StringView.h"
#include "Misc/CString.h"
#include "Templates/AndOrNot.h"
#include "Templates/EnableIf.h"
#include "Templates/IsArrayOrRefOfType.h"
#include "Templates/IsValidVariadicFunctionArg.h"
#include "Templates/UnrealTemplate.h"
#include "Traits/IsContiguousContainer.h"

/**
 * String Builder
 *
 * This class helps with the common task of constructing new strings.
 *
 * It does this by allocating buffer space which is used to hold the
 * constructed string. The intent is that the builder is allocated on
 * the stack as a function local variable to avoid heap allocations.
 *
 * The buffer is always contiguous and the class is not intended to be
 * used to construct extremely large strings.
 *
 * This is not intended to be used as a mechanism for holding on to
 * strings for a long time. The use case is explicitly to aid in
 * *constructing* strings on the stack and subsequently passing the
 * string into a function call or a more permanent string storage
 * mechanism like FString et al.
 *
 * The amount of buffer space to allocate is specified via a template
 * parameter and if the constructed string should overflow this initial
 * buffer, a new buffer will be allocated using regular dynamic memory
 * allocations.
 *
 * Overflow allocation should be the exceptional case however -- always
 * try to size the buffer so that it can hold the vast majority of
 * strings you expect to construct.
 *
 * Be mindful that stack is a limited resource, so if you are writing a
 * highly recursive function you may want to use some other mechanism
 * to build your strings.
 */
template <typename CharType>
class TStringBuilderBase
{
public:
	/** The character type that this builder operates on. */
	using ElementType = CharType;
	/** The string builder base type to be used by append operators and function output parameters. */
	using BuilderType = TStringBuilderBase<ElementType>;
	/** The string view type that this builder is compatible with. */
	using ViewType = TStringView<ElementType>;

	/** Whether the given type can be appended to this builder using the append operator. */
	template <typename AppendType>
	using TCanAppend = TIsSame<BuilderType&, decltype(DeclVal<BuilderType&>() << DeclVal<AppendType>())>;

	/** Whether the given range type can have its elements appended to the builder using the append operator. */
	template <typename RangeType>
	using TCanAppendRange = TAnd<TIsContiguousContainer<RangeType>, TCanAppend<decltype(*::GetData(DeclVal<RangeType>()))>>;

				TStringBuilderBase() = default;
	CORE_API	~TStringBuilderBase();

				TStringBuilderBase(const TStringBuilderBase&) = delete;
				TStringBuilderBase(TStringBuilderBase&&) = delete;

	TStringBuilderBase& operator=(const TStringBuilderBase&) = delete;
	TStringBuilderBase& operator=(TStringBuilderBase&&) = delete;

	inline TStringBuilderBase(CharType* BufferPointer, int32 BufferCapacity)
	{
		Initialize(BufferPointer, BufferCapacity);
	}

	inline int32 Len() const					{ return int32(CurPos - Base); }
	inline CharType* GetData()					{ return Base; }
	inline const CharType* GetData() const		{ return Base; }
	inline const CharType* ToString() const		{ EnsureNulTerminated(); return Base; }
	inline const CharType* operator*() const	{ EnsureNulTerminated(); return Base; }

	inline const CharType	LastChar() const	{ return *(CurPos - 1); }

	/**
	 * Empties the string builder, but doesn't change memory allocation.
	 */
	inline void Reset()
	{
		CurPos = Base;
	}

	/**
	 * Adds a given number of uninitialized characters into the string builder.
	 *
	 * @param InCount The number of uninitialized characters to add.
	 *
	 * @return The number of characters in the string builder before adding the new characters.
	 */
	inline int32 AddUninitialized(int32 InCount)
	{
		EnsureCapacity(InCount);
		const int32 OldCount = Len();
		CurPos += InCount;
		return OldCount;
	}

	/**
	 * Modifies the string builder to remove the given number of characters from the end.
	 */
	inline void RemoveSuffix(int32 InCount)
	{
		check(InCount <= Len());
		CurPos -= InCount;
	}

	inline BuilderType& Append(CharType Char)
	{
		EnsureCapacity(1);

		*CurPos++ = Char;

		return *this;
	}

	inline BuilderType& AppendAnsi(const ANSICHAR* NulTerminatedString)
	{
		if (!NulTerminatedString)
		{
			return *this;
		}

		return AppendAnsi(NulTerminatedString, TCString<ANSICHAR>::Strlen(NulTerminatedString));
	}

	inline BuilderType& AppendAnsi(const FAnsiStringView& AnsiString)
	{
		return AppendAnsi(AnsiString.GetData(), AnsiString.Len());
	}

	inline BuilderType& AppendAnsi(const ANSICHAR* String, const int32 Length)
	{
		EnsureCapacity(Length);

		CharType* RESTRICT Dest = CurPos;
		CurPos += Length;

		for (int32 i = 0; i < Length; ++i)
		{
			Dest[i] = String[i];
		}

		return *this;
	}

	inline BuilderType& Append(const CharType* NulTerminatedString)
	{
		if (!NulTerminatedString)
		{
			return *this;
		}

		return Append(NulTerminatedString, TCString<CharType>::Strlen(NulTerminatedString));
	}

	inline BuilderType& Append(const ViewType& StringView)
	{
		return Append(StringView.GetData(), StringView.Len());
	}

	inline BuilderType& Append(const CharType* String, int32 Length)
	{
		EnsureCapacity(Length);
		CharType* RESTRICT Dest = CurPos;
		CurPos += Length;

		FMemory::Memcpy(Dest, String, Length * sizeof(CharType));

		return *this;
	}

	/**
	 * Append every element of the range to the builder, separating the elements by the delimiter.
	 *
	 * This function is only available when the elements of the range and the delimiter can both be
	 * written to the builder using the append operator.
	 *
	 * @param InRange The range of elements to join and append.
	 * @param InDelimiter The delimiter to append as a separator for the elements.
	 *
	 * @return The builder, to allow additional operations to be composed with this one.
	 */
	template <typename RangeType, typename DelimiterType,
		typename = typename TEnableIf<TAnd<TCanAppendRange<RangeType&&>, TCanAppend<DelimiterType&&>>::Value>::Type>
	inline BuilderType& Join(RangeType&& InRange, DelimiterType&& InDelimiter)
	{
		bool bFirst = true;
		for (auto&& Elem : Forward<RangeType>(InRange))
		{
			if (bFirst)
			{
				bFirst = false;
			}
			else
			{
				*this << InDelimiter;
			}
			*this << Elem;
		}
		return *this;
	}

	/**
	 * Append every element of the range to the builder, separating the elements by the delimiter, and
	 * surrounding every element on each side with the given quote.
	 *
	 * This function is only available when the elements of the range, the delimiter, and the quote can be
	 * written to the builder using the append operator.
	 *
	 * @param InRange The range of elements to join and append.
	 * @param InDelimiter The delimiter to append as a separator for the elements.
	 * @param InQuote The quote to append on both sides of each element.
	 *
	 * @return The builder, to allow additional operations to be composed with this one.
	 */
	template <typename RangeType, typename DelimiterType, typename QuoteType,
		typename = typename TEnableIf<TAnd<TCanAppendRange<RangeType>, TCanAppend<DelimiterType&&>, TCanAppend<QuoteType&&>>::Value>::Type>
	inline BuilderType& JoinQuoted(RangeType&& InRange, DelimiterType&& InDelimiter, QuoteType&& InQuote)
	{
		bool bFirst = true;
		for (auto&& Elem : Forward<RangeType>(InRange))
		{
			if (bFirst)
			{
				bFirst = false;
			}
			else
			{
				*this << InDelimiter;
			}
			*this << InQuote << Elem << InQuote;
		}
		return *this;
	}

	/**
	 * Appends to the string builder similarly to how classic sprintf works.
	 *
	 * @param Format A format string that specifies how to format the additional arguments. Refer to standard printf format.
	 */
	template <typename FmtType, typename... Types>
	typename TEnableIf<TIsArrayOrRefOfType<FmtType, CharType>::Value, BuilderType&>::Type Appendf(const FmtType& Fmt, Types... Args)
	{
		static_assert(TIsArrayOrRefOfType<FmtType, CharType>::Value, "Formatting string must be a character array.");
		static_assert(TAnd<TIsValidVariadicFunctionArg<Types>...>::Value, "Invalid argument(s) passed to Appendf.");
		return AppendfImpl(*this, Fmt, Forward<Types>(Args)...);
	}

private:
	CORE_API static BuilderType& VARARGS AppendfImpl(BuilderType& Self, const CharType* Fmt, ...);

protected:
	inline void Initialize(CharType* InBase, int32 InCapacity)
	{
		Base	= InBase;
		CurPos	= InBase;
		End		= Base + InCapacity;
	}

	inline void EnsureNulTerminated() const
	{
		if (*CurPos)
		{
			*CurPos = 0;
		}
	}

	inline void EnsureCapacity(int32 RequiredAdditionalCapacity)
	{
		// precondition: we know the current buffer has enough capacity
		// for the existing string including NUL terminator

		if ((CurPos + RequiredAdditionalCapacity) < End)
		{
			return;
		}

		Extend(RequiredAdditionalCapacity);
	}

	CORE_API void	Extend(SIZE_T ExtraCapacity);
	CORE_API void*	AllocBuffer(SIZE_T CharCount);
	CORE_API void	FreeBuffer(void* Buffer, SIZE_T CharCount);

	CharType*	Base;
	CharType*	CurPos;
	CharType*	End;
	bool		bIsDynamic = false;
};

template <typename CharType>
constexpr inline SIZE_T GetNum(const TStringBuilderBase<CharType>& Builder)
{
	return Builder.Len();
}

//////////////////////////////////////////////////////////////////////////

/**
 * A string builder with inline storage.
 *
 * Avoid using this type directly. Prefer the aliases in StringFwd.h like TStringBuilder<N>.
 */
template <typename CharType, int32 BufferSize>
class TStringBuilderWithBuffer : public TStringBuilderBase<CharType>
{
public:
	inline TStringBuilderWithBuffer()
		: TStringBuilderBase<CharType>(StringBuffer, BufferSize)
	{
	}

private:
	CharType StringBuffer[BufferSize];
};

//////////////////////////////////////////////////////////////////////////

// String Append Operators

inline FAnsiStringBuilderBase&		operator<<(FAnsiStringBuilderBase& Builder, ANSICHAR Char)							{ return Builder.Append(Char); }
inline FWideStringBuilderBase&		operator<<(FWideStringBuilderBase& Builder, ANSICHAR Char)							{ return Builder.Append(Char); }
inline FWideStringBuilderBase&		operator<<(FWideStringBuilderBase& Builder, WIDECHAR Char)							{ return Builder.Append(Char); }

template <typename T>
inline auto operator<<(FAnsiStringBuilderBase& Builder, T&& Str) -> decltype(Builder.Append(ImplicitConv<FAnsiStringView>(Forward<T>(Str))))
{
	return Builder.Append(ImplicitConv<FAnsiStringView>(Forward<T>(Str)));
}

template <typename T>
inline auto operator<<(FWideStringBuilderBase& Builder, T&& Str) -> decltype(Builder.AppendAnsi(ImplicitConv<FAnsiStringView>(Forward<T>(Str))))
{
	return Builder.AppendAnsi(ImplicitConv<FAnsiStringView>(Forward<T>(Str)));
}

template <typename T>
inline auto operator<<(FWideStringBuilderBase& Builder, T&& Str) -> decltype(Builder.Append(ImplicitConv<FWideStringView>(Forward<T>(Str))))
{
	return Builder.Append(ImplicitConv<FWideStringView>(Forward<T>(Str)));
}

// Prefer using << instead of += as operator+= is only intended for mechanical FString -> FStringView replacement.
inline FStringBuilderBase&			operator+=(FStringBuilderBase& Builder, ANSICHAR Char)								{ return Builder.Append(Char); }
inline FStringBuilderBase&			operator+=(FStringBuilderBase& Builder, WIDECHAR Char)								{ return Builder.Append(Char); }
inline FStringBuilderBase&			operator+=(FStringBuilderBase& Builder, FAnsiStringView Str)						{ return Builder.AppendAnsi(Str); }
inline FStringBuilderBase&			operator+=(FStringBuilderBase& Builder, FWideStringView Str)						{ return Builder.Append(Str); }

// Integer Append Operators

inline FAnsiStringBuilderBase&		operator<<(FAnsiStringBuilderBase& Builder, int32 Value)							{ return Builder.Appendf("%d", Value); }
inline FAnsiStringBuilderBase&		operator<<(FAnsiStringBuilderBase& Builder, uint32 Value)							{ return Builder.Appendf("%u", Value); }
inline FWideStringBuilderBase&		operator<<(FWideStringBuilderBase& Builder, int32 Value)							{ return Builder.Appendf(TEXT("%d"), Value); }
inline FWideStringBuilderBase&		operator<<(FWideStringBuilderBase& Builder, uint32 Value)							{ return Builder.Appendf(TEXT("%u"), Value); }

inline FAnsiStringBuilderBase&		operator<<(FAnsiStringBuilderBase& Builder, int64 Value)							{ return Builder.Appendf("%" INT64_FMT, Value); }
inline FAnsiStringBuilderBase&		operator<<(FAnsiStringBuilderBase& Builder, uint64 Value)							{ return Builder.Appendf("%" UINT64_FMT, Value); }
inline FWideStringBuilderBase&		operator<<(FWideStringBuilderBase& Builder, int64 Value)							{ return Builder.Appendf(TEXT("%" INT64_FMT), Value); }
inline FWideStringBuilderBase&		operator<<(FWideStringBuilderBase& Builder, uint64 Value)							{ return Builder.Appendf(TEXT("%" UINT64_FMT), Value); }

inline FAnsiStringBuilderBase&		operator<<(FAnsiStringBuilderBase& Builder, int8 Value)								{ return Builder << int32(Value); }
inline FAnsiStringBuilderBase&		operator<<(FAnsiStringBuilderBase& Builder, uint8 Value)							{ return Builder << uint32(Value); }
inline FWideStringBuilderBase&		operator<<(FWideStringBuilderBase& Builder, int8 Value)								{ return Builder << int32(Value); }
inline FWideStringBuilderBase&		operator<<(FWideStringBuilderBase& Builder, uint8 Value)							{ return Builder << uint32(Value); }

inline FAnsiStringBuilderBase&		operator<<(FAnsiStringBuilderBase& Builder, int16 Value)							{ return Builder << int32(Value); }
inline FAnsiStringBuilderBase&		operator<<(FAnsiStringBuilderBase& Builder, uint16 Value)							{ return Builder << uint32(Value); }
inline FWideStringBuilderBase&		operator<<(FWideStringBuilderBase& Builder, int16 Value)							{ return Builder << int32(Value); }
inline FWideStringBuilderBase&		operator<<(FWideStringBuilderBase& Builder, uint16 Value)							{ return Builder << uint32(Value); }
