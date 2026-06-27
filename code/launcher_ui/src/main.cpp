/*
 * HogwartsMP launcher — process entry point.
 *
 * Windowed CEF host (FiveM-style) that also injects the game. Entry is `main` (not wWinMain):
 * FrameworkLoader forces /ENTRY:mainCRTStartup + /SUBSYSTEM:windows, so the CRT calls main()
 * yet no console window appears. The same exe is reused as its own CEF subprocess via
 * CefExecuteProcess (children must run THIS app so the hmp:// scheme + render-side router
 * match the browser). On connect the UI arms a launch and closes; after CEF tears down we run
 * it here (pak fetch + inject + game start) — a Steam relaunch + DLL injection can't coexist
 * with a live CEF message loop.
 */

#include <windows.h>

#include "include/cef_app.h"

#include "game_launch.h"
#include "launcher_app.h"

int main() {
    CefMainArgs mainArgs(::GetModuleHandleW(nullptr));
    CefRefPtr<HogwartsMP::LauncherUI::LauncherApp> app(new HogwartsMP::LauncherUI::LauncherApp());

    // Sub-process dispatch: renderer/gpu/utility processes re-enter here and return >= 0.
    const int exitCode = CefExecuteProcess(mainArgs, app, nullptr);
    if (exitCode >= 0) {
        return exitCode;
    }

    CefSettings settings;
    settings.no_sandbox                  = true;
    settings.multi_threaded_message_loop = false;
    settings.log_severity                = LOGSEVERITY_WARNING;

    if (!CefInitialize(mainArgs, settings, app, nullptr)) {
        return 1;
    }

    CefRunMessageLoop();
    CefShutdown();

    // CEF is gone — safe to run the Steam relaunch + DLL injection if the user picked a server.
    if (HogwartsMP::LauncherUI::GameLaunch::LaunchPending()) {
        return HogwartsMP::LauncherUI::GameLaunch::RunPendingLaunch();
    }
    return 0;
}
