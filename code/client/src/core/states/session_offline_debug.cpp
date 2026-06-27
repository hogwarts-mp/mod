#include "session_offline_debug.h"
#include "states.h"
#include <utils/safe_win32.h>

#include <utils/states/machine.h>

#include "../application.h"

namespace HogwartsMP::Core::States {
    SessionOfflineDebugState::SessionOfflineDebugState() {}

    SessionOfflineDebugState::~SessionOfflineDebugState() {}

    int32_t SessionOfflineDebugState::GetId() const {
        return StateIds::SessionOfflineDebug;
    }

    const char *SessionOfflineDebugState::GetName() const {
        return "SessionOfflineDebug";
    }

    bool SessionOfflineDebugState::OnEnter(Framework::Utils::States::Machine *) {
        gApplication->GetHud()->SetBanner("Offline debug mode\nPress F9 to return to menu");
        return true;
    }

    bool SessionOfflineDebugState::OnExit(Framework::Utils::States::Machine *) {
        gApplication->GetHud()->SetBanner("");
        return true;
    }

    bool SessionOfflineDebugState::OnUpdate(Framework::Utils::States::Machine *machine) {
        bool shouldProceed = false;

        if (gApplication->GetInput()->IsKeyPressed(FW_KEY_F9)) {
            machine->RequestNextState(StateIds::Menu);
            shouldProceed = true;
        }

        return shouldProceed;
    }
}
