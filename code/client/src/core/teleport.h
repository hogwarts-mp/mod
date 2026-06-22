#pragma once
// Named fast-travel teleport via the game's FastTravelManager. Shared by the dev UI
// (ui/teleport_manager) and the scripting builtin (builtins/game LocalPlayer.fastTravel).

#include <span>
#include <string_view>

namespace HogwartsMP::Core {
    // Known fast-travel destination names (game FastTravel point names). Stable for the process.
    // Curated list — may not be exhaustive; add a name here (or file an issue) to expose more.
    std::span<const std::string_view> FastTravelLocations();

    // Teleport the local player to a named fast-travel point. Returns false if `name` is not a known
    // destination (see FastTravelLocations) or the FastTravelManager isn't available right now. MUST
    // run on the game thread (does ProcessEvent + a GUObjectArray scan).
    bool FastTravelTo(std::string_view name);
} // namespace HogwartsMP::Core
