/*
 * HogwartsMP pre-launch launcher — process entry point.
 *
 * Windowed CEF host (FiveM-style). The same exe is reused as the CEF subprocess via
 * CefExecuteProcess, so no separate helper executable is required.
 */

#include <windows.h>

#include "include/cef_app.h"

#include "launcher_app.h"

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    CefMainArgs mainArgs(hInstance);
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
    return 0;
}
