#pragma once

#include <string>

// Character creator — drives the LOCAL player's CustomizableCharacterComponent
// (CCC) live from the web UI so a player can build their per-server look.
//
// Because this edits the real local CCC (not a remote proxy), every change
// renders at full fidelity — including the swatch-tinted robes / house crest
// that material-copy can't reproduce on remote avatars. The resulting look is
// then captured (AppearanceDump::BuildLocalCcd) and sent over the existing
// appearance wire so other clients see it on their proxies.
//
// All Request* calls are safe to invoke from the web event callbacks (any
// thread); they only enqueue. The actual CCC mutation (ProcessEvent / reflection)
// runs on the game thread in ProcessPending, drained from Playground_Tick.

namespace HogwartsMP::Core::CharacterCreator {
    // ── Apply: one edit per call, queued and applied on the game thread ──────
    // Pick a full outfit from the CCC's Outfits map (the house robe set lives here).
    void RequestSetOutfit(const std::string &outfitName);
    // Equip/swap one character piece (hair, brows, face, markings, ...). Names come
    // from the registry (the AvatarPresets DA_ ids the UI lists).
    void RequestSetAddOnMesh(const std::string &pieceType, const std::string &pieceName);
    // Explicit colour override on a mesh's material (eye/skin/hair/brow/robe tint).
    void RequestSetVectorParam(const std::string &meshName, const std::string &param, float r, float g, float b, float a);
    // Explicit scalar override (e.g. a complexion / roughness slider).
    void RequestSetScalarParam(const std::string &meshName, const std::string &param, float value);
    // Face/body morph slider.
    void RequestSetBoneSlider(const std::string &boneName, float value);
    // Overall character scale.
    void RequestSetScale(float scale);

    // Rebuild the character after a batch of edits (async build inside the CCC).
    // Call once after applying a group of changes, not per-edit.
    void RequestReload();

    // Read the local CCC's current option set (outfits / worn items) and log it as
    // JSON. A discovery aid so the UI knows the valid keys to feed the Request* calls;
    // pushing this straight to the web view is a later slice.
    void RequestEnumerate();

    // Apply option `index` (1-based) of a creator category (hairStyle / hairColour /
    // faceShape / skin / eyeColour / browShape / browColour) to the live player CCC via
    // AvatarPresetsManager::LoadPreset. The category's preset-type + name list are
    // resolved once (gender-aware) and cached.
    void RequestApplyPreset(const std::string &category, int index);

    // Set the player's voice pitch (AvaAudio::SetPlayerVoicePitch).
    void RequestSetVoicePitch(int value);

    // SPIKE: play a sample line in the player's voice at the current pitch (preview).
    void RequestVoicePreview();

    // Framing camera for the live preview: spawn/reposition a CameraActor in front of the
    // player (front view, given distance/height/pitch/fov) and make it the view target, so
    // the character is framed in the transparent viewport. Restore returns the player cam.
    void RequestCameraFrame(float dist, float height, float pitch, float fov, float shift);
    void RequestCameraRestore();

    // Rotate the live player by a yaw delta (drag-to-inspect). Freeze pauses the idle
    // animation so the character holds still (frozen=false resumes).
    void RequestRotate(float deltaYaw);
    void RequestFreeze(bool frozen);

    // Appearance undo: snapshot the live CCC's CacheCCD when the creator opens; restore it on
    // cancel (leave without Confirm); release the snapshot on Confirm (keep changes).
    void RequestSnapshotAppearance();
    void RequestRestoreAppearance();
    void RequestReleaseSnapshot();

    // Game-thread pump — drains the queues above. Called from Playground_Tick.
    void ProcessPending();
} // namespace HogwartsMP::Core::CharacterCreator
