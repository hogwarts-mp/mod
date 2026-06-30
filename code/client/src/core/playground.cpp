#include "playground.h"

#include "appearance_dump.h"
#include "application.h"
#include "aob_scan.h"
#include "broom_experiment.h"
#include "character_creator.h"
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

#include <mutex>
#include <optional>
#include <vector>

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

// Dev-menu spawn/destroy requests, queued from the HUD event bridge and drained
// on the game thread below. Spawn uses a fixed dev location; actors spawned this
// way are tracked so Destroy can clean them all up.
static std::mutex g_pgMutex;
static std::optional<std::string> g_pendingSpawn;
static bool g_pendingDestroy = false;
static std::vector<AActor *> g_spawnedActors;

void Playground_Tick() {
    // Game-thread pump: SpawnActor / StaticLoadObject / ProcessEvent must run
    // here, not from the CEF event callbacks. The dev menu only queues requests.
    HogwartsMP::Core::StudentProxy::ProcessPending();
    HogwartsMP::Core::AppearanceDump::ProcessPending();
    HogwartsMP::Core::BroomExperiment::ProcessPending();
    HogwartsMP::Core::CharacterCreator::ProcessPending();

    std::optional<std::string> spawn;
    bool destroy = false;
    {
        std::lock_guard<std::mutex> lock(g_pgMutex);
        spawn.swap(g_pendingSpawn);
        destroy          = g_pendingDestroy;
        g_pendingDestroy = false;
    }

    if (spawn) {
        auto *foundObject = FindUClass(spawn->c_str());
        if (!foundObject) {
            Framework::Logging::GetLogger("Hooks")->info("Unable to find object: {}", *spawn);
        }
        else {
            Framework::Logging::GetLogger("Hooks")->info("Found UObject: {}", narrow(foundObject->GetFName().ToString()).c_str());

            FVector pos  = {351002.25f, -463037.25f, -85707.94531f};
            FRotator rot = {0.0f, 0.0f, 0.0f};

            FActorSpawnParameters spawnParams {};
            spawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

            AActor *actor = UWorld__SpawnActor(*GWorld, foundObject, &pos, &rot, spawnParams);
            if (actor) {
                g_spawnedActors.push_back(actor);
            }
            Framework::Logging::GetLogger("Hooks")->info("Spawned actor: {}", (void *)actor);
        }
    }

    if (destroy) {
        for (auto *actor : g_spawnedActors) {
            UWorld__DestroyActor(*GWorld, actor, false, true);
        }
        g_spawnedActors.clear();
    }
}

namespace HogwartsMP::Core::Playground {
    void RequestSpawnActor(const std::string &objectPath) {
        std::lock_guard<std::mutex> lock(g_pgMutex);
        g_pendingSpawn = objectPath;
    }

    void RequestDestroyActors() {
        std::lock_guard<std::mutex> lock(g_pgMutex);
        g_pendingDestroy = true;
    }
} // namespace HogwartsMP::Core::Playground

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
