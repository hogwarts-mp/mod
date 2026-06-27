#include "menu.h"
#include "states.h"
#include <utils/safe_win32.h>

#include <utils/states/machine.h>

#include "../application.h"
#include "../launch_config.h"

namespace HogwartsMP::Core::States {
    InMenuState::InMenuState() {}

    InMenuState::~InMenuState() {}

    int32_t InMenuState::GetId() const {
        return StateIds::Menu;
    }

    const char *InMenuState::GetName() const {
        return "InMenu";
    }

    bool InMenuState::OnEnter(Framework::Utils::States::Machine *) {
        _shouldProceedConnection   = false;
        _shouldProceedOfflineDebug = false;

        // Nickname is provided by Discord when available, otherwise typed in.
        const bool discord   = gApplication->GetPresence()->IsInitialized();
        std::string nickname = "Player";
        if (discord) {
            discord::User currUser {};
            gApplication->GetPresence()->GetUserManager().GetCurrentUser(&currUser);
            nickname = currUser.GetUsername();
        }

        // Server selection is owned by the pre-launch launcher: if it wrote connect.json,
        // use it and skip the in-game prompt entirely. Consume it so a later
        // disconnect -> menu doesn't auto-reconnect in a loop.
        if (const auto cfg = ReadConnectConfig(true)) {
            Framework::Integrations::Client::CurrentState newApplicationState = gApplication->GetCurrentState();
            newApplicationState.host     = cfg->host;
            newApplicationState.port     = cfg->port;
            newApplicationState.nickname = !cfg->nickname.empty() ? cfg->nickname : (discord ? nickname : "Player");
            gApplication->SetCurrentState(newApplicationState);

            _shouldProceedConnection = true;
            return true;
        }

        // Fallback (no launcher selection, e.g. a direct dev injection): the manual
        // in-game connect prompt. The CEF HUD relays the user's choice back through these
        // callbacks. Accepts host or host:port.
        const auto hud = gApplication->GetHud();
        hud->SetOnConnectCallback([this, discord, nickname](const std::string &host, const std::string &submittedNick) {
            std::string parsedHost = host.empty() ? "127.0.0.1" : host;
            int32_t parsedPort     = 27015;
            const size_t colon     = parsedHost.rfind(':');
            if (colon != std::string::npos) {
                try {
                    const int v = std::stoi(parsedHost.substr(colon + 1));
                    if (v > 0 && v <= 65535) {
                        parsedPort = v;
                        parsedHost = parsedHost.substr(0, colon);
                    }
                }
                catch (...) {
                    // keep default port, leave host as typed
                }
            }

            Framework::Integrations::Client::CurrentState newApplicationState = gApplication->GetCurrentState();
            newApplicationState.host                                          = parsedHost;
            newApplicationState.port                                          = parsedPort;
            newApplicationState.nickname                                      = discord ? nickname : (submittedNick.empty() ? "Player" : submittedNick);
            gApplication->SetCurrentState(newApplicationState);

            _shouldProceedConnection = true;
        });
        hud->SetOnOfflineCallback([this]() {
            _shouldProceedOfflineDebug = true;
        });

        // Opening the connect screen focuses the HUD view and shows the cursor.
        hud->OpenConnect("127.0.0.1", nickname, discord);
        return true;
    }

    bool InMenuState::OnExit(Framework::Utils::States::Machine *) {
        const auto hud = gApplication->GetHud();
        hud->CloseConnect();  // releases input + hides cursor
        hud->SetOnConnectCallback({});
        hud->SetOnOfflineCallback({});
        return true;
    }

    bool InMenuState::OnUpdate(Framework::Utils::States::Machine *machine) {
        if (_shouldProceedOfflineDebug) {
            machine->RequestNextState(StateIds::SessionOfflineDebug);
        }
        if (_shouldProceedConnection) {
            machine->RequestNextState(StateIds::SessionConnection);
        }
        return _shouldProceedConnection || _shouldProceedOfflineDebug;
    }
}
