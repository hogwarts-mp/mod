#pragma once

#include "core/server.h"
#include <flecs.h>

namespace HogwartsMP::Core::Modules {
    class Human {
    private:
        static void InitRPCs(std::shared_ptr<Framework::World::ServerEngine> srv, Framework::Networking::NetworkServer *net);

      public:
        Human(flecs::world &world);

        static void Create(Framework::Networking::NetworkServer *net, flecs::entity e);

        // Spawn a server-owned human entity (no network peer) at a world
        // position — the basis for server NPCs, and usable to test the
        // remote-avatar path single-client. The regular streamer dresses,
        // moves, and despawns it on clients exactly like any other player.
        // Tear down with Framework::World::ServerEngine::RemoveEntity(e).
        static flecs::entity Spawn(Framework::Networking::NetworkServer *net, std::shared_ptr<Framework::World::ServerEngine> srv, float x, float y, float z);

        static void SetupMessages(std::shared_ptr<Framework::World::ServerEngine> srv, Framework::Networking::NetworkServer *net);
    };
} // namespace HogwartsMP
