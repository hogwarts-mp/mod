#include <utils/safe_win32.h>

#include "human.h"

#include <flecs/flecs.h>

#include <world/modules/base.hpp>

#include "shared/messages/human/human_despawn.h"
#include "shared/messages/human/human_self_update.h"
#include "shared/messages/human/human_spawn.h"
#include "shared/messages/human/human_update.h"
#include "shared/modules/human_sync.hpp"
#include "shared/modules/mod.hpp"

namespace HogwartsMP::Core::Modules {

    flecs::query<Human::Tracking> Human::findAllHumans;

    Human::Human(flecs::world &world) {
        world.module<Human>();

        world.component<Tracking>();
        world.component<LocalPlayer>();
        world.component<Interpolated>();
        world.component<HumanData>();

        findAllHumans = world.query_builder<Human::Tracking>().build();

        world.system<Tracking, Shared::Modules::HumanSync::UpdateData, LocalPlayer, Framework::World::Modules::Base::Transform>("UpdateLocalPlayer")
            .each([](flecs::entity e, Tracking &tracking, Shared::Modules::HumanSync::UpdateData &metadata, LocalPlayer &lp, Framework::World::Modules::Base::Transform &tr) {
                // if (tracking.human) {
                    // SDK::ue::sys::math::C_Vector newPos = tracking.human->GetPos();
                    // SDK::ue::sys::math::C_Quat newRot   = tracking.human->GetRot();
                    // tr.pos                              = {newPos.x, newPos.y, newPos.z};
                    // tr.rot                              = {newRot.w, newRot.x, newRot.y, newRot.z};

                    // Store the required metadata for onfoot sync
                    // todo
                // }
            });

        world.system<Tracking, Interpolated>("UpdateRemoteHuman").each([](flecs::entity e, Tracking &tracking, Interpolated &interpolated) {
            if (e.get<LocalPlayer>() == nullptr) {
                auto updateData = e.get_mut<Shared::Modules::HumanSync::UpdateData>();
                auto humanData  = e.get_mut<HumanData>();
                // todo
            }
        });
    }

    void Human::Create(flecs::entity e, uint64_t spawnProfile) {
        // auto info           = Core::gApplication->GetEntityFactory()->RequestHuman(spawnProfile);
        auto trackingData   = e.get_mut<Core::Modules::Human::Tracking>();
        // todo

        auto interp = e.get_mut<Interpolated>();
        interp->interpolator.GetPosition()->SetCompensationFactor(1.5f);

        e.add<HumanData>();
        e.set<Shared::Modules::Mod::EntityKind>({Shared::Modules::Mod::MOD_PLAYER});
        // todo spawn
    }

    void Human::SetupLocalPlayer(Application *, flecs::entity e) {
        auto trackingData   = e.get_mut<Core::Modules::Human::Tracking>();

        e.add<Shared::Modules::HumanSync::UpdateData>();
        e.add<Core::Modules::Human::LocalPlayer>();
        e.add<HumanData>();
        e.set<Shared::Modules::Mod::EntityKind>({Shared::Modules::Mod::MOD_PLAYER});

        const auto es            = e.get_mut<Framework::World::Modules::Base::Streamable>();
        es->modEvents.updateProc = [](Framework::Networking::NetworkPeer *peer, uint64_t guid, flecs::entity e) {
            const auto updateData = e.get<Shared::Modules::HumanSync::UpdateData>();

            Shared::Messages::Human::HumanUpdate humanUpdate {};
            humanUpdate.SetServerID(Framework::World::ClientEngine::GetServerID(e));
            humanUpdate.SetData(*updateData);
            peer->Send(humanUpdate, guid);
            return true;
        };
    }

    void Human::Update(flecs::entity e) {
        const auto trackingData = e.get<Core::Modules::Human::Tracking>();
        if (!trackingData) {
            return;
        }

        auto updateData                    = e.get_mut<Shared::Modules::HumanSync::UpdateData>();
        auto humanData                     = e.get_mut<HumanData>();
        // todo

        // Update basic data
        const auto tr = e.get<Framework::World::Modules::Base::Transform>();
        auto interp   = e.get_mut<Interpolated>();
        if (interp) {
            // const auto humanPos = trackingData->human->GetPos();
            // const auto humanRot = trackingData->human->GetRot();
            // interp->interpolator.GetPosition()->SetTargetValue({humanPos.x, humanPos.y, humanPos.z}, tr->pos, HogwartsMP::Core::gApplication->GetTickInterval());
            // interp->interpolator.GetRotation()->SetTargetValue({humanRot.w, humanRot.x, humanRot.y, humanRot.z}, tr->rot, HogwartsMP::Core::gApplication->GetTickInterval());
        }
        else {
            // SDK::ue::sys::math::C_Vector newPos    = {tr->pos.x, tr->pos.y, tr->pos.z};
            // SDK::ue::sys::math::C_Quat newRot      = {tr->rot.x, tr->rot.y, tr->rot.z, tr->rot.w};
            // SDK::ue::sys::math::C_Matrix transform = {};
            // transform.Identity();
            // transform.SetRot(newRot);
            // transform.SetPos(newPos);
            // trackingData->human->SetTransform(transform);
        }

        // todo additional sync data
    }

    void Human::Remove(flecs::entity e) {
        auto trackingData = e.get_mut<Core::Modules::Human::Tracking>();
        if (!trackingData || e.get<LocalPlayer>() != nullptr) {
            return;
        }

        // todo despawn
    }

    void Human::SetupMessages(Application *app) {
        const auto net = app->GetNetworkingEngine()->GetNetworkClient();
        net->RegisterMessage<Shared::Messages::Human::HumanSpawn>(Shared::Messages::ModMessages::MOD_HUMAN_SPAWN, [app](SLNet::RakNetGUID guid, Shared::Messages::Human::HumanSpawn *msg) {
            auto e = app->GetWorldEngine()->GetEntityByServerID(msg->GetServerID());
            if (!e.is_alive()) {
                return;
            }

            // Setup tracking info
            Create(e, msg->GetSpawnProfile());

            // Setup other components
            auto updateData = e.get_mut<Shared::Modules::HumanSync::UpdateData>();
            auto humanData  = e.get_mut<HumanData>();
            // todo spawn info

            // set up client updates (NPC streaming)
            // TODO disabled for now, we don't really need to stream NPCs atm
#if 0
                const auto es = e.get_mut<Framework::World::Modules::Base::Streamable>();
                es->modEvents.clientUpdateProc = [&](Framework::Networking::NetworkPeer *peer, uint64_t guid, flecs::entity e) {
                    Shared::Messages::Human::HumanClientUpdate humanUpdate;
                    humanUpdate.FromParameters(e.id());
                    // set up sync data
                    peer->Send(humanUpdate, guid);
                    return true;
                };
#endif
        });
        net->RegisterMessage<Shared::Messages::Human::HumanDespawn>(Shared::Messages::ModMessages::MOD_HUMAN_DESPAWN, [app](SLNet::RakNetGUID guid, Shared::Messages::Human::HumanDespawn *msg) {
            const auto e = app->GetWorldEngine()->GetEntityByServerID(msg->GetServerID());
            if (!e.is_alive()) {
                return;
            }

            Remove(e);
        });
        net->RegisterMessage<Shared::Messages::Human::HumanUpdate>(Shared::Messages::ModMessages::MOD_HUMAN_UPDATE, [app](SLNet::RakNetGUID guid, Shared::Messages::Human::HumanUpdate *msg) {
            const auto e = app->GetWorldEngine()->GetEntityByServerID(msg->GetServerID());
            if (!e.is_alive()) {
                return;
            }

            auto updateData = e.get_mut<Shared::Modules::HumanSync::UpdateData>();
            *updateData     = msg->GetData();

            Update(e);
        });
        net->RegisterMessage<Shared::Messages::Human::HumanSelfUpdate>(Shared::Messages::ModMessages::MOD_HUMAN_SELF_UPDATE, [app](SLNet::RakNetGUID guid, Shared::Messages::Human::HumanSelfUpdate *msg) {
            const auto e = app->GetWorldEngine()->GetEntityByServerID(msg->GetServerID());
            if (!e.is_alive()) {
                return;
            }

            auto trackingData = e.get_mut<Core::Modules::Human::Tracking>();
            if (!trackingData) {
                return;
            }

            auto frame       = e.get_mut<Framework::World::Modules::Base::Frame>();
            frame->modelHash = msg->GetSpawnProfile();

            // update actor data
        });
    }
    void Human::UpdateTransform(flecs::entity e) {
        const auto trackingData = e.get<Core::Modules::Human::Tracking>();
        if (!trackingData) {
            return;
        }

        // Update basic data
        const auto tr = e.get<Framework::World::Modules::Base::Transform>();
        auto interp   = e.get_mut<Interpolated>();
        if (interp) {
            // // todo reset lerp
            // const auto humanPos = trackingData->human->GetPos();
            // const auto humanRot = trackingData->human->GetRot();
            // interp->interpolator.GetPosition()->SetTargetValue({humanPos.x, humanPos.y, humanPos.z}, tr->pos, HogwartsMP::Core::gApplication->GetTickInterval());
            // interp->interpolator.GetRotation()->SetTargetValue({humanRot.w, humanRot.x, humanRot.y, humanRot.z}, tr->rot, HogwartsMP::Core::gApplication->GetTickInterval());
        }
        else {
            // SDK::ue::sys::math::C_Vector newPos    = {tr->pos.x, tr->pos.y, tr->pos.z};
            // SDK::ue::sys::math::C_Quat newRot      = {tr->rot.x, tr->rot.y, tr->rot.z, tr->rot.w};
            // SDK::ue::sys::math::C_Matrix transform = {};
            // transform.Identity();
            // transform.SetRot(newRot);
            // transform.SetPos(newPos);
            // trackingData->human->SetTransform(transform);
        }
    }
} // namespace HogwartsMP::Core::Modules
