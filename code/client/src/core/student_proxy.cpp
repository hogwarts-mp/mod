// Remote-avatar proxy spawner. Spawns BP_RemoteAvatarCCC (from the mounted pak) — an HL Biped_Character
// subclass with an SCS CustomizableCharacterComponent + a baked AnimBP, so it renders + idles immediately
// (no AI/population-pipeline or LOD-streaming delay). Appearance is layered on via CcdWire. SpawnGroup backs
// the dev "Spawn Student(s)" button; SpawnProxy is the production remote-player entry (modules/human.cpp).

#include <utils/safe_win32.h>

#include "student_proxy.h"

#include "application.h"
#include "ccd_wire.h"
#include "sdk/natives/ue4_natives.h"
#include "sdk/reflection/ue4_reflection.h"

#include <logging/logger.h>

#include "UObject/Class.h"
#include "UObject/UObjectArray.h"
#include "UObject/UnrealType.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
    using HogwartsMP::Core::gGlobals;
    using namespace HogwartsMP::Core::UE4;

    auto Log() {
        return Framework::Logging::GetLogger("StudentProxy");
    }


    constexpr float kPi = 3.14159265358979f;

    // Sanity cap per spawn request (one button press / one RequestSpawn call).
    constexpr int kMaxSpawnPerRequest = 25;

    // ── Registry — destroy must survive level changes that GC our actors ────

    struct StudentRef {
        AActor *actor;
        int32_t objectIndex;   // GUObjectArray slot (UObjectBase::GetUniqueID)
        int32_t serialNumber;  // FUObjectItem serial at spawn (0 = none allocated yet)
        UObjectBase *skinComp; // lifetime tied to actor
    };
    // Game thread only; the render thread sees the size via g_activeCount.
    std::vector<StudentRef> g_students;
    std::atomic<size_t> g_activeCount{0};
    std::atomic<int> g_spawnPending{0};
    std::atomic<bool> g_despawnRequested{false};

    void PublishActiveCount() {
        g_activeCount.store(g_students.size(), std::memory_order_relaxed);
    }

    // Weak-ref style resolution: only trust the stored pointer if the object
    // array still has it at the recorded index, it is not dying, the serial
    // number still matches, AND it is still our class (guards against
    // slot+address reuse after a GC).
    AActor *ResolveAlive(const StudentRef &ref) {
        auto *arr = gGlobals.objectArray;
        if (!arr || !ref.actor) {
            return nullptr;
        }
        auto *item = arr->IndexToObject(ref.objectIndex);
        if (!item || item->Object != reinterpret_cast<UObjectBase *>(ref.actor)) {
            return nullptr;
        }
        if (item->IsPendingKill() || item->IsUnreachable()) {
            return nullptr;
        }
        // The serial is reset to 0 on death and reallocated on the next
        // weak-ref, so a nonzero stored value pins the exact object. A stored
        // 0 seeing a nonzero serial just means the engine weak-referenced our
        // still-alive actor in the meantime — not a recycle.
        if (ref.serialNumber != 0 && item->GetSerialNumber() != ref.serialNumber) {
            return nullptr;
        }
        if (narrow(reinterpret_cast<UObjectBase *>(ref.actor)->GetClass()->GetFName()) != "Biped_Character") {
            return nullptr;
        }
        return ref.actor;
    }


    // Spawn BP_RemoteAvatarCCC from the mounted pak. Extends HL's Biped_Character (so its SCS CCC builds)
    // and ships a baked AnimBP, so it spawns + idles with no architect/streaming-starvation delay. Appearance
    // is layered on later; here we seed a base id to render. Mesh transform is baked into the BP.
    AActor *SpawnCccProxy(const FVector &pos, float yawDeg, UObjectBase **outBodyMesh) {
        if (outBodyMesh) {
            *outBodyMesh = nullptr;
        }
        if (!GWorld || !*GWorld || !UWorld__SpawnActor) {
            Log()->error("SpawnCccProxy: no world / SpawnActor not resolved");
            return nullptr;
        }

        // Load our pak pawn class. Requires RemoteAvatar_P.pak mounted (pak_mount hook).
        auto *classMeta = FindUClass("Class /Script/CoreUObject.Class");
        auto *cls       = classMeta ? reinterpret_cast<UClass *>(
                                    LoadObjectByPath(classMeta, L"/Game/Avatar/BP_RemoteAvatarCCC.BP_RemoteAvatarCCC_C"))
                                    : nullptr;
        if (!cls) {
            Log()->error("SpawnCccProxy: BP_RemoteAvatarCCC class not loaded (RemoteAvatar_P.pak mounted?)");
            return nullptr;
        }

        // Raw-spawning a Biped_Character-derived BP crashes on AI-controller init. Disable AutoPossessAI
        // on the CDO (the instance copies CDO values before BeginPlay), then restore so the game's own
        // NPC spawns are unaffected.
        uint8_t *aiByte = nullptr;
        uint8_t aiSaved = 0;
        if (auto *cdo = reinterpret_cast<UObjectBase *>(cls->ClassDefaultObject)) {
            if (auto *p = FindPropertyInChain(cls, "AutoPossessAI")) {
                aiByte  = reinterpret_cast<uint8_t *>(cdo) + p->GetOffset_ForInternal();
                aiSaved = *aiByte;
                *aiByte = 0; // EAutoPossessAI::Disabled
            }
        }

        FVector loc = pos;
        FRotator rot{0.f, yawDeg, 0.f};
        FActorSpawnParameters spawnParams{};
        spawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Log()->info("SpawnCccProxy: spawning BP_RemoteAvatarCCC (AutoPossessAI {})", aiByte ? "disabled" : "n/a");
        auto *actor = UWorld__SpawnActor(*GWorld, cls, &loc, &rot, spawnParams);
        if (aiByte) {
            *aiByte = aiSaved; // restore the CDO
        }
        if (!actor) {
            Log()->error("SpawnCccProxy: SpawnActor failed");
            return nullptr;
        }

        // Scenery proxy: no collision, no fall. Tick STAYS ON so the baked AnimBP animates.
        struct {
            bool b;
        } off{false};
        CallUFunction(actor, "SetActorEnableCollision", &off);
        if (auto *cmcCls = FindUClass("Class /Script/Engine.CharacterMovementComponent")) {
            struct {
                UClass *ComponentClass;
                UObjectBase *ReturnValue;
            } getCmc{cmcCls, nullptr};
            CallUFunction(actor, "GetComponentByClass", &getCmc);
            if (getCmc.ReturnValue) {
                CallUFunction(getCmc.ReturnValue, "DisableMovement", nullptr);
                SetFloatProperty(getCmc.ReturnValue, "GravityScale", 0.f);
            }
        }

        // Seed the SCS-baked CCC so it renders the base character (the appearance wire overrides it later).
        if (auto *cccCls = FindUClass("Class /Script/CustomizableCharacter.CustomizableCharacterComponent")) {
            struct {
                UClass *ComponentClass;
                UObjectBase *ReturnValue;
            } getCcc{cccCls, nullptr};
            CallUFunction(actor, "GetComponentByClass", &getCcc);
            if (getCcc.ReturnValue) {
                // "SebastianSallow" is a real shipped HL NPC id: it builds a COMPLETE base CCC so the
                // proxy renders a full character immediately. The appearance wire (a later commit)
                // replaces it with the actual remote player's CCD.
                FName cid = MakeFName(L"SebastianSallow");
                CallUFunction(getCcc.ReturnValue, "SetCharacterID", &cid);
                Log()->info("SpawnCccProxy: seeded SCS CCC with base id");
            }
            else {
                Log()->warn("SpawnCccProxy: no SCS CCC on the pawn (renders default)");
            }
        }

        // Hand back CharacterMesh0 for the caller (appearance apply, later commit).
        if (auto *smcCls = FindUClass("Class /Script/Engine.SkeletalMeshComponent")) {
            struct {
                UClass *ComponentClass;
                UObjectBase *ReturnValue;
            } getMesh{smcCls, nullptr};
            CallUFunction(actor, "GetComponentByClass", &getMesh);
            if (outBodyMesh) {
                *outBodyMesh = getMesh.ReturnValue;
            }
        }

        Log()->info("SpawnCccProxy: spawned at ({:.0f}, {:.0f}, {:.0f})", pos.X, pos.Y, pos.Z);
        return actor;
    }

    void SpawnGroup(int count) {
        const auto localPlayer = gGlobals.localPlayer;
        if (!localPlayer || !localPlayer->PlayerController || !localPlayer->PlayerController->Pawn) {
            Log()->warn("No local player pawn — cannot place students");
            return;
        }
        auto *pawn = reinterpret_cast<UObjectBase *>(localPlayer->PlayerController->Pawn);

        struct {
            float X, Y, Z;
        } loc{};
        struct {
            float Pitch, Yaw, Roll;
        } rot{};
        CallUFunction(pawn, "K2_GetActorLocation", &loc);
        CallUFunction(pawn, "K2_GetActorRotation", &rot);

        constexpr float kDistance   = 250.f;
        constexpr float kSpacingDeg = 25.f;
        int spawned                 = 0;
        for (int i = 0; i < count; ++i) {
            const float offsetDeg = (static_cast<float>(i) - static_cast<float>(count - 1) * 0.5f) * kSpacingDeg;
            const float rad       = (rot.Yaw + offsetDeg) * kPi / 180.f;
            const FVector pos{loc.X + std::cos(rad) * kDistance, loc.Y + std::sin(rad) * kDistance, loc.Z};
            UObjectBase *skinComp = nullptr;
            if (auto *actor = SpawnCccProxy(pos, rot.Yaw + offsetDeg + 180.f, &skinComp)) {
                auto *obj         = reinterpret_cast<UObjectBase *>(actor);
                const auto index  = static_cast<int32_t>(obj->GetUniqueID());
                int32_t serial    = 0;
                if (auto *arr = gGlobals.objectArray) {
                    if (auto *item = arr->IndexToObject(index)) {
                        serial = item->GetSerialNumber();
                    }
                }
                g_students.push_back({actor, index, serial, skinComp});
                ++spawned;

                // Dev preview: dress the proxy as the LOCAL player (verifies CCD reconstruct + LayerCCD).
                struct {
                    UClass *ComponentClass;
                    UObjectBase *ReturnValue;
                } getCcc{FindUClass("Class /Script/CustomizableCharacter.CustomizableCharacterComponent"), nullptr};
                if (getCcc.ComponentClass) {
                    CallUFunction(obj, "GetComponentByClass", &getCcc);
                    if (getCcc.ReturnValue) {
                        HogwartsMP::Core::CcdWire::MirrorLocalCcdToProxyCcc(getCcc.ReturnValue);
                    }
                }
            }
        }
        PublishActiveCount();
        Log()->info("Spawned {}/{} students ({} active)", spawned, count, g_students.size());
    }

    void DespawnAllNow() {
        if (g_students.empty()) {
            return;
        }
        if (!UWorld__DestroyActor) {
            // Native never resolved (stale AOB) — there is nothing we can ever
            // destroy with; abandon the registry rather than spin forever.
            Log()->error("DestroyActor unresolved — abandoning {} tracked students", g_students.size());
            g_students.clear();
            PublishActiveCount();
            return;
        }
        if (!GWorld || !*GWorld) {
            // No world right now (level transition) — keep the registry and
            // retry on a later tick; clearing here would leak live actors.
            g_despawnRequested = true;
            return;
        }
        int destroyed = 0, stale = 0;
        for (const auto &ref : g_students) {
            if (auto *actor = ResolveAlive(ref)) {
                UWorld__DestroyActor(*GWorld, actor, false, true);
                ++destroyed;
            }
            else {
                ++stale;
            }
        }
        g_students.clear();
        PublishActiveCount();
        Log()->info("Despawned {} students ({} already gone)", destroyed, stale);
    }
} // namespace

namespace HogwartsMP::Core::StudentProxy {
    void RequestSpawn(int count) {
        if (count > 0) {
            g_spawnPending += std::min(count, kMaxSpawnPerRequest);
        }
    }

    void RequestDespawnAll() {
        g_despawnRequested = true;
    }

    void ProcessPending() {
        if (g_despawnRequested.exchange(false)) {
            DespawnAllNow();
        }
        if (const int n = g_spawnPending.exchange(0); n > 0) {
            SpawnGroup(n);
        }
    }

    size_t ActiveCount() {
        return g_activeCount.load(std::memory_order_relaxed);
    }

    AActor *FirstActive() {
        for (const auto &ref : g_students) {
            if (auto *actor = ResolveAlive(ref)) {
                return actor;
            }
        }
        return nullptr;
    }

    UObjectBase *FirstActiveSkin() {
        for (const auto &ref : g_students) {
            if (ResolveAlive(ref)) {
                return ref.skinComp;
            }
        }
        return nullptr;
    }

    AActor *SpawnProxy(float x, float y, float z, float yawDeg, UObjectBase **outCcc) {
        if (outCcc) {
            *outCcc = nullptr;
        }
        UObjectBase *body = nullptr;
        auto *actor       = SpawnCccProxy({x, y, z}, yawDeg, &body);
        if (actor && outCcc) {
            struct {
                UClass *ComponentClass;
                UObjectBase *ReturnValue;
            } gc{FindUClass("Class /Script/CustomizableCharacter.CustomizableCharacterComponent"), nullptr};
            if (gc.ComponentClass) {
                CallUFunction(reinterpret_cast<UObjectBase *>(actor), "GetComponentByClass", &gc);
                *outCcc = gc.ReturnValue;
            }
        }
        return actor;
    }

    void DestroyProxy(AActor *actor) {
        if (actor && GWorld && *GWorld && UWorld__DestroyActor) {
            UWorld__DestroyActor(*GWorld, actor, false, true);
        }
    }
} // namespace HogwartsMP::Core::StudentProxy
