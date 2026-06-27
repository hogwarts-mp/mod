/*
 * HogwartsMP launcher — connect hand-off + in-process game launch.
 *
 * Single-exe model: the CEF UI and the injector live in one process. On connect we only
 * record the choice (write connect.json, which the injected client reads on init — chosen
 * over launch args / env because the Steam relaunch drops them) and close the window. The
 * real launch — fetch the avatar pak, inject HogwartsMPClient.dll, start the game — runs
 * from main() AFTER CefShutdown(), because a Steam relaunch + DLL injection can't share the
 * process with a live CEF message loop.
 */

#pragma once

#include <string>

namespace HogwartsMP::LauncherUI::GameLaunch {
    // Parse `address` (host[:port], optionally a hogwartsmp://join/ URL), write the
    // connect-config next to this exe, and arm the launch. Returns true on success; the
    // caller then closes the window. Does NOT launch (see RunPendingLaunch).
    bool PrepareConnect(const std::string &address);

    // True once PrepareConnect has armed a launch. main() checks this after the message
    // loop to decide whether to proceed.
    bool LaunchPending();

    // Fetch the avatar pak, then inject + launch the game. Call ONLY after CefShutdown.
    // Returns a process exit code (0 ok, non-zero on launch failure).
    int RunPendingLaunch();
} // namespace HogwartsMP::LauncherUI::GameLaunch
