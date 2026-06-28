#pragma once

// Ambient population control. Toggles the game's whole ambient-NPC ("Hobo"/Tier crowd) system on/off by
// no-oping UPopulationManager's per-frame tick. With it OFF, the manager never requests flesh, so
// NPCs are never spawned.

namespace HogwartsMP::Core::Hooks {
    // enabled=true => vanilla ambient population; false => no ambient NPCs. Default ON.
    // Exposed so a console command / scripting builtin can let server owners flip it.
    void SetAmbientPopulation(bool enabled);
    void ToggleAmbientPopulation();
    bool IsAmbientPopulationEnabled();
} // namespace HogwartsMP::Core::Hooks
