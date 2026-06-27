#pragma once

#include "utils/safe_win32.h"

#include <fu2/function2.hpp>

#include <string>

namespace HogwartsMP::Core::UI {
    // Web-backed in-game HUD (hud.html): corner version/connection text, the
    // main-menu connect screen, and the F8 dev menu. Owns the view lifecycle +
    // C++<->JS bridge. Drive from the game thread (PostUpdate), NOT an ImGui
    // widget — CEF clears key edges before deferred widgets run (like Chat).
    class Hud final {
      public:
        using OnConnectProc = fu2::function<void(const std::string &host, const std::string &nickname)>;
        using OnOfflineProc = fu2::function<void()>;

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

        // Main-menu connect screen. The menu state owns the connection flow and
        // receives the user's choice through these callbacks.
        void OpenConnect(const std::string &ip, const std::string &nickname, bool discord);
        void CloseConnect();
        inline void SetOnConnectCallback(OnConnectProc proc) {
            _onConnect = std::move(proc);
        }
        inline void SetOnOfflineCallback(OnOfflineProc proc) {
            _onOffline = std::move(proc);
        }

      private:
        void EnsureView();

        void PushStaticState();  // version, command list, locations, banner
        void PushHudValues();    // connection + ping, only when they change
        void PollLogs();         // forward ring-buffer delta, batched
        void PushWebDebug();     // diagnostics, only while that tab is open
        void PushPlayground();   // live student count, only while that tab is open
        void PushConnect();      // (re)apply the connect screen once the page is ready

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

        // Connect screen: desired state, kept so it can be (re)applied when the
        // page becomes ready (the menu may enter before the DOM loads).
        bool _connectOpen    = false;
        bool _connectLocked  = false;
        bool _connectDiscord = false;
        std::string _connectIp;
        std::string _connectNickname;
        OnConnectProc _onConnect {};
        OnOfflineProc _onOffline {};
    };
} // namespace HogwartsMP::Core::UI
