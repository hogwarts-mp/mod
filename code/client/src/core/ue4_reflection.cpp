#include <utils/safe_win32.h>

#include "aob_scan.h"

#include <logging/logger.h>

// Keep the sdk-heavy headers after the framework ones — the reversed UE
// headers tighten the MSVC warning state (4582 et al. become errors) in a way
// that breaks fmt's templates if they come first.
#include "ue4_natives.h"
#include "ue4_reflection.h"

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

        auto *lib = find_uobject("KismetStringLibrary /Script/Engine.Default__KismetStringLibrary");
        auto *fn  = reinterpret_cast<UFunction *>(find_uobject("Function /Script/Engine.KismetStringLibrary.Conv_StringToName"));
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
} // namespace HogwartsMP::Core::UE4
