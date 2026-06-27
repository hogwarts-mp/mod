#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace HogwartsMP::Core {
    // Server selection handed over by the pre-launch launcher (HogwartsMP.exe). The
    // launcher writes connect.json next to the client DLL; the client reads it on the
    // way through the Menu state to auto-connect instead of prompting in-game.
    struct LaunchConfig {
        std::string host;
        int32_t port = 27015;
        std::string nickname;
    };

    // Read connect.json from this module's directory. If `consume` is true the file is
    // deleted after a successful read so a later disconnect->menu doesn't auto-reconnect
    // in a loop. Returns nullopt if the file is absent/invalid or has no host.
    std::optional<LaunchConfig> ReadConnectConfig(bool consume);
} // namespace HogwartsMP::Core
