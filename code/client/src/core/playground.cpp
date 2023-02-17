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

typedef AActor *(__fastcall *UWorld__SpawnActor_t)(UWorld *world, UClass *Class, FTransform const *UserTransformPtr, const FActorSpawnParameters &SpawnParameters);
UWorld__SpawnActor_t UWorld__SpawnActor = nullptr;

UWorld **GWorld = nullptr;

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

        auto *new_object = UWorld__SpawnActor(*GWorld, found_object, &transform, spawnParams);
        Framework::Logging::GetLogger("Hooks")->info("Spawned actor: {}", (void *)found_object);
    }
}

AActor *__fastcall UWorld__SpawnActor_Hook(UWorld *world, UClass *Class, FTransform const *UserTransformPtr, const FActorSpawnParameters &SpawnParameters) {
    //Framework::Logging::GetLogger("Hooks")->info("Spawned actor class: {} name: {}", narrow(Class->GetFName().ToString()).c_str(), narrow(SpawnParameters.Name.ToString()));
    return UWorld__SpawnActor(world, Class, UserTransformPtr, SpawnParameters);
}

static InitFunction init([]() {
    //NOTE: get GObjectArray
    auto Obj_Array_Scan      = hook::pattern("48 8D 0D ? ? ? ? E8 ? ? ? ? 48 8D 8D A0 02 00 00").get_first();
    uint8_t *Obj_Array_Bytes = reinterpret_cast<uint8_t *>(Obj_Array_Scan);
    GObjectArray             = reinterpret_cast<FUObjectArray *>(Obj_Array_Bytes + *(int32_t *)(Obj_Array_Bytes + 3) + 7);

    auto UWorld__SpawnActor_Addr = reinterpret_cast<uint64_t>(hook::pattern("40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 08 FF FF FF 48 81 EC F8 01 00 00 "
                                                                            "48 8B 05 ? ? ? ? 48 33 C4 48 89 45")
                                                                  .get_first());

    MH_CreateHook((LPVOID)UWorld__SpawnActor_Addr, &UWorld__SpawnActor_Hook, (LPVOID *)&UWorld__SpawnActor);

    //NOTE: get pointer to world
    auto GWorld_Scan                  = hook::pattern("48 8B 1D ? ? ? ? 48 85 DB 74 3B 41 B0 01").get_first();
    uint8_t *GWorld_Instruction_Bytes = reinterpret_cast<uint8_t *>(GWorld_Scan);
    uint64_t GWorld_Addr              = reinterpret_cast<uint64_t>(GWorld_Instruction_Bytes + *(int32_t *)(GWorld_Instruction_Bytes + 3) + 7);
    GWorld                            = (UWorld **)(GWorld_Addr);
});
