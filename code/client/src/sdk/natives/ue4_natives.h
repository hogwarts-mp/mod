#pragma once
// Shared UE4 natives + string helpers. Definitions live in playground.cpp
// (sigscanned there at init from the core/game_layout.h pattern table); this
// header is the single source of truth for their signatures — do not
// re-declare them by hand in other translation units.
//
// INCLUDE-ORDER WARNING: this header pulls in the vendored UE Runtime headers,
// which tighten the MSVC warning state (4582 et al. become errors) in a way
// that breaks fmt's templates. In any TU that logs, include
// <logging/logger.h> (or application.h) BEFORE this header.

#include <string>
#include <string_view>

#include "UObject/Class.h"
#include "UObject/UObjectArray.h"
#include "UObject/UnrealType.h"

#include "sdk/Headers/FActorSpawnParameters.h"

class UWorld;

std::string narrow_(std::wstring_view str);
std::string narrow(const FString &fstr);
std::string narrow(const FName &fname);
std::string get_full_name(UObjectBase *obj);

// The game's global UObject table. Resolved (sigscanned) at init in
// playground.cpp; consumed by the reflection helpers (UE4::FindUObject).
extern FUObjectArray *GObjectArray;

// Native UWorld::SpawnActor — the FVector+FRotator overload (the
// engine/console/UE4SS one; re-derived via Ghidra, see game_layout.h).
typedef AActor *(__fastcall *UWorld__SpawnActor_t)(UWorld *world, UClass *Class, FVector const *Location, FRotator const *Rotation, const FActorSpawnParameters &SpawnParameters);
typedef bool (__fastcall *UWorld__DestroyActor_t)(UWorld *world, AActor *ThisActor, bool bNetForce, bool bShouldModifyLevel);

extern UWorld__SpawnActor_t UWorld__SpawnActor;
extern UWorld__DestroyActor_t UWorld__DestroyActor;
extern UWorld **GWorld;
