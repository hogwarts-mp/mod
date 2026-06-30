// AppearanceDump — in-game appearance harvester (Playground → "Dump Nearby
// NPC Appearances", logs to the AppearanceDump channel). Scans live NPC
// students and logs everything the student proxy dressing flow needs:
//
//   - per-actor SkeletalMeshComponent mesh paths + material paths (runtime
//     MIDs resolved to their loadable on-disk parent MI)
//   - full MID parameter arrays (scalar/vector/texture) for one robe per
//     gender — the source of the kit_params_*_uniNN.h headers
//   - per-robe house color signatures (Color_Tint_*/UVBias + textures),
//     labeled by CharacterName — the source of kit_params_houses.h
//   - loaded head/arms skeletal mesh assets and T_Patch crest textures
//   - the CustomizableCharacterComponent property list (field hunting for
//     future appearance work: gear slots, Outfits/CharacterItems maps)
//
// See APPEARANCE_SYNC_RESEARCH.md for the findings this produced.

#include <utils/safe_win32.h>

#include "appearance_dump.h"

#include "application.h"
#include "sdk/natives/ue4_natives.h"
#include "sdk/reflection/ue4_reflection.h"

#include "UObject/Class.h"
#include "UObject/UObjectArray.h"
#include "UObject/UnrealType.h"

#include <logging/logger.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
    using HogwartsMP::Core::gGlobals;
    using namespace HogwartsMP::Core::UE4;

    std::atomic<bool> g_dumpPending{false};

    // ── MID parameter-array dump ─────────────────────────────────────────────
    // Walks UMaterialInstance's ScalarParameterValues / VectorParameterValues /
    // TextureParameterValues TArrays raw. UE4.27 shipping layouts (FName = 8):
    //   FMaterialParameterInfo { FName Name; uint8 Association; int32 Index; } = 16
    //   FScalarParameterValue  { Info; float Value;        FGuid; } = 36, stride 36
    //   FVectorParameterValue  { Info; FLinearColor Value; FGuid; } = 48, stride 48
    //   FTextureParameterValue { Info; UTexture *Value;    FGuid; } = 40, stride 40
    // This is the same data the kit_params headers were harvested from —
    // needed per-gender and per-house.
    template <typename TLogger>
    void DumpMidParams(TLogger log, UObjectBase *mid, const char *label) {
        auto *cls = mid->GetClass();
        auto arrOf = [&](const char *propName) -> std::pair<uint8_t *, int> {
            auto *prop = FindPropertyInChain(cls, propName);
            if (!prop) {
                return {nullptr, 0};
            }
            auto *base = reinterpret_cast<uint8_t *>(mid) + prop->GetOffset_ForInternal();
            return {*reinterpret_cast<uint8_t **>(base), *reinterpret_cast<int32_t *>(base + 8)};
        };

        log->info("--- MID params [{}] parent={} ---", label,
                  AssetPath(ReadObjectProperty(mid, "Parent")));

        if (auto [data, num] = arrOf("ScalarParameterValues"); data) {
            for (int k = 0; k < num && k < 100; ++k) {
                auto *e = data + k * 36;
                log->info("  scalar {} = {}", narrow(*reinterpret_cast<FName *>(e)),
                          *reinterpret_cast<float *>(e + 20));
            }
        }
        if (auto [data, num] = arrOf("VectorParameterValues"); data) {
            for (int k = 0; k < num && k < 100; ++k) {
                auto *e = data + k * 48;
                auto *v = reinterpret_cast<float *>(e + 16);
                log->info("  vector {} = ({}, {}, {}, {})", narrow(*reinterpret_cast<FName *>(e)),
                          v[0], v[1], v[2], v[3]);
            }
        }
        if (auto [data, num] = arrOf("TextureParameterValues"); data) {
            for (int k = 0; k < num && k < 100; ++k) {
                auto *e = data + k * 40;
                auto *tex = *reinterpret_cast<UObjectBase **>(e + 16);
                log->info("  texture {} = {}", narrow(*reinterpret_cast<FName *>(e)),
                          tex ? AssetPath(tex) : "(null)");
            }
        }
    }

    void DumpNearbyNow() {
        auto log = Framework::Logging::GetLogger("AppearanceDump");
        auto *arr = gGlobals.objectArray;
        if (!arr) {
            log->error("No object array");
            return;
        }

        // ── Collect: SkeletalMeshComponents owned by CustomizableCharacterComponent ──
        // UE4 outer chain for dressed NPC students:
        //   SMC  →  outer: CustomizableCharacterComponent (CCC)  →  outer: pawn actor
        //
        // Confirmed by diagnostic: 57 SMCs have CCC as outer. All others (Actor,
        // Package, ABP_*, BTS_*, etc.) are unrelated and are skipped here.

        struct MeshEntry {
            std::string smcName;
            std::string meshPath;
            std::vector<std::string> matPaths;
        };
        struct ActorEntry {
            std::string actorClass;
            std::string actorName;
            std::string cccClass;   // exact class name of the CCC
            std::string cccType;    // CCC "Type" FName
            std::string charName;   // CCC "CharacterName" FName — encodes house+gender
            int gender{-1};
            std::vector<MeshEntry> meshes;
        };

        // Keyed by actor UObjectBase* so multiple SMCs on the same actor merge.
        std::unordered_map<UObjectBase *, ActorEntry> actorMap;

        // First robe ("Upper") MID seen per gender — full param harvest targets.
        UObjectBase *upperMidM   = nullptr;
        UObjectBase *upperMidM03 = nullptr; // male StuUni03 robe specifically
        UObjectBase *upperMidF   = nullptr;
        // Every robe MID with its owner, for the per-house color signature table.
        struct UpperRef {
            UObjectBase *mid;
            std::string actorName;
            std::string charName;
            int gender;
        };
        std::vector<UpperRef> upperMids;
        // Any CCC instance — used to enumerate its property list below.
        UObjectBase *anyCcc = nullptr;

        const int total = arr->GetObjectArrayNum();
        for (int i = 0; i < total; ++i) {
            auto *item = arr->IndexToObject(i);
            if (!item || !item->Object) {
                continue;
            }
            auto *obj = item->Object;

            if (narrow(obj->GetClass()->GetFName()) != "SkeletalMeshComponent") {
                continue;
            }

            auto *ccc = obj->GetOuter();
            if (!ccc) {
                continue;
            }
            // Only student/NPC dressed characters — skip raw actor-owned SMCs (our own proxies etc.)
            if (narrow(ccc->GetClass()->GetFName()) != "CustomizableCharacterComponent") {
                continue;
            }

            auto *actor = ccc->GetOuter();
            if (!actor) {
                continue;
            }

            // First time seeing this actor: record it
            if (actorMap.find(actor) == actorMap.end()) {
                ActorEntry e;
                e.actorClass = narrow(actor->GetClass()->GetFName());
                e.actorName  = narrow(actor->GetFName());
                e.cccClass   = narrow(ccc->GetClass()->GetFName());
                // The CCC has no house property — house is encoded in
                // CharacterName (e.g. "T4StudentGryffindorMale3").
                e.gender   = ReadByteProperty(ccc, "Gender");
                e.cccType  = ReadNameProperty(ccc, "Type");
                e.charName = ReadNameProperty(ccc, "CharacterName");
                actorMap.emplace(actor, std::move(e));
            }
            if (!anyCcc) {
                anyCcc = ccc;
            }

            // Read the SkeletalMesh asset path
            MeshEntry me;
            me.smcName = narrow(obj->GetFName());
            if (auto *meshObj = ReadObjectProperty(obj, "SkeletalMesh")) {
                me.meshPath = AssetPath(meshObj);
            }
            else {
                me.meshPath = "(none)";
            }

            // Read material slots: call GetNumMaterials then GetMaterial(slot).
            // Runtime MIDs aren't loadable by path, so when the slot holds a
            // MaterialInstanceDynamic, follow its Parent chain to the on-disk
            // MI asset — that is the path SpawnStudent can pass to
            // StaticLoadObject.
            struct { int32_t ReturnValue{}; } numMatsP;
            CallUFunction(obj, "GetNumMaterials", &numMatsP);
            for (int slot = 0; slot < numMatsP.ReturnValue && slot < 8; ++slot) {
                struct {
                    int32_t ElementIndex{};
                    UObjectBase *ReturnValue{};
                } getMatP{slot, nullptr};
                CallUFunction(obj, "GetMaterial", &getMatP);
                auto *mat = getMatP.ReturnValue;
                if (!mat) {
                    continue;
                }
                if (narrow(mat->GetClass()->GetFName()) == "MaterialInstanceDynamic") {
                    // Remember the first robe MID per gender for the full
                    // parameter harvest below, and every robe MID for the
                    // house color signature table.
                    if (me.smcName == "Upper" && slot == 0) {
                        const int g = actorMap[actor].gender;
                        if (g == 0 && !upperMidM) {
                            upperMidM = mat;
                        }
                        else if (g == 1 && !upperMidF) {
                            upperMidF = mat;
                        }
                        // Male StuUni03 robe specifically — base param set for
                        // switching the male proxy off StuUni01 (whose slot
                        // layout doesn't match the house overlay table).
                        if (g == 0 && !upperMidM03) {
                            if (auto *parent = ReadObjectProperty(mat, "Parent")) {
                                if (AssetPath(parent).find("StuUni03") != std::string::npos) {
                                    upperMidM03 = mat;
                                }
                            }
                        }
                        if (upperMids.size() < 40) {
                            upperMids.push_back({mat, actorMap[actor].actorName,
                                                 actorMap[actor].charName, g});
                        }
                    }
                    if (auto *parent = ReadObjectProperty(mat, "Parent")) {
                        me.matPaths.push_back("parent=" + AssetPath(parent));
                        continue;
                    }
                }
                me.matPaths.push_back(AssetPath(mat));
            }

            actorMap[actor].meshes.push_back(std::move(me));
        }

        log->info("=== AppearanceDump: {} NPC actors with CCC-owned SMCs ===",
                  static_cast<int>(actorMap.size()));

        // Print high-quality student actors first (BP_Student_C, BP_Tier3_Character_C)
        // with no cap — needed to harvest female mesh paths. Then up to 6 misc actors.
        auto printActor = [&](int idx, const ActorEntry &e) {
            log->info("Actor[{}] class={} name={} type={} char={} gender={}",
                      idx, e.actorClass, e.actorName, e.cccType, e.charName, e.gender);
            for (auto &me : e.meshes) {
                log->info("  SMC={} mesh={}", me.smcName, me.meshPath);
                for (size_t m = 0; m < me.matPaths.size(); ++m) {
                    log->info("    mat[{}]={}", static_cast<int>(m), me.matPaths[m]);
                }
            }
        };

        int idx = 0;
        for (auto &[actorPtr, e] : actorMap) {
            if (e.actorClass == "BP_Student_C" || e.actorClass == "BP_Tier3_Character_C") {
                printActor(idx++, e);
            }
        }
        int misc = 0;
        for (auto &[actorPtr, e] : actorMap) {
            if (e.actorClass == "BP_Student_C" || e.actorClass == "BP_Tier3_Character_C") {
                continue;
            }
            if (misc >= 6) {
                log->info("  ... ({} more misc actors omitted)",
                          static_cast<int>(actorMap.size()) - idx - 6);
                break;
            }
            printActor(idx++, e);
            ++misc;
        }

        // ── Loaded head / arms mesh assets ───────────────────────────────────
        // The student SMC scan never shows head/skin components (heads live on
        // a different component on NPCs), so list SkeletalMesh ASSETS in memory
        // instead — heads for the proxy skin, arms/hands because the female
        // outfit + head meshes have no hands.
        log->info("--- Loaded SkeletalMesh assets (heads / arms / hands) ---");
        int headCount = 0, armsCount = 0;
        for (int i = 0; i < total; ++i) {
            auto *item = arr->IndexToObject(i);
            if (!item || !item->Object) {
                continue;
            }
            auto *obj = item->Object;
            const auto clsName = narrow(obj->GetClass()->GetFName());
            // Crest patch texture variants (house + possibly generic/phoenix).
            if (clsName == "Texture2D") {
                const auto tpath = AssetPath(obj);
                if (tpath.find("T_Patch") != std::string::npos) {
                    log->info("  patch={}", tpath);
                }
                continue;
            }
            if (clsName != "SkeletalMesh") {
                continue;
            }
            const auto path = AssetPath(obj);
            if (path.find("/Heads/") != std::string::npos) {
                if (headCount < 40) {
                    log->info("  head[{}]={}", headCount, path);
                }
                ++headCount;
            }
            else if (path.find("Arms") != std::string::npos || path.find("Hand") != std::string::npos) {
                if (armsCount < 40) {
                    log->info("  arms[{}]={}", armsCount, path);
                }
                ++armsCount;
            }
        }

        // ── Full robe MID parameter harvest (one male, one female) ───────────
        // This is the source data for the kit_params_*_uniNN.h headers.
        if (upperMidM) {
            DumpMidParams(log, upperMidM, "male Upper");
        }
        if (upperMidM03 && upperMidM03 != upperMidM) {
            DumpMidParams(log, upperMidM03, "male Upper StuUni03");
        }
        if (upperMidF) {
            DumpMidParams(log, upperMidF, "female Upper");
        }

        // ── House color signature table ──────────────────────────────────────
        // One line per robe MID, only the house-distinguishing params
        // (Color_Tint_* and UVBias). Standing near students of different houses
        // and diffing these lines gives the per-house color/crest overlay.
        log->info("--- Robe color signatures ({} MIDs) ---", static_cast<int>(upperMids.size()));
        for (auto &ur : upperMids) {
            std::string sig;
            auto *prop = FindPropertyInChain(ur.mid->GetClass(), "VectorParameterValues");
            if (!prop) {
                continue;
            }
            auto *base = reinterpret_cast<uint8_t *>(ur.mid) + prop->GetOffset_ForInternal();
            auto *data = *reinterpret_cast<uint8_t **>(base);
            const int num = *reinterpret_cast<int32_t *>(base + 8);
            for (int k = 0; k < num && k < 100; ++k) {
                auto *e = data + k * 48;
                const auto name = narrow(*reinterpret_cast<FName *>(e));
                if (name.rfind("Color_Tint", 0) != 0 && name.rfind("UVBias", 0) != 0) {
                    continue;
                }
                auto *v = reinterpret_cast<float *>(e + 16);
                char buf[96];
                snprintf(buf, sizeof(buf), " %s=(%.3f,%.3f,%.3f)", name.c_str(), v[0], v[1], v[2]);
                sig += buf;
            }
            // Texture params too — the house crest is texture-selected (the
            // tint-only overlay left males with the donor's Hufflepuff crest).
            if (auto *tprop = FindPropertyInChain(ur.mid->GetClass(), "TextureParameterValues")) {
                auto *tbase = reinterpret_cast<uint8_t *>(ur.mid) + tprop->GetOffset_ForInternal();
                auto *tdata = *reinterpret_cast<uint8_t **>(tbase);
                const int tnum = *reinterpret_cast<int32_t *>(tbase + 8);
                for (int k = 0; k < tnum && k < 60; ++k) {
                    auto *e   = tdata + k * 40;
                    auto *tex = *reinterpret_cast<UObjectBase **>(e + 16);
                    sig += " T:" + narrow(*reinterpret_cast<FName *>(e)) + "=" + (tex ? AssetPath(tex) : "(null)");
                }
            }
            log->info("  sig actor={} char={} gender={}{}", ur.actorName, ur.charName, ur.gender, sig);
        }

        // ── CCC property list ────────────────────────────────────────────────
        // Inventory of CustomizableCharacterComponent's reflection surface —
        // useful when hunting fields for future appearance work (gear slots,
        // Outfits/CharacterItems maps, CCD data assets). The component has no
        // house property; house comes from CharacterName.
        if (anyCcc) {
            log->info("--- CustomizableCharacterComponent properties ---");
            int propCount = 0;
            for (UStruct *s = anyCcc->GetClass(); s && propCount < 150; s = s->GetSuperStruct()) {
                for (FField *f = s->ChildProperties; f && propCount < 150; f = f->Next) {
                    log->info("  prop {}::{} ({})", narrow(s->GetFName()),
                              narrow(f->GetFName()), narrow(f->GetClass()->GetFName()));
                    ++propCount;
                }
            }
        }
        log->info("=== End AppearanceDump ===");
    }
} // namespace

namespace HogwartsMP::Core::AppearanceDump {
    UObjectBase *FindLocalPlayerCcc() {
        using namespace HogwartsMP::Core::UE4;
        auto *lp = HogwartsMP::Core::gGlobals.localPlayer;
        if (!lp || !lp->PlayerController || !lp->PlayerController->Pawn) {
            return nullptr;
        }
        auto *pawn = reinterpret_cast<UObjectBase *>(lp->PlayerController->Pawn);
        auto *arr  = HogwartsMP::Core::gGlobals.objectArray;
        if (!arr) {
            return nullptr;
        }
        const int total = arr->GetObjectArrayNum();
        for (int i = 0; i < total; ++i) {
            auto *it = arr->IndexToObject(i);
            if (it && it->Object && narrow(it->Object->GetClass()->GetFName()) == "CustomizableCharacterComponent" && it->Object->GetOuter() == pawn) {
                return it->Object;
            }
        }
        return nullptr;
    }

    bool BuildLocalCcd(Shared::Modules::CcdProfile &out) {
        using namespace HogwartsMP::Core::UE4;
        UObjectBase *ccc = FindLocalPlayerCcc();
        auto *ccd        = ccc ? ReadObjectProperty(ccc, "CacheCCD") : nullptr;
        if (!ccd) {
            return false;
        }
        auto *ccdCls = reinterpret_cast<UClass *>(ccd->GetClass());
        out          = {};
        if (int g = ReadByteProperty(ccd, "Gender"); g >= 0) {
            out.gender = static_cast<uint8_t>(g);
        }
        if (auto *sp = FindPropertyInChain(ccdCls, "Scale"); sp && narrow(sp->GetClass()->GetFName()) == "FloatProperty") {
            out.scale = *reinterpret_cast<float *>(reinterpret_cast<uint8_t *>(ccd) + sp->GetOffset_ForInternal());
        }

        // Fill a CcdPiece from a CharacterPieceDefinition struct value at pbase.
        auto readPiece = [&](uint8_t *pbase, UStruct *ps, Shared::Modules::CcdPiece &dst) {
            auto *cls = reinterpret_cast<UClass *>(ps);
            if (auto *cpP = FindPropertyInChain(cls, "CharacterPiece")) {
                if (auto *o = *reinterpret_cast<UObjectBase **>(pbase + cpP->GetOffset_ForInternal())) {
                    dst.characterPiece = AssetPath(o);
                }
            }
            auto readBool = [&](const char *n) -> bool {
                auto *p = FindPropertyInChain(cls, n);
                if (!p) {
                    return false;
                }
                auto *bp = static_cast<FBoolProperty *>(p);
                return (*(pbase + bp->GetOffset_ForInternal() + bp->ByteOffset) & bp->ByteMask) != 0;
            };
            dst.setEvenIfNone = readBool("bSetCharacterPieceEvenIfNone");
            dst.isFlipped     = readBool("bIsFlipped");
            auto readMap = [&](const char *n, auto fn) {
                auto *p = FindPropertyInChain(cls, n);
                if (!p || narrow(p->GetClass()->GetFName()) != "MapProperty") {
                    return;
                }
                auto *mp = static_cast<FMapProperty *>(p);
                FScriptMapHelper h(mp, pbase + p->GetOffset_ForInternal());
                for (int i = 0; i < h.GetMaxIndex(); ++i) {
                    if (h.IsValidIndex(i)) {
                        fn(narrow(*reinterpret_cast<FName *>(h.GetKeyPtr(i))), h.GetValuePtr(i));
                    }
                }
            };
            readMap("ScalarOverrides", [&](const std::string &k, uint8_t *v) { dst.scalars.emplace_back(k, *reinterpret_cast<float *>(v)); });
            readMap("VectorOverrides", [&](const std::string &k, uint8_t *v) { auto *c = reinterpret_cast<float *>(v); dst.vectors.push_back({k, {c[0], c[1], c[2], c[3]}}); });
            readMap("TextureOverrides", [&](const std::string &k, uint8_t *v) { auto *o = *reinterpret_cast<UObjectBase **>(v); dst.textures.push_back({k, o ? AssetPath(o) : std::string()}); });
        };

        // Read a map<Name, CharacterPieceDefinition> at mapBase into a CcdPieceMap.
        auto readPieceMap = [&](uint8_t *mapBase, FMapProperty *mp, Shared::Modules::CcdPieceMap &dst) {
            auto *vStr = (narrow(mp->ValueProp->GetClass()->GetFName()) == "StructProperty") ? static_cast<FStructProperty *>(mp->ValueProp)->Struct : nullptr;
            if (!vStr) {
                return;
            }
            FScriptMapHelper h(mp, mapBase);
            for (int i = 0; i < h.GetMaxIndex(); ++i) {
                if (!h.IsValidIndex(i)) {
                    continue;
                }
                Shared::Modules::CcdPiece piece;
                readPiece(h.GetValuePtr(i), reinterpret_cast<UStruct *>(vStr), piece);
                dst.emplace_back(narrow(*reinterpret_cast<FName *>(h.GetKeyPtr(i))), std::move(piece));
            }
        };

        if (auto *p = FindPropertyInChain(ccdCls, "BoneScaleValues"); p && narrow(p->GetClass()->GetFName()) == "MapProperty") {
            FScriptMapHelper h(static_cast<FMapProperty *>(p), reinterpret_cast<uint8_t *>(ccd) + p->GetOffset_ForInternal());
            for (int i = 0; i < h.GetMaxIndex(); ++i) {
                if (h.IsValidIndex(i)) {
                    out.boneScales.emplace_back(narrow(*reinterpret_cast<FName *>(h.GetKeyPtr(i))), *reinterpret_cast<float *>(h.GetValuePtr(i)));
                }
            }
        }
        if (auto *p = FindPropertyInChain(ccdCls, "CharacterItems"); p && narrow(p->GetClass()->GetFName()) == "MapProperty") {
            readPieceMap(reinterpret_cast<uint8_t *>(ccd) + p->GetOffset_ForInternal(), static_cast<FMapProperty *>(p), out.characterItems);
        }
        if (auto *p = FindPropertyInChain(ccdCls, "Outfits"); p && narrow(p->GetClass()->GetFName()) == "MapProperty") {
            auto *mp   = static_cast<FMapProperty *>(p);
            auto *oStr = (narrow(mp->ValueProp->GetClass()->GetFName()) == "StructProperty") ? static_cast<FStructProperty *>(mp->ValueProp)->Struct : nullptr;
            FScriptMapHelper h(mp, reinterpret_cast<uint8_t *>(ccd) + p->GetOffset_ForInternal());
            for (int i = 0; i < h.GetMaxIndex(); ++i) {
                if (!h.IsValidIndex(i)) {
                    continue;
                }
                Shared::Modules::CcdPieceMap items;
                if (oStr) {
                    if (auto *oiP = FindPropertyInChain(reinterpret_cast<UClass *>(oStr), "OutfitItems"); oiP && narrow(oiP->GetClass()->GetFName()) == "MapProperty") {
                        readPieceMap(h.GetValuePtr(i) + oiP->GetOffset_ForInternal(), static_cast<FMapProperty *>(oiP), items);
                    }
                }
                out.outfits.emplace_back(narrow(*reinterpret_cast<FName *>(h.GetKeyPtr(i))), std::move(items));
            }
        }
        return true;
    }

    void RequestDump() {
        g_dumpPending = true;
    }

    void ProcessPending() {
        if (g_dumpPending.exchange(false)) {
            DumpNearbyNow();
            // Verify the local-CCD read (commit 5 wires the actual send).
            Shared::Modules::CcdProfile prof;
            if (BuildLocalCcd(prof)) {
                Framework::Logging::GetLogger("CCD")->info("BuildLocalCcd: gender={} scale={:.3f} bones={} items={} outfits={}",
                                                           static_cast<int>(prof.gender), prof.scale, prof.boneScales.size(), prof.characterItems.size(), prof.outfits.size());
            }
            else {
                Framework::Logging::GetLogger("CCD")->warn("BuildLocalCcd failed (no local pawn/CCC/CCD)");
            }
        }
    }
} // namespace HogwartsMP::Core::AppearanceDump
