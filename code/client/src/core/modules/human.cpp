#include <utils/safe_win32.h>

#include "human.h"

#include <flecs.h>

#include <logging/logger.h>

#include <world/modules/base.hpp>

#include "core/student_proxy.h"
#include "core/ue4_natives.h"
#include "core/ue4_reflection.h"

#include "shared/messages/human/human_despawn.h"
#include "shared/messages/human/human_self_update.h"
#include "shared/messages/human/human_spawn.h"
#include "shared/messages/human/human_update.h"
#include "shared/modules/human_sync.hpp"
#include "shared/modules/mod.hpp"

#include <cmath>

namespace {
    using namespace HogwartsMP::Core::UE4;

    // Weak-ref resolution against GC slot reuse (cf. StudentProxy).
    AActor *AliveActor(AActor *actor, int32_t index) {
        auto *arr = HogwartsMP::Core::gGlobals.objectArray;
        if (!arr || !actor || index < 0) {
            return nullptr;
        }
        auto *item = arr->IndexToObject(index);
        if (!item || item->Object != reinterpret_cast<UObjectBase *>(actor)) {
            return nullptr;
        }
        return actor;
    }

    int32_t ObjectIndex(AActor *actor) {
        return actor ? static_cast<int32_t>(reinterpret_cast<UObjectBase *>(actor)->GetUniqueID()) : -1;
    }

    struct Vec3f {
        float X, Y, Z;
    };
    struct Rot3f {
        float Pitch, Yaw, Roll;
    };

    Vec3f GetActorPos(void *actor) {
        Vec3f loc{};
        CallUFunction(actor, "K2_GetActorLocation", &loc);
        return loc;
    }

    Rot3f GetActorRot(void *actor) {
        Rot3f rot{};
        CallUFunction(actor, "K2_GetActorRotation", &rot);
        return rot;
    }

    // UE rotator (degrees) <-> quaternion, ported from FRotator::Quaternion()
    // and FQuat::Rotator() so axis/sign conventions match the engine exactly.
    glm::quat QuatFromRotator(const Rot3f &r) {
        constexpr float kDegToRad = glm::pi<float>() / 180.f;
        const float sp = std::sin(r.Pitch * kDegToRad * 0.5f), cp = std::cos(r.Pitch * kDegToRad * 0.5f);
        const float sy = std::sin(r.Yaw * kDegToRad * 0.5f), cy = std::cos(r.Yaw * kDegToRad * 0.5f);
        const float sr = std::sin(r.Roll * kDegToRad * 0.5f), cr = std::cos(r.Roll * kDegToRad * 0.5f);
        glm::quat q;
        q.x = cr * sp * sy - sr * cp * cy;
        q.y = -cr * sp * cy - sr * cp * sy;
        q.z = cr * cp * sy - sr * sp * cy;
        q.w = cr * cp * cy + sr * sp * sy;
        return q;
    }

    float NormalizeAxisDeg(float deg) {
        deg = std::fmod(deg + 180.f, 360.f);
        if (deg < 0.f) {
            deg += 360.f;
        }
        return deg - 180.f;
    }

    Rot3f RotatorFromQuat(const glm::quat &q) {
        constexpr float kRadToDeg  = 180.f / glm::pi<float>();
        constexpr float kThreshold = 0.4999995f; // gimbal-lock guard (UE's SINGULARITY_THRESHOLD)
        const float singularity    = q.z * q.x - q.w * q.y;
        const float yawY           = 2.f * (q.w * q.z + q.x * q.y);
        const float yawX           = 1.f - 2.f * (q.y * q.y + q.z * q.z);
        Rot3f r{};
        r.Yaw = std::atan2(yawY, yawX) * kRadToDeg;
        if (singularity < -kThreshold) {
            r.Pitch = -90.f;
            r.Roll  = NormalizeAxisDeg(-r.Yaw - 2.f * std::atan2(q.x, q.w) * kRadToDeg);
        }
        else if (singularity > kThreshold) {
            r.Pitch = 90.f;
            r.Roll  = NormalizeAxisDeg(r.Yaw - 2.f * std::atan2(q.x, q.w) * kRadToDeg);
        }
        else {
            r.Pitch = std::asin(2.f * singularity) * kRadToDeg;
            r.Roll  = std::atan2(-2.f * (q.w * q.x + q.y * q.z), 1.f - 2.f * (q.x * q.x + q.y * q.y)) * kRadToDeg;
        }
        return r;
    }

    void TeleportActor(void *actor, const Vec3f &pos, const Rot3f &rot) {
        struct {
            Vec3f DestLocation;
            Rot3f DestRotation;
            bool ReturnValue;
        } params{pos, rot, false};
        CallUFunction(actor, "K2_TeleportTo", &params);
    }
} // namespace

namespace HogwartsMP::Core::Modules {

    flecs::query<Human::Tracking> Human::findAllHumans;

    Human::Human(flecs::world &world) {
        world.module<Human>();

        world.component<Tracking>();
        world.component<LocalPlayer>();
        world.component<Interpolated>();
        world.component<HumanData>();
        world.component<Avatar>();

        findAllHumans = world.query_builder<Human::Tracking>().build();

        world.system<Tracking, Shared::Modules::HumanSync::UpdateData, LocalPlayer, Framework::World::Modules::Base::Transform>("UpdateLocalPlayer")
            .each([](flecs::entity e, Tracking &tracking, Shared::Modules::HumanSync::UpdateData &, LocalPlayer &lp, Framework::World::Modules::Base::Transform &tr) {
                if (!tracking.player) {
                    return;
                }
                // PlayerController can be null transiently (e.g. during the
                // fast-travel the game runs right after connecting).
                const auto pc = tracking.player->PlayerController;
                if (!pc) {
                    return;
                }
                const auto pawn = pc->Pawn;
                if (!pawn) {
                    return;
                }
                // Read the pawn's WORLD location via the game's own getter —
                // RootComponent->RelativeLocation is parent-relative, not the
                // world position, so it stays static while the player moves.
                const auto worldLoc = GetActorPos(pawn);
                tr.pos              = {worldLoc.X, worldLoc.Y, worldLoc.Z};
                tr.rot              = QuatFromRotator(GetActorRot(pawn));
            });

        world.system<Interpolated, Avatar>("UpdateRemoteHuman").each([](flecs::entity e, Interpolated &interpolated, Avatar &av) {
            if (e.try_get<LocalPlayer>() != nullptr) {
                return;
            }
            auto *target = AliveActor(av.actor, av.actorIndex);
            if (!target) {
                return;
            }
            const auto cur    = GetActorPos(target);
            const auto newPos = interpolated.interpolator.GetPosition()->UpdateTargetValue({cur.X, cur.Y, cur.Z});
            const auto newRot = interpolated.interpolator.GetRotation()->UpdateTargetValue(QuatFromRotator(GetActorRot(target)));
            TeleportActor(target, {newPos.x, newPos.y, newPos.z}, RotatorFromQuat(newRot));
        });
    }

    void Human::Create(flecs::entity e, uint64_t spawnProfile) {
        e.ensure<Core::Modules::Human::Tracking>();

        auto &interp = e.ensure<Interpolated>();
        interp.interpolator.GetPosition()->SetCompensationFactor(1.5f);

        e.add<HumanData>();
        e.add<Shared::Modules::Mod::EntityKind>();
        e.set<Shared::Modules::Mod::EntityKind>({Shared::Modules::Mod::MOD_PLAYER});
        e.add<Shared::Modules::HumanSync::UpdateData>();

        // Remote avatar: a student proxy at the entity's last known position.
        const auto tr = e.try_get<Framework::World::Modules::Base::Transform>();
        const float x = tr ? static_cast<float>(tr->pos.x) : 0.f;
        const float y = tr ? static_cast<float>(tr->pos.y) : 0.f;
        const float z = tr ? static_cast<float>(tr->pos.z) : 0.f;

        // TODO(appearance sync): derive gender/house from the wire once the
        // HumanSpawn message carries appearance; defaults to Gryffindor male.
        const StudentProxy::Appearance appearance{};
        UObjectBase *skin = nullptr;
        auto *actor       = StudentProxy::SpawnProxy(x, y, z, 0.f, appearance, &skin);
        if (!actor) {
            Framework::Logging::GetLogger("Human")->error("Remote avatar spawn failed");
            return;
        }
        auto &av      = e.ensure<Avatar>();
        av.actor      = actor;
        av.actorIndex = ObjectIndex(actor);
        av.skin       = skin;
    }

    void Human::SetupLocalPlayer(Application *, flecs::entity e) {
        //e.world().defer_begin();
        e.add<Core::Modules::Human::Tracking>();

        e.add<Shared::Modules::HumanSync::UpdateData>();
        e.add<Core::Modules::Human::LocalPlayer>();
        e.add<HumanData>();
        e.add<Shared::Modules::Mod::EntityKind>();
        e.set<Shared::Modules::Mod::EntityKind>({Shared::Modules::Mod::MOD_PLAYER});
        e.add<Framework::World::Modules::Base::Frame>();

        const auto localPlayer = Core::gGlobals.localPlayer;

        if (!localPlayer->PlayerController->Pawn) {
            Framework::Logging::GetLogger("Human")->error("No pawn found.");
            Core::gApplication->GetNetworkingEngine()->GetNetworkClient()->Disconnect();
            return;
        }

        const auto rootComponent = localPlayer->PlayerController->Pawn->RootComponent;
        if (!rootComponent) {
            Framework::Logging::GetLogger("Human")->error("No pawn root component");
            Core::gApplication->GetNetworkingEngine()->GetNetworkClient()->Disconnect();
            return;
        }

        // Fetch the component only after ALL adds above: every add() moves the
        // entity to a new archetype table, dangling earlier ensure() refs (the
        // original code wrote tracking.player through one — it never landed).
        e.ensure<Core::Modules::Human::Tracking>().player = localPlayer;

        auto es       = e.try_get_mut<Framework::World::Modules::Base::Streamable>();
        es->modEvents.updateProc = [](Framework::Networking::NetworkPeer *peer, uint64_t guid, flecs::entity e) {
            const auto updateData = e.try_get<Shared::Modules::HumanSync::UpdateData>();

            Shared::Messages::Human::HumanUpdate humanUpdate {};
            humanUpdate.SetServerID(Framework::World::ClientEngine::GetServerID(e));
            humanUpdate.SetData(*updateData);
            peer->Send(humanUpdate, guid);
            return true;
        };
        //e.world().defer_end();
    }

    void Human::Update(flecs::entity e) {
        auto av = e.try_get_mut<Avatar>();
        if (!av) {
            return;
        }
        const auto tr = e.try_get<Framework::World::Modules::Base::Transform>();

        auto *target = AliveActor(av->actor, av->actorIndex);
        if (!target || !tr) {
            return;
        }
        const auto cur        = GetActorPos(target);
        const auto curRot     = QuatFromRotator(GetActorRot(target));
        const glm::vec3 delta = glm::vec3(tr->pos) - glm::vec3{cur.X, cur.Y, cur.Z};
        const bool farAway    = glm::dot(delta, delta) > 5000.f * 5000.f;
        auto interp           = e.try_get_mut<Interpolated>();
        if (interp && !farAway) {
            interp->interpolator.GetPosition()->SetTargetValue({cur.X, cur.Y, cur.Z}, tr->pos, HogwartsMP::Core::gApplication->GetTickInterval());
            interp->interpolator.GetRotation()->SetTargetValue(curRot, tr->rot, HogwartsMP::Core::gApplication->GetTickInterval());
        }
        else {
            // Streaming-in / teleport-sized jumps snap instead of crawling.
            TeleportActor(target, {static_cast<float>(tr->pos.x), static_cast<float>(tr->pos.y), static_cast<float>(tr->pos.z)}, RotatorFromQuat(tr->rot));
            if (interp) {
                interp->interpolator.GetPosition()->SetTargetValue(tr->pos, tr->pos, HogwartsMP::Core::gApplication->GetTickInterval());
                interp->interpolator.GetRotation()->SetTargetValue(tr->rot, tr->rot, HogwartsMP::Core::gApplication->GetTickInterval());
            }
        }
    }

    void Human::Remove(flecs::entity e) {
        if (e.try_get<LocalPlayer>() != nullptr) {
            return;
        }
        auto av = e.try_get_mut<Avatar>();
        if (!av) {
            return;
        }
        StudentProxy::DestroyProxy(AliveActor(av->actor, av->actorIndex));
        *av = {};
    }

    void Human::SetupMessages(Application *app) {
        const auto net = app->GetNetworkingEngine()->GetNetworkClient();
        net->RegisterMessage<Shared::Messages::Human::HumanSpawn>(Shared::Messages::ModMessages::MOD_HUMAN_SPAWN, [app](MafiaNet::RakNetGUID guid, Shared::Messages::Human::HumanSpawn *msg) {
            auto e = app->GetWorldEngine()->GetEntityByServerID(msg->GetServerID());
            if (!e.is_alive()) {
                return;
            }

            Create(e, msg->GetSpawnProfile());
        });
        net->RegisterMessage<Shared::Messages::Human::HumanDespawn>(Shared::Messages::ModMessages::MOD_HUMAN_DESPAWN, [app](MafiaNet::RakNetGUID guid, Shared::Messages::Human::HumanDespawn *msg) {
            const auto e = app->GetWorldEngine()->GetEntityByServerID(msg->GetServerID());
            if (!e.is_alive()) {
                return;
            }

            Remove(e);
        });
        net->RegisterMessage<Shared::Messages::Human::HumanUpdate>(Shared::Messages::ModMessages::MOD_HUMAN_UPDATE, [app](MafiaNet::RakNetGUID guid, Shared::Messages::Human::HumanUpdate *msg) {
            const auto e = app->GetWorldEngine()->GetEntityByServerID(msg->GetServerID());
            if (!e.is_alive()) {
                return;
            }

            auto updateData = e.try_get_mut<Shared::Modules::HumanSync::UpdateData>();
            *updateData     = msg->GetData();

            Update(e);
        });
        net->RegisterMessage<Shared::Messages::Human::HumanSelfUpdate>(Shared::Messages::ModMessages::MOD_HUMAN_SELF_UPDATE, [app](MafiaNet::RakNetGUID guid, Shared::Messages::Human::HumanSelfUpdate *msg) {
            const auto e = app->GetWorldEngine()->GetEntityByServerID(msg->GetServerID());
            if (!e.is_alive()) {
                return;
            }

            auto trackingData = e.try_get_mut<Core::Modules::Human::Tracking>();
            if (!trackingData) {
                return;
            }

            auto frame       = e.try_get_mut<Framework::World::Modules::Base::Frame>();
            frame->modelHash = msg->GetSpawnProfile();
        });
    }

    void Human::UpdateTransform(flecs::entity e) {
        auto av = e.try_get_mut<Avatar>();
        if (!av) {
            return;
        }
        const auto tr = e.try_get<Framework::World::Modules::Base::Transform>();
        auto *target  = AliveActor(av->actor, av->actorIndex);
        if (!target || !tr) {
            return;
        }
        // Hard set (streaming-in / teleport) — skip interpolation.
        TeleportActor(target, {static_cast<float>(tr->pos.x), static_cast<float>(tr->pos.y), static_cast<float>(tr->pos.z)}, RotatorFromQuat(tr->rot));
    }
} // namespace HogwartsMP::Core::Modules
