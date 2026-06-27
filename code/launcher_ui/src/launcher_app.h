/*
 * HogwartsMP pre-launch launcher — CEF application object.
 *
 * Single self-contained CEF process: this object serves as both the browser-process
 * handler (creates the windowed UI on context init) and the render-process handler
 * (wires up the message router so window.cefQuery works). The same executable is reused
 * as the CEF subprocess via CefExecuteProcess in main(), so no separate helper exe is
 * needed.
 */

#pragma once

#include "include/cef_app.h"
#include "include/wrapper/cef_message_router.h"

namespace HogwartsMP::LauncherUI {
    class LauncherApp final : public CefApp, public CefBrowserProcessHandler, public CefRenderProcessHandler {
      public:
        LauncherApp() = default;

        // CefApp
        CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
            return this;
        }
        CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override {
            return this;
        }
        void OnBeforeCommandLineProcessing(const CefString &processType, CefRefPtr<CefCommandLine> commandLine) override;
        void OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) override;

        // CefBrowserProcessHandler (browser process only)
        void OnContextInitialized() override;

        // CefRenderProcessHandler (render process only)
        void OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) override;
        void OnContextReleased(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) override;
        bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId sourceProcess, CefRefPtr<CefProcessMessage> message) override;

      private:
        // Render-process side of the query router (created lazily on the render thread).
        CefRefPtr<CefMessageRouterRendererSide> _rendererRouter;

        IMPLEMENT_REFCOUNTING(LauncherApp);
    };
} // namespace HogwartsMP::LauncherUI
