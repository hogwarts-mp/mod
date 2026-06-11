#include "playground.h"

#include "application.h"
#include "aob_scan.h"

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

static FUObjectArray *GObjectArray{nullptr};

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

UObjectBase *find_uobject(const char *obj_full_name) {
    static std::unordered_map<std::string, UObjectBase *> obj_map{};
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

std::vector<UObjectBase *> find_uobjects(const char *obj_full_name) {
    std::vector<UObjectBase *> objects{};

    static std::unordered_map<std::string, UObjectBase *> obj_map{};

    for (auto i = 0; i < GObjectArray->GetObjectArrayNum(); ++i) {
        if (auto obj_item = GObjectArray->IndexToObject(i)) {
            if (auto obj_base = obj_item->Object) {
                auto full_name = get_full_name(obj_base);
                if (full_name == obj_full_name) {
                    objects.push_back(obj_base);
                }
            }
        }
    }

    return objects;
}

enum class ESpawnActorCollisionHandlingMethod : uint8_t {
    /** Fall back to default settings. */
    Undefined,
    /** Actor will spawn in desired location, regardless of collisions. */
    AlwaysSpawn,
    /** Actor will try to find a nearby non-colliding location (based on shape components), but will always spawn even
       if one cannot be found. */
    AdjustIfPossibleButAlwaysSpawn,
    /** Actor will try to find a nearby non-colliding location (based on shape components), but will NOT spawn unless
       one is found. */
    AdjustIfPossibleButDontSpawnIfColliding,
    /** Actor will fail to spawn. */
    DontSpawnIfColliding,
};

///* Struct of optional parameters passed to SpawnActor function(s). */
struct FActorSpawnParameters {
    // FActorSpawnParameters();

    /* A name to assign as the Name of the Actor being spawned. If no value is specified, the name of the spawned Actor
     * will be automatically generated using the form [Class]_[Number]. */
    FName Name;

    /* An Actor to use as a template when spawning the new Actor. The spawned Actor will be initialized using the
     * property values of the template Actor. If left NULL the class default object (CDO) will be used to initialize the
     * spawned Actor. */
    AActor *Template;

    /* The Actor that spawned this Actor. (Can be left as NULL). */
    AActor *Owner;

    /* The APawn that is responsible for damage done by the spawned Actor. (Can be left as NULL). */
    APawn *Instigator;

    /* The ULevel to spawn the Actor in, i.e. the Outer of the Actor. If left as NULL the Outer of the Owner is used. If
     * the Owner is NULL the persistent level is used. */
    class ULevel *OverrideLevel;

#if WITH_EDITOR
    /* The UPackage to set the Actor in. If left as NULL the Package will not be set and the actor will be saved in the
     * same package as the persistent level. */
    class UPackage *OverridePackage;

    /* The parent component to set the Actor in. */
    class UChildActorComponent *OverrideParentComponent;

    /** The Guid to set to this actor. Should only be set when reinstancing blueprint actors. */
    FGuid OverrideActorGuid;
#endif

    /** Method for resolving collisions at the spawn point. Undefined means no override, use the actor's setting. */
    ESpawnActorCollisionHandlingMethod SpawnCollisionHandlingOverride;

private:
    friend class UPackageMapClient;

    /* Is the actor remotely owned. This should only be set true by the package map when it is creating an actor on a
     * client that was replicated from the server. */
    uint8_t bRemoteOwned : 1;

public:
    bool IsRemoteOwned() const {
        return bRemoteOwned;
    }

    /* Determines whether spawning will not fail if certain conditions are not met. If true, spawning will not fail
     * because the class being spawned is `bStatic=true` or because the class of the template Actor is not the same as
     * the class of the Actor being spawned. */
    uint8_t bNoFail : 1;

    /* Determines whether the construction script will be run. If true, the construction script will not be run on the
     * spawned Actor. Only applicable if the Actor is being spawned from a Blueprint. */
    uint8_t bDeferConstruction : 1;

    /* Determines whether or not the actor may be spawned when running a construction script. If true spawning will fail
     * if a construction script is being run. */
    uint8_t bAllowDuringConstructionScript : 1;

#if WITH_EDITOR
    /* Determines whether the begin play cycle will run on the spawned actor when in the editor. */
    uint8_t bTemporaryEditorActor : 1;

    /* Determines whether or not the actor should be hidden from the Scene Outliner */
    uint8_t bHideFromSceneOutliner : 1;

    /** Determines whether to create a new package for the actor or not. */
    uint16_t bCreateActorPackage : 1;
#endif

    /* Modes that SpawnActor can use the supplied name when it is not None. */
    enum class ESpawnActorNameMode : uint8 {
        /* Fatal if unavailable, application will assert */
        Required_Fatal,

        /* Report an error return null if unavailable */
        Required_ErrorAndReturnNull,

        /* Return null if unavailable */
        Required_ReturnNull,

        /* If the supplied Name is already in use the generate an unused one using the supplied version as a base */
        Requested
    };

    /* In which way should SpawnActor should treat the supplied Name if not none. */
    ESpawnActorNameMode NameMode;

    /* Flags used to describe the spawned actor/object instance. */
    EObjectFlags ObjectFlags;
};

typedef AActor *(__fastcall *UWorld__SpawnActor_t)(UWorld *world, UClass *Class, FVector const *Location, FRotator const *Rotation, const FActorSpawnParameters &SpawnParameters);
UWorld__SpawnActor_t UWorld__SpawnActor = nullptr;

typedef bool *(__fastcall *UWorld__DestroyActor_t)(UWorld *world, AActor *ThisActor, bool bNetForce, bool bShouldModifyLevel);
UWorld__DestroyActor_t UWorld__DestroyActor = nullptr;

UWorld **GWorld = nullptr;

using namespace Framework::Utils::StringUtils;

void Playground_Tick() {
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
            UClass *fastTravelManager = (UClass *)find_uobject("Class /Script/Phoenix.FastTravelManager");
            UFunction *fastTravelManagerGetter = (UFunction *)find_uobject("Function /Script/Phoenix.FastTravelManager.Get");

            UClass *fastTravelmanagerInsance{nullptr};
            fastTravelManager->ProcessEvent(fastTravelManagerGetter, (void *)&fastTravelmanagerInsance);

            if (fastTravelmanagerInsance) {
                auto wideTeleportLocation = NormalToWide(teleportLocation);
                FString name(wideTeleportLocation.c_str());
                Framework::Logging::GetLogger("Hooks")->info("Teleporting to {}, instance: {} !", teleportLocation, (void *)fastTravelmanagerInsance);

                UFunction *fastTravelTo = (UFunction *)find_uobject("Function /Script/Phoenix.FastTravelManager.FastTravel_To");
                fastTravelmanagerInsance->ProcessEvent(fastTravelTo, (void *)&name);
            }
        }

        ImGui::Separator();
        ImGui::InputText("UObject name", spawnObject, 250);
        if (ImGui::Button("Spawn Actor")) {
            auto *foundObject = (UClass *)find_uobject(spawnObject);
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


typedef UObject *(__fastcall *StaticConstructObject_Internal_t)(const FStaticConstructObjectParameters &Params);
StaticConstructObject_Internal_t StaticConstructObject_Internal_original = nullptr;

UObject *__fastcall StaticConstructObject_Internal_Hook(const FStaticConstructObjectParameters &Params) {
    auto res = StaticConstructObject_Internal_original(Params);
    return res;
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

    //NOTE: create static object hook
    auto StaticConstructObject_Internal_Addr = reinterpret_cast<uint64_t>(AobFirst(gLayout.staticConstructObject));
    if (StaticConstructObject_Internal_Addr) {
        MH_CreateHook((LPVOID)StaticConstructObject_Internal_Addr, &StaticConstructObject_Internal_Hook, (LPVOID *)&StaticConstructObject_Internal_original);
    }

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
