#pragma once

#include <cstdint>
#include <string>

namespace HogwartsMP::Shared {
    enum SeasonKind : uint8_t { SEASON_SPRING, SEASON_SUMMER, SEASON_AUTUMN, SEASON_WINTER };

    // Server-authoritative environment state, broadcast to clients via the SetWeather RPC. Replaces
    // the old flecs Mod::Weather singleton component.
    struct WeatherState {
        uint8_t timeHour   = 11;
        uint8_t timeMinute = 0;
        uint8_t dateDay    = 12;
        uint8_t dateMonth  = 6;
        std::string weather = "Clear";
        uint8_t season      = SEASON_SUMMER;
    };
} // namespace HogwartsMP::Shared
