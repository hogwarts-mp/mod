#pragma once

#include "utils/safe_win32.h"

#include <fu2/function2.hpp>

#include <string>
#include <vector>

namespace HogwartsMP::Core::UI {
    // Web-backed chat overlay (chat.html served from the hosted UI). The HTML page
    // owns presentation; this class owns the view lifecycle, the open-on-Enter edge
    // detection and the C++<->JS event bridge.
    class Chat final {
      public:
        using OnMessageSentProc = fu2::function<void(const std::string &text)>;

        Chat() = default;

        // Must run on the game thread outside any ImGui widget lambda: key edges
        // set by the CEF message pump are cleared before deferred widgets run.
        void Update();

        // Hide the overlay and release input (e.g. on disconnect).
        void Hide();

        inline void SetOnMessageSentCallback(OnMessageSentProc proc) {
            onMessageSentProc = proc;
        }

        void AddMessage(std::string msg);

      private:
        void EnsureView();
        void OpenInput();
        void CloseInput();

        OnMessageSentProc onMessageSentProc {};
        int _viewId           = -1;
        bool _pageReady       = false;
        bool _inputOpen       = false;
        bool _justClosedInput = false;
        std::vector<std::string> _pendingMessages;
    };
} // namespace HogwartsMP::Core::UI
