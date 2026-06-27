/*
 * HogwartsMP pre-launch launcher — custom scheme serving the web UI (remote + cache).
 *
 * The React build uses ES module <script type="module"> tags, which Chromium refuses to
 * load over file:// (origin "null" -> CORS). Serving the UI over a registered standard
 * scheme (hmp://app/) gives it a real, secure origin so modules load and fetch() works.
 *
 * Thin-shell model: the UI is NOT bundled with the exe. Each resource is fetched from a
 * remote base URL (the GitHub Pages build) into a local cache dir, then served from that
 * cache. When the remote is unreachable the last-good cache is served, so the launcher
 * still renders offline after one successful run.
 */

#pragma once

#include "include/cef_scheme.h"

#include <filesystem>
#include <string>

namespace HogwartsMP::LauncherUI {
    inline constexpr char kAppScheme[] = "hmp";
    inline constexpr char kAppHost[]   = "app";
    inline constexpr char kAppUrl[]    = "hmp://app/index.html";

    // GitHub Pages build of the launcher UI (matches the in-game UI convention,
    // https://hogwarts-mp.github.io/assets/...). Override with HOGWARTSMP_LAUNCHER_REMOTE
    // to point at a local static server while iterating.
    inline constexpr char kDefaultRemoteBase[] = "https://hogwarts-mp.github.io/assets/launcher/";

    // Register the custom scheme. Call from CefApp::OnRegisterCustomSchemes (runs in
    // every process so they agree on the scheme's properties).
    void RegisterLauncherSchemes(CefRawPtr<CefSchemeRegistrar> registrar);

    // Register the handler factory that serves hmp://app/. Most paths are mirrored from
    // `remoteBase` into `cacheDir` (remote + cache). Paths under hmp://app/local/ are served
    // straight from `localDir` (assets shipped next to the exe, e.g. fonts) — never remote,
    // so they're always present regardless of network or our hosting. Call once in the
    // browser process after CefInitialize.
    void RegisterLauncherSchemeHandlerFactory(const std::string &remoteBase, const std::filesystem::path &cacheDir, const std::filesystem::path &localDir);
} // namespace HogwartsMP::LauncherUI
