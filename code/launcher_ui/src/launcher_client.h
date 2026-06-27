/*
 * HogwartsMP pre-launch launcher — CEF client.
 *
 * Owns the browser-side message router (routes window.cefQuery -> BridgeHandler) and the
 * browser lifetime. Frameless-window dragging is forwarded from the page's
 * -webkit-app-region regions to the CefWindow.
 */

#pragma once

#include "include/cef_client.h"
#include "include/wrapper/cef_message_router.h"

#include <memory>

namespace HogwartsMP::LauncherUI {
    class BridgeHandler;

    class LauncherClient final : public CefClient, public CefLifeSpanHandler, public CefRequestHandler, public CefDragHandler {
      public:
        LauncherClient();
        ~LauncherClient() override;

        // CefClient
        CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override {
            return this;
        }
        CefRefPtr<CefRequestHandler> GetRequestHandler() override {
            return this;
        }
        CefRefPtr<CefDragHandler> GetDragHandler() override {
            return this;
        }
        bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId sourceProcess, CefRefPtr<CefProcessMessage> message) override;

        // CefLifeSpanHandler
        void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
        bool DoClose(CefRefPtr<CefBrowser> browser) override;
        void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

        // CefRequestHandler
        bool OnBeforeBrowse(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefRequest> request, bool userGesture, bool isRedirect) override;
        void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser, TerminationStatus status, int errorCode, const CefString &errorString) override;

        // CefDragHandler
        void OnDraggableRegionsChanged(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const std::vector<CefDraggableRegion> &regions) override;

      private:
        // Declared before _messageRouter so the router (which holds a raw pointer to the
        // handler) is destroyed first.
        std::unique_ptr<BridgeHandler> _bridgeHandler;
        CefRefPtr<CefMessageRouterBrowserSide> _messageRouter;
        int _browserCount = 0;

        IMPLEMENT_REFCOUNTING(LauncherClient);
    };
} // namespace HogwartsMP::LauncherUI
