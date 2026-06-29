#pragma once

namespace HogwartsMP::Core::SubstrateLoader {
    // Auto-load the substrate save so the client boots straight into the world (skips HL's menu).
    // Call every tick from PostUpdate UNCONDITIONALLY — the front-end is itself a world with a local
    // player, so it can't sit behind the !world/!localPlayer guards. Game-thread; self-latches.
    void TryAutoLoad();
} // namespace HogwartsMP::Core::SubstrateLoader
