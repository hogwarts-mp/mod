#include "human.h"

#include "world/modules/base.hpp"

#include "shared/messages/human/human_despawn.h"
#include "shared/messages/human/human_self_update.h"
#include "shared/messages/human/human_spawn.h"
#include "shared/messages/human/human_update.h"
#include "shared/modules/human_sync.hpp"

#include <flecs/flecs.h>

namespace HogwartsMP::Core::Modules {
    Human::Human(flecs::world &world) {
        world.module<Human>();
    }

    void Human::Create(Framework::Networking::NetworkServer *net, flecs::entity e) {
        auto frame       = e.get_mut<Framework::World::Modules::Base::Frame>();
        frame->modelHash = 335218123840277515; /* TODO */

        e.add<Shared::Modules::HumanSync::UpdateData>();

        auto es = e.get_mut<Framework::World::Modules::Base::Streamable>();

        es->modEvents.spawnProc = [net](Framework::Networking::NetworkPeer *peer, uint64_t guid, flecs::entity e) {
            const auto frame = e.get<Framework::World::Modules::Base::Frame>();
            Shared::Messages::Human::HumanSpawn humanSpawn;
            humanSpawn.FromParameters(frame->modelHash);
            humanSpawn.SetServerID(e.id());

            net->Send(humanSpawn, guid);
            // todo other stuff
            return true;
        };

        es->modEvents.despawnProc = [net](Framework::Networking::NetworkPeer *peer, uint64_t guid, flecs::entity e) {
            Shared::Messages::Human::HumanDespawn humanDespawn;
            humanDespawn.SetServerID(e.id());
            net->Send(humanDespawn, guid);
            return true;
        };

        es->modEvents.selfUpdateProc = [net](Framework::Networking::NetworkPeer *peer, uint64_t guid, flecs::entity e) {
            Shared::Messages::Human::HumanSelfUpdate humanSelfUpdate;
            humanSelfUpdate.SetServerID(e.id());
            net->Send(humanSelfUpdate, guid);
            return true;
        };

        es->modEvents.updateProc = [net](Framework::Networking::NetworkPeer *peer, uint64_t guid, flecs::entity e) {
            const auto trackingMetadata = e.get<Shared::Modules::HumanSync::UpdateData>();
            const auto frame            = e.get<Framework::World::Modules::Base::Frame>();

            Shared::Messages::Human::HumanUpdate humanUpdate {};
            humanUpdate.SetServerID(e.id());
            humanUpdate.SetData(*trackingMetadata);
            net->Send(humanUpdate, guid);
            return true;
        };
    }

    void Human::SetupMessages(std::shared_ptr<Framework::World::ServerEngine> srv, Framework::Networking::NetworkServer *net) {
        net->RegisterMessage<Shared::Messages::Human::HumanUpdate>(Shared::Messages::ModMessages::MOD_HUMAN_UPDATE, [srv](SLNet::RakNetGUID guid, Shared::Messages::Human::HumanUpdate *msg) {
            const auto e = srv->WrapEntity(msg->GetServerID());
            if (!e.is_alive()) {
                return;
            }
            if (!srv->IsEntityOwner(e, guid.g)) {
                return;
            }

            auto trackingMetadata = e.get_mut<Shared::Modules::HumanSync::UpdateData>();
            *trackingMetadata = msg->GetData();
        });
    }
} // namespace HogwartsMP::Core::Modules
