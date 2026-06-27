#include "seasonchanger.h"

#include "sdk/reflection/ue4_reflection.h"

#include <string>

using HogwartsMP::Core::UE4::FindInstancesOfClass;
using HogwartsMP::Core::UE4::FindUClass;
using HogwartsMP::Core::UE4::FindUFunction;
using HogwartsMP::Core::UE4::FindUObjects;
using HogwartsMP::Core::UE4::MakeFName;

void SDK::SetSeason(ESeasonEnum season) {
    auto *seasonChanger    = FindUClass("Class /Script/Phoenix.SeasonChanger");
    auto *setCurrentSeason = FindUFunction("Function /Script/Phoenix.SeasonChanger.SetCurrentSeason");
    if (!seasonChanger || !setCurrentSeason)
        return;

    USeasonChanger_SetCurrentSeason_Params params{season};
    seasonChanger->ProcessEvent(setCurrentSeason, &params);
}

void SDK::AdvanceHours(int hours) {
    auto *advanceHours = FindUFunction("Function /Script/GameScheduler.Scheduler.AdvanceHours");
    if (!advanceHours)
        return;

    // Every active Scheduler instance gets advanced.
    for (auto *scheduler : FindUObjects("Class /Script/GameScheduler.Scheduler")) {
        UScheduler_AdvanceHours_Params params{hours};
        static_cast<UObject *>(scheduler)->ProcessEvent(advanceHours, &params);
    }
}

void SDK::SetCurrentTime(int hour, int minute, int second) {
    // SetCurrentTime / GetSimulatedTime are instance methods (no world-context param) — they
    // must run on the live Scheduler, not the class object/CDO (that derefs garbage -> crash).
    // The live instance's class is exactly Scheduler (verified in-game), so exact-class match
    // is fine here — no subclass to chase.
    auto schedulers = FindInstancesOfClass("Class /Script/GameScheduler.Scheduler");
    auto *setTime   = FindUFunction("Function /Script/GameScheduler.Scheduler.SetCurrentTime");
    if (schedulers.empty() || !setTime)
        return;

    // SetCurrentTime takes the full date, so read the current one back and keep it — we only
    // want to move the time-of-day, not the calendar.
    int year = 1, month = 1, day = 1;
    if (auto *getTime = FindUFunction("Function /Script/GameScheduler.Scheduler.GetSimulatedTime")) {
        struct {
            int64_t ticks;
        } dt{0};
        static_cast<UObject *>(schedulers.front())->ProcessEvent(getTime, &dt);
        if (dt.ticks > 0) {
            // FDateTime = 100ns ticks since 0001-01-01; Hinnant days->civil (epoch-shifted).
            int64_t z   = dt.ticks / 864000000000LL - 719162 + 719468;
            int64_t era = (z >= 0 ? z : z - 146096) / 146097;
            int64_t doe = z - era * 146097;
            int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
            int64_t y   = yoe + era * 400;
            int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
            int64_t mp  = (5 * doy + 2) / 153;
            day         = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
            month       = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
            year        = static_cast<int>(y + (month <= 2));
        }
    }

    UScheduler_SetCurrentTime_Params params{hour, minute, second, month, day, year};
    for (auto *scheduler : schedulers) {
        static_cast<UObject *>(scheduler)->ProcessEvent(setTime, &params);
    }
}

void SDK::SetOverrideWeather(void *worldContext, const std::string &weatherName) {
    // SetOverrideWeather has a WorldContextObject param — it's a static/world-context call, so
    // (unlike Scheduler's instance methods) calling on the class object is correct: it resolves
    // the real WeatherMasterComponent from worldContext and ignores `this`.
    auto *weatherCls  = FindUClass("Class /Script/Weather.WeatherMasterComponent");
    auto *setOverride = FindUFunction("Function /Script/Weather.WeatherMasterComponent.SetOverrideWeather");
    if (!weatherCls || !setOverride || !worldContext || weatherName.empty())
        return;

    struct {
        void *WorldContextObject;
        FName NewOverrideWeather;
        bool bInstantChange;
    } params{worldContext, MakeFName(std::wstring(weatherName.begin(), weatherName.end()).c_str()), true};
    weatherCls->ProcessEvent(setOverride, &params);
}
