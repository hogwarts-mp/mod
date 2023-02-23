#pragma once

#include "utils/safe_win32.h"
#include <sdk/game/seasonchanger.h>

namespace HogwartsMP::Core::UI {
    class SeasonManager final {
      public:
        void Update();
        void SetSeason(SDK::ESeasonEnum season);
        void SetRandomSeason();
    };
} // namespace HogwartsMP::Core::UI
