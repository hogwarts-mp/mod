#pragma once

#include <cstddef>

class AActor;
class UObjectBase;

namespace HogwartsMP::Core::StudentProxy {
    enum class House : int { Gryffindor = 0, Slytherin = 1, Ravenclaw = 2, Hufflepuff = 3 };

    // Visual identity for a proxy. Maps to SpawnStudent's per-gender outfit +
    // per-house tint/crest overlay (see kit_params_houses.h).
    struct Appearance {
        bool female = false;
        int  house  = 0; // House value; clamped to [0,3] when applied
    };

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

    // Registry-free spawn/destroy for remote-player avatars (game thread
    // only). appearance selects gender + house; outSkinComp receives the skin
    // component for anim playback.
    AActor *SpawnProxy(float x, float y, float z, float yawDeg, Appearance appearance, UObjectBase **outSkinComp);
    void DestroyProxy(AActor *actor);
} // namespace HogwartsMP::Core::StudentProxy
