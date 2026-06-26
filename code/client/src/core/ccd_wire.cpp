#include <utils/safe_win32.h>

#include "ccd_wire.h"

#include "aob_scan.h"
#include "application.h"
#include "appearance_dump.h"
#include "game_layout.h"
#include "modules/human.h"
#include "sdk/natives/ue4_natives.h"
#include "sdk/reflection/ue4_reflection.h"

#include "UObject/Class.h"
#include "UObject/UObjectArray.h"
#include "UObject/UnrealType.h"

#include <logging/logger.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace {
    using HogwartsMP::Core::gGlobals;
    using namespace HogwartsMP::Core::UE4;

    auto Log() {
        return Framework::Logging::GetLogger("CcdWire");
    }

    // Pin a constructed/loaded object into the GC root set (same as UObject::AddToRoot) so a cached
    // pointer can't dangle after a collection. Mirror of human.cpp's RootObject.
    void RootObject(UObjectBase *obj) {
        auto *arr = gGlobals.objectArray;
        if (!obj || !arr) {
            return;
        }
        auto *item = arr->IndexToObject(static_cast<int32_t>(obj->GetUniqueID()));
        if (item && item->Object == obj) {
            item->SetFlags(EInternalObjectFlags::RootSet);
        }
    }

    // Mirror of FStaticConstructObjectParameters (UObjectGlobals.h) — its real ctor is COREUOBJECT_API
    // (engine-exported, can't link), so we lay out the struct by hand and pass it by const& to the
    // sigscanned StaticConstructObject_Internal. Layout must match the engine's exactly.
    struct FConstructParams {
        const void *Class;
        void *Outer;
        FName Name;
        uint32_t SetFlags;        // EObjectFlags
        int32_t InternalSetFlags; // EInternalObjectFlags
        bool bCopyTransientsFromClassDefaults;
        bool bAssumeTemplateIsArchetype;
        void *Template;
        void *InstanceGraph;
        void *ExternalPackage;
    };

    uint8_t *FindMapValueByKey(FScriptMapHelper &h, const char *key) {
        for (int i = 0; i < h.GetMaxIndex(); ++i) {
            if (h.IsValidIndex(i) && narrow(*reinterpret_cast<FName *>(h.GetKeyPtr(i))) == key) {
                return h.GetValuePtr(i);
            }
        }
        return nullptr;
    }

    // RECEIVER: construct a CCD from a portable CcdProfile (the wire payload) — construct + AddPair-rebuild
    // every map. Top struct-valued maps (CharacterItems/Outfits/OutfitItems): AddPair an empty (zeroed) value,
    // then FindMapValueByKey it back and fill in place (set CharacterPiece via StaticLoadObject, bools, and
    // AddPair the override sub-maps). Leaf maps (overrides, BoneScaleValues): AddPair the value directly.
    // Returns the GC-rooted owned CCD (or null). Uses only the proven AddPair primitive (no Rehash).
    UObjectBase *ReconstructCcdFromProfile(const HogwartsMP::Shared::Modules::CcdProfile &in) {
        auto log                 = Log();
        using ConstructFn        = void *(__fastcall *)(const void *);
        static auto *constructFn = reinterpret_cast<ConstructFn>(HogwartsMP::Core::AobFirst(HogwartsMP::Game::gLayout.staticConstructObject));
        static auto *objCls      = reinterpret_cast<UClass *>(FindUClass("Class /Script/CoreUObject.Object"));
        static auto *texCls      = reinterpret_cast<UClass *>(FindUClass("Class /Script/Engine.Texture"));
        if (!constructFn) {
            log->error("[recon] no constructFn");
            return nullptr;
        }
        UObjectBase *refCcd = nullptr;
        auto *arr           = gGlobals.objectArray;
        const int total     = arr ? arr->GetObjectArrayNum() : 0;
        for (int i = 0; i < total && !refCcd; ++i) {
            auto *it = arr->IndexToObject(i);
            if (it && it->Object && narrow(it->Object->GetClass()->GetFName()) == "CustomizableCharacterDefinition") {
                refCcd = it->Object;
            }
        }
        if (!refCcd) {
            log->error("[recon] no reference CustomizableCharacterDefinition resident");
            return nullptr;
        }
        auto *ccdCls = reinterpret_cast<UClass *>(refCcd->GetClass());
        FConstructParams cp{};
        cp.Class  = ccdCls;
        cp.Outer  = refCcd->GetOuter();
        auto *ccd = reinterpret_cast<UObjectBase *>(constructFn(&cp));
        if (!ccd) {
            log->error("[recon] construct returned null");
            return nullptr;
        }
        // TODO(commit 7): per-proxy cache + unroot — every apply roots a fresh CCD and never frees the prior.
        RootObject(ccd);
        auto *ccdBase = reinterpret_cast<uint8_t *>(ccd);
        auto wstr     = [](const std::string &s) { return std::wstring(s.begin(), s.end()); };

        if (auto *gp = FindPropertyInChain(ccdCls, "Gender")) {
            *(ccdBase + gp->GetOffset_ForInternal()) = in.gender;
        }
        if (auto *sp = FindPropertyInChain(ccdCls, "Scale")) {
            *reinterpret_cast<float *>(ccdBase + sp->GetOffset_ForInternal()) = in.scale;
        }

        auto fillPiece = [&](uint8_t *vbase, UStruct *ps, const HogwartsMP::Shared::Modules::CcdPiece &src) {
            // FindPropertyInChain only walks UStruct::SuperStruct, so a UScriptStruct* is safe as UClass*.
            auto *pcls = reinterpret_cast<UClass *>(ps);
            if (auto *cpP = FindPropertyInChain(pcls, "CharacterPiece"); cpP && !src.characterPiece.empty()) {
                if (auto *o = LoadObjectByPath(objCls, wstr(src.characterPiece).c_str())) {
                    *reinterpret_cast<UObjectBase **>(vbase + cpP->GetOffset_ForInternal()) = o;
                }
            }
            auto setBool = [&](const char *n, bool v) {
                if (auto *p = FindPropertyInChain(pcls, n)) {
                    auto *bp      = reinterpret_cast<FBoolProperty *>(p);
                    uint8_t *byte = vbase + bp->GetOffset_ForInternal() + bp->ByteOffset;
                    if (v) {
                        *byte |= bp->ByteMask;
                    }
                    else {
                        *byte &= static_cast<uint8_t>(~bp->ByteMask);
                    }
                }
            };
            setBool("bSetCharacterPieceEvenIfNone", src.setEvenIfNone);
            setBool("bIsFlipped", src.isFlipped);
            auto sub = [&](const char *n) -> FMapProperty * {
                auto *p = FindPropertyInChain(pcls, n);
                return (p && narrow(p->GetClass()->GetFName()) == "MapProperty") ? reinterpret_cast<FMapProperty *>(p) : nullptr;
            };
            if (auto *mp = sub("ScalarOverrides")) {
                FScriptMapHelper h(mp, vbase + mp->GetOffset_ForInternal());
                for (auto &s : src.scalars) {
                    FName k = MakeFName(wstr(s.first).c_str());
                    float v = s.second;
                    h.AddPair(&k, &v);
                }
            }
            if (auto *mp = sub("VectorOverrides")) {
                FScriptMapHelper h(mp, vbase + mp->GetOffset_ForInternal());
                for (auto &v : src.vectors) {
                    FName k    = MakeFName(wstr(v.first).c_str());
                    float c[4] = {v.second[0], v.second[1], v.second[2], v.second[3]};
                    h.AddPair(&k, c);
                }
            }
            if (auto *mp = sub("TextureOverrides")) {
                FScriptMapHelper h(mp, vbase + mp->GetOffset_ForInternal());
                for (auto &t : src.textures) {
                    if (t.second.empty()) {
                        continue;
                    }
                    auto *tex = LoadObjectByPath(texCls, wstr(t.second).c_str());
                    if (!tex) {
                        continue;
                    }
                    FName k = MakeFName(wstr(t.first).c_str());
                    h.AddPair(&k, &tex);
                }
            }
        };

        auto fillPieceMap = [&](uint8_t *mapBase, FMapProperty *mp, const HogwartsMP::Shared::Modules::CcdPieceMap &src) {
            auto *vStr = (narrow(mp->ValueProp->GetClass()->GetFName()) == "StructProperty") ? static_cast<FStructProperty *>(mp->ValueProp)->Struct : nullptr;
            if (!vStr) {
                return;
            }
            std::vector<uint8_t> zero(static_cast<size_t>(mp->ValueProp->ElementSize), 0);
            FScriptMapHelper h(mp, mapBase);
            for (auto &e : src) {
                FName k = MakeFName(wstr(e.first).c_str());
                h.AddPair(&k, zero.data());
                if (auto *hv = FindMapValueByKey(h, e.first.c_str())) {
                    fillPiece(hv, reinterpret_cast<UStruct *>(vStr), e.second);
                }
            }
        };

        if (auto *p = FindPropertyInChain(ccdCls, "BoneScaleValues"); p && narrow(p->GetClass()->GetFName()) == "MapProperty") {
            auto *mp = reinterpret_cast<FMapProperty *>(p);
            FScriptMapHelper h(mp, ccdBase + p->GetOffset_ForInternal());
            for (auto &b : in.boneScales) {
                FName k = MakeFName(wstr(b.first).c_str());
                float v = b.second;
                h.AddPair(&k, &v);
            }
        }
        if (auto *p = FindPropertyInChain(ccdCls, "CharacterItems"); p && narrow(p->GetClass()->GetFName()) == "MapProperty") {
            fillPieceMap(ccdBase + p->GetOffset_ForInternal(), reinterpret_cast<FMapProperty *>(p), in.characterItems);
        }
        if (auto *p = FindPropertyInChain(ccdCls, "Outfits"); p && narrow(p->GetClass()->GetFName()) == "MapProperty") {
            auto *mp   = reinterpret_cast<FMapProperty *>(p);
            auto *oStr = (narrow(mp->ValueProp->GetClass()->GetFName()) == "StructProperty") ? static_cast<FStructProperty *>(mp->ValueProp)->Struct : nullptr;
            std::vector<uint8_t> zero(static_cast<size_t>(mp->ValueProp->ElementSize), 0);
            FScriptMapHelper h(mp, ccdBase + p->GetOffset_ForInternal());
            for (auto &o : in.outfits) {
                FName k = MakeFName(wstr(o.first).c_str());
                h.AddPair(&k, zero.data());
                auto *ov = FindMapValueByKey(h, o.first.c_str());
                if (ov && oStr) {
                    if (auto *oiP = FindPropertyInChain(reinterpret_cast<UClass *>(oStr), "OutfitItems"); oiP && narrow(oiP->GetClass()->GetFName()) == "MapProperty") {
                        fillPieceMap(ov + oiP->GetOffset_ForInternal(), reinterpret_cast<FMapProperty *>(oiP), o.second);
                    }
                }
            }
        }
        return ccd;
    }

    // Give the proxy a synchronous Sebastian base (so its CacheCCD is populated), then LayerCCDOverTarget
    // `newCcd` over it + ReloadCharacter. Confirmed in-game: this renders on a runtime-added proxy CCC.
    void ApplyCcdToProxy(UObjectBase *proxyCcc, UObjectBase *newCcd) {
        auto log         = Log();
        const FName base = MakeFName(L"SebastianSallow");
        struct {
            FName InRegistryID;
        } sid{base};
        CallUFunction(proxyCcc, "SetCharacterID", &sid);
        struct {
            bool a, r;
        } al{false, false};
        CallUFunction(proxyCcc, "SetAsyncLoad", &al);
        CallUFunction(proxyCcc, "ReloadCharacter", nullptr);
        auto *target = ReadObjectProperty(proxyCcc, "CacheCCD");
        if (!target) {
            log->warn("[ccdmirror] proxy CacheCCD null after base build");
            return;
        }
        UObjectBase *ugc = nullptr;
        auto *arr        = gGlobals.objectArray;
        const int total  = arr ? arr->GetObjectArrayNum() : 0;
        for (int i = 0; i < total && !ugc; ++i) {
            auto *it = arr->IndexToObject(i);
            if (it && it->Object && narrow(it->Object->GetClass()->GetFName()) == "UGCBlueprintLibrary") {
                ugc = it->Object;
            }
        }
        auto *fn = ugc ? FindFunctionInChain(ugc, "LayerCCDOverTarget") : nullptr;
        if (!fn) {
            log->warn("[ccdmirror] no LayerCCDOverTarget");
            return;
        }
        std::vector<uint8_t> buf(fn->ParmsSize, 0);
        auto setP = [&](const char *n, const void *src, size_t sz) {
            for (FField *p = fn->ChildProperties; p; p = p->Next) {
                if (narrow(p->GetFName()) == n) {
                    std::memcpy(buf.data() + static_cast<FProperty *>(p)->GetOffset_ForInternal(), src, sz);
                    return;
                }
            }
        };
        setP("NewCCD", &newCcd, sizeof(void *));
        setP("TargetCCD", &target, sizeof(void *));
        const bool incl = true;
        setP("bIncludeHead", &incl, 1);
        reinterpret_cast<UObject *>(ugc)->ProcessEvent(fn, buf.data());
        CallUFunction(proxyCcc, "ReloadCharacter", nullptr);
        log->info("[ccdmirror] LayerCCDOverTarget over proxy + reload");
    }

    // Network path: reconstruct a CCD from the wire profile, then layer it. Needs StaticConstructObject
    // (currently stale in game_layout.h on this build — must be re-derived before the network path renders).
    void ApplyCcdProfileToProxy(UObjectBase *proxyCcc, const HogwartsMP::Shared::Modules::CcdProfile &prof) {
        if (!proxyCcc) {
            Log()->error("[ccdmirror] no proxy CCC");
            return;
        }
        auto *ccd = ReconstructCcdFromProfile(prof);
        if (!ccd) {
            Log()->error("[ccdmirror] reconstruct failed");
            return;
        }
        ApplyCcdToProxy(proxyCcc, ccd);
    }

    // Local preview: harvest the LOCAL player's CCD, then reconstruct + layer it — the same path the network
    // receiver uses, so this validates the full round-trip.
    void MirrorLocalCcdToProxyCccImpl(UObjectBase *proxyCcc) {
        HogwartsMP::Shared::Modules::CcdProfile prof;
        if (!HogwartsMP::Core::AppearanceDump::BuildLocalCcd(prof)) {
            Log()->error("[ccdmirror] BuildLocalCcd failed");
            return;
        }
        ApplyCcdProfileToProxy(proxyCcc, prof);
    }
} // namespace

namespace HogwartsMP::Core::CcdWire {
    void MirrorLocalCcdToProxyCcc(void *proxyCcc) {
        MirrorLocalCcdToProxyCccImpl(reinterpret_cast<UObjectBase *>(proxyCcc));
    }

    void MirrorCcdToProxyCcc(void *proxyCcc, const HogwartsMP::Shared::Modules::CcdProfile &profile) {
        ApplyCcdProfileToProxy(reinterpret_cast<UObjectBase *>(proxyCcc), profile);
    }
} // namespace HogwartsMP::Core::CcdWire
