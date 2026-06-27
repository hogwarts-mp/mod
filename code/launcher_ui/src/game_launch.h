/*
 * HogwartsMP pre-launch launcher — connect hand-off + game launch.
 *
 * Writes the chosen server to a small connect-config JSON that the injected client reads
 * on init (chosen over launch args / env vars because the Steam relaunch can drop them),
 * then spawns the existing injector (HogwartsMPLauncher.exe) which starts + injects the
 * game. The launcher closes itself afterwards.
 */

#pragma once

#include <string>

namespace HogwartsMP::LauncherUI::GameLaunch {
    // Parse `address` (host[:port], optionally a hogwartsmp://join/ URL), write the
    // connect-config next to this exe, and spawn the injector. Returns true on success.
    bool ConnectAndLaunch(const std::string &address);
} // namespace HogwartsMP::LauncherUI::GameLaunch
