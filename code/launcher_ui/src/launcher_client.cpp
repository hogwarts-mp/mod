#include "launcher_client.h"

#include "include/cef_app.h"
#include "include/cef_browser.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_helpers.h"

#include "bridge_handler.h"

namespace HogwartsMP::LauncherUI {
    LauncherClient::LauncherClient() {
        _bridgeHandler = std::make_unique<BridgeHandler>();
        _messageRouter = CefMessageRouterBrowserSide::Create(CefMessageRouterConfig());
        _messageRouter->AddHandler(_bridgeHandler.get(), /*first=*/false);
    }

    LauncherClient::~LauncherClient() {
        if (_messageRouter && _bridgeHandler) {
            _messageRouter->RemoveHandler(_bridgeHandler.get());
        }
    }

    bool LauncherClient::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId sourceProcess, CefRefPtr<CefProcessMessage> message) {
        return _messageRouter->OnProcessMessageReceived(browser, frame, sourceProcess, message);
    }

    void LauncherClient::OnAfterCreated(CefRefPtr<CefBrowser>) {
        CEF_REQUIRE_UI_THREAD();
        _browserCount++;
    }

    bool LauncherClient::DoClose(CefRefPtr<CefBrowser>) {
        // Allow the default close behaviour (OnBeforeClose follows).
        return false;
    }

    void LauncherClient::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
        CEF_REQUIRE_UI_THREAD();
        _messageRouter->OnBeforeClose(browser);
        if (--_browserCount == 0) {
            CefQuitMessageLoop();
        }
    }

    bool LauncherClient::OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest>, bool, bool) {
        _messageRouter->OnBeforeBrowse(browser, frame);
        return false;
    }

    void LauncherClient::OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser, TerminationStatus, int, const CefString &) {
        _messageRouter->OnRenderProcessTerminated(browser);
    }

    void LauncherClient::OnDraggableRegionsChanged(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame>, const std::vector<CefDraggableRegion> &regions) {
        CEF_REQUIRE_UI_THREAD();
        CefRefPtr<CefBrowserView> browserView = CefBrowserView::GetForBrowser(browser);
        if (!browserView) {
            return;
        }
        CefRefPtr<CefWindow> window = browserView->GetWindow();
        if (window) {
            window->SetDraggableRegions(regions);
        }
    }
} // namespace HogwartsMP::LauncherUI
