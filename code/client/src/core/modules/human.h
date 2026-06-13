#pragma once

#include <flecs.h>

#include "core/application.h"

#include <world/modules/base.hpp>
#include <utils/interpolator.h>

#include <sdk/entities/uplayer.h>

#include <cstdint>

class AActor;
class UObjectBase;

namespace HogwartsMP::Core::Modules {
    struct Human {
        struct Tracking {
            SDK::UPlayer *player = nullptr;
        };

        // Remote-player avatar: a student proxy spawned on entity creation.
        // actorIndex guards the pointer against GC slot reuse (cf.
        // StudentProxy::ResolveAlive). skin is the head/hands component the
        // outfit is master-posed to (kept for anim / future mount handling).
        struct Avatar {
            AActor *actor = nullptr;
            int32_t actorIndex = -1;
            UObjectBase *skin = nullptr;
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
    
    //private:
        //static void InitRPCs(Application *app);
    };
} // namespace HogwartsMP::Core::Modules
