#pragma once
// UE4 reflection plumbing shared by features that drive the game through its
// own reflection (student proxies, appearance sync, ...). Everything here
// MUST run on the game thread: ProcessEvent, StaticLoadObject and the
// GUObjectArray scans behind find_uobject are not safe off-thread.

#include "UObject/Class.h"
#include "UObject/UnrealType.h"

namespace HogwartsMP::Core::UE4 {
    // Equivalent of UE4SS's GetFunctionByNameInChain: walk the class hierarchy
    // looking for a UFunction by short name. Avoids hardcoding declaring
    // classes for every engine function. Positive results are cached per
    // (class, name); the cache assumes rooted (native) classes — Blueprint
    // classes can be GC'd and their address reused, so don't rely on it for
    // BP classes without revalidating.
    UFunction *FindFunctionInChain(UObjectBase *obj, const char *name);

    // ProcessEvent by short function name; `params` is the UFunction's
    // parameter block (layout must match the reflected signature exactly).
    bool CallUFunction(void *obj, const char *name, void *params);

    // FName from string via KismetStringLibrary.Conv_StringToName — uses the
    // game's own FName interning, no FName-constructor sigscan required.
    // (The SDK's FName(const char*) constructor is non-functional — it scans
    // an empty cache — so it must not be used.)
    FName MakeFName(const wchar_t *str);

    // StaticLoadObject (native, sigscanned): loads assets by path with
    // nothing resident.
    UObjectBase *LoadObjectByPath(UClass *ofClass, const wchar_t *path);

    // Property access via the game's reflection (FField chain).
    FProperty *FindPropertyInChain(UClass *cls, const char *name);
    bool SetBoolProperty(void *obj, const char *name, bool value);
    // Unchecked: assumes the named property really is a float.
    bool SetFloatProperty(void *obj, const char *name, float value);
} // namespace HogwartsMP::Core::UE4
