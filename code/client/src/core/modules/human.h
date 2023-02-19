#pragma once

#include <flecs/flecs.h>

#include "core/application.h"

#include <world/modules/base.hpp>
#include <utils/interpolator.h>

#include <sdk/entities/uplayer.h>

namespace HogwartsMP::Core::Modules {
    struct Human {
        struct Tracking {
            SDK::UPlayer *player = nullptr;
        };

        struct Interpolated {
            Framework::Utils::Interpolator interpolator = {};
        };

        struct LocalPlayer {
            [[maybe_unused]] char _unused;
        };

        struct HumanData {
            // todo
            char _unused;
        };

        Human(flecs::world &world);

        static void Create(flecs::entity e, uint64_t spawnProfile);

        static void SetupLocalPlayer(Application *app, flecs::entity e);

        static void Update(flecs::entity e);

        static void Remove(flecs::entity e);

        static void SetupMessages(Application *app);

        static void UpdateTransform(flecs::entity);

        static flecs::query<Tracking> findAllHumans;
    };
} // namespace HogwartsMP::Core::Modules
