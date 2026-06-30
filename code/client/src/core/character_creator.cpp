#include <utils/safe_win32.h>

#include "character_creator.h"

#include "application.h"
#include "appearance_dump.h"
#include "sdk/natives/ue4_natives.h"
#include "sdk/reflection/ue4_reflection.h"

#include "UObject/Class.h"
#include "UObject/UObjectArray.h"
#include "UObject/UnrealType.h"

#include <logging/logger.h>
#include <nlohmann/json.hpp>

#include <mutex>
#include <string>
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
        enum Kind { Outfit, AddOn, Vec, Scal, Bone, Scale, Reload, Enumerate } kind;
        std::string a;
        std::string b;
        float v[4]{};
    };

    std::mutex g_mtx;
    std::vector<Cmd> g_queue;

    void Enqueue(Cmd c) {
        std::lock_guard<std::mutex> lock(g_mtx);
        g_queue.push_back(std::move(c));
    }

    // The local player pawn (BP_Biped_Player_C) owns a CCC named "Customization".
    UObjectBase *FindLocalPlayerCCC() {
        auto *lp = gGlobals.localPlayer;
        if (!lp || !lp->PlayerController || !lp->PlayerController->Pawn) {
            return nullptr;
        }
        auto *pawn = reinterpret_cast<UObjectBase *>(lp->PlayerController->Pawn);
        auto *arr  = gGlobals.objectArray;
        if (!arr) {
            return nullptr;
        }
        const int total = arr->GetObjectArrayNum();
        for (int i = 0; i < total; ++i) {
            auto *it = arr->IndexToObject(i);
            if (it && it->Object && it->Object->GetOuter() == pawn &&
                narrow(it->Object->GetClass()->GetFName()) == "CustomizableCharacterComponent") {
                return it->Object;
            }
        }
        return nullptr;
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
