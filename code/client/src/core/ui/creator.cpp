#include "creator.h"

#include "core/application.h"
#include "core/character_creator.h"

#include <logging/logger.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <string>

namespace HogwartsMP::Core::UI {
    // Hosted default (assets repo / Pages), next to hud.html/chat.html;
    // For local iteration set HOGWARTSMP_CREATOR_URL=http://localhost:5173
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
        view->Display(true); // visible during load; hidden again on creator:ready until opened
        view->Focus(false);

        view->SetOnConsoleMessageCallback([](const std::string &msg, uint32_t line, uint32_t, const std::string &source) {
            Framework::Logging::GetLogger("Web")->info("[creator] {}:{} {}", source, line, msg);
        });

        view->AddEventListener("creator:ready", [this](const std::string &) {
            _pageReady = true;
            // Preloaded but closed — hide until the host opens it, so an idle session doesn't
            // composite a full-screen view every frame (Open() re-shows via Display(true)).
            // Guard on !_open: a page reload while the user is in the creator must not hide a
            // live panel (controls locked + no visible Close button = soft-lock).
            if (!_open) {
                if (auto *wm = gApplication->GetWebManager()) {
                    if (auto *v = (_viewId >= 0) ? wm->GetView(_viewId) : nullptr) {
                        v->Display(false);
                    }
                }
            }
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

        // New live-avatar UI: a slider selects option `index` within a `category`.
        view->AddEventListener("cc:option", [](const std::string &payload) {
            const auto j = nlohmann::json::parse(payload, nullptr, false);
            if (j.is_discarded()) {
                return;
            }
            const std::string category = j.value("category", "");
            const int index            = j.value("index", 0);
            if (!category.empty() && index > 0) {
                HogwartsMP::Core::CharacterCreator::RequestApplyPreset(category, index);
            }
        });
        view->AddEventListener("cc:pitch", [](const std::string &payload) {
            const auto j = nlohmann::json::parse(payload, nullptr, false);
            if (!j.is_discarded()) {
                HogwartsMP::Core::CharacterCreator::RequestSetVoicePitch(j.value("value", 0));
            }
        });
        view->AddEventListener("cc:voicepreview", [](const std::string &) {
            HogwartsMP::Core::CharacterCreator::RequestVoicePreview();
        });
        // Voice tone / name / dormitory / confirm — logged for now; wired to persistence
        // + the join flow in a later slice.
        view->AddEventListener("cc:voice", [](const std::string &payload) {
            Framework::Logging::GetLogger("CharacterCreator")->info("cc:voice {}", payload);
        });
        view->AddEventListener("cc:name", [](const std::string &payload) {
            Framework::Logging::GetLogger("CharacterCreator")->info("cc:name {}", payload);
        });
        view->AddEventListener("cc:dormitory", [](const std::string &payload) {
            Framework::Logging::GetLogger("CharacterCreator")->info("cc:dormitory {}", payload);
        });
        view->AddEventListener("cc:confirm", [this](const std::string &) {
            Framework::Logging::GetLogger("CharacterCreator")->info("cc:confirm — keeping changes");
            _confirmed = true;
            Close();
        });
        // Per-section framing camera (front view onto the live avatar).
        view->AddEventListener("cc:camera", [](const std::string &payload) {
            const auto j = nlohmann::json::parse(payload, nullptr, false);
            if (j.is_discarded()) {
                return;
            }
            HogwartsMP::Core::CharacterCreator::RequestCameraFrame(
                j.value("dist", 160.0f), j.value("height", 40.0f), j.value("pitch", -3.0f), j.value("fov", 30.0f), j.value("shift", 0.0f));
        });
        view->AddEventListener("cc:rotate", [](const std::string &payload) {
            const auto j = nlohmann::json::parse(payload, nullptr, false);
            if (!j.is_discarded()) {
                HogwartsMP::Core::CharacterCreator::RequestRotate(j.value("yaw", 0.0f));
            }
        });
        view->AddEventListener("cc:freeze", [](const std::string &payload) {
            const auto j = nlohmann::json::parse(payload, nullptr, false);
            if (!j.is_discarded()) {
                HogwartsMP::Core::CharacterCreator::RequestFreeze(j.value("on", true));
            }
        });

        Framework::Logging::GetLogger("Web")->info("Creator web view {} created ({})", _viewId, url);
    }

    void Creator::Open() {
        const auto webManager = gApplication->GetWebManager();
        const auto view       = (webManager && _viewId >= 0) ? webManager->GetView(_viewId) : nullptr;
        if (!view || !_pageReady) {
            return;
        }
        _open      = true;
        _confirmed = false;
        // Show the view (it's kept hidden while closed / after Hide()) — otherwise we'd lock
        // controls over an invisible panel (no visible Close button = soft-lock).
        view->Display(true);
        // Snapshot the current appearance so an unconfirmed session can roll back.
        HogwartsMP::Core::CharacterCreator::RequestSnapshotAppearance();
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
        // Restore the normal player camera + resume idle animation when the creator closes.
        HogwartsMP::Core::CharacterCreator::RequestCameraRestore();
        HogwartsMP::Core::CharacterCreator::RequestFreeze(false);
        // Appearance: keep changes if confirmed, else roll back to the open-state snapshot.
        if (_confirmed) {
            HogwartsMP::Core::CharacterCreator::RequestReleaseSnapshot();
        }
        else {
            HogwartsMP::Core::CharacterCreator::RequestRestoreAppearance();
        }

        const auto webManager = gApplication->GetWebManager();
        const auto view       = (webManager && _viewId >= 0) ? webManager->GetView(_viewId) : nullptr;
        if (view) {
            view->Focus(false);
            view->EvaluateScript("window.hmpCreator && window.hmpCreator.close();");
            view->Display(false); // stop compositing the idle view until reopened
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
