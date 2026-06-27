/*
 * HogwartsMP pre-launch launcher — JS bridge handler.
 *
 * Receives window.cefQuery requests from the React UI (see web/src/bridge.ts) as JSON
 * { "action": "connect", "address": "host:port" } or
 * { "action": "window", "op": "minimize"|"maximize"|"close" }
 * and acts on them: connect writes the connect-config + spawns the game injector then
 * closes the launcher; window ops drive the CefWindow.
 */

#pragma once

#include "include/wrapper/cef_message_router.h"

namespace HogwartsMP::LauncherUI {
    class BridgeHandler final : public CefMessageRouterBrowserSide::Handler {
      public:
        bool OnQuery(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int64_t queryId, const CefString &request, bool persistent, CefRefPtr<Callback> callback) override;
    };
} // namespace HogwartsMP::LauncherUI
