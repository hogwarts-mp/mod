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
        // Reset camera by player
        // TODO

        // Give back controls
        // TODO
        return true;
    }

    bool SessionConnectedState::OnExit(Framework::Utils::States::Machine *) {
        return true;
    }

    bool SessionConnectedState::OnUpdate(Framework::Utils::States::Machine *) {
        gApplication->GetImGUI()->PushWidget([]() {
            if (!gApplication->GetDevConsole()->IsOpen()) {
                gApplication->GetChat()->Update();
            }
        });
        return false;
    }
} // namespace MafiaMP::Core::States
