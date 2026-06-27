#pragma once

#include <cstddef>

class AActor;
class UObjectBase;

namespace HogwartsMP::Core::StudentProxy {
    // Thread-safe request setters; the actual spawn/despawn happens on the
    // next engine tick via ProcessPending (game thread required: SpawnActor,
    // StaticLoadObject and ProcessEvent are not safe off-thread).
    void RequestSpawn(int count);
    void RequestDespawnAll();

    // Game-thread pump — Playground_Tick calls this once per engine tick.
    void ProcessPending();

    size_t ActiveCount();

    // First still-alive spawned student (nullptr if none) — game thread only.
    // Used by the broom experiment as the rider.
    AActor *FirstActive();

    // Skin (head+hands) SkeletalMeshComponent of FirstActive() — the outfit is
    // master-posed to it, so playing an AnimSequence here drives the whole
    // student. Only valid while FirstActive() is alive.
    UObjectBase *FirstActiveSkin();

    // Spawn a remote-avatar proxy (BP_RemoteAvatarCCC from the pak). outCcc receives the proxy's
    // CustomizableCharacterComponent so the caller can apply the player's CCD (see CcdWire); outMesh
    // (optional) receives CharacterMesh0, the body skeletal mesh used as the locomotion anim target.
    // Game thread only.
    AActor *SpawnProxy(float x, float y, float z, float yawDeg, UObjectBase **outCcc, UObjectBase **outMesh = nullptr);
    void DestroyProxy(AActor *actor);
} // namespace HogwartsMP::Core::StudentProxy
