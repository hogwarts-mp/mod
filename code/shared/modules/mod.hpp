#pragma once

#include <flecs/flecs.h>

namespace HogwartsMP::Shared::Modules {
    struct Mod {
        enum EntityTypes { MOD_PLAYER };
        enum SeasonKind { SEASON_SPRING, SEASON_SUMMER, SEASON_AUTUMN, SEASON_WINTER };

        struct EntityKind {
            EntityTypes kind;
        };

        struct Weather {
            uint8_t timeHour;
            uint8_t timeMinute;
            uint8_t dateDay;
            uint8_t dateMonth;

            std::string weather;
            SeasonKind season;
        };

        Mod(flecs::world &world) {
            world.module<Mod>();

            world.component<EntityKind>();
            world.component<Weather>();
        }
    };
} // namespace HogwartsMP::Shared::Modules
