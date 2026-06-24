#pragma once

namespace SDK {
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

    struct UScheduler_AdvanceHours_Params {
    public:
        int InHours;
    };

    void SetSeason(ESeasonEnum season);
    void AdvanceHours(int hours);
}
