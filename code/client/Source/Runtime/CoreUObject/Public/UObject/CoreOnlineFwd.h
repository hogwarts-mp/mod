// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

class FUniqueNetId;
struct FUniqueNetIdWrapper;

using FUniqueNetIdPtr = TSharedPtr<const FUniqueNetId, UNIQUENETID_ESPMODE>;
using FUniqueNetIdRef = TSharedRef<const FUniqueNetId, UNIQUENETID_ESPMODE>;
using FUniqueNetIdWeakPtr = TWeakPtr<const FUniqueNetId, UNIQUENETID_ESPMODE>;