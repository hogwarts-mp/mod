#pragma once

#include "utils/safe_win32.h"

#include <string>

namespace HogwartsMP::Core::UI {
    class TeleportManager final {
      public:
        void Update();

        void TeleportTo(const std::string &name);
    };
} // namespace HogwartsMP::Core::UI
