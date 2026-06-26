#include "playground.h"

#include "appearance_dump.h"
#include "application.h"
#include "aob_scan.h"
#include "student_proxy.h"
#include "sdk/natives/ue4_natives.h"
#include "sdk/reflection/ue4_reflection.h"

#include <utils/safe_win32.h>

#include <MinHook.h>
#include <logging/logger.h>
#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking.h>

#include "UObject/Class.h"
#include "UObject/EnumProperty.h"
#include "UObject/UObjectArray.h"
#include "UObject/UnrealType.h"

#include <utils/string_utils.h>
#include <imgui.h>

// Defined here (resolved in the InitFunction below) but shared via
// ue4_natives.h so the reflection helpers can scan it.
FUObjectArray *GObjectArray{nullptr};

std::string narrow_(std::wstring_view str) {
    auto length = WideCharToMultiByte(CP_UTF8, 0, str.data(), (int)str.length(), nullptr, 0, nullptr, nullptr);
    std::string narrowStr{};

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

// Declarations (typedefs + externs) live in ue4_natives.h; the natives are
// resolved in the InitFunction at the bottom of this file.
UWorld__SpawnActor_t UWorld__SpawnActor     = nullptr;
UWorld__DestroyActor_t UWorld__DestroyActor = nullptr;

UWorld **GWorld = nullptr;

using namespace Framework::Utils::StringUtils;
using HogwartsMP::Core::UE4::FindUClass;
using HogwartsMP::Core::UE4::FindUFunction;

void Playground_Tick() {
    // Game-thread pump: SpawnActor / StaticLoadObject / ProcessEvent must run
    // here, not in the (render-thread) ImGui callback. The buttons below only
    // set thread-safe request flags.
    HogwartsMP::Core::StudentProxy::ProcessPending();
    HogwartsMP::Core::AppearanceDump::ProcessPending();

    static AActor *lastActor = nullptr;

    static char teleportLocation[250] = "FT_HW_TrophyRoom";
    static bool doTeleport = false;

    static char spawnObject[250] = "BlueprintGeneratedClass /Game/Pawn/NPC/Creature/GreyCat/BP_GreyCat_Creature.BP_GreyCat_Creature_C";
    static bool doSpawn = false;

    static std::vector<AActor *> spawnedActors;

    HogwartsMP::Core::gApplication->GetImGUI()->PushWidget([&]() {
        ImGui::Begin("Playground");

        ImGui::Separator();
        ImGui::InputText("Location", teleportLocation, 250);
        if (ImGui::Button("Teleport")) {
            UClass *fastTravelManager = FindUClass("Class /Script/Phoenix.FastTravelManager");
            UFunction *fastTravelManagerGetter = FindUFunction("Function /Script/Phoenix.FastTravelManager.Get");

            UClass *fastTravelmanagerInsance{nullptr};
            fastTravelManager->ProcessEvent(fastTravelManagerGetter, (void *)&fastTravelmanagerInsance);

            if (fastTravelmanagerInsance) {
                auto wideTeleportLocation = NormalToWide(teleportLocation);
                FString name(wideTeleportLocation.c_str());
                Framework::Logging::GetLogger("Hooks")->info("Teleporting to {}, instance: {} !", teleportLocation, (void *)fastTravelmanagerInsance);

                UFunction *fastTravelTo = FindUFunction("Function /Script/Phoenix.FastTravelManager.FastTravel_To");
                fastTravelmanagerInsance->ProcessEvent(fastTravelTo, (void *)&name);
            }
        }

        ImGui::Separator();
        ImGui::InputText("UObject name", spawnObject, 250);
        if (ImGui::Button("Spawn Actor")) {
            auto *foundObject = FindUClass(spawnObject);
            if (!foundObject) {
                Framework::Logging::GetLogger("Hooks")->info("Unable to find object !");
                ImGui::End();
                return;
            }

            Framework::Logging::GetLogger("Hooks")->info("Found UObject: {}", narrow(foundObject->GetFName().ToString()).c_str());

            FVector pos = {351002.25f, -463037.25f, -85707.94531f};
            FRotator rot = {0.0f, 0.0f, 0.0f};

            FActorSpawnParameters spawnParams{};
            spawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

            lastActor = UWorld__SpawnActor(*GWorld, foundObject, &pos, &rot, spawnParams);
            if (lastActor != nullptr) {
                spawnedActors.push_back(lastActor);
            }
            Framework::Logging::GetLogger("Hooks")->info("Spawned actor: {}", (void *)lastActor);
        }

        if (ImGui::Button("Destroy Actor")) {
            if (lastActor) {
                // UWorld__DestroyActor(*GWorld, lastActor, false, true);
                // Framework::Logging::GetLogger("Hooks")->info("Destroyed actor: {}", (void *)lastActor);
                // lastActor = nullptr;
                for (auto *actor : spawnedActors) {
                    UWorld__DestroyActor(*GWorld, actor, false, true);
                }
            }
        }

        // Student proxies — no-pak student kit (core/student_proxy.cpp). Buttons
        // only queue a request; the spawn/despawn runs on the game thread in
        // ProcessPending() at the top of Playground_Tick.
        ImGui::Separator();
        ImGui::Text("Students active: %zu", HogwartsMP::Core::StudentProxy::ActiveCount());
        static int studentCount = 1;
        ImGui::SliderInt("Count", &studentCount, 1, 10);
        if (ImGui::Button("Spawn Student(s)")) {
            HogwartsMP::Core::StudentProxy::RequestSpawn(studentCount);
        }
        ImGui::SameLine();
        if (ImGui::Button("Despawn Students")) {
            HogwartsMP::Core::StudentProxy::RequestDespawnAll();
        }

        ImGui::Separator();
        ImGui::TextDisabled("Appearance harvest");
        if (ImGui::Button("Dump Nearby NPC Appearances")) {
            HogwartsMP::Core::AppearanceDump::RequestDump();
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(logs to AppearanceDump channel)");

        ImGui::End();
    });
}

AActor *__fastcall UWorld__SpawnActor_Hook(UWorld *world, UClass *Class, FVector const *Location, FRotator const *Rotation, const FActorSpawnParameters &SpawnParameters) {
    return UWorld__SpawnActor(world, Class, Location, Rotation, SpawnParameters);
}

struct FURL {
    // Protocol, i.e. "unreal" or "http".
    FString Protocol;

    // Optional hostname, i.e. "204.157.115.40" or "unreal.epicgames.com", blank if local.
    FString Host;

    // Optional host port.
    int32_t Port;

    int32_t Valid;

    // Map name, i.e. "SkyCity", default is "Entry".
    FString Map;

    // Optional place to download Map if client does not possess it
    FString RedirectURL;

    // Options.
    TArray<FString> Op;

    // Portal to enter through, default is "".
    FString Portal;
};

typedef void *UEngine;
typedef void *FWorldContext;
typedef bool (__fastcall *UEngine__LoadMap_t)(UEngine *_this, FWorldContext *WorldContext, FURL URL, class UPendingNetGame *Pending, FString &Error);

UEngine__LoadMap_t UEngine__LoadMap_original = nullptr;

bool __fastcall UEngine__LoadMap_Hook(UEngine *_this, FWorldContext *WorldContext, FURL URL, class UPendingNetGame *Pending, FString &Error) {
    if (narrow(URL.Map).find("RootLevel") != std::string::npos) {
        URL.Map = L"/Game/Levels/Overland/Overland";
    }

    return UEngine__LoadMap_original(_this, WorldContext, URL, Pending, Error);
}


static InitFunction init([]() {
    using HogwartsMP::Core::AobFirst;
    using HogwartsMP::Game::gLayout;

    //NOTE: get GObjectArray
    auto Obj_Array_Scan = AobFirst(gLayout.gObjectArray);
    uint8_t *Obj_Array_Bytes = reinterpret_cast<uint8_t *>(Obj_Array_Scan);
    if (Obj_Array_Bytes) {
        GObjectArray = reinterpret_cast<FUObjectArray *>(Obj_Array_Bytes + *(int32_t *)(Obj_Array_Bytes + 3) + 7);
    }

    //NOTE: spawn actor hook
    auto UWorld__SpawnActor_Addr = reinterpret_cast<uint64_t>(AobFirst(gLayout.uworldSpawnActor));
    if (UWorld__SpawnActor_Addr) {
        MH_CreateHook((LPVOID)UWorld__SpawnActor_Addr, &UWorld__SpawnActor_Hook, (LPVOID *)&UWorld__SpawnActor);
    }

    //NOTE: destroy actor
    UWorld__DestroyActor = reinterpret_cast<UWorld__DestroyActor_t>(AobFirst(gLayout.uworldDestroyActor));

    //NOTE: get pointer to world
    auto GWorld_Scan = AobFirst(gLayout.gWorld);
    uint8_t *GWorld_Instruction_Bytes = reinterpret_cast<uint8_t *>(GWorld_Scan);
    if (GWorld_Instruction_Bytes) {
        uint64_t GWorld_Addr = reinterpret_cast<uint64_t>(GWorld_Instruction_Bytes + *(int32_t *)(GWorld_Instruction_Bytes + 3) + 7);
        GWorld = (UWorld **)(GWorld_Addr);
    }

    //NOTE: UEngine::LoadMap hook (optional: dev redirect RootLevel->Overland, no consumer)
    auto UEngine_LoadMap_Addr = reinterpret_cast<uint64_t>(AobFirst(gLayout.uengineLoadMap));
    if (UEngine_LoadMap_Addr) {
        MH_CreateHook((LPVOID)UEngine_LoadMap_Addr, &UEngine__LoadMap_Hook, (LPVOID *)&UEngine__LoadMap_original);
    }
},"Playground");
