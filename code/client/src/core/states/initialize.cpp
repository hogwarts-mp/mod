#include "initialize.h"

#include "states.h"

#include <utils/states/machine.h>

namespace HogwartsMP::Core::States {
    InitializeState::InitializeState() {}

    InitializeState::~InitializeState() {}

    int32_t InitializeState::GetId() const {
        return StateIds::Initialize;
    }

    const char *InitializeState::GetName() const {
        return "Initialize";
    }

    bool InitializeState::OnEnter(Framework::Utils::States::Machine *) {
        return true;
    }

    bool InitializeState::OnExit(Framework::Utils::States::Machine *machine) {
        machine->RequestNextState(StateIds::Menu);
        return true;
    }

    bool InitializeState::OnUpdate(Framework::Utils::States::Machine *) {
        return true;
    }
} // namespace MafiaMP::Core::States
