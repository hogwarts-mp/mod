#pragma once

#include <networking/replication/network_entity.h>

#include <logging/logger.h>

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace HogwartsMP::Shared::Modules {
    // A networked player appearance — the data needed to clone a player's worn look onto their remote
    // proxy. Sent ONCE in the entity construction snapshot (and re-sent on an outfit change), not per
    // tick. Built from the local player's CustomizableCharacterComponent (the player carries a CCC just
    // like NPCs; every worn item is its own SkeletalMeshComponent with a loadable mesh + materials).
    //
    // Tier 1: per-slot mesh + the on-disk material-instance parent path (face shape, hair style, outfit).
    // Tier 2 (this): the curated identity param OVERRIDES per material — colour vectors (skin tint,
    // hair/iris/brow colours, robe house tint), identity textures (skin/complexion/eyebrows/freckles/
    // scars/eye/haircap), and the non-rig scalars. The head material's facial-rig morph scalars
    // (brows_up_out_r, eye_blink_l, smile_r, …) are runtime ANIMATION (0 at rest) and are NOT carried.

    // One worn material: the on-disk MI parent + the parameter overrides to recreate as a dynamic MID.
    inline bool AppearancePathAllowed(const std::string &path) {
        return path.empty() ||
               path.rfind("/Game/RiggedObjects/", 0) == 0 ||              // skeletal meshes + tileable textures
               path.rfind("/Game/Data/", 0) == 0 ||                      // CCD CharacterPiece DAs + MaterialPropertyData (/Game/Data/CC/...)
               path.rfind("/Game/Environment/MasterMaterials/", 0) == 0; // master materials
    }

    // Allowed-or-log: same as AppearancePathAllowed, but warns with the dropped path + a kind label so a
    // legitimate-but-missing content root shows up in the (server) log instead of silently becoming a
    // default/Sebastian body. Empty paths are allowed and never logged.
    inline bool AppearancePathAllowedLog(const std::string &path, const char *kind) {
        if (AppearancePathAllowed(path)) {
            return true;
        }
        Framework::Logging::GetLogger("Appearance")->warn("[sanitize] dropped {} path (not allowlisted): {}", kind, path);
        return false;
    }

    // Drop any asset path outside the allowlist (cleared to "") and clamp counts. Server-side gate before
    // storing/relaying an owner-supplied profile.

    inline constexpr uint32_t kMaxCcdPieces     = 32;  // per CharacterItems / per OutfitItems
    inline constexpr uint32_t kMaxCcdOutfits    = 8;
    inline constexpr uint32_t kMaxCcdBoneScales = 64;
    inline constexpr uint32_t kMaxCcdOverrides  = 256; // per kind (scalar/vector/texture) per piece

    struct CcdPiece {
        std::string characterPiece; // loadable DA path ("" = none / explicit-empty when setEvenIfNone)
        bool setEvenIfNone = false;
        bool isFlipped     = false;
        std::vector<std::pair<std::string, float>> scalars;              // ScalarOverrides: paramName -> value
        std::vector<std::pair<std::string, std::array<float, 4>>> vectors; // VectorOverrides: paramName -> RGBA
        std::vector<std::pair<std::string, std::string>> textures;       // TextureOverrides: paramName -> loadable path
    };

    // name -> Piece (used for CharacterItems and for an outfit's OutfitItems).
    using CcdPieceMap = std::vector<std::pair<std::string, CcdPiece>>;

    struct CcdProfile {
        uint8_t gender = 0;
        float scale    = 1.0f;
        std::vector<std::pair<std::string, float>> boneScales;     // BoneScaleValues
        CcdPieceMap characterItems;                                // body: head/Hair/Arms/Legs
        std::vector<std::pair<std::string, CcdPieceMap>> outfits;  // outfitName -> OutfitItems
    };

    inline void SerializeCcdPiece(Framework::Networking::Replication::FieldSerializer &fs, CcdPiece &p) {
        fs.Field(p.characterPiece);
        fs.Field(p.setEvenIfNone);
        fs.Field(p.isFlipped);
        uint32_t sc = static_cast<uint32_t>(p.scalars.size());
        fs.Field(sc);
        if (!fs.Writing()) {
            p.scalars.resize(sc > kMaxCcdOverrides ? 0 : sc);
        }
        for (auto &s : p.scalars) {
            fs.Field(s.first);
            fs.Field(s.second);
        }
        uint32_t vc = static_cast<uint32_t>(p.vectors.size());
        fs.Field(vc);
        if (!fs.Writing()) {
            p.vectors.resize(vc > kMaxCcdOverrides ? 0 : vc);
        }
        for (auto &v : p.vectors) {
            fs.Field(v.first);
            for (int k = 0; k < 4; ++k) {
                fs.Field(v.second[k]);
            }
        }
        uint32_t tc = static_cast<uint32_t>(p.textures.size());
        fs.Field(tc);
        if (!fs.Writing()) {
            p.textures.resize(tc > kMaxCcdOverrides ? 0 : tc);
        }
        for (auto &t : p.textures) {
            fs.Field(t.first);
            fs.Field(t.second);
        }
    }

    inline void SerializeCcdPieceMap(Framework::Networking::Replication::FieldSerializer &fs, CcdPieceMap &m) {
        uint32_t n = static_cast<uint32_t>(m.size());
        fs.Field(n);
        if (!fs.Writing()) {
            m.resize(n > kMaxCcdPieces ? 0 : n);
        }
        for (auto &e : m) {
            fs.Field(e.first);
            SerializeCcdPiece(fs, e.second);
        }
    }

    inline void SerializeCcd(Framework::Networking::Replication::FieldSerializer &fs, CcdProfile &c) {
        fs.Field(c.gender);
        fs.Field(c.scale);
        uint32_t bc = static_cast<uint32_t>(c.boneScales.size());
        fs.Field(bc);
        if (!fs.Writing()) {
            c.boneScales.resize(bc > kMaxCcdBoneScales ? 0 : bc);
        }
        for (auto &b : c.boneScales) {
            fs.Field(b.first);
            fs.Field(b.second);
        }
        SerializeCcdPieceMap(fs, c.characterItems);
        uint32_t oc = static_cast<uint32_t>(c.outfits.size());
        fs.Field(oc);
        if (!fs.Writing()) {
            c.outfits.resize(oc > kMaxCcdOutfits ? 0 : oc);
        }
        for (auto &o : c.outfits) {
            fs.Field(o.first);
            SerializeCcdPieceMap(fs, o.second);
        }
    }

    // Server-side gate: drop any DA/texture path outside the allowlist (a peer can't make others
    // StaticLoadObject arbitrary assets). Counts are already clamped on read by the serializer.
    inline void SanitizeCcdPiece(CcdPiece &p) {
        if (!AppearancePathAllowedLog(p.characterPiece, "CharacterPiece")) {
            p.characterPiece.clear();
        }
        for (auto &t : p.textures) {
            if (!AppearancePathAllowedLog(t.second, "ccd-texture")) {
                t.second.clear();
            }
        }
    }
    inline void SanitizeCcd(CcdProfile &c) {
        for (auto &e : c.characterItems) {
            SanitizeCcdPiece(e.second);
        }
        for (auto &o : c.outfits) {
            for (auto &e : o.second) {
                SanitizeCcdPiece(e.second);
            }
        }
    }
} // namespace HogwartsMP::Shared::Modules
