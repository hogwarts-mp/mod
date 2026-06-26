#pragma once

#include "utils/safe_win32.h"

#include <string>

namespace HogwartsMP::Core::UI {
    // Web-backed in-game HUD (hud.html on the hosted UI). One fullscreen CEF
    // overlay hosting the corner version/connection text plus a tabbed dev menu
    // (F8): console, playground (spawn/students/dump), teleport and web debug.
    //
    // The HTML page owns presentation; this class owns the view lifecycle, the
    // C++<->JS event bridge and the focus/control-lock state. Drive it from the
    // game thread (Application::PostUpdate), NOT inside an ImGui widget lambda —
    // key edges set by the CEF message pump are cleared before deferred widgets
    // run (same constraint as Chat).
    class Hud final {
      public:
        Hud() = default;

        // Ensure the view, forward new log lines, refresh dynamic HUD values.
        void Update();

        // Release input + clear the connected banner (e.g. on disconnect).
        void Hide();

        // Tabbed dev menu (F8). Console / Playground / Teleport / Web Debug.
        void ToggleDevMenu();
        void OpenDevMenu(const std::string &tab);
        void CloseDevMenu();
        bool IsDevMenuOpen() const {
            return _devMenuOpen;
        }

        // Centered banner text ("" clears it). Pushed once the page is ready.
        void SetBanner(const std::string &text);

      private:
        void EnsureView();

        void PushStaticState();  // version, command list, locations, banner
        void PushHudValues();    // connection + ping, only when they change
        void PollLogs();         // forward ring-buffer delta, batched
        void PushWebDebug();     // diagnostics, only while that tab is open
        void PushPlayground();   // live student count, only while that tab is open

        int _viewId      = -1;
        bool _pageReady  = false;

        bool _devMenuOpen      = false;
        bool _devLocked        = false;
        std::string _activeTab = "console";

        // Pushed once the page reports ready
        bool _staticPushed = false;

        // Dynamic HUD cache — push only on change so the idle HUD never repaints
        std::string _lastConnection;
        int _lastPing = -1;

        // Banner is set by the session state before the page may be ready
        std::string _banner;

        // Log forwarding: track the last line we sent to detect the delta across
        // a bounded ring buffer
        std::string _lastForwardedLine;

        // Web-debug / playground refresh throttle (ticks)
        int _webDebugTick = 0;
    };
} // namespace HogwartsMP::Core::UI
