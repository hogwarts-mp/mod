#pragma once

#include <string>

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

    // Scheduler.SetCurrentTime takes the full date too (verified by reflection dump); we read
    // the current date back and only change H/M/S so the calendar is preserved.
    struct UScheduler_SetCurrentTime_Params {
    public:
        int InHours;
        int InMinutes;
        int InSeconds;
        int Month;
        int Day;
        int Year;
    };

    void SetSeason(ESeasonEnum season);
    void AdvanceHours(int hours);

    // Absolute time-of-day via Scheduler.SetCurrentTime (date kept from GetSimulatedTime).
    void SetCurrentTime(int hour, int minute, int second);

    // Force the weather sequence by name (Override layer, instant) so the server's choice wins
    // over the local season lottery / weather volumes. `worldContext` is any live UObject with a
    // world (e.g. the local pawn); `weatherName` is a weather-sequence asset name (FName in-game).
    void SetOverrideWeather(void *worldContext, const std::string &weatherName);
}
