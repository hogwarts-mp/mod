#include "session_connected.h"
#include "states.h"

#include <utils/states/machine.h>

#include "core/application.h"

namespace HogwartsMP::Core::States {
    SessionConnectedState::SessionConnectedState() {}

    SessionConnectedState::~SessionConnectedState() {}

    int32_t SessionConnectedState::GetId() const {
        return StateIds::SessionConnected;
    }

    const char *SessionConnectedState::GetName() const {
        return "SessionConnected";
    }

    bool SessionConnectedState::OnEnter(Framework::Utils::States::Machine *) {
        Core::gApplication->GetDevFeatures().GetTeleportManager()->TeleportTo("Hogwarts");

        gApplication->GetHud()->SetBanner("You are connected\nPress F9 to disconnect");
        return true;
    }

    bool SessionConnectedState::OnExit(Framework::Utils::States::Machine *) {
        gApplication->GetChat()->Hide();
        gApplication->GetHud()->SetBanner("");
        return true;
    }

    bool SessionConnectedState::OnUpdate(Framework::Utils::States::Machine *) {
        // Web-backed chat runs on the game thread, NOT inside an ImGui widget — key
        // edges set by the CEF message pump are cleared before deferred widgets run.
        gApplication->GetChat()->Update();

        if (gApplication->GetInput()->IsKeyPressed(FW_KEY_F9)) {
            (void)gApplication->GetNetworkingEngine()->GetNetworkClient()->Disconnect();
        }

        return false;
    }
} // namespace MafiaMP::Core::States
