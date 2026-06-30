#include "creator.h"

#include "core/application.h"
#include "core/character_creator.h"

#include <logging/logger.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <string>

namespace HogwartsMP::Core::UI {
    // Hosted default (assets repo / Pages); see PAGES_UI_HANDOFF.md. For local
    // iteration set HOGWARTSMP_CREATOR_URL=http://localhost:5173 (pixi run
    // creator-ui-dev) — no code change between dev and ship.
    static constexpr const char *kCreatorUrl = "https://hogwarts-mp.github.io/assets/ui/creator.html";

    static std::string ResolveUrl() {
        if (const char *env = std::getenv("HOGWARTSMP_CREATOR_URL"); env && *env) {
            return env;
        }
        return kCreatorUrl;
    }

    void Creator::EnsureView() {
        const auto webManager = gApplication->GetWebManager();
        if (!webManager || !webManager->IsInitialized()) {
            return;
        }

        if (_viewId >= 0) {
            if (webManager->GetView(_viewId)) {
                return;
            }
            // View got destroyed externally; release the lock if we held it.
            if (_locked) {
                gApplication->LockControls(false);
                _locked = false;
            }
            _viewId    = -1;
            _pageReady = false;
            _open      = false;
        }

        const auto url = ResolveUrl();
        _viewId        = webManager->CreateView(url, 0, 0);
        if (_viewId < 0) {
            Framework::Logging::GetLogger("Web")->error("Failed to create creator web view");
            return;
        }

        const auto view = webManager->GetView(_viewId);
        view->Display(true);
        view->Focus(false);

        view->SetOnConsoleMessageCallback([](const std::string &msg, uint32_t line, uint32_t, const std::string &source) {
            Framework::Logging::GetLogger("Web")->info("[creator] {}:{} {}", source, line, msg);
        });

        view->AddEventListener("creator:ready", [this](const std::string &) {
            _pageReady = true;
            Framework::Logging::GetLogger("Web")->info("Creator web view ready");
        });
        view->AddEventListener("creator:close", [this](const std::string &) {
            Close();
        });

        // Character-creator edits — all queued and applied on the game thread
        // (CharacterCreator::ProcessPending). The page batches edits then sends
        // cc:reload to rebuild the character once.
        view->AddEventListener("cc:outfit", [](const std::string &payload) {
            if (!payload.empty()) {
                HogwartsMP::Core::CharacterCreator::RequestSetOutfit(payload);
            }
        });
        view->AddEventListener("cc:addon", [](const std::string &payload) {
            const auto j = nlohmann::json::parse(payload, nullptr, false);
            if (j.is_discarded()) {
                return;
            }
            HogwartsMP::Core::CharacterCreator::RequestSetAddOnMesh(j.value("type", ""), j.value("name", ""));
        });
        view->AddEventListener("cc:vector", [](const std::string &payload) {
            const auto j = nlohmann::json::parse(payload, nullptr, false);
            if (j.is_discarded()) {
                return;
            }
            HogwartsMP::Core::CharacterCreator::RequestSetVectorParam(
                j.value("mesh", ""), j.value("param", ""),
                j.value("r", 0.0f), j.value("g", 0.0f), j.value("b", 0.0f), j.value("a", 1.0f));
        });
        view->AddEventListener("cc:scalar", [](const std::string &payload) {
            const auto j = nlohmann::json::parse(payload, nullptr, false);
            if (j.is_discarded()) {
                return;
            }
            HogwartsMP::Core::CharacterCreator::RequestSetScalarParam(j.value("mesh", ""), j.value("param", ""), j.value("value", 0.0f));
        });
        view->AddEventListener("cc:bone", [](const std::string &payload) {
            const auto j = nlohmann::json::parse(payload, nullptr, false);
            if (j.is_discarded()) {
                return;
            }
            HogwartsMP::Core::CharacterCreator::RequestSetBoneSlider(j.value("bone", ""), j.value("value", 1.0f));
        });
        view->AddEventListener("cc:scale", [](const std::string &payload) {
            try {
                HogwartsMP::Core::CharacterCreator::RequestSetScale(std::stof(payload));
            }
            catch (...) {
            }
        });
        view->AddEventListener("cc:reload", [](const std::string &) {
            HogwartsMP::Core::CharacterCreator::RequestReload();
        });
        view->AddEventListener("cc:enumerate", [](const std::string &) {
            HogwartsMP::Core::CharacterCreator::RequestEnumerate();
        });

        Framework::Logging::GetLogger("Web")->info("Creator web view {} created ({})", _viewId, url);
    }

    void Creator::Open() {
        const auto webManager = gApplication->GetWebManager();
        const auto view       = (webManager && _viewId >= 0) ? webManager->GetView(_viewId) : nullptr;
        if (!view || !_pageReady) {
            return;
        }
        _open = true;
        // Restore visibility in case a prior Hide() disabled the display — otherwise we'd
        // lock controls over an invisible panel (no visible Close button = soft-lock).
        view->Display(true);
        view->Focus(true);
        if (!_locked) {
            gApplication->LockControls(true);
            _locked = true;
        }
        view->EvaluateScript("window.hmpCreator && window.hmpCreator.open();");
    }

    void Creator::Close() {
        if (!_open) {
            return;
        }
        _open = false;

        const auto webManager = gApplication->GetWebManager();
        const auto view       = (webManager && _viewId >= 0) ? webManager->GetView(_viewId) : nullptr;
        if (view) {
            view->Focus(false);
            view->EvaluateScript("window.hmpCreator && window.hmpCreator.close();");
        }
        if (_locked) {
            gApplication->LockControls(false);
            _locked = false;
        }
    }

    void Creator::Toggle() {
        if (_open) {
            Close();
        }
        else {
            Open();
        }
    }

    void Creator::Update() {
        EnsureView();
    }

    void Creator::Hide() {
        Close();
        const auto webManager = gApplication->GetWebManager();
        const auto view       = (webManager && _viewId >= 0) ? webManager->GetView(_viewId) : nullptr;
        if (view) {
            view->Display(false);
        }
    }
} // namespace HogwartsMP::Core::UI
