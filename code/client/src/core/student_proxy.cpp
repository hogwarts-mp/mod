// Student proxy spawner — port of the proven no-pak student recipe from the
// HogwartsLegacyMP research repo (confirmed in-game 2026-06-10). This removes
// the old "needs StudentManager reversal" blocker: no pak, no StudentManager,
// no live NPC needed.
//
// The recipe:
//   1. Spawn the NATIVE /Script/Phoenix.Biped_Character — no Blueprint
//      BeginPlay graph, so no AI-state crash (BP_Student_C and friends crash
//      on raw SpawnActor because their BeginPlay touches AI/game state).
//      Spawns via native UWorld::SpawnActor — the FVector+FRotator overload
//      (the engine/console/UE4SS one; the FTransform overload the mod used to
//      hook was the wrong function on this build). See game_layout.h.
//   2. Dress CharacterMesh0 with the student outfit mesh + the harvested
//      swatch MID parameters (kit_params_*_uni03.h) — without them the
//      outfit renders as a white placeholder. House identity is a per-gender
//      tint + crest-texture overlay on top (kit_params_houses.h).
//   3. Add a second SkeletalMeshComponent for the head+hands skin, attach it
//      to CharacterMesh0 (single actor — no z-fighting). Females get a third
//      component for hair (their head mesh has none baked in).
//   4. Animation: the game's AnimationArchitect middleware starves regular
//      AnimBPs on raw-spawned actors (they instantiate but output ref pose),
//      so play a raw idle AnimSequence on the skin component and master-pose
//      the outfit to it (bone-name mapping bridges the Biped vs
//      Biped_ClothJoints skeletons).
//
// Implementation notes: everything goes through the shared reflection
// plumbing in core/ue4_reflection.h (ProcessEvent by function-name-in-chain,
// FProperty offsets), with exactly one extra native sigscan (StaticLoadObject
// — load assets by path with no NPCs around).
//
// Known cosmetic quirk inherited from the source repo: the FIRST spawn of a
// session can render low-poly (skeletal mesh LOD streaming warm-up; low LODs
// strip eyes/teeth geometry). Repeat spawns are perfect.

#include <utils/safe_win32.h>

#include "student_proxy.h"

#include "application.h"
#include "kit_params.h"
#include "kit_params_female_uni03.h"
#include "kit_params_houses.h"
#include "kit_params_male_uni03.h"
#include "sdk/natives/ue4_natives.h"
#include "ue4_reflection.h"

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

    // ── Student kit asset paths (harvested from live NPCs via AppearanceDump;
    //    loadable by path from any save, no NPCs required in memory) ─────────
    //
    // Both genders wear the StuUni03 robe family (what T3 ambient students
    // wear) so the per-gender house overlays in kit_params_houses.h land on
    // the right material zones.

    // ── Male ─────────────────────────────────────────────────────────────────
    const wchar_t *OUTFIT_MESH_PATH_M =
        L"/Game/RiggedObjects/Characters/Human/Clothing/Combined_M/StuUni03/SK_HUM_M_CMB_StuUni03_Dynamics_Master.SK_HUM_M_CMB_StuUni03_Dynamics_Master";
    constexpr std::array OUTFIT_MATS_M{
        L"/Game/RiggedObjects/Characters/Human/Clothing/Combined_M/StuUni03/Materials/MI_HUM_M_CMB_StuUni03_Robed.MI_HUM_M_CMB_StuUni03_Robed",
        L"/Game/RiggedObjects/Characters/Human/Clothing/Combined_M/StuUni03/Materials/MI_HUM_M_CMB_StuUni03_Robed.MI_HUM_M_CMB_StuUni03_Robed",
        L"/Game/RiggedObjects/Characters/Human/Clothing/Combined_M/StuUni03/Materials/MI_HUM_M_CMB_StuUni03_Robed.MI_HUM_M_CMB_StuUni03_Robed",
        L"/Game/RiggedObjects/Characters/Human/Clothing/Combined_M/StuUni03/Materials/MI_HUM_M_CMB_StuUni03_Robed.MI_HUM_M_CMB_StuUni03_Robed",
        L"/Game/RiggedObjects/MasterMaterials/Misc/MI_ClothSim.MI_ClothSim",
        L"/Game/RiggedObjects/MasterMaterials/Misc/MI_ClothSim.MI_ClothSim",
        L"/Game/RiggedObjects/MasterMaterials/Misc/MI_ClothSim.MI_ClothSim",
    };
    static_assert(OUTFIT_MATS_M.size() == 7); // one per outfit material slot
    // Sebastian Sallow head+hands ("Sebastien" typo is in the game assets).
    const wchar_t *SKIN_MESH_PATH_M =
        L"/Game/RiggedObjects/Characters/Human/Heads/NPC_YM_SebastianSallow/NPC_YM_SebastienSallow_Master.NPC_YM_SebastienSallow_Master";
    constexpr std::array SKIN_MATS_M{
        L"/Game/RiggedObjects/Characters/Human/Heads/Materials/MI_HUM_N_Heads_Eyelash.MI_HUM_N_Heads_Eyelash",
        L"/Game/RiggedObjects/Characters/Human/Heads/Materials/MI_HUM_N_Heads_Teeth.MI_HUM_N_Heads_Teeth",
        L"/Game/RiggedObjects/Characters/Human/Heads/Young_M/Materials/MI_Young_M_Head.MI_Young_M_Head",
        L"/Game/RiggedObjects/Characters/Human/Heads/Materials/MI_HUM_N_Heads_Eye.MI_HUM_N_Heads_Eye",
        L"/Game/RiggedObjects/Characters/Human/Heads/Materials/MI_HUM_N_Heads_EyeOcclusion.MI_HUM_N_Heads_EyeOcclusion",
    };
    static_assert(SKIN_MATS_M.size() == 5); // one per skin material slot

    // ── Female ───────────────────────────────────────────────────────────────
    // Mesh + material harvested from live female BP_Tier3_Character_C robes
    // (material via the runtime MID's Parent chain — naming is inconsistent
    // between genders in the game's own assets).
    const wchar_t *OUTFIT_MESH_PATH_F =
        L"/Game/RiggedObjects/Characters/Human/Clothing/Combined_F/StuUni03/SK_HUM_F_CMB_StuUni03_Robed_Dynamics_Master.SK_HUM_F_CMB_StuUni03_Robed_Dynamics_Master";
    constexpr std::array OUTFIT_MATS_F{
        L"/Game/RiggedObjects/Characters/Human/Clothing/Combined_F/StuUni03/Materials/MI_HUM_F_CMBH_StuUni03_Robed.MI_HUM_F_CMBH_StuUni03_Robed",
        L"/Game/RiggedObjects/Characters/Human/Clothing/Combined_F/StuUni03/Materials/MI_HUM_F_CMBH_StuUni03_Robed.MI_HUM_F_CMBH_StuUni03_Robed",
        L"/Game/RiggedObjects/Characters/Human/Clothing/Combined_F/StuUni03/Materials/MI_HUM_F_CMBH_StuUni03_Robed.MI_HUM_F_CMBH_StuUni03_Robed",
        L"/Game/RiggedObjects/Characters/Human/Clothing/Combined_F/StuUni03/Materials/MI_HUM_F_CMBH_StuUni03_Robed.MI_HUM_F_CMBH_StuUni03_Robed",
        L"/Game/RiggedObjects/MasterMaterials/Misc/MI_ClothSim.MI_ClothSim",
        L"/Game/RiggedObjects/MasterMaterials/Misc/MI_ClothSim.MI_ClothSim",
        L"/Game/RiggedObjects/MasterMaterials/Misc/MI_ClothSim.MI_ClothSim",
    };
    static_assert(OUTFIT_MATS_F.size() == 7);
    // Generic young-female NPC head (incl. hands), confirmed loaded in the
    // AppearanceDump head scan. Slot layout differs from the male head — her
    // mesh's default materials are kept (the male override made the face
    // translucent and hid the hands), so no SKIN_MATS_F set exists.
    const wchar_t *SKIN_MESH_PATH_F =
        L"/Game/RiggedObjects/Characters/Human/Heads/NPC_Young_F/SK_NPC_Young_F_Head_Master.SK_NPC_Young_F_Head_Master";
    // The female head has no baked hair (Sebastian's male mesh does) — add a
    // separate hair component. Mesh + material harvested from live female NPCs.
    const wchar_t *HAIR_MESH_PATH_F =
        L"/Game/RiggedObjects/Characters/Human/Hair/Hair_F/ChunkHair01/SK_HUM_F_Hair_ChunkHair01_Bang0Bun1_Master.SK_HUM_F_Hair_ChunkHair01_Bang0Bun1_Master";
    constexpr std::array HAIR_MATS_F{
        L"/Game/RiggedObjects/Characters/Human/Hair/Hair_F/ChunkHair01/Materials/MI_HUM_F_Hair_ChunkHair01_Bang0Bun1.MI_HUM_F_Hair_ChunkHair01_Bang0Bun1",
    };

    // Raw idle sequences (engine single-node playback bypasses the
    // AnimationArchitect middleware). Index 0=male, 1=female.
    constexpr std::array IDLE_SEQ_PATHS{
        L"/Game/Animation/Human/Student/Student_M/StuM_BM_Idle_Loop_Stand_anm.StuM_BM_Idle_Loop_Stand_anm",
        L"/Game/Animation/Human/Student/Student_F/StuF_BM_Idle_Loop_Stand_anm.StuF_BM_Idle_Loop_Stand_anm",
    };

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

    // ── Dressing helpers (port of the proven recipe) ─────────────────────────

    void DressComponent(void *comp, UObjectBase *mesh, std::span<const wchar_t *const> matPaths, UClass *matCls) {
        struct {
            UObjectBase *NewMesh;
            bool bReinitPose;
        } setMesh{mesh, false};
        CallUFunction(comp, "SetSkeletalMesh", &setMesh);

        // matPaths repeats the same asset across slots — load each path once.
        std::unordered_map<std::wstring_view, UObjectBase *> loaded;
        for (size_t i = 0; i < matPaths.size(); ++i) {
            auto [it, fresh] = loaded.try_emplace(matPaths[i], nullptr);
            if (fresh) {
                it->second = LoadObjectByPath(matCls, matPaths[i]);
            }
            if (auto *mat = it->second) {
                struct {
                    int32_t ElementIndex;
                    UObjectBase *Material;
                } setMat{static_cast<int32_t>(i), mat};
                CallUFunction(comp, "SetMaterial", &setMat);
            }
        }

        struct {
            bool bNewVisibility;
            bool bPropagateToChildren;
        } vis{true, false};
        CallUFunction(comp, "SetVisibility", &vis);
        struct {
            bool NewHidden;
            bool bPropagateToChildren;
        } hid{false, false};
        CallUFunction(comp, "SetHiddenInGame", &hid);
    }

    struct FLinearColorF {
        float R, G, B, A;
    };

    // One MID per slot + the harvested parameter set (one shared set per mesh,
    // verified for both outfit and skin). Without these the outfit renders as
    // the white placeholder.
    void ApplyMidParams(void *comp, std::span<const wchar_t *const> matPaths,
                        std::span<const HogwartsMP::KitParams::ScalarParam> scalars,
                        std::span<const HogwartsMP::KitParams::VectorParam> vectors,
                        std::span<const HogwartsMP::KitParams::TextureParam> textures,
                        UClass *matCls, UClass *texCls) {
        // The texture set is shared by every slot — resolve each asset once
        // instead of once per slot.
        std::vector<std::pair<FName, UObjectBase *>> texParams;
        texParams.reserve(textures.size());
        for (const auto &t : textures) {
            if (auto *tex = LoadObjectByPath(texCls, t.path)) {
                texParams.emplace_back(MakeFName(t.name), tex);
            }
        }

        // Base materials repeat across slots too — load each path once.
        std::unordered_map<std::wstring_view, UObjectBase *> baseMats;
        int applied = 0;
        for (size_t i = 0; i < matPaths.size(); ++i) {
            auto [it, fresh] = baseMats.try_emplace(matPaths[i], nullptr);
            if (fresh) {
                it->second = LoadObjectByPath(matCls, matPaths[i]);
            }
            auto *base = it->second;
            if (!base) {
                continue;
            }

            struct {
                int32_t ElementIndex;
                UObjectBase *SourceMaterial;
                FName OptionalName;
                UObjectBase *ReturnValue;
            } cdmi{static_cast<int32_t>(i), base, FName(), nullptr};
            CallUFunction(comp, "CreateDynamicMaterialInstance", &cdmi);
            auto *mid = cdmi.ReturnValue;
            if (!mid) {
                Log()->warn("CreateDynamicMaterialInstance failed on slot {}", i);
                continue;
            }

            for (const auto &s : scalars) {
                struct {
                    FName Name;
                    float Value;
                } sp{MakeFName(s.name), s.value};
                CallUFunction(mid, "SetScalarParameterValue", &sp);
            }
            for (const auto &p : vectors) {
                struct {
                    FName Name;
                    FLinearColorF Value;
                } vp{MakeFName(p.name), {p.r, p.g, p.b, p.a}};
                CallUFunction(mid, "SetVectorParameterValue", &vp);
            }
            for (const auto &[texName, tex] : texParams) {
                struct {
                    FName Name;
                    UObjectBase *Value;
                } tp{texName, tex};
                CallUFunction(mid, "SetTextureParameterValue", &tp);
            }
            ++applied;
        }
        Log()->info("MID params applied on {}/{} slots", applied, matPaths.size());
    }

    // Apply per-house tint overrides + crest patch textures on top of the base
    // kit params. Works on the MIDs ApplyMidParams already installed (fetched
    // back via GetMaterial). House order matches kit_params_houses.h:
    // 0=Gry, 1=Sly, 2=Rav, 3=Huf.
    void ApplyHouseOverlay(void *comp, int numSlots,
                           std::span<const HogwartsMP::KitParams::HouseParam> params,
                           std::span<const HogwartsMP::KitParams::HouseTexture> htex,
                           int house, UClass *texCls) {
        house = std::clamp(house, 0, 3);

        // Crest patch textures (T_Patch_{G,S,R,H}) — same set for every slot.
        std::vector<std::pair<FName, UObjectBase *>> texParams;
        texParams.reserve(htex.size());
        for (const auto &t : htex) {
            if (auto *tex = LoadObjectByPath(texCls, t.path[house])) {
                texParams.emplace_back(MakeFName(t.name), tex);
            }
        }

        for (int i = 0; i < numSlots; ++i) {
            struct {
                int32_t ElementIndex;
                UObjectBase *ReturnValue;
            } gm{i, nullptr};
            CallUFunction(comp, "GetMaterial", &gm);
            auto *mid = gm.ReturnValue;
            if (!mid || narrow(mid->GetClass()->GetFName()) != "MaterialInstanceDynamic") {
                continue;
            }
            for (const auto &p : params) {
                struct {
                    FName Name;
                    FLinearColorF Value;
                } vp{MakeFName(p.name), {p.v[house][0], p.v[house][1], p.v[house][2], p.v[house][3]}};
                CallUFunction(mid, "SetVectorParameterValue", &vp);
            }
            for (const auto &[texName, tex] : texParams) {
                struct {
                    FName Name;
                    UObjectBase *Value;
                } tp{texName, tex};
                CallUFunction(mid, "SetTextureParameterValue", &tp);
            }
        }
    }

    // ── The spawn itself ─────────────────────────────────────────────────────

    AActor *SpawnStudent(const FVector &pos, float yawDeg, bool female = false, int house = 0, UObjectBase **outSkinComp = nullptr) {
        auto *cls = FindUClass("Class /Script/Phoenix.Biped_Character");
        if (!cls) {
            Log()->error("Biped_Character class not found");
            return nullptr;
        }
        if (!GWorld || !*GWorld || !UWorld__SpawnActor) {
            Log()->error("No world / SpawnActor not resolved");
            return nullptr;
        }

        // Native UWorld::SpawnActor — the FVector+FRotator overload (the one the
        // engine/console/UE4SS use; re-derived via Ghidra, see game_layout.h).
        FVector loc = pos;
        FRotator rot{0.f, yawDeg, 0.f}; // Pitch, Yaw, Roll
        FActorSpawnParameters spawnParams{};
        spawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto *actor = UWorld__SpawnActor(*GWorld, cls, &loc, &rot, spawnParams);
        if (!actor) {
            Log()->error("SpawnActor failed");
            return nullptr;
        }

        // Every exit path below must undo the tick neutralization — a partially
        // dressed actor (missing mesh/asset) still gets handed back, registered
        // for despawn, and must not stay frozen.
        const auto finalize = [](AActor *a) {
            struct {
                bool b;
            } tickOn{true};
            CallUFunction(a, "SetActorTickEnabled", &tickOn);
            struct {
                bool NewHidden;
            } unhide{false};
            CallUFunction(a, "SetActorHiddenInGame", &unhide);
            return a;
        };

        // Neutralize before anything else — an AnimBP-less character must not
        // tick while we dress it, and it must not collide or fall (collision
        // stays off for good: proxies are scenery).
        struct {
            bool b;
        } off{false};
        CallUFunction(actor, "SetActorTickEnabled", &off);
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

        // Outfit goes on the existing CharacterMesh0 (the only SMC on a fresh
        // native Biped_Character).
        auto *smcCls = FindUClass("Class /Script/Engine.SkeletalMeshComponent");
        struct {
            UClass *ComponentClass;
            UObjectBase *ReturnValue;
        } getMesh{smcCls, nullptr};
        CallUFunction(actor, "GetComponentByClass", &getMesh);
        auto *meshComp = getMesh.ReturnValue;
        if (!meshComp) {
            Log()->warn("No CharacterMesh0 found — actor stays naked (still registered for despawn)");
            return finalize(actor);
        }

        auto *skelMeshCls = FindUClass("Class /Script/Engine.SkeletalMesh");
        auto *matCls      = FindUClass("Class /Script/Engine.MaterialInterface");
        auto *texCls      = FindUClass("Class /Script/Engine.Texture2D");

        using namespace HogwartsMP::KitParams;

        const auto *outfitMeshPath = female ? OUTFIT_MESH_PATH_F : OUTFIT_MESH_PATH_M;
        std::span<const wchar_t *const> outfitMats = female ? OUTFIT_MATS_F : OUTFIT_MATS_M;

        auto *outfitMesh = LoadObjectByPath(skelMeshCls, outfitMeshPath);
        if (!outfitMesh && female) {
            // Fall back to the male outfit so the proxy is still visible
            // rather than invisible.
            Log()->warn("Female outfit mesh not found — falling back to male");
            female         = false;
            outfitMeshPath = OUTFIT_MESH_PATH_M;
            outfitMats     = OUTFIT_MATS_M;
            outfitMesh     = LoadObjectByPath(skelMeshCls, outfitMeshPath);
        }
        if (!outfitMesh) {
            return finalize(actor);
        }
        DressComponent(meshComp, outfitMesh, outfitMats, matCls);
        if (female) {
            ApplyMidParams(meshComp, outfitMats,
                           FemaleUni03Scalars, FemaleUni03Vectors, FemaleUni03Textures,
                           matCls, texCls);
        }
        else {
            ApplyMidParams(meshComp, outfitMats,
                           MaleUni03Scalars, MaleUni03Vectors, MaleUni03Textures,
                           matCls, texCls);
        }
        // House tint + crest overlay on top of the base params (zone layouts
        // are per-gender, see kit_params_houses.h).
        ApplyHouseOverlay(meshComp, static_cast<int>(outfitMats.size()),
                          female ? std::span<const HouseParam>{HouseParamsF} : std::span<const HouseParam>{HouseParamsM},
                          female ? std::span<const HouseTexture>{HouseTexturesF} : std::span<const HouseTexture>{HouseTexturesM},
                          house, texCls);

        // The outfit is a cloth-sim mesh — keep cloth sim off; the robe is
        // master-posed to the skin below instead of simulating.
        SetBoolProperty(meshComp, "bDisableClothSimulation", true);

        // Skin (head+hands) on ONE added component, attached to CharacterMesh0.
        const auto *skinMeshPath = female ? SKIN_MESH_PATH_F : SKIN_MESH_PATH_M;

        UObjectBase *skinComp = nullptr;
        auto *skinMesh        = LoadObjectByPath(skelMeshCls, skinMeshPath);
        if (!skinMesh && female) {
            Log()->warn("Female skin mesh not found — falling back to male head");
            skinMesh = LoadObjectByPath(skelMeshCls, SKIN_MESH_PATH_M);
        }
        if (smcCls && skinMesh) {
            // AddComponentByClass(UClass*, bool bManualAttachment, FTransform, bool bDeferredFinish)
            // UE4.27 FTransform in a ProcessEvent param block: vectorized floats,
            // 16-byte aligned, 0x30 bytes (quat xyzw / translation xyz+pad / scale xyz+pad).
            struct alignas(16) {
                UClass *Class{};                                            // 0x00
                bool bManualAttachment{};                                   // 0x08
                uint8_t pad[7]{};
                float Transform[12]{0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 0};   // 0x10 (identity)
                bool bDeferredFinish{};                                     // 0x40
                uint8_t pad2[7]{};
                UObjectBase *ReturnValue{};                                 // 0x48
            } addParams{};
            addParams.Class = smcCls;
            CallUFunction(actor, "AddComponentByClass", &addParams);
            skinComp = addParams.ReturnValue;
            if (skinComp) {
                struct {
                    UObjectBase *Parent{};
                    FName SocketName{};
                    uint8_t LocationRule{2}; // SnapToTarget
                    uint8_t RotationRule{2};
                    uint8_t ScaleRule{2};
                    bool bWeldSimulatedBodies{};
                    bool ReturnValue{};
                } attach{};
                attach.Parent = meshComp;
                CallUFunction(skinComp, "K2_AttachToComponent", &attach);

                if (female) {
                    // Female head: keep the mesh's default materials (single
                    // slot, MI_NPC_Young_F) — the male override set made her
                    // face translucent and hid the hands.
                    DressComponent(skinComp, skinMesh, {}, matCls);
                }
                else {
                    DressComponent(skinComp, skinMesh, SKIN_MATS_M, matCls);
                    // Skin materials are parameterized like the outfit's — the
                    // *_WithHands_* textures in this set are what fixes the hands.
                    ApplyMidParams(skinComp, SKIN_MATS_M, SkinScalars, SkinVectors, SkinTextures, matCls, texCls);
                }
            }
            else {
                Log()->warn("AddComponentByClass returned null — no skin component");
            }
        }

        // Hair for females — separate component master-posed to the head.
        if (female && skinComp) {
            if (auto *hairMesh = LoadObjectByPath(skelMeshCls, HAIR_MESH_PATH_F)) {
                struct alignas(16) {
                    UClass *Class{};
                    bool bManualAttachment{};
                    uint8_t pad[7]{};
                    float Transform[12]{0, 0, 0, 1, 0, 0, 0, 0, 1, 1, 1, 0};
                    bool bDeferredFinish{};
                    uint8_t pad2[7]{};
                    UObjectBase *ReturnValue{};
                } addHair{};
                addHair.Class = smcCls;
                CallUFunction(actor, "AddComponentByClass", &addHair);
                if (auto *hairComp = addHair.ReturnValue) {
                    struct {
                        UObjectBase *Parent{};
                        FName SocketName{};
                        uint8_t LocationRule{2};
                        uint8_t RotationRule{2};
                        uint8_t ScaleRule{2};
                        bool bWeldSimulatedBodies{};
                        bool ReturnValue{};
                    } attachHair{};
                    attachHair.Parent = skinComp;
                    CallUFunction(hairComp, "K2_AttachToComponent", &attachHair);

                    DressComponent(hairComp, hairMesh, HAIR_MATS_F, matCls);
                    struct {
                        UObjectBase *NewMasterBoneComponent;
                        bool bForceUpdate;
                    } hairPose{skinComp, true};
                    CallUFunction(hairComp, "SetMasterPoseComponent", &hairPose);
                }
                else {
                    Log()->warn("AddComponentByClass returned null — no hair component");
                }
            }
        }

        // Idle animation: raw sequence playback on the skin comp (bypasses the
        // starved AnimBPs), outfit master-posed to it. Try the gender-matched
        // idle first; fall through to the other if it fails.
        auto *seqCls      = FindUClass("Class /Script/Engine.AnimSequence");
        UObjectBase *idle = LoadObjectByPath(seqCls, IDLE_SEQ_PATHS[female ? 1 : 0]);
        if (!idle) {
            idle = LoadObjectByPath(seqCls, IDLE_SEQ_PATHS[female ? 0 : 1]);
        }
        if (idle && skinComp) {
            struct {
                bool b;
            } on{true};
            CallUFunction(skinComp, "SetComponentTickEnabled", &on);
            struct {
                UObjectBase *NewAnimToPlay;
                bool bLooping;
            } play{idle, true};
            CallUFunction(skinComp, "PlayAnimation", &play);
            struct {
                UObjectBase *NewMasterBoneComponent;
                bool bForceUpdate;
            } mpose{skinComp, true};
            CallUFunction(meshComp, "SetMasterPoseComponent", &mpose);
        }
        else if (!idle) {
            Log()->warn("Idle AnimSequence not found — student will hold ref pose");
        }

        if (outSkinComp) {
            *outSkinComp = skinComp;
        }

        Log()->info("Student spawned at ({:.0f}, {:.0f}, {:.0f})", pos.X, pos.Y, pos.Z);
        return finalize(actor);
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
            // Alternate gender and cycle houses (spawn 8 to see all four houses
            // in both genders); +180 so the students face back toward the player.
            const bool female     = (i % 2 == 1);
            const int house       = (i / 2) % 4;
            UObjectBase *skinComp = nullptr;
            if (auto *actor = SpawnStudent(pos, rot.Yaw + offsetDeg + 180.f, female, house, &skinComp)) {
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

    AActor *SpawnProxy(float x, float y, float z, float yawDeg, Appearance appearance, UObjectBase **outSkinComp) {
        return SpawnStudent({x, y, z}, yawDeg, appearance.female, std::clamp(appearance.house, 0, 3), outSkinComp);
    }

    void DestroyProxy(AActor *actor) {
        if (actor && GWorld && *GWorld && UWorld__DestroyActor) {
            UWorld__DestroyActor(*GWorld, actor, false, true);
        }
    }
} // namespace HogwartsMP::Core::StudentProxy
