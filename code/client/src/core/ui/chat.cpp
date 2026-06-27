#include "chat.h"

#include "core/application.h"

#include <logging/logger.h>
#include <nlohmann/json.hpp>

namespace HogwartsMP::Core::UI {
    // Hosted in-game UI (GitHub Pages interim; swap to MafiaHub CDN later). Keep base in sync with the
    // Pages deployment (see PAGES_UI_HANDOFF.md).
    static constexpr const char *kChatUrl = "https://hogwarts-mp.github.io/assets/ui/chat.html";

    void Chat::EnsureView() {
        const auto webManager = gApplication->GetWebManager();
        if (!webManager || !webManager->IsInitialized()) {
            return;
        }

        if (_viewId >= 0) {
            if (const auto view = webManager->GetView(_viewId)) {
                view->Display(true);
                return;
            }
            // View got destroyed externally; recreate. Release the control lock if
            // the input was open when it vanished, else it stays held forever.
            if (_inputOpen) {
                gApplication->LockControls(false);
            }
            _viewId    = -1;
            _pageReady = false;
            _inputOpen = false;
        }

        // Fullscreen overlay (0x0 = viewport size); the page lays out the chat
        // box itself and keeps everything else transparent
        _viewId = webManager->CreateView(kChatUrl, 0, 0);
        if (_viewId < 0) {
            Framework::Logging::GetLogger("Web")->error("Failed to create chat web view");
            return;
        }

        const auto view = webManager->GetView(_viewId);
        view->Display(true);
        view->Focus(false);

        view->SetOnConsoleMessageCallback([](const std::string &msg, uint32_t line, uint32_t, const std::string &source) {
            Framework::Logging::GetLogger("Web")->info("[chat] {}:{} {}", source, line, msg);
        });

        // Fired by the page once window.hmpChat is installed; messages received
        // before that are queued and flushed here
        view->AddEventListener("chat:ready", [this](const std::string &) {
            _pageReady = true;
            const auto pending = std::move(_pendingMessages);
            _pendingMessages.clear();
            for (const auto &msg : pending) {
                AddMessage(msg);
            }
            Framework::Logging::GetLogger("Web")->info("Chat web view ready");
        });

        view->AddEventListener("chat:send", [this](const std::string &payload) {
            if (!payload.empty() && onMessageSentProc) {
                onMessageSentProc(payload);
            }
        });

        view->AddEventListener("chat:close", [this](const std::string &) {
            CloseInput();
        });

        Framework::Logging::GetLogger("Web")->info("Chat web view {} created ({})", _viewId, kChatUrl);
    }

    void Chat::AddMessage(std::string msg) {
        const auto webManager = gApplication->GetWebManager();
        const auto view       = (webManager && _viewId >= 0) ? webManager->GetView(_viewId) : nullptr;
        if (!view || !_pageReady) {
            _pendingMessages.push_back(std::move(msg));
            return;
        }

        // nlohmann dump() yields a quoted, escaped JS string literal
        view->EvaluateScript(fmt::format("window.hmpChat && window.hmpChat.addMessage({});", nlohmann::json(msg).dump()));
    }

    void Chat::OpenInput() {
        const auto webManager = gApplication->GetWebManager();
        const auto view       = (webManager && _viewId >= 0) ? webManager->GetView(_viewId) : nullptr;
        if (!view || !_pageReady) {
            return;
        }

        _inputOpen = true;
        view->Focus(true);
        gApplication->LockControls(true);
        view->EvaluateScript("window.hmpChat && window.hmpChat.open();");
    }

    void Chat::CloseInput() {
        if (!_inputOpen) {
            return;
        }
        _inputOpen       = false;
        _justClosedInput = true;

        const auto webManager = gApplication->GetWebManager();
        const auto view       = (webManager && _viewId >= 0) ? webManager->GetView(_viewId) : nullptr;
        if (view) {
            view->Focus(false);
            view->EvaluateScript("window.hmpChat && window.hmpChat.close();");
        }
        gApplication->LockControls(false);
    }

    void Chat::Update() {
        EnsureView();

        // If the page just closed itself (submit/escape), the Enter edge that
        // triggered it may still be pending in GameInput this tick — eat it
        if (_justClosedInput) {
            _justClosedInput = false;
            return;
        }

        if (_inputOpen) {
            return;
        }

        // Don't fight another focused view (e.g. the dev menu) for input
        const auto webManager = gApplication->GetWebManager();
        if (webManager && webManager->IsAnyViewFocused()) {
            return;
        }

        if (gApplication->GetInput()->IsKeyPressed(FW_KEY_RETURN)) {
            OpenInput();
        }
    }

    void Chat::Hide() {
        CloseInput();
        _justClosedInput = false;

        const auto webManager = gApplication->GetWebManager();
        const auto view       = (webManager && _viewId >= 0) ? webManager->GetView(_viewId) : nullptr;
        if (view) {
            view->Display(false);
        }
    }
} // namespace HogwartsMP::Core::UI
