#pragma once

#include <string>

// Game-thread pump (called once per engine tick from the EngineTick hook):
// drains the dev-menu request queues and runs the student/appearance pumps.
void Playground_Tick();

namespace HogwartsMP::Core::Playground {
    // Queue a spawn/destroy from the HUD event bridge (any thread); the work runs
    // on the game thread in Playground_Tick. Spawn uses a fixed dev location;
    // Destroy removes everything spawned this way.
    void RequestSpawnActor(const std::string &objectPath);
    void RequestDestroyActors();
} // namespace HogwartsMP::Core::Playground
