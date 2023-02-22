#pragma once

#include "utils/safe_win32.h"

namespace HogwartsMP::Core::UI {
    enum class ESeasonEnum : unsigned char {
        Season_Invalid = 0,
        Season_Fall = 1,
        Season_Winter = 2,
        Season_Spring = 3,
        Season_Summer = 4,
        Season_MAX = 5
    };

    struct USeasonChanger_SetCurrentSeason_Params {
    public:
        ESeasonEnum NewSeason;
    };

    class SeasonManager final {
      public:
        void Update();

        // void SetSeason(const HogwartsMP::Shared::Modules::Mod::SeasonKind season);
        void SetRandomSeason();
    };
} // namespace HogwartsMP::Core::UI
