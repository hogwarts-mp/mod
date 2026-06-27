#include "bridge_handler.h"

#include "include/cef_browser.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"

#include "game_launch.h"

#include <nlohmann/json.hpp>

#include <string>

namespace HogwartsMP::LauncherUI {
    namespace {
        CefRefPtr<CefWindow> WindowFor(CefRefPtr<CefBrowser> browser) {
            CefRefPtr<CefBrowserView> browserView = CefBrowserView::GetForBrowser(browser);
            return browserView ? browserView->GetWindow() : nullptr;
        }
    } // namespace

    bool BridgeHandler::OnQuery(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>, int64_t, const CefString &request, bool, CefRefPtr<Callback> callback) {
        // The UI (web/src/bridge.ts) sends a small JSON object. Parse without throwing.
        const nlohmann::json j = nlohmann::json::parse(request.ToString(), nullptr, /*allow_exceptions=*/false);
        if (j.is_discarded() || !j.is_object()) {
            callback->Failure(2, "Malformed bridge request");
            return true;
        }
        const std::string action = j.value("action", "");

        if (action == "connect") {
            const std::string address = j.value("address", "");
            if (GameLaunch::ConnectAndLaunch(address)) {
                callback->Success("ok");
                // Game is launching — close the launcher.
                if (CefRefPtr<CefWindow> window = WindowFor(browser)) {
                    window->Close();
                }
            } else {
                callback->Failure(1, "Failed to launch the game");
            }
            return true;
        }

        if (action == "window") {
            const std::string op        = j.value("op", "");
            CefRefPtr<CefWindow> window = WindowFor(browser);
            if (window) {
                if (op == "minimize") {
                    window->Minimize();
                } else if (op == "maximize") {
                    // Toggle: restore if already maximized, otherwise maximize.
                    if (window->IsMaximized()) {
                        window->Restore();
                    } else {
                        window->Maximize();
                    }
                } else if (op == "close") {
                    window->Close();
                }
            }
            callback->Success("ok");
            return true;
        }

        return false;
    }
} // namespace HogwartsMP::LauncherUI
