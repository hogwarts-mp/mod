#include "hud.h"

#include "core/application.h"
#include "core/playground.h"
#include "core/student_proxy.h"
#include "core/appearance_dump.h"
#include "core/teleport.h"

#include <logging/logger.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <utils/version.h>
#include "shared/version.h"

namespace HogwartsMP::Core::UI {
    // Hosted in-game UI (GitHub Pages interim; swap to MafiaHub CDN later). Keep in sync with chat.cpp's
    // base + the Pages deployment (see PAGES_UI_HANDOFF.md).
    static constexpr const char *kHudUrl = "https://hogwarts-mp.github.io/assets/ui/hud.html";

    // Mirrors the connection-state labels the old corner text used
    static const char *kConnStateNames[] = {"Connecting", "Online", "Offline"};

    void Hud::EnsureView() {
        const auto webManager = gApplication->GetWebManager();
        if (!webManager || !webManager->IsInitialized()) {
            return;
        }

        if (_viewId >= 0) {
            if (webManager->GetView(_viewId)) {
                return;
            }
            // View got destroyed externally; recreate and re-push state. Release
            // the control lock if the dev menu was open when it vanished, else the
            // player is left with locked controls.
            if (_devLocked) {
                gApplication->LockControls(false);
                _devLocked = false;
            }
            _devMenuOpen       = false;
            _viewId            = -1;
            _pageReady         = false;
            _staticPushed      = false;
            _lastConnection.clear();
            _lastPing          = -1;
            _lastForwardedLine.clear();
        }

        // Fullscreen passive overlay (0x0 = viewport size). Displayed but NOT
        // focused: the HUD paints over the game without stealing input. Focus is
        // taken only while the dev menu is open.
        _viewId = webManager->CreateView(kHudUrl, 0, 0);
        if (_viewId < 0) {
            Framework::Logging::GetLogger("Web")->error("Failed to create HUD web view");
            return;
        }

        const auto view = webManager->GetView(_viewId);
        view->Display(true);
        view->Focus(false);

        view->SetOnConsoleMessageCallback([](const std::string &msg, uint32_t line, uint32_t, const std::string &source) {
            Framework::Logging::GetLogger("Web")->info("[hud] {}:{} {}", source, line, msg);
        });

        view->AddEventListener("hud:ready", [this](const std::string &) {
            _pageReady    = true;
            _staticPushed = false;
            PushStaticState();
            Framework::Logging::GetLogger("Web")->info("HUD web view ready");
        });

        // Console command entry — same dispatch (and error logging) the old ImGui
        // console used; results land back in the log and stream to the page.
        view->AddEventListener("console:exec", [this](const std::string &payload) {
            if (payload.empty()) {
                return;
            }
            const auto logger = Framework::Logging::GetLogger("Console");
            const auto result = gApplication->GetCommandProcessor()->ProcessCommand(payload);
            switch (result.GetError()) {
            case Framework::Utils::CommandProcessorError::COMMAND_PRINT_HELP: logger->info("{}", result.GetValue()); break;
            case Framework::Utils::CommandProcessorError::COMMAND_ALREADY_EXISTS: logger->warn("Command already exists: {}", result.GetValue()); break;
            case Framework::Utils::CommandProcessorError::COMMAND_UNSPECIFIED_NAME: logger->warn("Command name was unspecified"); break;
            case Framework::Utils::CommandProcessorError::COMMAND_UNKNOWN: logger->warn("Command not found: {}", result.GetValue()); break;
            case Framework::Utils::CommandProcessorError::COMMAND_INTERNAL_ERROR: logger->warn("Input error: {}", result.GetValue()); break;
            default: break;
            }
        });

        view->AddEventListener("console:setlevel", [](const std::string &payload) {
            const auto ring = Framework::Logging::GetInstance()->GetRingBuffer();
            if (ring) {
                ring->set_level(spdlog::level::from_str(payload));
            }
        });

        view->AddEventListener("devmenu:tab", [this](const std::string &payload) {
            _activeTab = payload;
        });

        view->AddEventListener("devmenu:close", [this](const std::string &) {
            CloseDevMenu();
        });

        view->AddEventListener("hud:teleport", [this](const std::string &payload) {
            if (!payload.empty()) {
                gApplication->GetDevFeatures().GetTeleportManager()->TeleportTo(payload);
            }
        });

        // Playground actions — all run on the game thread via request queues
        view->AddEventListener("pg:spawn", [](const std::string &payload) {
            if (!payload.empty()) {
                HogwartsMP::Core::Playground::RequestSpawnActor(payload);
            }
        });
        view->AddEventListener("pg:destroy", [](const std::string &) {
            HogwartsMP::Core::Playground::RequestDestroyActors();
        });
        view->AddEventListener("pg:students-spawn", [](const std::string &payload) {
            int count = 1;
            try {
                count = std::max(1, std::stoi(payload));
            }
            catch (...) {
            }
            HogwartsMP::Core::StudentProxy::RequestSpawn(count);
        });
        view->AddEventListener("pg:students-despawn", [](const std::string &) {
            HogwartsMP::Core::StudentProxy::RequestDespawnAll();
        });
        view->AddEventListener("pg:dump", [](const std::string &) {
            HogwartsMP::Core::AppearanceDump::RequestDump();
        });

        Framework::Logging::GetLogger("Web")->info("HUD web view {} created ({})", _viewId, kHudUrl);
    }

    void Hud::PushStaticState() {
        const auto webManager = gApplication->GetWebManager();
        const auto view       = (webManager && _viewId >= 0) ? webManager->GetView(_viewId) : nullptr;
        if (!view || !_pageReady || _staticPushed) {
            return;
        }

        const auto fw  = fmt::format("Framework {} ({})", Framework::Utils::Version::rel, Framework::Utils::Version::git);
        const auto mod = fmt::format("HogwartsMP {} ({})", HogwartsMP::Version::rel, HogwartsMP::Version::git);
        view->EvaluateScript(fmt::format("window.hmpHud && window.hmpHud.setHud({{framework:{},mod:{}}});", nlohmann::json(fw).dump(), nlohmann::json(mod).dump()));

        // Command list for console autocomplete
        nlohmann::json cmds = nlohmann::json::array();
        for (const auto &name : gApplication->GetCommandProcessor()->GetCommandNames()) {
            cmds.push_back(name);
        }
        view->EvaluateScript(fmt::format("window.hmpHud && window.hmpHud.console.setCommands({});", cmds.dump()));

        // Teleport locations (shared list, also used by the ImGui teleport manager)
        nlohmann::json locs = nlohmann::json::array();
        for (const auto name : HogwartsMP::Core::FastTravelLocations()) {
            locs.push_back(std::string(name));
        }
        view->EvaluateScript(fmt::format("window.hmpHud && window.hmpHud.teleport.setLocations({});", locs.dump()));

        // Banner (may have been set before the page was ready)
        view->EvaluateScript(fmt::format("window.hmpHud && window.hmpHud.setBanner({});", nlohmann::json(_banner).dump()));

        _staticPushed = true;
    }

    void Hud::PushHudValues() {
        const auto webManager = gApplication->GetWebManager();
        const auto view       = (webManager && _viewId >= 0) ? webManager->GetView(_viewId) : nullptr;
        if (!view || !_pageReady) {
            return;
        }

        const auto networkClient = gApplication->GetNetworkingEngine()->GetNetworkClient();
        const auto connState     = networkClient->GetConnectionState();
        const auto ping          = static_cast<int>(networkClient->GetPing());

        const std::string conn = kConnStateNames[static_cast<size_t>(connState)];

        // Only touch the DOM when a value actually changes — otherwise the page
        // never repaints and the HUD costs a single textured quad per frame
        if (conn == _lastConnection && ping == _lastPing) {
            return;
        }
        _lastConnection = conn;
        _lastPing       = ping;

        view->EvaluateScript(fmt::format("window.hmpHud && window.hmpHud.setHud({{connection:{},ping:{}}});", nlohmann::json(conn).dump(), nlohmann::json(std::to_string(ping)).dump()));
    }

    void Hud::PollLogs() {
        const auto webManager = gApplication->GetWebManager();
        const auto view       = (webManager && _viewId >= 0) ? webManager->GetView(_viewId) : nullptr;
        if (!view || !_pageReady) {
            return;
        }

        const auto ring = Framework::Logging::GetInstance()->GetRingBuffer();
        if (!ring) {
            return;
        }

        const auto formatted = ring->last_formatted();
        if (formatted.empty()) {
            return;
        }

        // Forward only the lines newer than the last one we sent. If the marker
        // can't be found (first poll, or the bounded ring wrapped past it) we
        // resync by sending the whole current buffer.
        size_t start = 0;
        if (!_lastForwardedLine.empty()) {
            bool found = false;
            for (size_t i = formatted.size(); i-- > 0;) {
                if (formatted[i] == _lastForwardedLine) {
                    start = i + 1;
                    found = true;
                    break;
                }
            }
            if (!found) {
                start = 0;  // marker scrolled out of the ring -> resync everything
            }
        }

        if (start >= formatted.size()) {
            return;
        }

        // Batch the delta into one call so log bursts coalesce into one repaint
        nlohmann::json lines = nlohmann::json::array();
        for (size_t i = start; i < formatted.size(); i++) {
            lines.push_back(formatted[i]);
        }
        _lastForwardedLine = formatted.back();

        view->EvaluateScript(fmt::format("window.hmpHud && window.hmpHud.console.addLines({});", lines.dump()));
    }

    void Hud::PushWebDebug() {
        const auto webManager = gApplication->GetWebManager();
        const auto view       = (webManager && _viewId >= 0) ? webManager->GetView(_viewId) : nullptr;
        if (!view || !_pageReady || !webManager) {
            return;
        }

        // ~4 Hz is plenty for diagnostics and keeps repaints rare
        if ((_webDebugTick++ % 15) != 0) {
            return;
        }

        const auto views = webManager->GetAllViews();
        std::string body = fmt::format("views: {}\n", views.size());

        const auto renderer = gApplication->GetRenderer();
        if (renderer && renderer->GetD3D12Backend()) {
            body += fmt::format("free srv slots: {}/{}\n", renderer->GetD3D12Backend()->GetFreeSRVSlotCount(), Framework::Graphics::D3D12Backend::kExtraSrvSlots);
        }
        for (const auto *v : views) {
            body += fmt::format("#{} focus={}\n", v->GetId(), v->HasFocus());
        }

        view->EvaluateScript(fmt::format("window.hmpHud && window.hmpHud.webdebug.set({});", nlohmann::json(body).dump()));
    }

    void Hud::PushPlayground() {
        const auto webManager = gApplication->GetWebManager();
        const auto view       = (webManager && _viewId >= 0) ? webManager->GetView(_viewId) : nullptr;
        if (!view || !_pageReady) {
            return;
        }

        // ~4 Hz; live student count
        if ((_webDebugTick++ % 15) != 0) {
            return;
        }

        nlohmann::json s;
        s["students"] = HogwartsMP::Core::StudentProxy::ActiveCount();
        view->EvaluateScript(fmt::format("window.hmpHud && window.hmpHud.playground.setState({});", s.dump()));
    }

    void Hud::Update() {
        EnsureView();
        PushStaticState();
        PushHudValues();
        PollLogs();

        // Live data only for the tab that's actually showing
        if (_devMenuOpen) {
            if (_activeTab == "webdebug") {
                PushWebDebug();
            }
            else if (_activeTab == "playground") {
                PushPlayground();
            }
        }
    }

    void Hud::OpenDevMenu(const std::string &tab) {
        const auto webManager = gApplication->GetWebManager();
        const auto view       = (webManager && _viewId >= 0) ? webManager->GetView(_viewId) : nullptr;
        if (!view || !_pageReady) {
            return;
        }

        _activeTab = tab;
        if (!_devMenuOpen) {
            _devMenuOpen = true;
            view->Focus(true);
            if (!_devLocked) {
                gApplication->LockControls(true);
                _devLocked = true;
            }
        }
        view->EvaluateScript(fmt::format("window.hmpHud && window.hmpHud.devmenu.open({});", nlohmann::json(tab).dump()));
    }

    void Hud::CloseDevMenu() {
        if (!_devMenuOpen) {
            return;
        }
        _devMenuOpen = false;

        const auto webManager = gApplication->GetWebManager();
        const auto view       = (webManager && _viewId >= 0) ? webManager->GetView(_viewId) : nullptr;
        if (view) {
            view->EvaluateScript("window.hmpHud && window.hmpHud.devmenu.close();");
            view->Focus(false);
        }
        if (_devLocked) {
            gApplication->LockControls(false);
            _devLocked = false;
        }
    }

    void Hud::ToggleDevMenu() {
        if (_devMenuOpen) {
            CloseDevMenu();
        }
        else {
            OpenDevMenu(_activeTab);  // re-open on the last-used tab
        }
    }

    void Hud::SetBanner(const std::string &text) {
        _banner = text;

        const auto webManager = gApplication->GetWebManager();
        const auto view       = (webManager && _viewId >= 0) ? webManager->GetView(_viewId) : nullptr;
        if (view && _pageReady) {
            view->EvaluateScript(fmt::format("window.hmpHud && window.hmpHud.setBanner({});", nlohmann::json(_banner).dump()));
        }
    }

    void Hud::Hide() {
        CloseDevMenu();
        SetBanner("");
    }
} // namespace HogwartsMP::Core::UI
