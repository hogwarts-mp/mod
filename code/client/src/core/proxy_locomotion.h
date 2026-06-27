#pragma once

// On-foot locomotion clip selection for remote-avatar proxies.
//
// The proxy is moved by the network position (movement disabled), so its movement component velocity
// stays 0 and an AnimBP that reads velocity would idle. This module picks a gait from the avatar's
// ground speed (derived from successive synced positions) and either drives our packed
// ABP_RemoteAvatar's Speed input (smooth blend) or, as a fallback, plays an in-place locomotion clip
// raw on the body mesh — the network position drives the actual travel either way.
//
// Single source of truth for the gait clips + speed bands; used by the live remote-human sync
// (modules/human.cpp). Game thread only (PlayAnimation / StaticLoadObject).

class UObjectBase;

namespace HogwartsMP::Core::ProxyLocomotion {
    enum class Gait { Idle = 0, Walk = 1, Run = 2, Sprint = 3 };

    // Representative ground speed (UE cm/s) for a gait.
    float SpeedForGait(Gait gait);

    // Map a horizontal ground speed to a gait band, sticky against `current` (hysteresis), so a speed
    // hovering near a boundary doesn't flip-flop the clip (which would restart it = dropped-frame look).
    Gait GaitForSpeed(float speedCmPerSec, Gait current);

    // Short name ("idle"/"walk"/"run"/"sprint") for logging.
    const char *Name(Gait gait);

    // Play the looping locomotion clip for `gait` on the proxy's body mesh (single-node, crossfaded).
    void PlayGait(UObjectBase *skinComp, Gait gait);

    // Play the looping in-air (fall) clip — used while a remote avatar is airborne; the vertical arc
    // comes from the synced position.
    void PlayAir(UObjectBase *skinComp);

    // ── Single-node move blendspace (smooth walk/jog/run) ─────────────────────
    // Play the game's 1D forward-move blendspace looped on the mesh, then steer it by ground speed each
    // frame. Used moving-only (the move space bottoms out at a slow walk, not a true idle, so callers
    // hold a discrete idle clip at rest).
    bool PlayMoveBlend(UObjectBase *skinComp);       // false if the asset failed to load
    bool DriveMoveBlend(UObjectBase *skinComp, float speedCmPerSec); // false if not reflected in this build
} // namespace HogwartsMP::Core::ProxyLocomotion
