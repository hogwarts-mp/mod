#include <utils/safe_win32.h>

#include "character_creator.h"

#include "application.h"
#include "appearance_dump.h"
#include "sdk/natives/ue4_natives.h"
#include "sdk/reflection/ue4_reflection.h"

#include "UObject/Class.h"
#include "UObject/EnumProperty.h"
#include "UObject/UObjectArray.h"
#include "UObject/UnrealType.h"

#include <logging/logger.h>
#include <nlohmann/json.hpp>

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace {
    using HogwartsMP::Core::gGlobals;
    using namespace HogwartsMP::Core::UE4;

    auto Log() {
        return Framework::Logging::GetLogger("CharacterCreator");
    }

    std::wstring wide(const std::string &s) {
        if (s.empty()) {
            return {};
        }
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
        return w;
    }

    // One queued edit. Applied on the game thread in ProcessPending against the
    // local player's CCC. a/b are FName-ish strings (mesh/piece/param/bone/outfit),
    // v holds colour (RGBA) or a single scalar in v[0].
    struct Cmd {
        enum Kind { Outfit, AddOn, Vec, Scal, Bone, Scale, Reload, Enumerate, Preset, VoicePitch, VoicePreview, Camera, CameraRestore, Rotate, Freeze, Snapshot, Restore, ReleaseSnap } kind;
        std::string a;
        std::string b;
        float v[5]{};
    };

    std::mutex g_mtx;
    std::vector<Cmd> g_queue;

    void Enqueue(Cmd c) {
        std::lock_guard<std::mutex> lock(g_mtx);
        g_queue.push_back(std::move(c));
    }

    // The local player's CCC — shared with appearance_dump (one GUObjectArray scan).
    UObjectBase *FindLocalPlayerCCC() {
        return HogwartsMP::Core::AppearanceDump::FindLocalPlayerCcc();
    }

    // ── AvatarPresetsManager-driven preset apply ─────────────────────────────
    // Call a UFunction by short name with named args (memcpy'd by reflected offset into a
    // ParmsSize buffer), optionally copying out "ReturnValue". Handles the manager calls
    // whose params (objects, enums, name, array return) make fixed structs fragile.
    bool CallNamed(UObjectBase *obj, const char *fnName,
                   std::initializer_list<std::tuple<const char *, const void *, size_t>> args,
                   void *retOut = nullptr, size_t retSize = 0) {
        auto *fn = FindFunctionInChain(obj, fnName);
        if (!fn) {
            Log()->warn("preset: no fn {}", fnName);
            return false;
        }
        std::vector<uint8_t> buf(fn->ParmsSize, 0);
        for (const auto &[n, src, sz] : args) {
            bool found = false;
            for (FField *p = fn->ChildProperties; p; p = p->Next) {
                if (narrow(p->GetFName()) == n) {
                    std::memcpy(buf.data() + static_cast<FProperty *>(p)->GetOffset_ForInternal(), src, sz);
                    found = true;
                    break;
                }
            }
            if (!found) {
                Log()->warn("preset: fn {} has no param {}", fnName, n);
            }
        }
        reinterpret_cast<UObject *>(obj)->ProcessEvent(fn, buf.data());
        if (retOut && retSize) {
            for (FField *p = fn->ChildProperties; p; p = p->Next) {
                if (narrow(p->GetFName()) == "ReturnValue") {
                    std::memcpy(retOut, buf.data() + static_cast<FProperty *>(p)->GetOffset_ForInternal(), retSize);
                    break;
                }
            }
        }
        return true;
    }

    UObjectBase *FindCdoByClass(const char *shortName) {
        auto *arr = gGlobals.objectArray;
        if (!arr) {
            return nullptr;
        }
        const int total = arr->GetObjectArrayNum();
        for (int i = 0; i < total; ++i) {
            auto *it = arr->IndexToObject(i);
            if (it && it->Object && narrow(it->Object->GetClass()->GetFName()) == shortName) {
                return it->Object;
            }
        }
        return nullptr;
    }

    // GetComponentByClass(classPath) on an actor — defined in the camera section below.
    UObjectBase *GetComp(void *actor, const char *classPath);

    // Write a UObject* property by name (unchecked; assumes it's an ObjectProperty).
    void SetObjectProp(UObjectBase *obj, const char *name, UObjectBase *val) {
        if (auto *p = FindPropertyInChain(obj->GetClass(), name)) {
            *reinterpret_cast<UObjectBase **>(reinterpret_cast<uint8_t *>(obj) + p->GetOffset_ForInternal()) = val;
        }
    }

    // UI category key -> AvatarPresets folder + DA prefix (used to load one sample DA and
    // read its EAvatarPresetType byte; the manager then enumerates that type's names).
    struct CatInfo {
        const char *key;
        const char *folder;
        const char *prefix;
        const char *sampleTail;  // sample DA = DA_<prefix><Gender><sampleTail>, read for its PresetType
    };
    const CatInfo kCats[] = {
        {"hairStyle", "HairStyle", "Hair", "001"},
        {"hairColour", "HairColor", "HairColor", "001"},
        {"faceShape", "HeadStyle", "Face", "001"},
        {"skin", "SKINCOLOR", "SkinColor", "001"},
        {"eyeColour", "EyeColor", "EyeColor", "001"},
        {"browShape", "EyebrowShape", "Eyebrow", "001"},
        {"browColour", "EyebrowColor", "EyebrowColor", "001"},
        // Markings: DA_Marking<Gender>00<slot><letter>; slots 0-3 are distinct preset types.
        // All four exposed for blind mapping — user identifies which is freckles/scars/etc.
        {"marking0", "FaceMarking0", "Marking", "000a"},
        {"marking1", "FaceMarking1", "Marking", "001a"},
        {"marking2", "FaceMarking2", "Marking", "002a"},
        {"marking3", "FaceMarking3", "Marking", "003a"},
    };

    struct CatState {
        bool resolved = false;
        uint8_t presetType = 0;
        std::vector<FName> names;
    };
    std::unordered_map<std::string, CatState> g_cats;
    std::vector<FName> g_fbNames;  // full-body preset names (the "preset" tab)
    UObjectBase *g_mgr = nullptr;
    int g_gender       = -1;

    // FString header (TArray<TCHAR>) for the sample-DA path read.
    struct TArrHdr {
        void *data;
        int32_t num;
        int32_t max;
    };

    // Free the heap backing of a by-value TArray returned through CallNamed. The params buffer
    // only frees its own bytes, not the array's allocation — and its elements (FName/int) are
    // trivially destructible, so freeing the backing via the game allocator is sufficient.
    void FreeArr(TArrHdr &a) {
        if (a.data) {
            FMemory::Free(a.data);
            a = {nullptr, 0, 0};
        }
    }

    UObjectBase *PresetsManager() {
        if (g_mgr) {
            return g_mgr;
        }
        // AvatarPresetsManager::Get() is a static getter — call it on the CDO (per the
        // reflection playbook), it returns the live singleton.
        if (auto *cdo = FindCdoByClass("AvatarPresetsManager")) {
            struct {
                UObjectBase *ReturnValue;
            } r{nullptr};
            CallUFunction(cdo, "Get", &r);
            g_mgr = r.ReturnValue;
        }
        if (!g_mgr) {
            Log()->warn("preset: AvatarPresetsManager not found");
        }
        return g_mgr;
    }

    int PlayerGender() {
        if (g_gender < 0) {
            // Prefer the live CCC's Gender — it reflects the current character (updates when a
            // preset rebuilds it). GetPlayerGenderRig can report the rig / lag, which gave male
            // face shapes on a female body.
            if (auto *ccc = FindLocalPlayerCCC()) {
                if (int g = ReadByteProperty(ccc, "Gender"); g >= 0) {
                    g_gender = g;
                }
            }
            if (g_gender < 0) {
                if (auto *mgr = PresetsManager()) {
                    struct {
                        uint8_t ReturnValue;
                    } r{0};
                    CallUFunction(mgr, "GetPlayerGenderRig", &r);
                    g_gender = r.ReturnValue;
                }
            }
            Log()->info("preset: player gender = {}", g_gender);
        }
        return g_gender;
    }

    // Resolve a category's preset-type + name list once (cached). Loads one sample DA to
    // read EAvatarPresetType, then asks the manager for that type's names for our gender.
    CatState *ResolveCategory(const std::string &key) {
        auto it = g_cats.find(key);
        if (it != g_cats.end() && it->second.resolved) {
            return &it->second;
        }
        const CatInfo *info = nullptr;
        for (const auto &c : kCats) {
            if (key == c.key) {
                info = &c;
                break;
            }
        }
        if (!info) {
            Log()->warn("preset: unsupported category '{}'", key);
            return nullptr;
        }
        auto *mgr = PresetsManager();
        const int gender = PlayerGender();
        if (!mgr || gender < 0) {
            return nullptr;
        }

        const char *genderStr = gender == 1 ? "Female" : "Male";
        std::wstring path = wide(std::string("/Game/Data/AvatarPresets/") + info->folder + "/DA_" + info->prefix + genderStr + info->sampleTail);
        auto *sample = LoadObjectByPath(nullptr, path.c_str());
        if (!sample) {
            Log()->warn("preset: sample DA not found for category '{}' ({})", key, narrow_(path));
            return nullptr;
        }
        const int ptype = ReadByteProperty(sample, "PresetType");
        if (ptype < 0) {
            Log()->warn("preset: no PresetType on sample DA for '{}'", key);
            return nullptr;
        }

        CatState &st = g_cats[key];
        st.presetType = static_cast<uint8_t>(ptype);

        // GetPresetsOfType(Gender, PresetType, bIncludeHidden) -> TArray<FName>.
        uint8_t g = static_cast<uint8_t>(gender), pt = st.presetType, inclHidden = 0;
        TArrHdr arr{nullptr, 0, 0};
        CallNamed(mgr, "GetPresetsOfType",
                  {{"Gender", &g, 1}, {"PresetType", &pt, 1}, {"bIncludeHidden", &inclHidden, 1}},
                  &arr, sizeof(TArrHdr));
        if (arr.data && arr.num > 0) {
            auto *names = reinterpret_cast<FName *>(arr.data);
            for (int i = 0; i < arr.num; ++i) {
                st.names.push_back(names[i]);
            }
        }
        FreeArr(arr);
        st.resolved = true;
        Log()->info("preset: category '{}' type={} count={}", key, st.presetType, static_cast<int>(st.names.size()));
        return &st;
    }

    int ClampIdx(int index, int count) {
        int i = index - 1;
        return i < 0 ? 0 : (i >= count ? count - 1 : i);
    }

    // Glasses are GearAppearanceItemDefinitions in the FACE gear slot, not AvatarPresets. We show
    // them by FORCING the appearance on that slot (GearManager::SetForcedGearAppearances) — a forced
    // appearance renders regardless of what's equipped and leaves the rest of the character alone, so
    // no look-rebuild is needed. FACE numbers -> ids "GA_Face_<nnn>"; Some IDs like GA_Face_002 is absent
    // in the build. The FACE slot mixes glasses and masks.
    const int kGlassesNums[] = {4, 5, 6, 7, 8};

    // GearManager singleton (its own Get(), like AvatarPresetsManager).
    UObjectBase *g_gearMgr = nullptr;
    UObjectBase *GearManagerInstance() {
        if (g_gearMgr) {
            return g_gearMgr;
        }
        if (auto *cdo = FindCdoByClass("GearManager")) {
            struct {
                UObjectBase *ReturnValue;
            } r{nullptr};
            CallUFunction(cdo, "Get", &r);
            g_gearMgr = r.ReturnValue;
        }
        return g_gearMgr;
    }

    // Re-evaluate the local actor's gear so a just-changed forced appearance renders.
    void RefreshGear(UObjectBase *gm) {
        auto *lp = gGlobals.localPlayer;
        if (!lp || !lp->PlayerController || !lp->PlayerController->Pawn) {
            return;
        }
        auto *pawn = reinterpret_cast<UObjectBase *>(lp->PlayerController->Pawn);
        bool yes = true;
        TArrHdr emptyStr{nullptr, 0, 0};
        CallNamed(gm, "UpdateGearOutfitItems",
                  {{"Actor", &pawn, sizeof(void *)}, {"UpdateIfNothingEquipped", &yes, 1}, {"bIncludeSlotDefaultGear", &yes, 1}, {"GearActorID", &emptyStr, sizeof(TArrHdr)}});
    }

    // Apply glasses option `index` (1-based): 1 = none, 2.. pick kGlassesNums[index-2]. Overlays the
    // chosen appearance onto the FACE gear slot via GearManager::SetForcedGearAppearances — a forced
    // appearance renders regardless of equipped gear and leaves the rest of the character untouched.
    void ApplyGlasses(int index) {
        auto *gm = GearManagerInstance();
        if (!gm) {
            Log()->warn("glasses: no GearManager");
            return;
        }
        // Resolve the forced-appearance map property + the FACE slot value once (both paths use it).
        auto *fn = FindFunctionInChain(gm, "SetForcedGearAppearances");
        if (!fn) {
            Log()->warn("glasses: no SetForcedGearAppearances");
            return;
        }
        FMapProperty *mapProp = nullptr;
        for (FField *p = fn->ChildProperties; p; p = p->Next) {
            if (narrow(p->GetFName()) == "GearAppearanceNames") {
                mapProp = static_cast<FMapProperty *>(static_cast<FProperty *>(p));
            }
        }
        if (!mapProp || !mapProp->KeyProp) {
            return;
        }
        // FACE slot's EGearSlotIDEnum value, read from the map key's enum (its name is "...::FACE").
        int64_t faceVal = -1;
        if (auto *en = static_cast<FEnumProperty *>(mapProp->KeyProp)->GetEnum()) {
            for (int32_t i = 0; i < en->Names.Num(); ++i) {
                if (narrow(en->Names[i].Key).find("FACE") != std::string::npos) {
                    faceVal = en->Names[i].Value;
                    break;
                }
            }
        }
        if (faceVal < 0) {
            Log()->warn("glasses: FACE slot value not found");
            return;
        }
        uint8_t faceByte = static_cast<uint8_t>(faceVal);

        // none -> clear ONLY the FACE forced slot (RemoveAll would wipe any other forced gear that
        // may be added later). GearSlotIDs is a TArray<EGearSlotIDEnum> with the single FACE entry.
        if (index < 2) {
            TArrHdr slots{&faceByte, 1, 1};
            CallNamed(gm, "RemoveForcedGearAppearances", {{"GearSlotIDs", &slots, sizeof(TArrHdr)}});
            RefreshGear(gm);
            Log()->info("glasses: cleared (none)");
            return;
        }

        const int count = static_cast<int>(sizeof(kGlassesNums) / sizeof(kGlassesNums[0]));
        int k           = index - 2;
        k               = k < 0 ? 0 : (k >= count ? count - 1 : k);
        std::string n   = std::to_string(kGlassesNums[k]);
        while (n.size() < 3) {
            n = "0" + n;
        }
        const std::string id = "GA_Face_" + n;

        // Build {FACE -> glasses} into the params buffer (FScriptMapHelper handles the TMap layout)
        // and force it; free the map we built afterwards.
        std::vector<uint8_t> buf(fn->ParmsSize, 0);
        uint8_t *mapPtr = buf.data() + mapProp->GetOffset_ForInternal();
        FScriptMapHelper helper(mapProp, mapPtr);
        uint8_t keyBuf[8] = {0};
        keyBuf[0]         = faceByte;
        FName val         = MakeFName(wide(id).c_str());
        helper.AddPair(keyBuf, &val);
        reinterpret_cast<UObject *>(gm)->ProcessEvent(fn, buf.data());
        mapProp->DestroyValue(mapPtr);
        RefreshGear(gm);
        Log()->info("glasses: forced FACE slot = {}", id);
    }

    // Apply preset `index` (1-based) of `category` to the live CCC via LoadPreset.
    void ApplyPreset(UObjectBase *ccc, const std::string &category, int index) {
        // Glasses are gear, not an AvatarPreset — a separate (GearManager) path.
        if (category == "glasses") {
            ApplyGlasses(index);
            return;
        }
        auto *mgr = PresetsManager();
        if (!mgr) {
            return;
        }

        // The "Presets" tab is a full-body preset — a different API (LoadFullBodyPreset)
        // than the per-type LoadPreset used by every other category.
        if (category == "preset") {
            if (g_fbNames.empty()) {
                // Fetch each gender's full-body presets, then interleave [F1, M1, F2, M2, ...].
                // LoadFullBodyPreset applies a complete character, so a female entry switches
                // the character to female (and vice-versa). Gender 1 = Female, 0 = Male.
                std::vector<FName> byGender[2];
                for (uint8_t g = 0; g <= 1; ++g) {
                    uint8_t inclHidden = 0;
                    TArrHdr arr{nullptr, 0, 0};
                    CallNamed(mgr, "GetFullBodyPresetNames", {{"Gender", &g, 1}, {"bIncludeHidden", &inclHidden, 1}}, &arr, sizeof(TArrHdr));
                    if (arr.data && arr.num > 0) {
                        auto *n = reinterpret_cast<FName *>(arr.data);
                        for (int i = 0; i < arr.num; ++i) {
                            byGender[g].push_back(n[i]);
                        }
                    }
                    FreeArr(arr);
                }
                const size_t maxN = byGender[0].size() > byGender[1].size() ? byGender[0].size() : byGender[1].size();
                for (size_t i = 0; i < maxN; ++i) {
                    if (i < byGender[1].size()) {
                        g_fbNames.push_back(byGender[1][i]);  // Female
                    }
                    if (i < byGender[0].size()) {
                        g_fbNames.push_back(byGender[0][i]);  // Male
                    }
                }
                Log()->info("preset: fullbody count={} (interleaved F/M)", static_cast<int>(g_fbNames.size()));
            }
            if (g_fbNames.empty()) {
                return;
            }
            FName name     = g_fbNames[ClampIdx(index, static_cast<int>(g_fbNames.size()))];
            bool forceSync = true, showGear = true, retVal = false;
            CallNamed(mgr, "LoadFullBodyPreset",
                      {{"CCC", &ccc, sizeof(void *)}, {"PresetName", &name, sizeof(FName)}, {"bForceSync", &forceSync, 1}, {"bShowDefaultAvatarGear", &showGear, 1}},
                      &retVal, 1);
            Log()->info("preset: LoadFullBodyPreset #{} -> {}", index, retVal);
            // A full-body preset can switch gender — drop the cached gender + per-category
            // name lists so every category re-resolves for the new gender on next use.
            g_gender = -1;
            g_cats.clear();
            return;
        }

        auto *st = ResolveCategory(category);
        if (!st || st->names.empty()) {
            return;
        }
        FName name = st->names[ClampIdx(index, static_cast<int>(st->names.size()))];
        // LoadPreset(CCC, PresetType, PresetName, OnCharacterLoadComplete, IsLoading, bForceSync).
        uint8_t pt = st->presetType;
        bool forceSync = true;
        bool retVal = false;
        CallNamed(mgr, "LoadPreset",
                  {{"CCC", &ccc, sizeof(void *)}, {"PresetType", &pt, 1}, {"PresetName", &name, sizeof(FName)}, {"bForceSync", &forceSync, 1}},
                  &retVal, 1);
        Log()->info("preset: LoadPreset {} #{} -> {}", category, index, retVal);
    }

    // Appearance undo via a CacheCCD-object snapshot (reconstruction-from-CcdProfile is stale
    // on this build). On open we grab the live CCC's CacheCCD and root it so edits can't free
    // it; on cancel we swap it back + reload. Works if edits REPLACE the CacheCCD (new object);
    // if they mutate it in place, snapshot==current and restore no-ops (then we'd need a copy).
    UObjectBase *g_snapshotCcd = nullptr;

    void RootCcd(UObjectBase *obj, bool root) {
        auto *arr = gGlobals.objectArray;
        if (!obj || !arr) {
            return;
        }
        auto *item = arr->IndexToObject(static_cast<int32_t>(obj->GetUniqueID()));
        if (item && item->Object == obj) {
            if (root) {
                item->SetFlags(EInternalObjectFlags::RootSet);
            }
            else {
                item->ClearFlags(EInternalObjectFlags::RootSet);
            }
        }
    }

    void SnapshotAppearance() {
        auto *ccc = FindLocalPlayerCCC();
        if (!ccc) {
            Log()->warn("snapshot: no CCC");
            return;
        }
        if (g_snapshotCcd) {
            RootCcd(g_snapshotCcd, false);  // release a stale snapshot (re-open without close)
        }
        g_snapshotCcd = ReadObjectProperty(ccc, "CacheCCD");
        if (g_snapshotCcd) {
            RootCcd(g_snapshotCcd, true);
        }
        Log()->info("snapshot: captured CacheCCD {}", (void *)g_snapshotCcd);
    }

    void ReleaseSnapshot() {
        if (g_snapshotCcd) {
            RootCcd(g_snapshotCcd, false);
            g_snapshotCcd = nullptr;
        }
    }

    void RestoreAppearance() {
        auto *ccc = FindLocalPlayerCCC();
        if (!ccc || !g_snapshotCcd) {
            Log()->warn("restore: no ccc/snapshot");
            ReleaseSnapshot();
            return;
        }
        auto *current = ReadObjectProperty(ccc, "CacheCCD");
        SetObjectProp(ccc, "CacheCCD", g_snapshotCcd);
        struct {
            bool a, r;
        } al{false, false};
        CallUFunction(ccc, "SetAsyncLoad", &al);
        CallUFunction(ccc, "ReloadCharacter", nullptr);
        Log()->info("restore: CacheCCD {} -> {} + reload", (void *)current, (void *)g_snapshotCcd);
        ReleaseSnapshot();
        g_gender = -1;
        g_cats.clear();
    }

    void ApplyVoicePitch(int value) {
        // AvaAudio::SetPlayerVoicePitch(int PlayerVoicePitch, bool WriteToSave).
        auto *cdo = FindCdoByClass("AvaAudio");
        if (!cdo) {
            Log()->warn("voice: AvaAudio not found");
            return;
        }
        int32_t v = value;
        bool writeToSave = false;
        CallNamed(cdo, "SetPlayerVoicePitch", {{"PlayerVoicePitch", &v, 4}, {"WriteToSave", &writeToSave, 1}});
        Log()->info("voice: pitch -> {}", value);
    }

    // Preview the player's voice: animate the mouth for a sample line.
    //
    // Audio is intentionally not fired here. HL voice audio is driven by the Able ability system
    // (an AblPlayDialogTask referencing an AvaAudioDialogueEvent like VO_Spell), not by a callable
    // function — every reflection path we tried (PostDialogueEvent/PrepareDialogueEvent cold,
    // DebugDialoguePlay, PostEventPlayerVoice) either returns prepared=false or plays only Wwise's
    // "Voice One, English" external-source placeholder, because nothing sets up the speaker cast.
    // The real path (a future improvement): a minimal pak ability with a single AblPlayDialogTask,
    // triggered on the local player via Able_Character::PlayAbilityByClass. EditorPlayDialogueLine
    // below is editor-preview anim that survives cooking, so the mouth still moves.
    void ApplyVoicePreview() {
        auto *lp = gGlobals.localPlayer;
        if (!lp || !lp->PlayerController || !lp->PlayerController->Pawn) {
            return;
        }
        auto *pawn = reinterpret_cast<UObjectBase *>(lp->PlayerController->Pawn);
        // Scan for the FacialComponent whose outer chain reaches the pawn (the engine class
        // path for GetComponentByClass isn't /Script/Phoenix, and it may sit under a mesh).
        UObjectBase *facial = nullptr;
        auto *objArr        = gGlobals.objectArray;
        const int objTotal  = objArr ? objArr->GetObjectArrayNum() : 0;
        for (int i = 0; i < objTotal && !facial; ++i) {
            auto *it = objArr->IndexToObject(i);
            if (!it || !it->Object || narrow(it->Object->GetClass()->GetFName()) != "FacialComponent") {
                continue;
            }
            for (auto *o = it->Object->GetOuter(); o; o = o->GetOuter()) {
                if (o == pawn) {
                    facial = it->Object;
                    break;
                }
            }
        }
        if (!facial) {
            Log()->warn("voice: no FacialComponent found for pawn");
            return;
        }
        TArrHdr arr{nullptr, 0, 0};
        CallNamed(facial, "EditorGetDialogueLineIds", {}, &arr, sizeof(TArrHdr));
        if (!arr.data || arr.num <= 0) {
            Log()->warn("voice: no dialogue line ids (count={})", arr.num);
            FreeArr(arr);
            return;
        }
        auto *lines = reinterpret_cast<FName *>(arr.data);
        static int s_idx = 0;
        FName line = lines[s_idx % arr.num];
        ++s_idx;
        FreeArr(arr); // line is a copy; the array's backing is no longer needed

        // Mouth: drive the facial preview anim for `line` (silent in the cooked build).
        struct {
            bool ReturnValue;
        } cancel{false};
        CallUFunction(facial, "EditorCancelPlayingCurrentDialogueLine", &cancel);
        struct {
            FName DialogueLine;
            FName OverrideDialogueLineEmotion;
            bool ReturnValue;
        } p{line, FName{}, false};
        CallUFunction(facial, "EditorPlayDialogueLine", &p);
        Log()->info("voice: play line '{}' (of {}) -> {}", narrow(line), arr.num, p.ReturnValue);
    }

    // ── Creator framing camera ───────────────────────────────────────────────
    struct V3 {
        float X, Y, Z;
    };
    struct Rt {
        float Pitch, Yaw, Roll;
    };

    UObjectBase *GetComp(void *actor, const char *classPath) {
        auto *cls = FindUClass(classPath);
        if (!cls) {
            return nullptr;
        }
        struct {
            UClass *ComponentClass;
            UObjectBase *ReturnValue;
        } gc{cls, nullptr};
        CallUFunction(actor, "GetComponentByClass", &gc);
        return gc.ReturnValue;
    }

    AActor *g_camActor = nullptr;

    UObjectBase *PlayerController() {
        auto *lp = gGlobals.localPlayer;
        return (lp && lp->PlayerController) ? reinterpret_cast<UObjectBase *>(lp->PlayerController) : nullptr;
    }

    void SetViewTarget(UObjectBase *target, float blend) {
        auto *pc = PlayerController();
        if (!pc || !target) {
            return;
        }
        CallNamed(pc, "SetViewTargetWithBlend", {{"NewViewTarget", &target, sizeof(void *)}, {"BlendTime", &blend, 4}});
    }

    // Frame the live player from the front at the given distance/height/pitch/fov (a
    // section-specific framing pushed from the UI). Spawns the camera once, then just
    // repositions it + (re)targets the view.
    void ApplyCameraFrame(float dist, float height, float pitch, float fov, float shift) {
        auto *lp = gGlobals.localPlayer;
        if (!lp || !lp->PlayerController || !lp->PlayerController->Pawn) {
            return;
        }
        auto *pawn = reinterpret_cast<UObjectBase *>(lp->PlayerController->Pawn);
        if (!GWorld || !*GWorld || !UWorld__SpawnActor) {
            return;
        }

        // Female meshes stand ~10cm shorter than male, so the framed height (tuned on male)
        // sits too high on a female head — drop it to keep the face centred.
        if (PlayerGender() == 1) {
            height -= 10.f;
        }

        V3 loc{};
        Rt rot{};
        CallUFunction(pawn, "K2_GetActorLocation", &loc);
        CallUFunction(pawn, "K2_GetActorRotation", &rot);

        // In front of the pawn (along its facing), looking back at it. A lateral strafe
        // (camera-right = (fy,-fx,0)) of `shift` slides the camera left WITHOUT re-aiming,
        // so the subject shifts right in frame to clear the left panel — stays face-on.
        const float kPi = 3.14159265f, yr = rot.Yaw * kPi / 180.f;
        const float fx = std::cos(yr), fy = std::sin(yr);
        V3 camPos{loc.X + fx * dist - fy * shift, loc.Y + fy * dist + fx * shift, loc.Z + height};
        Rt camRot{pitch, rot.Yaw + 180.f, 0.f};

        if (!g_camActor) {
            auto *cls = FindUClass("Class /Script/Engine.CameraActor");
            if (!cls) {
                Log()->warn("camera: no CameraActor class");
                return;
            }
            FActorSpawnParameters sp{};
            sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
            g_camActor = UWorld__SpawnActor(*GWorld, cls, reinterpret_cast<FVector const *>(&camPos), reinterpret_cast<FRotator const *>(&camRot), sp);
            if (!g_camActor) {
                Log()->warn("camera: spawn failed");
                return;
            }
        }
        else {
            CallNamed(reinterpret_cast<UObjectBase *>(g_camActor), "K2_SetActorLocationAndRotation",
                      {{"NewLocation", &camPos, sizeof(camPos)}, {"NewRotation", &camRot, sizeof(camRot)}});
        }

        if (auto *cam = GetComp(reinterpret_cast<void *>(g_camActor), "Class /Script/Engine.CameraComponent")) {
            CallNamed(cam, "SetFieldOfView", {{"InFieldOfView", &fov, 4}});
        }
        SetViewTarget(reinterpret_cast<UObjectBase *>(g_camActor), 0.25f);
    }

    // Rotate the live player by a yaw delta so the user can inspect different sides. The
    // framing camera stays put (only reframes on open/tab-switch), so the character turns
    // in front of it.
    void ApplyRotate(float deltaYaw) {
        auto *lp = gGlobals.localPlayer;
        if (!lp || !lp->PlayerController || !lp->PlayerController->Pawn) {
            return;
        }
        auto *pawn = reinterpret_cast<UObjectBase *>(lp->PlayerController->Pawn);
        V3 loc{};
        Rt rot{};
        CallUFunction(pawn, "K2_GetActorLocation", &loc);
        CallUFunction(pawn, "K2_GetActorRotation", &rot);
        rot.Yaw += deltaYaw;
        CallNamed(pawn, "K2_SetActorLocationAndRotation", {{"NewLocation", &loc, sizeof(loc)}, {"NewRotation", &rot, sizeof(rot)}});
    }

    // Freeze/unfreeze the player's idle animation (SetPlayRate 0/1) so the character holds
    // still while inspecting details. rate=0 freezes, rate=1 resumes.
    void ApplyFreeze(float rate) {
        auto *lp = gGlobals.localPlayer;
        if (!lp || !lp->PlayerController || !lp->PlayerController->Pawn) {
            return;
        }
        auto *pawn = reinterpret_cast<UObjectBase *>(lp->PlayerController->Pawn);
        if (auto *mesh = GetComp(pawn, "Class /Script/Engine.SkeletalMeshComponent")) {
            CallNamed(mesh, "SetPlayRate", {{"Rate", &rate, 4}});
        }
    }

    void ApplyCameraRestore() {
        auto *lp = gGlobals.localPlayer;
        if (lp && lp->PlayerController && lp->PlayerController->Pawn) {
            SetViewTarget(reinterpret_cast<UObjectBase *>(lp->PlayerController->Pawn), 0.25f);
        }
        if (g_camActor && GWorld && *GWorld && UWorld__DestroyActor) {
            UWorld__DestroyActor(*GWorld, g_camActor, false, true);
        }
        g_camActor = nullptr;
    }

    void Apply(UObjectBase *ccc, const Cmd &c) {
        switch (c.kind) {
        case Cmd::Outfit: {
            struct {
                FName OutfitName;
            } p{MakeFName(wide(c.a).c_str())};
            CallUFunction(ccc, "SetCurrentOutfit", &p);
            break;
        }
        case Cmd::AddOn: {
            struct {
                FName CharacterPieceType;
                FName CharacterPieceName;
                bool bResetMaterialParams;
            } p{MakeFName(wide(c.a).c_str()), MakeFName(wide(c.b).c_str()), false};
            CallUFunction(ccc, "SetAddOnMesh", &p);
            break;
        }
        case Cmd::Vec: {
            struct {
                FName MeshName;
                FName ParameterName;
                float R, G, B, A;
                bool bResetMaterialParameters;
            } p{MakeFName(wide(c.a).c_str()), MakeFName(wide(c.b).c_str()), c.v[0], c.v[1], c.v[2], c.v[3], false};
            CallUFunction(ccc, "SetVectorParameter", &p);
            break;
        }
        case Cmd::Scal: {
            struct {
                FName MeshName;
                FName ParameterName;
                float Value;
                bool bResetMaterialParameters;
            } p{MakeFName(wide(c.a).c_str()), MakeFName(wide(c.b).c_str()), c.v[0], false};
            CallUFunction(ccc, "SetScalarParameter", &p);
            break;
        }
        case Cmd::Bone: {
            struct {
                FName BoneName;
                float Value;
            } p{MakeFName(wide(c.a).c_str()), c.v[0]};
            CallUFunction(ccc, "SetBoneSliderScale", &p);
            break;
        }
        case Cmd::Scale: {
            struct {
                float NewScale;
            } p{c.v[0]};
            CallUFunction(ccc, "SetScale", &p);
            break;
        }
        case Cmd::Reload: {
            // Synchronous build so the edit is visible this frame (matches ccd_wire).
            struct {
                bool a, r;
            } al{false, false};
            CallUFunction(ccc, "SetAsyncLoad", &al);
            CallUFunction(ccc, "ReloadCharacter", nullptr);
            break;
        }
        case Cmd::Enumerate: {
            HogwartsMP::Shared::Modules::CcdProfile prof;
            if (!HogwartsMP::Core::AppearanceDump::BuildLocalCcd(prof)) {
                Log()->warn("enumerate: no local CCC/CCD");
                break;
            }
            nlohmann::json j;
            j["gender"] = static_cast<int>(prof.gender);
            j["scale"]  = prof.scale;
            for (auto &o : prof.outfits) {
                j["outfits"].push_back(o.first);
            }
            for (auto &it : prof.characterItems) {
                j["items"].push_back(it.first);
            }
            for (auto &b : prof.boneScales) {
                j["boneSliders"].push_back(b.first);
            }
            Log()->info("enumerate: {}", j.dump());
            break;
        }
        case Cmd::Preset: {
            ApplyPreset(ccc, c.a, static_cast<int>(c.v[0]));
            break;
        }
        case Cmd::VoicePitch: {
            ApplyVoicePitch(static_cast<int>(c.v[0]));
            break;
        }
        case Cmd::VoicePreview: {
            ApplyVoicePreview();
            break;
        }
        case Cmd::Camera: {
            ApplyCameraFrame(c.v[0], c.v[1], c.v[2], c.v[3], c.v[4]);
            break;
        }
        case Cmd::CameraRestore: {
            ApplyCameraRestore();
            break;
        }
        case Cmd::Rotate: {
            ApplyRotate(c.v[0]);
            break;
        }
        case Cmd::Freeze: {
            ApplyFreeze(c.v[0]);
            break;
        }
        case Cmd::Snapshot: {
            SnapshotAppearance();
            break;
        }
        case Cmd::Restore: {
            RestoreAppearance();
            break;
        }
        case Cmd::ReleaseSnap: {
            ReleaseSnapshot();
            break;
        }
        }
    }
} // namespace

namespace HogwartsMP::Core::CharacterCreator {
    void RequestSetOutfit(const std::string &outfitName) {
        Enqueue({Cmd::Outfit, outfitName, {}, {}});
    }

    void RequestSetAddOnMesh(const std::string &pieceType, const std::string &pieceName) {
        Enqueue({Cmd::AddOn, pieceType, pieceName, {}});
    }

    void RequestSetVectorParam(const std::string &meshName, const std::string &param, float r, float g, float b, float a) {
        Enqueue({Cmd::Vec, meshName, param, {r, g, b, a}});
    }

    void RequestSetScalarParam(const std::string &meshName, const std::string &param, float value) {
        Enqueue({Cmd::Scal, meshName, param, {value, 0, 0, 0}});
    }

    void RequestSetBoneSlider(const std::string &boneName, float value) {
        Enqueue({Cmd::Bone, boneName, {}, {value, 0, 0, 0}});
    }

    void RequestSetScale(float scale) {
        Enqueue({Cmd::Scale, {}, {}, {scale, 0, 0, 0}});
    }

    void RequestReload() {
        Enqueue({Cmd::Reload, {}, {}, {}});
    }

    void RequestEnumerate() {
        Enqueue({Cmd::Enumerate, {}, {}, {}});
    }

    void RequestApplyPreset(const std::string &category, int index) {
        Enqueue({Cmd::Preset, category, {}, {static_cast<float>(index), 0, 0, 0}});
    }

    void RequestSetVoicePitch(int value) {
        Enqueue({Cmd::VoicePitch, {}, {}, {static_cast<float>(value), 0, 0, 0}});
    }

    void RequestVoicePreview() {
        Enqueue({Cmd::VoicePreview, {}, {}, {}});
    }

    void RequestCameraFrame(float dist, float height, float pitch, float fov, float shift) {
        Enqueue({Cmd::Camera, {}, {}, {dist, height, pitch, fov, shift}});
    }

    void RequestCameraRestore() {
        Enqueue({Cmd::CameraRestore, {}, {}, {}});
    }

    void RequestRotate(float deltaYaw) {
        Enqueue({Cmd::Rotate, {}, {}, {deltaYaw, 0, 0, 0}});
    }

    void RequestFreeze(bool frozen) {
        Enqueue({Cmd::Freeze, {}, {}, {frozen ? 0.0f : 1.0f, 0, 0, 0}});
    }

    void RequestSnapshotAppearance() {
        Enqueue({Cmd::Snapshot, {}, {}, {}});
    }

    void RequestRestoreAppearance() {
        Enqueue({Cmd::Restore, {}, {}, {}});
    }

    void RequestReleaseSnapshot() {
        Enqueue({Cmd::ReleaseSnap, {}, {}, {}});
    }

    void ProcessPending() {
        std::vector<Cmd> batch;
        {
            std::lock_guard<std::mutex> lock(g_mtx);
            if (g_queue.empty()) {
                return;
            }
            batch.swap(g_queue);
        }

        UObjectBase *ccc = FindLocalPlayerCCC();
        if (!ccc) {
            Log()->warn("no local player CCC; dropping {} queued edit(s)", batch.size());
            return;
        }
        for (const auto &c : batch) {
            Apply(ccc, c);
        }
    }
} // namespace HogwartsMP::Core::CharacterCreator
