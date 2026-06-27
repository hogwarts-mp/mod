#include <utils/safe_win32.h>

#include "core/aob_scan.h"

#include <logging/logger.h>

// Keep the sdk-heavy headers after the framework ones — the reversed UE
// headers tighten the MSVC warning state (4582 et al. become errors) in a way
// that breaks fmt's templates if they come first.
#include "sdk/natives/ue4_natives.h"
#include "sdk/reflection/ue4_reflection.h"

#include <string>
#include <unordered_map>

namespace {
    auto Log() {
        return Framework::Logging::GetLogger("UE4Reflection");
    }

    using StaticLoadObject_t = void *(*)(UClass *objectClass, void *outer, const wchar_t *name, const wchar_t *filename, uint32_t loadFlags, void *sandbox, bool allowReconcile, void *serializeCtx);

    // SEH isolation — keep __try out of functions with C++ unwinding.
    void *CallStaticLoadObjectGuarded(StaticLoadObject_t fn, UClass *cls, const wchar_t *path) {
        __try {
            return fn(cls, nullptr, path, nullptr, 0, nullptr, true, nullptr);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
    }
} // namespace

namespace HogwartsMP::Core::UE4 {
    UObjectBase *FindUObject(const char *objFullName) {
        static std::unordered_map<std::string, UObjectBase *> objMap{};
        if (auto search = objMap.find(objFullName); search != objMap.end()) {
            return search->second;
        }

        for (auto i = 0; i < GObjectArray->GetObjectArrayNum(); ++i) {
            if (auto objItem = GObjectArray->IndexToObject(i)) {
                if (auto objBase = objItem->Object) {
                    if (get_full_name(objBase) == objFullName) {
                        objMap[objFullName] = objBase;
                        return objBase;
                    }
                }
            }
        }

        return nullptr;
    }

    UClass *FindUClass(const char *classFullName) {
        return static_cast<UClass *>(FindUObject(classFullName));
    }

    UFunction *FindUFunction(const char *functionFullName) {
        return static_cast<UFunction *>(FindUObject(functionFullName));
    }

    std::vector<UObjectBase *> FindUObjects(const char *objFullName) {
        std::vector<UObjectBase *> objects{};
        for (auto i = 0; i < GObjectArray->GetObjectArrayNum(); ++i) {
            if (auto objItem = GObjectArray->IndexToObject(i)) {
                if (auto objBase = objItem->Object) {
                    if (get_full_name(objBase) == objFullName) {
                        objects.push_back(objBase);
                    }
                }
            }
        }
        return objects;
    }

    UFunction *FindFunctionInChain(UObjectBase *obj, const char *name) {
        // Cache positive results only: a lookup that failed once (e.g. before
        // the class was fully loaded) must stay retryable. Keyed per class so
        // no string concatenation happens on the hot path.
        static std::unordered_map<UStruct *, std::unordered_map<std::string, UFunction *>> cache;
        auto *cls      = obj->GetClass();
        auto &perClass = cache[cls];
        if (const auto it = perClass.find(name); it != perClass.end()) {
            return it->second;
        }

        UFunction *found = nullptr;
        for (UStruct *s = cls; s && !found; s = s->GetSuperStruct()) {
            for (UField *f = s->Children; f; f = f->Next) {
                if (narrow(f->GetFName()) == name) {
                    found = static_cast<UFunction *>(f);
                    break;
                }
            }
        }
        if (found) {
            perClass.emplace(name, found);
        }
        return found;
    }

    bool CallUFunction(void *obj, const char *name, void *params) {
        if (!obj) {
            return false;
        }
        auto *fn = FindFunctionInChain(static_cast<UObjectBase *>(obj), name);
        if (!fn) {
            Log()->warn("UFunction not found: {}", name);
            return false;
        }
        static_cast<UObject *>(obj)->ProcessEvent(fn, params);
        return true;
    }

    FName MakeFName(const wchar_t *str) {
        static std::unordered_map<std::wstring, FName> cache;
        if (const auto it = cache.find(str); it != cache.end()) {
            return it->second;
        }

        auto *lib = FindUObject("KismetStringLibrary /Script/Engine.Default__KismetStringLibrary");
        auto *fn  = FindUFunction("Function /Script/Engine.KismetStringLibrary.Conv_StringToName");
        if (!lib || !fn) {
            Log()->error("Conv_StringToName unavailable (lib={}, fn={})", (void *)lib, (void *)fn);
            return FName();
        }

        struct {
            FString InString;
            FName ReturnValue;
        } params{FString(str), FName()};
        reinterpret_cast<UObject *>(lib)->ProcessEvent(fn, &params);
        cache.emplace(str, params.ReturnValue);
        return params.ReturnValue;
    }

    UObjectBase *LoadObjectByPath(UClass *ofClass, const wchar_t *path) {
        // Resolved once from the central pattern table (core/game_layout.h).
        // AOB verified on the current Steam build. MUST run on the game thread.
        static StaticLoadObject_t fn = reinterpret_cast<StaticLoadObject_t>(
            HogwartsMP::Core::AobFirst(HogwartsMP::Game::gLayout.staticLoadObject));
        if (!fn) {
            return nullptr;
        }
        auto *obj = static_cast<UObjectBase *>(CallStaticLoadObjectGuarded(fn, ofClass, path));
        if (!obj) {
            Log()->warn("Asset load failed: {}", narrow_(path));
        }
        return obj;
    }

    FProperty *FindPropertyInChain(UClass *cls, const char *name) {
        for (UStruct *s = cls; s; s = s->GetSuperStruct()) {
            for (FField *f = s->ChildProperties; f; f = f->Next) {
                if (narrow(f->GetFName()) == name) {
                    return static_cast<FProperty *>(f);
                }
            }
        }
        return nullptr;
    }

    bool SetBoolProperty(void *obj, const char *name, bool value) {
        auto *prop = static_cast<FBoolProperty *>(FindPropertyInChain(static_cast<UObjectBase *>(obj)->GetClass(), name));
        if (!prop) {
            return false;
        }
        auto *b = reinterpret_cast<uint8_t *>(obj) + prop->GetOffset_ForInternal() + prop->ByteOffset;
        if (value) {
            *b |= prop->ByteMask;
        }
        else {
            *b &= ~prop->ByteMask;
        }
        return true;
    }

    bool SetFloatProperty(void *obj, const char *name, float value) {
        auto *prop = FindPropertyInChain(static_cast<UObjectBase *>(obj)->GetClass(), name);
        if (!prop) {
            return false;
        }
        *reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(obj) + prop->GetOffset_ForInternal()) = value;
        return true;
    }

    bool SetVec3Property(void *obj, const char *name, float a, float b, float c) {
        auto *prop = FindPropertyInChain(static_cast<UObjectBase *>(obj)->GetClass(), name);
        if (!prop) {
            return false;
        }
        auto *f = reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(obj) + prop->GetOffset_ForInternal());
        f[0]    = a;
        f[1]    = b;
        f[2]    = c;
        return true;
    }

    int ReadByteProperty(void *obj, const char *name) {
        auto *prop = FindPropertyInChain(static_cast<UObjectBase *>(obj)->GetClass(), name);
        if (!prop) {
            return -1;
        }
        const auto type = narrow(prop->GetClass()->GetFName());
        if (type != "ByteProperty" && type != "EnumProperty") {
            return -1;
        }
        return static_cast<int>(*reinterpret_cast<uint8_t *>(
            reinterpret_cast<uint8_t *>(obj) + prop->GetOffset_ForInternal()));
    }

    std::string ReadNameProperty(void *obj, const char *name) {
        auto *prop = FindPropertyInChain(static_cast<UObjectBase *>(obj)->GetClass(), name);
        if (!prop || narrow(prop->GetClass()->GetFName()) != "NameProperty") {
            return {};
        }
        auto *fname = reinterpret_cast<FName *>(
            reinterpret_cast<uint8_t *>(obj) + prop->GetOffset_ForInternal());
        return narrow(*fname);
    }

    std::string ReadStringProperty(void *obj, const char *name) {
        auto *prop = FindPropertyInChain(static_cast<UObjectBase *>(obj)->GetClass(), name);
        if (!prop || narrow(prop->GetClass()->GetFName()) != "StrProperty") {
            return {};
        }
        auto *fstr = reinterpret_cast<FString *>(
            reinterpret_cast<uint8_t *>(obj) + prop->GetOffset_ForInternal());
        return narrow(*fstr);
    }

    UObjectBase *ReadObjectProperty(void *obj, const char *name) {
        auto *prop = FindPropertyInChain(static_cast<UObjectBase *>(obj)->GetClass(), name);
        if (!prop) {
            return nullptr;
        }
        // Only dereference if the property really stores a UObject* — otherwise
        // we'd reinterpret arbitrary bytes (a struct/array/etc.) as a pointer.
        // TObjectPtr<T> (ObjectPtrProperty) is layout-compatible with T* here.
        const auto type = narrow(prop->GetClass()->GetFName());
        if (type != "ObjectProperty" && type != "ObjectPtrProperty") {
            return nullptr;
        }
        return *reinterpret_cast<UObjectBase **>(
            reinterpret_cast<uint8_t *>(obj) + prop->GetOffset_ForInternal());
    }

    PropertyValue ReadProperty(void *obj, const char *name) {
        if (!obj) {
            return std::monostate{};
        }
        auto *prop = FindPropertyInChain(static_cast<UObjectBase *>(obj)->GetClass(), name);
        if (!prop) {
            return std::monostate{};
        }
        auto *base      = reinterpret_cast<uint8_t *>(obj) + prop->GetOffset_ForInternal();
        const auto type = narrow(prop->GetClass()->GetFName());

        // Each branch returns an exact variant alternative, so there's no converting-constructor
        // ambiguity (bool / int64_t / double / std::string are picked directly).
        if (type == "BoolProperty") {
            // Bitfield bools: same ByteOffset/ByteMask handling as SetBoolProperty.
            auto *bp           = static_cast<FBoolProperty *>(prop);
            const uint8_t byte = *(reinterpret_cast<uint8_t *>(obj) + bp->GetOffset_ForInternal() + bp->ByteOffset);
            return (byte & bp->ByteMask) != 0;
        }
        if (type == "IntProperty") {
            return static_cast<int64_t>(*reinterpret_cast<int32_t *>(base));
        }
        if (type == "Int64Property") {
            return *reinterpret_cast<int64_t *>(base);
        }
        if (type == "ByteProperty" || type == "EnumProperty") {
            return static_cast<int64_t>(*reinterpret_cast<uint8_t *>(base));
        }
        if (type == "FloatProperty") {
            return static_cast<double>(*reinterpret_cast<float *>(base));
        }
        if (type == "DoubleProperty") {
            return *reinterpret_cast<double *>(base);
        }
        if (type == "NameProperty") {
            return narrow(*reinterpret_cast<FName *>(base));
        }
        if (type == "StrProperty") {
            return narrow(*reinterpret_cast<FString *>(base));
        }
        // Structs/objects/arrays etc. intentionally unsupported here (monostate) — they need typed
        // handling (and object handles), a later reflection-bridge slice.
        return std::monostate{};
    }

    std::vector<std::string> ListPropertyNames(void *obj) {
        std::vector<std::string> names;
        if (!obj) {
            return names;
        }
        for (UStruct *s = static_cast<UObjectBase *>(obj)->GetClass(); s; s = s->GetSuperStruct()) {
            for (FField *f = s->ChildProperties; f; f = f->Next) {
                names.push_back(narrow(f->GetFName()));
            }
        }
        return names;
    }

    namespace {
        // Split "A.B.C" into {"A","B","C"}. Empty/sole-empty segments are skipped so a stray dot can't
        // produce an empty hop.
        std::vector<std::string> SplitPath(const std::string &path) {
            std::vector<std::string> segs;
            size_t start = 0;
            while (start <= path.size()) {
                const size_t dot = path.find('.', start);
                const size_t end = (dot == std::string::npos) ? path.size() : dot;
                if (end > start) {
                    segs.push_back(path.substr(start, end - start));
                }
                if (dot == std::string::npos) {
                    break;
                }
                start = dot + 1;
            }
            return segs;
        }

        // Hop object-typed properties named by `hops` from `obj`; nullptr if any hop is missing/non-object.
        void *FollowObjectHops(void *obj, const std::vector<std::string> &hops, size_t count) {
            void *cur = obj;
            for (size_t i = 0; i < count && cur; ++i) {
                cur = ReadObjectProperty(cur, hops[i].c_str());
            }
            return cur;
        }
    } // namespace

    PropertyValue ReadPropertyPath(void *obj, const std::string &path) {
        if (!obj) {
            return std::monostate{};
        }
        const auto segs = SplitPath(path);
        if (segs.empty()) {
            return std::monostate{};
        }
        // Hop all segments but the last (those must be object properties), then read the final scalar.
        void *target = FollowObjectHops(obj, segs, segs.size() - 1);
        if (!target) {
            return std::monostate{};
        }
        return ReadProperty(target, segs.back().c_str());
    }

    std::vector<std::string> ListPropertyNamesPath(void *obj, const std::string &path) {
        const auto segs  = SplitPath(path);
        void *target     = FollowObjectHops(obj, segs, segs.size());
        return target ? ListPropertyNames(target) : std::vector<std::string>{};
    }

    std::string AssetPath(UObjectBase *obj) {
        if (!obj) {
            return "(null)";
        }
        std::string name = narrow(obj->GetFName());
        for (auto *outer = obj->GetOuter(); outer; outer = outer->GetOuter()) {
            name = narrow(outer->GetFName()) + "." + name;
        }
        return name;
    }

    bool IsSubclassOf(UClass *cls, const char *baseName) {
        for (UStruct *s = cls; s; s = s->GetSuperStruct()) {
            if (narrow(s->GetFName()) == baseName) {
                return true;
            }
        }
        return false;
    }

    UObjectBase *LoadAnimSequence(const wchar_t *path) {
        static std::unordered_map<std::wstring, UObjectBase *> cache;
        if (auto it = cache.find(path); it != cache.end()) {
            return it->second;
        }
        static auto *seqCls = FindUClass("Class /Script/Engine.AnimSequence");
        auto *seq           = seqCls ? LoadObjectByPath(seqCls, path) : nullptr;
        cache[path]         = seq; // negatives cached too — a missing asset won't reload-spam each frame
        return seq;
    }

    UObjectBase *LoadAnimAsset(const wchar_t *path) {
        // Validate against the AnimationAsset base so a BlendSpace / BlendSpace1D / AnimSequence all pass.
        static std::unordered_map<std::wstring, UObjectBase *> cache;
        if (auto it = cache.find(path); it != cache.end()) {
            return it->second;
        }
        static auto *assetCls = FindUClass("Class /Script/Engine.AnimationAsset");
        auto *obj             = assetCls ? LoadObjectByPath(assetCls, path) : nullptr;
        cache[path]           = obj;
        return obj;
    }

    bool PlayAnimBlended(UObjectBase *skin, UObjectBase *asset, bool loop, float blendInSec) {
        if (!skin || !asset) {
            return false;
        }
        // SetAnimationAsset on the existing single-node instance snapshots the current pose and blends
        // into the new asset over InBlendInTime — the crossfade plain PlayAnimation lacks. Needs an
        // instance to already exist (the proxy's baked AnimBP / a prior PlayAnimation creates one), so
        // the first-ever play / a build without SetAnimationAsset falls through to the instant path.
        if (auto *inst = ReadObjectProperty(skin, "AnimScriptInstance")) {
            struct {
                UObjectBase *NewAsset;
                bool bIsLooping;
                float InBlendInTime;
            } p {asset, loop, blendInSec};
            if (CallUFunction(inst, "SetAnimationAsset", &p)) {
                return true;
            }
        }
        struct {
            UObjectBase *NewAnimToPlay;
            bool bLooping;
        } play {asset, loop};
        CallUFunction(skin, "PlayAnimation", &play); // no instance yet, or not reflected — instant swap
        return false;
    }

    bool SetBlendSpaceInputOnSkin(UObjectBase *skin, float x, float y) {
        if (!skin) {
            return false;
        }
        // The single-node instance (created by PlayAnimation / a blendspace PlayAnimBlended) owns the
        // blendspace coordinate.
        auto *inst = ReadObjectProperty(skin, "AnimScriptInstance");
        if (!inst) {
            return false;
        }
        struct {
            float X, Y, Z;
        } in {x, y, 0.f}; // FVector InBlendInput (UE4.27 floats)
        return CallUFunction(inst, "SetBlendSpaceInput", &in);
    }
} // namespace HogwartsMP::Core::UE4
