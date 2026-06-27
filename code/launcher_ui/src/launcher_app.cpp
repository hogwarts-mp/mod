#include "launcher_app.h"

#include "include/cef_browser.h"
#include "include/cef_command_line.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_window.h"
#include "include/views/cef_window_delegate.h"
#include "include/wrapper/cef_helpers.h"

#include "launcher_client.h"
#include "scheme_handler.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <string>

namespace HogwartsMP::LauncherUI {
    namespace {
        // The design window is a fixed 1280x840 frame (it draws its own titlebar).
        constexpr int kWindowWidth  = 1280;
        constexpr int kWindowHeight = 840;

        std::filesystem::path ExeDir() {
            wchar_t exePath[MAX_PATH] = {};
            GetModuleFileNameW(nullptr, exePath, MAX_PATH);
            return std::filesystem::path(exePath).parent_path();
        }

        // Where the thin shell mirrors the remote UI. Next to the exe so it's easy to wipe
        // while iterating; survives across runs as the offline fallback.
        std::filesystem::path CacheDir() {
            return ExeDir() / "launcher_cache";
        }

        // Assets shipped with the exe (fonts, etc.), served under hmp://app/local/. Mirrors
        // MafiaMP's files/data convention (CMake copies files/ -> bin/files/).
        std::filesystem::path LocalAssetsDir() {
            return ExeDir() / "files" / "data";
        }

        // Remote base the UI is fetched from (GitHub Pages by default). Override with
        // HOGWARTSMP_LAUNCHER_REMOTE to mirror a local static server while iterating.
        std::string ResolveRemoteBase() {
            wchar_t envBuf[1024] = {};
            const DWORD n        = GetEnvironmentVariableW(L"HOGWARTSMP_LAUNCHER_REMOTE", envBuf, 1024);
            if (n > 0 && n < 1024) {
                return CefString(std::wstring(envBuf, n)).ToString();
            }
            return kDefaultRemoteBase;
        }

        std::string ResolveStartUrl() {
            // Dev override: point at the Vite dev server for hot reload
            // (set HOGWARTSMP_LAUNCHER_URL=http://localhost:5173).
            wchar_t envBuf[1024] = {};
            const DWORD n        = GetEnvironmentVariableW(L"HOGWARTSMP_LAUNCHER_URL", envBuf, 1024);
            if (n > 0 && n < 1024) {
                return CefString(std::wstring(envBuf, n)).ToString();
            }

            // Production: served over our custom scheme so ES modules + fetch() work
            // (file:// would block them with a "null" origin).
            return kAppUrl;
        }

        // Frameless, fixed-size, centered window hosting the browser view.
        class LauncherWindowDelegate final : public CefWindowDelegate {
          public:
            explicit LauncherWindowDelegate(CefRefPtr<CefBrowserView> browserView) : _browserView(browserView) {}

            void OnWindowCreated(CefRefPtr<CefWindow> window) override {
                window->AddChildView(_browserView);
                window->CenterWindow(CefSize(kWindowWidth, kWindowHeight));
                window->Show();
                _browserView->RequestFocus();
            }

            void OnWindowDestroyed(CefRefPtr<CefWindow>) override {
                _browserView = nullptr;
            }

            CefSize GetPreferredSize(CefRefPtr<CefView>) override {
                return CefSize(kWindowWidth, kWindowHeight);
            }

            bool IsFrameless(CefRefPtr<CefWindow>) override {
                return true;
            }
            bool CanResize(CefRefPtr<CefWindow>) override {
                return false;
            }
            bool CanMaximize(CefRefPtr<CefWindow>) override {
                return true;
            }
            bool CanMinimize(CefRefPtr<CefWindow>) override {
                return true;
            }

            bool CanClose(CefRefPtr<CefWindow>) override {
                CefRefPtr<CefBrowser> browser = _browserView ? _browserView->GetBrowser() : nullptr;
                if (browser) {
                    return browser->GetHost()->TryCloseBrowser();
                }
                return true;
            }

          private:
            CefRefPtr<CefBrowserView> _browserView;
            IMPLEMENT_REFCOUNTING(LauncherWindowDelegate);
        };
    } // namespace

    void LauncherApp::OnBeforeCommandLineProcessing(const CefString &, CefRefPtr<CefCommandLine> commandLine) {
        commandLine->AppendSwitch("disable-spell-checking");
        commandLine->AppendSwitch("disable-pdf-extension");
        commandLine->AppendSwitch("disable-component-update");
    }

    void LauncherApp::OnRegisterCustomSchemes(CefRawPtr<CefSchemeRegistrar> registrar) {
        RegisterLauncherSchemes(registrar);
    }

    void LauncherApp::OnContextInitialized() {
        CEF_REQUIRE_UI_THREAD();

        // Serve the UI over the custom scheme (remote-fetched into the local cache) before
        // the browser navigates. Skipped in effect when HOGWARTSMP_LAUNCHER_URL points the
        // start URL straight at the Vite dev server.
        RegisterLauncherSchemeHandlerFactory(ResolveRemoteBase(), CacheDir(), LocalAssetsDir());

        CefRefPtr<LauncherClient> client(new LauncherClient());

        CefBrowserSettings browserSettings;
        const std::string url = ResolveStartUrl();

        CefRefPtr<CefBrowserView> browserView = CefBrowserView::CreateBrowserView(client, url, browserSettings, nullptr, nullptr, nullptr);

        CefWindow::CreateTopLevelWindow(new LauncherWindowDelegate(browserView));
    }

    void LauncherApp::OnContextCreated(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) {
        if (!_rendererRouter) {
            _rendererRouter = CefMessageRouterRendererSide::Create(CefMessageRouterConfig());
        }
        _rendererRouter->OnContextCreated(browser, frame, context);
    }

    void LauncherApp::OnContextReleased(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) {
        if (_rendererRouter) {
            _rendererRouter->OnContextReleased(browser, frame, context);
        }
    }

    bool LauncherApp::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, CefProcessId sourceProcess, CefRefPtr<CefProcessMessage> message) {
        if (_rendererRouter) {
            return _rendererRouter->OnProcessMessageReceived(browser, frame, sourceProcess, message);
        }
        return false;
    }
} // namespace HogwartsMP::LauncherUI
