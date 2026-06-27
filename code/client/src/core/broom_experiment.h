#pragma once

// Broom rider for remote-avatar proxies — the broom is per-rider state.
//
//   Mount:    resolve the rideable broom class from a broom inventory item's
//             class defaults (FlyingBroomClass), spawn the broom at the
//             rider, attach the rider to the broom mesh at its seat socket
//             ("playerAttachSocket"), play the riding anims.
//   Dismount: dismount anim, detach (keep world), destroy the broom, restore
//             the proxy's baked AnimBP.
//
// BroomRider is the production mount handler driven by the mounted-state flag
// in human.cpp; BroomExperiment is a dev harness (offline smoke test) that
// mounts the first dev-spawned student proxy.
//
// Same threading contract as StudentProxy: requests queue, the work runs on
// the engine tick via ProcessPending().

class AActor;
class UObjectBase;

namespace HogwartsMP::Core::BroomRider {
    // Spawn a broom at the rider and seat them on it. broomClassName is the
    // rideable class short name from the wire (empty/unknown -> default
    // broom). Plays the hover idle on animMesh. Returns the broom actor.
    AActor *Mount(AActor *rider, UObjectBase *animMesh, const char *broomClassName);

    // Detach the rider (keep world), restore the baked AnimBP, destroy the broom.
    void Dismount(AActor *rider, UObjectBase *animMesh, AActor *broom);
} // namespace HogwartsMP::Core::BroomRider

namespace HogwartsMP::Core::BroomExperiment {
    void RequestMountStudent();    // needs an active dev student proxy
    void RequestDismountStudent();
    void RequestCleanup();         // destroy a leftover broom

    // Game-thread pump — Playground_Tick calls this once per engine tick.
    void ProcessPending();

    // One-line status for the dev UI.
    const char *Status();
} // namespace HogwartsMP::Core::BroomExperiment
