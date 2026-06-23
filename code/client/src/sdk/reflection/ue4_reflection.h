#pragma once
// UE4 reflection plumbing shared by features that drive the game through its
// own reflection (student proxies, appearance sync, ...). Everything here
// MUST run on the game thread: ProcessEvent, StaticLoadObject and the
// GUObjectArray scans behind FindUObject are not safe off-thread.

#include "UObject/Class.h"
#include "UObject/UnrealType.h"

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace HogwartsMP::Core::UE4 {
    // Linear GUObjectArray scan for a UObject by its full name
    // ("Class /Script/Foo.Bar"), cached by name. Game thread only — the scan
    // over the live object array is not safe off-thread.
    UObjectBase *FindUObject(const char *objFullName);

    // FindUObject + cast to UClass*, since most callers want a class. Returns
    // nullptr if not found. (No type check — pass a "Class /Script/..." path.)
    UClass *FindUClass(const char *classFullName);

    // FindUObject + cast to UFunction*. Returns nullptr if not found.
    // (No type check — pass a "Function /Script/..." path.)
    UFunction *FindUFunction(const char *functionFullName);

    // Like FindUObject but returns every match (e.g. multiple instances of the
    // same class). Uncached — full GUObjectArray scan each call. Game thread only.
    std::vector<UObjectBase *> FindUObjects(const char *objFullName);

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

    // Typed property reads (used by the AppearanceDump harvester).
    // Read a byte/uint8 (or enum) property by name; returns -1 if not found.
    int ReadByteProperty(void *obj, const char *name);
    // Read an FName property by name; returns empty string if not found.
    std::string ReadNameProperty(void *obj, const char *name);
    // Read an FString property by name; returns empty string if not found.
    std::string ReadStringProperty(void *obj, const char *name);
    // Read a raw UObject* property (ObjectProperty / ObjectPtrProperty —
    // TObjectPtr<T> is layout-compatible with T* in UE4.27). Returns nullptr
    // if the property is not found or the stored pointer is null.
    UObjectBase *ReadObjectProperty(void *obj, const char *name);

    // A property value read generically by name (see ReadProperty). std::monostate = not found or an
    // unsupported type; integral kinds (int/byte/enum) widen to int64_t, float/double to double.
    using PropertyValue = std::variant<std::monostate, bool, int64_t, double, std::string>;

    // Read a property by name off a UObject, dispatching on its reflected type. Supports the common
    // scalars (bool / int / byte / enum / float / double) and name / string; unsupported types
    // (structs, objects, arrays, ...) return std::monostate. Must run on the game thread.
    PropertyValue ReadProperty(void *obj, const char *name);

    // Every property name on the object's class chain — a discovery aid so scripts know what
    // ReadProperty can be called with. Can be large.
    std::vector<std::string> ListPropertyNames(void *obj);

    // Dotted-path variants: hop object-typed properties from `obj` (e.g. a component), then act on the
    // final object. `path` = "A.B.C" follows object properties A, B and reads scalar C. Each hop and
    // the final read resolve fresh from `obj` in one call — so when `obj` is the live pawn there are no
    // stored handles to dangle. A hop through a missing/non-object property yields None / empty.
    // (Following references to objects that aren't owned sub-objects of `obj` is at the caller's risk —
    // the stored pointer could be stale; full liveness validation is a later slice.)
    PropertyValue ReadPropertyPath(void *obj, const std::string &path);

    // List the property names of the object reached by hopping `path` (empty path = `obj` itself).
    std::vector<std::string> ListPropertyNamesPath(void *obj, const std::string &path);

    // Full UE4 asset path for an object: outer chain joined onto the package
    // path, e.g. "/Game/RiggedObjects/.../SK_Foo.SK_Foo" — the form
    // LoadObjectByPath expects.
    std::string AssetPath(UObjectBase *obj);

    // True if cls or any superclass has the given short name.
    bool IsSubclassOf(UClass *cls, const char *baseName);
} // namespace HogwartsMP::Core::UE4
