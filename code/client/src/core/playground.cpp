#include <utils/safe_win32.h>
#include "playground.h"
#include <MinHook.h>
#include <logging/logger.h>
#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking.h>

#include "UObject/Class.h"
#include "UObject/EnumProperty.h"
#include "UObject/UObjectArray.h"
#include "UObject/UnrealType.h"

#include "application.h"

static FUObjectArray *GObjectArray {nullptr};

std::string narrow_(std::wstring_view str) {
    auto length = WideCharToMultiByte(CP_UTF8, 0, str.data(), (int)str.length(), nullptr, 0, nullptr, nullptr);
    std::string narrowStr {};

    narrowStr.resize(length); 
    WideCharToMultiByte(CP_UTF8, 0, str.data(), (int)str.length(), (LPSTR)narrowStr.c_str(), length, nullptr, nullptr);

    return narrowStr;
}

std::string narrow(const FString &fstr) {
    auto &char_array = fstr.GetCharArray();
    return narrow_(char_array.Num() ? char_array.GetData() : L"");
}

std::string narrow(const FName &fname) {
    return narrow(fname.ToString());
}

std::string get_full_name(UObjectBase *obj) {
    auto c = obj->GetClass();

    if (c == nullptr) {
        return "null";
    }

    auto obj_name = narrow(obj->GetFName());

    for (auto outer = obj->GetOuter(); outer != nullptr; outer = outer->GetOuter()) {
        obj_name = narrow(outer->GetFName()) + '.' + obj_name;
    }

    return narrow(c->GetFName()) + ' ' + obj_name;
}

UObjectBase *find_uobject(const char *obj_full_name) {
    static std::unordered_map<std::string, UObjectBase *> obj_map {};
    if (auto search = obj_map.find(obj_full_name); search != obj_map.end()) {
        return search->second;
    }

    for (auto i = 0; i < GObjectArray->GetObjectArrayNum(); ++i) {
        if (auto obj_item = GObjectArray->IndexToObject(i)) {
            if (auto obj_base = obj_item->Object) {
                auto full_name = get_full_name(obj_base);
                if (full_name == obj_full_name) {
                    obj_map[obj_full_name] = obj_base;
                    return obj_base;
                }
            }
        }
    }

    return nullptr;
}
void Playground_Tick() {
    if (GetAsyncKeyState(FW_KEY_F2) & 1) {
        
        auto *found_object = (UClass *)find_uobject("BlueprintGeneratedClass /Game/Pawn/NPC/Creature/GreyCat/BP_GreyCat_Creature.BP_GreyCat_Creature_C");
        if (!found_object) {
            Framework::Logging::GetLogger("Hooks")->info("Unable to find object !");
            return;
        }

        Framework::Logging::GetLogger("Hooks")->info("Found UObject: {}", narrow(found_object->GetFName().ToString()).c_str());

        FTransform transform {};
        FVector pos = { 351002.25f, -463037.25f, -85707.94531f };
        transform.SetTranslation(pos);

        FActorSpawnParameters spawnParams {};
        spawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
        //spawnParams.Name                           = found_object->GetFName();

        HogwartsMP::Core::gGlobals.world

        auto *new_object = UWorld__SpawnActor(*GWorld, found_object, &transform, spawnParams);
        Framework::Logging::GetLogger("Hooks")->info("Spawned actor: {}", (void *)found_object);
    }
}
