#include "teleport_manager.h"

#include "core/teleport.h"

namespace HogwartsMP::Core::UI {
    void TeleportManager::TeleportTo(const std::string &name) {
        HogwartsMP::Core::FastTravelTo(name);
    }
} // namespace HogwartsMP::Core::UI
