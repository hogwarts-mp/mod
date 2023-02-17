// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"

/**
 * Removes one dimension of extents from an array type.
 */
template <typename T>           struct TRemoveExtent       { typedef T Type; };
template <typename T>           struct TRemoveExtent<T[]>  { typedef T Type; };
template <typename T, uint32 N> struct TRemoveExtent<T[N]> { typedef T Type; };
