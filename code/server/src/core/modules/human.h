#pragma once

#include <integrations/server/instance.h>
#include <networking/replication/replication_manager.h>

#include "shared/game/human.h"

namespace HogwartsMP::Core::Modules {
    class Human {
      public:
        // Register the Human network type (server-side constructor) with the EntityRegistry. Call once
        // at startup, before any connection is accepted.
        static void Register();

        // Create a connecting player's avatar: owned by the player's peer and registered as that
        // connection's viewer (drives its interest set). Returns nullptr on failure.
        static Shared::HumanEntity *CreatePlayer(Framework::Networking::Replication::ReplicationManager *repl, const Framework::Integrations::Server::PlayerConnectionData &data);

        // Spawn a server-owned human (NPC) at a world position — the basis for NPCs and a way to test
        // the remote-avatar path single-client. Stays server-owned (the server is authoritative over
        // its transform). Tear down with ReplicationManager::DestroyEntity.
        static Shared::HumanEntity *Spawn(Framework::Networking::Replication::ReplicationManager *repl, float x, float y, float z);
    };
} // namespace HogwartsMP::Core::Modules
