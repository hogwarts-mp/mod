#pragma once

#include <cstdint>

#include <flecs/flecs.h>
#include <glm/glm.hpp>


namespace HogwartsMP::Shared::Modules {
    struct HumanSync {
        struct UpdateData {
            char _unused;
        };

        HumanSync(flecs::world& world) {
            world.module<HumanSync>();

            world.component<UpdateData>();
        }
    };
}
