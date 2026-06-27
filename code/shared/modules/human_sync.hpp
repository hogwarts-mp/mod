#pragma once

#include <cstdint>

namespace HogwartsMP::Shared::Modules {
    struct HumanSync {
        // Packed boolean per-tick state. Its own delta Field (one byte): these toggle often (InAir on
        // every jump), so keeping them out of UpdateData means a jump doesn't re-send the string. Add new
        // boolean states (wand drawn, hooded, crouch, swim) as further bits here.
        enum StateFlag : uint8_t {
            Mounted = 1u << 0, // riding a broom (broom sync — Phase 4 commit 11)
            InAir   = 1u << 1, // jumping/falling — proxy plays the fall clip; vertical arc from synced pos
            Dodge   = 1u << 2, // mid dodge-roll; proxy plays the roll montage on the rising edge (spell commit)
            Cast    = 1u << 3, // casting; proxy plays the cast montage on the rising edge (spell commit)
            Lumos   = 1u << 4, // wand light on; sustained state (spell commit)
        };

        // Per-tick string-ish payload replicated through HumanEntity. Its own delta Field. POD only
        // (delta-tracked as a whole by VariableDeltaSerializer), so it must stay trivially copyable.
        struct UpdateData {
            // Which broom (1-based id into the MountClasses allowlist; 0 = default/unknown). Broom commit.
            uint8_t mountId  = 0;
            // Which spell (1-based id into the SpellRecords allowlist; 0 = none/unknown). Spell commit.
            uint8_t spellId  = 0;
            // Aim pitch (deg, -90..90) for an in-flight cast — the proxy rebuilds the cast direction from
            // synced facing-yaw + this. Spell commit.
            int8_t aimPitch  = 0;
        };
    };
} // namespace HogwartsMP::Shared::Modules
