#pragma once

#include <cstddef>

namespace HogwartsMP::Core::StudentProxy {
    // Thread-safe request setters; the actual spawn/despawn happens on the
    // next engine tick via ProcessPending (game thread required: SpawnActor,
    // StaticLoadObject and ProcessEvent are not safe off-thread).
    void RequestSpawn(int count);
    void RequestDespawnAll();

    // Game-thread pump — Playground_Tick calls this once per engine tick.
    void ProcessPending();

    size_t ActiveCount();
} // namespace HogwartsMP::Core::StudentProxy
