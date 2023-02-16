#pragma once

#include <flecs/flecs.h>

namespace HogwartsMP::Shared::Modules {
    struct Mod {
        enum EntityTypes { MOD_PLAYER };
        struct EntityKind {
            EntityTypes kind;
        };

        Mod(flecs::world &world) {
            world.module<Mod>();

            world.component<EntityKind>();
        }
    };
} // namespace HogwartsMP::Shared::Modules
