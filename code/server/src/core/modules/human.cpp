#include "human.h"

#include <networking/replication/entity_registry.h>

#include <logging/logger.h>

namespace HogwartsMP::Core::Modules {
    using Framework::Networking::Replication::EntityRegistry;
    using Framework::Networking::Replication::ReplicationManager;

    namespace {
        // TODO(appearance sync): derive from the wire once appearance is carried; defaults to the
        // model the proxy path already knows how to build.
        constexpr uint64_t kDefaultSpawnProfile = 335218123840277515ULL;

        Shared::HumanEntity *CreateHuman(ReplicationManager *repl) {
            if (!repl) {
                return nullptr;
            }
            const auto typeId = EntityRegistry::Get().TypeId(Shared::kHumanTypeName);
            auto *human       = static_cast<Shared::HumanEntity *>(repl->CreateEntity(typeId));
            if (!human) {
                Framework::Logging::GetLogger("Human")->error("Failed to create Human entity (type not registered?)");
                return nullptr;
            }
            human->spawnProfile = kDefaultSpawnProfile;
            return human;
        }
    } // namespace

    void Human::Register() {
        EntityRegistry::Get().Register<Shared::HumanEntity>(Shared::kHumanTypeName);
    }

    Shared::HumanEntity *Human::CreatePlayer(ReplicationManager *repl, const Framework::Integrations::Server::PlayerConnectionData &data) {
        auto *human = CreateHuman(repl);
        if (!human) {
            return nullptr;
        }
        human->nickname = data.nickname;
        // The framework's default interest radius is 100 units — ~1 m in Hogwarts' cm scale, so
        // nothing more than a step away streams. Use a game-sized range (500 m) for the player's
        // viewer entity so other players/NPCs are visible.
        human->streaming.range = 50000.f;
        repl->SetOwner(human, data.guid);
        repl->SetViewer(data.guid, human);
        return human;
    }

    Shared::HumanEntity *Human::Spawn(ReplicationManager *repl, float x, float y, float z) {
        auto *human = CreateHuman(repl);
        if (!human) {
            return nullptr;
        }
        human->position = {x, y, z};
        // Left unowned (ownerGUID stays UNASSIGNED): the server keeps authority, so it won't get
        // handed to a client that would then echo a stale transform back every tick.
        Framework::Logging::GetLogger("Human")->debug("Spawned NPC entity {} at ({}, {}, {})", human->GetNetworkID(), x, y, z);
        return human;
    }
} // namespace HogwartsMP::Core::Modules
