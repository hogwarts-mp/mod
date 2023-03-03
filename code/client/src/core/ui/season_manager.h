#pragma once

#include <sdk/game/seasonchanger.h>

namespace HogwartsMP::Core::UI {
    class SeasonManager final {
      public:
        void SetSeason(SDK::ESeasonEnum season);
        void SetRandomSeason();
        void AdvanceHours(int hours);
    };
} // namespace HogwartsMP::Core::UI
