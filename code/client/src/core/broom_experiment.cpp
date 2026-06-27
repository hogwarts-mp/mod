// Broom rider for remote-avatar proxies — see broom_experiment.h.
//
// The broom is per-rider state: spawned on mount, destroyed on dismount.
// Everything runs through the game's own reflection (ProcessEvent by name,
// FProperty reads); the only natives used are the already-sigscanned
// UWorld::SpawnActor / DestroyActor pair.
//
// Key game facts this relies on:
//   - Broom inventory items (BP_Broom*Item_C) carry the rideable actor class
//     in their FlyingBroomClass property — readable from the class defaults,
//     so the item never needs to be spawned.
//   - The broom mesh has a "playerAttachSocket" socket: attaching a character
//     there with SnapToTarget seats it correctly and follows the hover-bob.
//   - Rider animations are standalone AnimSequences under
//     /Game/Animation/Human/Hu_Broom_*; played raw on the proxy's body mesh
//     (CharacterMesh0). PlayAnimation puts the mesh in single-node mode, which
//     suppresses the proxy's baked AnimBP for the duration — fine while
//     mounted (the broom drives the transform).

#include <utils/safe_win32.h>

#include "broom_experiment.h"

#include "application.h"
#include "student_proxy.h"
#include "sdk/natives/ue4_natives.h"
#include "sdk/reflection/ue4_reflection.h"

#include <logging/logger.h>

#include "UObject/Class.h"
#include "UObject/UObjectArray.h"
#include "UObject/UnrealType.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
    using namespace HogwartsMP::Core::UE4;
    using HogwartsMP::Core::gGlobals;

    auto BroomLog() {
        return Framework::Logging::GetLogger("Broom");
    }

    // Known-good broom item classes (the scan below prefers whatever is
    // already resident, e.g. the broom the local player owns).
    const wchar_t *ITEM_CLASS_PATHS[] = {
        L"/Game/Gameplay/ToolSet/Items/InventoryItems/Broom/BP_BroomHouseItem.BP_BroomHouseItem_C",
        L"/Game/Gameplay/ToolSet/Items/InventoryItems/Broom/BP_BroomMoonTrimmerItem.BP_BroomMoonTrimmerItem_C",
    };
    // Rideable broom classes for the no-item direct route.
    const wchar_t *BROOM_CLASS_PATHS[] = {
        L"/Game/Pawn/Player/Broom/Blueprints/Brooms/BP_FlyingBroomCapsule.BP_FlyingBroomCapsule_C",
        L"/Game/Pawn/Player/Broom/Blueprints/Brooms/BP_FlyingBroomProp.BP_FlyingBroomProp_C",
    };

    std::atomic<bool> g_mountRequested{false};
    std::atomic<bool> g_dismountRequested{false};
    std::atomic<bool> g_cleanupRequested{false};

    // One-shot anim followup: play X once, then switch to Y when X ends
    // (mount -> hover idle, dismount -> standing idle). Game-thread only.
    UObjectBase *g_followupAnim = nullptr;
    bool g_followupLoop = true;
    std::chrono::steady_clock::time_point g_followupAt{};
    bool g_followupPending = false;

    // Picked by the riding-pose scan, reused by the dismount.
    UObjectBase *g_idleSeq = nullptr;
    UObjectBase *g_dismountSeq = nullptr;

    struct ActorRef {
        AActor *actor = nullptr;
        int32_t objectIndex = -1;
    };
    ActorRef g_broom;
    char g_status[160] = "idle";

    // Same weak-ref discipline as StudentProxy::ResolveAlive, minus the class
    // check.
    AActor *ResolveAlive(const ActorRef &ref) {
        auto *arr = gGlobals.objectArray;
        if (!arr || !ref.actor) {
            return nullptr;
        }
        auto *item = arr->IndexToObject(ref.objectIndex);
        if (!item || item->Object != reinterpret_cast<UObjectBase *>(ref.actor)) {
            return nullptr;
        }
        return ref.actor;
    }

    ActorRef MakeRef(AActor *actor) {
        return {actor, actor ? static_cast<int32_t>(reinterpret_cast<UObjectBase *>(actor)->GetUniqueID()) : -1};
    }

    bool ContainsCI(const std::string &haystack, const char *needle) {
        std::string lower(haystack);
        for (auto &c : lower) {
            c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
        }
        return lower.find(needle) != std::string::npos;
    }

    // Prefer a broom class that is already resident in GObjects (its real
    // path included) over loading by hardcoded path. Only spawnable picks:
    // the inventory items (FlyingBroomClass carriers) or the rideable
    // capsule itself — the name filter also matches UI/VFX/spline classes,
    // which must not be spawned.
    UClass *ScanResidentBroomClasses() {
        auto *arr = gGlobals.objectArray;
        if (!arr) {
            return nullptr;
        }
        UClass *houseItem = nullptr, *anyItem = nullptr, *capsule = nullptr;
        const int num = arr->GetObjectArrayNum();
        for (int i = 0; i < num; ++i) {
            auto *item = arr->IndexToObject(i);
            if (!item || !item->Object) {
                continue;
            }
            auto *obj = item->Object;
            auto *cls = obj->GetClass();
            if (!cls) {
                continue;
            }
            // UClass instances only (class-of name carries "Class"); CDOs and
            // actors fall out naturally.
            if (!ContainsCI(narrow(cls->GetFName()), "class")) {
                continue;
            }
            const auto name = narrow(obj->GetFName());
            if (!ContainsCI(name, "broom")) {
                continue;
            }
            if (name == "BP_BroomHouseItem_C") {
                houseItem = reinterpret_cast<UClass *>(obj);
            }
            else if (name == "BP_FlyingBroomCapsule_C") {
                capsule = reinterpret_cast<UClass *>(obj);
            }
            else if (!anyItem && name.rfind("BP_Broom", 0) == 0 && name.size() > 6 && name.compare(name.size() - 6, 6, "Item_C") == 0) {
                anyItem = reinterpret_cast<UClass *>(obj);
            }
        }
        return houseItem ? houseItem : (anyItem ? anyItem : capsule);
    }

    // Only dereference real object-pointer properties. Soft/struct/array
    // properties store inline data at the offset, not a UObject*.
    UObjectBase *GetObjectProperty(void *obj, const char *name) {
        auto *prop = FindPropertyInChain(static_cast<UObjectBase *>(obj)->GetClass(), name);
        if (!prop) {
            return nullptr;
        }
        const auto type = narrow(prop->GetClass()->GetFName());
        if (type != "ObjectProperty" && type != "ClassProperty") {
            BroomLog()->warn("property {} is {} — not a raw object pointer, skipping", name, type);
            return nullptr;
        }
        return *reinterpret_cast<UObjectBase **>(reinterpret_cast<uint8_t *>(obj) + prop->GetOffset_ForInternal());
    }

    // Resident UClass lookup by short name (e.g. a broom class name received
    // over the wire).
    UClass *FindResidentClassByName(const char *shortName) {
        auto *arr = gGlobals.objectArray;
        if (!arr || !shortName || !*shortName) {
            return nullptr;
        }
        const int num = arr->GetObjectArrayNum();
        for (int i = 0; i < num; ++i) {
            auto *item = arr->IndexToObject(i);
            if (!item || !item->Object) {
                continue;
            }
            auto *obj = item->Object;
            auto *cls = obj->GetClass();
            if (!cls || !ContainsCI(narrow(cls->GetFName()), "class")) {
                continue;
            }
            if (narrow(obj->GetFName()) == shortName) {
                return reinterpret_cast<UClass *>(obj);
            }
        }
        return nullptr;
    }

    float GetAnimLength(UObjectBase *seq) {
        struct {
            float ReturnValue;
        } p{};
        if (!CallUFunction(seq, "GetPlayLength", &p) || p.ReturnValue <= 0.f) {
            return 3.f;
        }
        return p.ReturnValue;
    }

    void PlayOnSkin(UObjectBase *skin, UObjectBase *seq, bool loop) {
        if (!skin || !seq) {
            return;
        }
        struct {
            UObjectBase *NewAnimToPlay;
            bool bLooping;
        } play{seq, loop};
        CallUFunction(skin, "PlayAnimation", &play);
        BroomLog()->info("playing {} (loop={})", narrow(seq->GetFName()), loop);
    }

    void PlayOnRiderSkin(UObjectBase *seq, bool loop) {
        PlayOnSkin(HogwartsMP::Core::StudentProxy::FirstActiveSkin(), seq, loop);
    }

    void ScheduleFollowup(UObjectBase *afterSeq, UObjectBase *thenSeq, bool loop) {
        g_followupAnim = thenSeq;
        g_followupLoop = loop;
        g_followupAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(static_cast<int64_t>(GetAnimLength(afterSeq) * 1000.f));
        g_followupPending = thenSeq != nullptr;
    }

    void SetStatus(const char *fmt, ...) {
        va_list args;
        va_start(args, fmt);
        std::vsnprintf(g_status, sizeof(g_status), fmt, args);
        va_end(args);
        BroomLog()->info("status: {}", g_status);
    }

    UClass *ResolveBroomClass() {
        auto *itemCls = ScanResidentBroomClasses();
        if (!itemCls) {
            auto *uclassCls = FindUClass("Class /Script/CoreUObject.Class");
            for (const auto *path : ITEM_CLASS_PATHS) {
                itemCls = reinterpret_cast<UClass *>(LoadObjectByPath(uclassCls, path));
                if (itemCls) {
                    break;
                }
            }
            // No item loadable -> the rideable broom class directly.
            if (!itemCls) {
                for (const auto *path : BROOM_CLASS_PATHS) {
                    itemCls = reinterpret_cast<UClass *>(LoadObjectByPath(uclassCls, path));
                    if (itemCls) {
                        break;
                    }
                }
            }
        }
        if (!itemCls) {
            return nullptr;
        }

        // The item's class defaults already carry FlyingBroomClass — no need
        // to spawn the item actor at all.
        if (FindPropertyInChain(itemCls, "FlyingBroomClass")) {
            if (auto *cdo = reinterpret_cast<UObjectBase *>(itemCls->ClassDefaultObject)) {
                if (auto *broomCls = reinterpret_cast<UClass *>(GetObjectProperty(cdo, "FlyingBroomClass"))) {
                    BroomLog()->info("FlyingBroomClass from item defaults: {}", narrow(broomCls->GetFName()));
                    return broomCls;
                }
            }
            BroomLog()->warn("item defaults had no FlyingBroomClass value — using item class itself");
        }
        // Scan fallback may already be the rideable class.
        return itemCls;
    }

    // ── Riding pose ──────────────────────────────────────────────────────────
    // Riding AnimSequences played raw on the body mesh. The rider anims live in
    // the Hu_Broom_*NoStirrups_* family; Idlebreak variants (waving etc.) are
    // excluded from the steady idle pick.

    // Fills g_idleSeq/g_dismountSeq from resident sequences (idempotent);
    // returns the optional mount-transition pick.
    UObjectBase *ScanRiderSeqs() {
        auto *arr = gGlobals.objectArray;
        UObjectBase *mountSeq = nullptr, *fallback = nullptr;
        const int num = arr ? arr->GetObjectArrayNum() : 0;
        for (int i = 0; i < num; ++i) {
            auto *item = arr->IndexToObject(i);
            if (!item || !item->Object) {
                continue;
            }
            auto *obj = item->Object;
            auto *cls = obj->GetClass();
            if (!cls || narrow(cls->GetFName()) != "AnimSequence") {
                continue;
            }
            const auto name = narrow(obj->GetFName());
            if (!ContainsCI(name, "nostirrups")) {
                continue;
            }
            if (ContainsCI(name, "dismount")) {
                if (!g_dismountSeq) {
                    g_dismountSeq = obj;
                }
            }
            else if (ContainsCI(name, "mount")) {
                if (!mountSeq) {
                    mountSeq = obj;
                }
            }
            else if (ContainsCI(name, "idle") && !ContainsCI(name, "idlebreak")) {
                // Prefer the straight hover idle over directional (_Lft/_Rht/
                // _Up/_Dn) and additive variants.
                if (!g_idleSeq || ContainsCI(name, "hover_idle_anm")) {
                    g_idleSeq = obj;
                }
            }
            if (!fallback) {
                fallback = obj;
            }
        }
        if (!g_idleSeq) {
            g_idleSeq = fallback;
        }
        return mountSeq;
    }

    // Spawn a broom of the given class at the rider and seat them on it
    // (attach to the broom MESH at its seat socket so the rider follows the
    // hover-bob).
    AActor *SpawnAndSeat(AActor *rider, UClass *broomCls) {
        if (!rider || !broomCls || !GWorld || !*GWorld || !UWorld__SpawnActor) {
            return nullptr;
        }
        struct {
            float X, Y, Z;
        } loc{};
        struct {
            float Pitch, Yaw, Roll;
        } rot{};
        CallUFunction(rider, "K2_GetActorLocation", &loc);
        CallUFunction(rider, "K2_GetActorRotation", &rot);
        FVector pos{loc.X, loc.Y, loc.Z + 50.f};
        FRotator spawnRot{0.f, rot.Yaw, 0.f};
        FActorSpawnParameters spawnParams{};
        spawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto *broom = UWorld__SpawnActor(*GWorld, broomCls, &pos, &spawnRot, spawnParams);
        if (!broom) {
            return nullptr;
        }

        UObjectBase *broomMesh = GetObjectProperty(broom, "BroomMeshComponent");
        if (!broomMesh) {
            auto *smcCls = FindUClass("Class /Script/Engine.SkeletalMeshComponent");
            struct {
                UClass *ComponentClass;
                UObjectBase *ReturnValue;
            } getMesh{smcCls, nullptr};
            CallUFunction(broom, "GetComponentByClass", &getMesh);
            broomMesh = getMesh.ReturnValue;
        }
        if (!broomMesh) {
            BroomLog()->warn("no broom mesh component — destroying broom");
            UWorld__DestroyActor(*GWorld, broom, false, true);
            return nullptr;
        }

        struct {
            bool b;
        } off{false};
        CallUFunction(rider, "SetActorEnableCollision", &off);

        struct {
            UObjectBase *Parent;       // 0
            FName SocketName;          // 8
            uint8_t LocationRule;      // 16 (EAttachmentRule: 2 = SnapToTarget)
            uint8_t RotationRule;      // 17
            uint8_t ScaleRule;         // 18 (1 = KeepWorld)
            bool bWeldSimulatedBodies; // 19
        } att{broomMesh, MakeFName(L"playerAttachSocket"), 2, 2, 1, false};
        CallUFunction(rider, "K2_AttachToComponent", &att);

        struct {
            UObjectBase *ReturnValue;
        } parent{};
        CallUFunction(rider, "GetAttachParentActor", &parent);
        if (!parent.ReturnValue) {
            BroomLog()->warn("seat attach failed — destroying broom");
            UWorld__DestroyActor(*GWorld, broom, false, true);
            return nullptr;
        }
        return broom;
    }

    void FixRiderPoseNow() {
        auto *rider = HogwartsMP::Core::StudentProxy::FirstActive();
        auto *skin  = HogwartsMP::Core::StudentProxy::FirstActiveSkin();
        if (!rider || !skin) {
            SetStatus("no live student/mesh component");
            return;
        }

        auto *mountSeq = ScanRiderSeqs();
        auto *idleSeq  = g_idleSeq;
        auto *arr      = gGlobals.objectArray;
        const int num  = arr ? arr->GetObjectArrayNum() : 0;
        // The mount transition is not always resident with the rest of the
        // family — broader pass: any rider AnimSequence (Hu_*) with "mount".
        if (!mountSeq) {
            for (int i = 0; i < num; ++i) {
                auto *item = arr->IndexToObject(i);
                if (!item || !item->Object) {
                    continue;
                }
                auto *obj = item->Object;
                auto *cls = obj->GetClass();
                if (!cls || narrow(cls->GetFName()) != "AnimSequence") {
                    continue;
                }
                const auto name = narrow(obj->GetFName());
                if (!ContainsCI(name, "mount") || ContainsCI(name, "dismount")) {
                    continue;
                }
                if (!ContainsCI(name, "broom") && !ContainsCI(name, "hover")) {
                    continue;
                }
                if (name.rfind("Hu_", 0) == 0) {
                    mountSeq = obj;
                    BroomLog()->info("mount transition: {}", narrow(obj->GetFName()));
                    break;
                }
            }
        }
        if (!mountSeq && !idleSeq) {
            SetStatus("no riding anims resident");
            return;
        }

        if (mountSeq) {
            PlayOnRiderSkin(mountSeq, false);
            ScheduleFollowup(mountSeq, idleSeq, true);
            SetStatus("mounted (%s -> idle)", narrow(mountSeq->GetFName()).c_str());
        }
        else {
            PlayOnRiderSkin(idleSeq, true);
            SetStatus("mounted (%s)", narrow(idleSeq->GetFName()).c_str());
        }
    }

    // ── Mount: spawn broom at the rider + seat + anims ───────────────────────

    void MountStudentNow() {
        if (!GWorld || !*GWorld || !UWorld__SpawnActor) {
            SetStatus("no world / SpawnActor unresolved");
            return;
        }
        auto *rider = HogwartsMP::Core::StudentProxy::FirstActive();
        if (!rider) {
            SetStatus("no live student — spawn one first");
            return;
        }
        if (ResolveAlive(g_broom)) {
            SetStatus("already mounted — dismount first");
            return;
        }

        auto *broomCls = ResolveBroomClass();
        if (!broomCls) {
            SetStatus("broom class not found");
            return;
        }

        auto *broom = SpawnAndSeat(rider, broomCls);
        if (!broom) {
            SetStatus("mount failed — see log");
            return;
        }
        g_broom = MakeRef(broom);

        FixRiderPoseNow();
    }

    // ── Dismount: anim + detach + destroy broom ──────────────────────────────

    void DismountNow() {
        auto *rider = HogwartsMP::Core::StudentProxy::FirstActive();
        if (!rider) {
            SetStatus("no live student");
            return;
        }
        // Reuse the real dismount path (detach + standing idle + destroy broom)
        // instead of duplicating it.
        HogwartsMP::Core::BroomRider::Dismount(rider, HogwartsMP::Core::StudentProxy::FirstActiveSkin(), ResolveAlive(g_broom));
        g_broom = {};
        SetStatus("dismounted");
    }

    void CleanupNow() {
        if (auto *broom = ResolveAlive(g_broom)) {
            if (GWorld && *GWorld && UWorld__DestroyActor) {
                UWorld__DestroyActor(*GWorld, broom, false, true);
            }
        }
        g_broom = {};
        g_idleSeq = nullptr;
        g_dismountSeq = nullptr;
        g_followupAnim = nullptr;
        g_followupPending = false;
        SetStatus("cleaned up");
    }
} // namespace

namespace HogwartsMP::Core::BroomRider {
    AActor *Mount(AActor *rider, UObjectBase *animMesh, const char *broomClassName) {
        auto *cls = FindResidentClassByName(broomClassName);
        if (!cls) {
            cls = ResolveBroomClass();
        }
        if (!cls) {
            BroomLog()->warn("no broom class available for rider mount");
            return nullptr;
        }
        auto *broom = SpawnAndSeat(rider, cls);
        if (!broom) {
            return nullptr;
        }
        // SpawnAndSeat's SnapToTarget attach leaves the BP_RemoteAvatarCCC rider mis-seated: its mesh sits
        // ~one capsule-height below the broom and yawed ~90° off the broom's forward. Correct both as a
        // relative offset on the seated rider (the broom's hover-bob still propagates the relative each
        // frame). TUNE THESE in-game per the proxy/broom geometry.
        constexpr float kSeatLiftZ  = 90.0f; // lift the rider up onto the broom
        constexpr float kSeatYawDeg = 90.0f; // cancel the mesh's yaw offset vs the broom forward
        if (auto *root = ReadObjectProperty(rider, "RootComponent")) {
            SetVec3Property(root, "RelativeLocation", 0.0f, 0.0f, kSeatLiftZ);
            SetVec3Property(root, "RelativeRotation", 0.0f, kSeatYawDeg, 0.0f);
        }
        ScanRiderSeqs();
        PlayOnSkin(animMesh, g_idleSeq, true);
        return broom;
    }

    void Dismount(AActor *rider, UObjectBase *animMesh, AActor *broom) {
        if (rider) {
            struct {
                uint8_t LocationRule; // EDetachmentRule: 1 = KeepWorld
                uint8_t RotationRule;
                uint8_t ScaleRule;
            } det{1, 1, 1};
            CallUFunction(rider, "K2_DetachFromActor", &det);
        }
        if (animMesh) {
            // Standing idle clip (single-node). NOTE: this does not restore the
            // proxy's baked AnimBP — locomotion stays frozen after a dismount;
            // restoring the AnimBP is a follow-up.
            auto *seqCls    = FindUClass("Class /Script/Engine.AnimSequence");
            auto *standIdle = LoadObjectByPath(seqCls, L"/Game/Animation/Human/Student/Student_F/StuF_BM_Idle_Loop_Stand_anm.StuF_BM_Idle_Loop_Stand_anm");
            PlayOnSkin(animMesh, standIdle, true);
        }
        if (broom && GWorld && *GWorld && UWorld__DestroyActor) {
            UWorld__DestroyActor(*GWorld, broom, false, true);
        }
    }
} // namespace HogwartsMP::Core::BroomRider

namespace HogwartsMP::Core::BroomExperiment {
    void RequestMountStudent() {
        g_mountRequested = true;
    }

    void RequestDismountStudent() {
        g_dismountRequested = true;
    }

    void RequestCleanup() {
        g_cleanupRequested = true;
    }

    void ProcessPending() {
        if (g_cleanupRequested.exchange(false)) {
            CleanupNow();
        }
        if (g_mountRequested.exchange(false)) {
            MountStudentNow();
        }
        if (g_dismountRequested.exchange(false)) {
            DismountNow();
        }
        if (g_followupPending && std::chrono::steady_clock::now() >= g_followupAt) {
            g_followupPending = false;
            PlayOnRiderSkin(g_followupAnim, g_followupLoop);
        }
    }

    const char *Status() {
        return g_status;
    }
} // namespace HogwartsMP::Core::BroomExperiment
