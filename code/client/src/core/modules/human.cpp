#include <utils/safe_win32.h>

#include "human.h"

#include "core/appearance_dump.h"
#include "core/application.h"
#include "core/ccd_wire.h"
#include "core/student_proxy.h"
#include "sdk/natives/ue4_natives.h"
#include "sdk/reflection/ue4_reflection.h"

#include "shared/rpc/set_appearance.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/replication/entity_registry.h>
#include <networking/replication/replication_manager.h>

#include <sdk/offsets/entities/uplayer.h>

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

    // FNV-1a content signature of a CcdProfile — re-send only when the look actually changes.
    uint64_t CcdSignature(const HogwartsMP::Shared::Modules::CcdProfile &c) {
        uint64_t h    = 1469598103934665603ull;
        auto mix      = [&](uint64_t v) { h = (h ^ v) * 1099511628211ull; };
        auto mixStr   = [&](const std::string &s) { for (unsigned char ch : s) { mix(ch); } mix(0xFEull); };
        auto mixF     = [&](float f) { mix(static_cast<uint64_t>(static_cast<int64_t>(f * 100000.0f))); };
        auto mixPiece = [&](const HogwartsMP::Shared::Modules::CcdPiece &p) {
            mixStr(p.characterPiece);
            for (auto &s : p.scalars) { mixStr(s.first); mixF(s.second); }
            for (auto &v : p.vectors) { mixStr(v.first); for (float f : v.second) { mixF(f); } }
            for (auto &t : p.textures) { mixStr(t.first); mixStr(t.second); }
        };
        mix(c.gender);
        for (auto &it : c.characterItems) { mixStr(it.first); mixPiece(it.second); }
        for (auto &o : c.outfits) {
            mixStr(o.first);
            for (auto &e : o.second) { mixStr(e.first); mixPiece(e.second); }
        }
        return h;
    }

    struct Vec3f {
        float X, Y, Z;
    };
    struct Rot3f {
        float Pitch, Yaw, Roll;
    };

    Vec3f GetActorPos(void *actor) {
        Vec3f loc {};
        CallUFunction(actor, "K2_GetActorLocation", &loc);
        return loc;
    }

    Rot3f GetActorRot(void *actor) {
        Rot3f rot {};
        CallUFunction(actor, "K2_GetActorRotation", &rot);
        return rot;
    }

    // UE rotator (degrees) <-> quaternion, ported from FRotator::Quaternion() and FQuat::Rotator() so
    // axis/sign conventions match the engine exactly.
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
        Rot3f r {};
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
        } params {pos, rot, false};
        CallUFunction(actor, "K2_TeleportTo", &params);
    }
} // namespace

namespace HogwartsMP::Core::Modules {
    using Framework::Networking::Replication::EntityRegistry;
    using Framework::Networking::Replication::NetworkEntity;

    void ClientHuman::OnConstructed() {
        // Reference() runs before DeserializeConstruction, so the manager (and thus IsOwner) is valid
        // here; ownerGUID has already been read off the construction snapshot.
        _isLocal = IsOwner();
        if (_isLocal) {
            // The local player's pawn already exists in-game; nothing to spawn. We push its transform
            // upstream each frame (the owned entity serializes to the server automatically).
            Human::SetLocal(this);
            return;
        }
        SpawnProxy();
    }

    void ClientHuman::SpawnProxy() {
        _interpolator.GetPosition()->SetCompensationFactor(1.5f);

        UObjectBase *ccc = nullptr;
        auto *actor      = StudentProxy::SpawnProxy(position.x, position.y, position.z, 0.f, &ccc);
        if (!actor) {
            Framework::Logging::GetLogger("Human")->error("Remote avatar spawn failed");
            return;
        }
        _actor         = actor;
        _actorIndex    = ObjectIndex(actor);
        _ccc           = ccc;
        _lastTarget    = position;
        _lastTargetRot = rotation;
        _hasTarget     = true;

        // Dress the proxy from the ccd carried on the construction snapshot (empty until it arrives).
        ApplyAppearance();
    }

    // Apply the entity's ccd (from construction or a later AppearanceUpdate) to the proxy CCC. No-op until
    // the proxy exists and the ccd carries worn pieces — an all-empty ccd is a remote whose appearance
    // hasn't arrived yet, so the seeded base stays. (characterItems || outfits = what reconstruct dresses.)
    void ClientHuman::ApplyAppearance() {
        if (_ccc && (!ccd.characterItems.empty() || !ccd.outfits.empty())) {
            CcdWire::MirrorCcdToProxyCcc(_ccc, ccd);
        }
    }

    void ClientHuman::Update(float tickInterval) {
        if (_isLocal) {
            UpdateLocal(tickInterval);
        }
        else {
            UpdateRemote(tickInterval);
        }
    }

    void ClientHuman::UpdateLocal(float) {
        const auto localPlayer = Core::gGlobals.localPlayer;
        if (!localPlayer) {
            return;
        }
        // PlayerController can be null transiently (e.g. the fast-travel right after connecting).
        const auto pc = localPlayer->PlayerController;
        if (!pc || !pc->Pawn) {
            return;
        }
        // Read the pawn's WORLD location via the game's own getter — RootComponent->RelativeLocation
        // is parent-relative, not the world position.
        const auto worldLoc = GetActorPos(pc->Pawn);
        position            = {worldLoc.X, worldLoc.Y, worldLoc.Z};
        rotation            = QuatFromRotator(GetActorRot(pc->Pawn));

        // On a CacheCCD rebuild (pointer change — assumes HL reallocates it), harvest + send the look; the
        // content signature suppresses redundant sends.
        auto *cccCls = FindUClass("Class /Script/CustomizableCharacter.CustomizableCharacterComponent");
        if (!cccCls) {
            return;
        }
        struct {
            UClass *ComponentClass;
            UObjectBase *ReturnValue;
        } gc{cccCls, nullptr};
        CallUFunction(reinterpret_cast<UObjectBase *>(pc->Pawn), "GetComponentByClass", &gc);
        auto *cache = gc.ReturnValue ? ReadObjectProperty(gc.ReturnValue, "CacheCCD") : nullptr;
        if (!cache || cache == _lastCacheCcd) {
            return;
        }
        _lastCacheCcd = cache;
        Shared::RPC::SetAppearance payload;
        if (!AppearanceDump::BuildLocalCcd(payload.ccd)) {
            return;
        }
        const uint64_t sig = CcdSignature(payload.ccd);
        if (sig == _apprSig) {
            return;
        }
        if (auto *peer = Framework::CoreModules::GetNetworkPeer()) {
            peer->BroadcastRPC(payload); // the client's only connection is the server
            _apprSig = sig;
            Framework::Logging::GetLogger("Human")->info("(re)sent appearance: items={} outfits={} sig={:x}",
                                                         static_cast<int>(payload.ccd.characterItems.size()),
                                                         static_cast<int>(payload.ccd.outfits.size()), sig);
        }
    }

    void ClientHuman::UpdateRemote(float tickInterval) {
        auto *target = AliveActor(_actor, _actorIndex);
        if (!target) {
            return;
        }
        const auto curRaw = GetActorPos(target);
        const glm::vec3 cur {curRaw.X, curRaw.Y, curRaw.Z};
        const glm::quat curRot = QuatFromRotator(GetActorRot(target));

        // A fresh replicated transform since the last leg? Set up a new interpolation (or snap on a
        // teleport-sized jump). Detected by comparison — Deserialize updates position/rotation in
        // place with no callback.
        if (!_hasTarget || position != _lastTarget || rotation != _lastTargetRot) {
            const glm::vec3 delta = position - cur;
            const bool farAway    = glm::dot(delta, delta) > 5000.f * 5000.f;
            if (!farAway) {
                _interpolator.GetPosition()->SetTargetValue(cur, position, tickInterval);
                _interpolator.GetRotation()->SetTargetValue(curRot, rotation, tickInterval);
            }
            else {
                // Streaming-in / teleport-sized jumps snap instead of crawling.
                TeleportActor(target, {position.x, position.y, position.z}, RotatorFromQuat(rotation));
                _interpolator.GetPosition()->SetTargetValue(position, position, tickInterval);
                _interpolator.GetRotation()->SetTargetValue(rotation, rotation, tickInterval);
            }
            _lastTarget    = position;
            _lastTargetRot = rotation;
            _hasTarget     = true;
            return;
        }

        const auto newPos = _interpolator.GetPosition()->UpdateTargetValue(cur);
        const auto newRot = _interpolator.GetRotation()->UpdateTargetValue(curRot);
        TeleportActor(target, {newPos.x, newPos.y, newPos.z}, RotatorFromQuat(newRot));
    }

    void ClientHuman::DeallocReplica(MafiaNet::Connection_RM3 *) {
        if (_isLocal) {
            if (Human::GetLocal() == this) {
                Human::SetLocal(nullptr);
            }
        }
        else {
            StudentProxy::DestroyProxy(AliveActor(_actor, _actorIndex));
            CcdWire::ForgetProxy(_ccc); // unroot the CCD we layered onto this proxy
        }
        delete this;
    }

    void Human::Register() {
        EntityRegistry::Get().Register<ClientHuman>(Shared::kHumanTypeName);
    }

    void Human::UpdateAll(float tickInterval) {
        auto *repl = Framework::CoreModules::GetReplication();
        if (!repl) {
            return;
        }
        repl->ForEachEntity([tickInterval](NetworkEntity *entity) {
            if (auto *human = dynamic_cast<ClientHuman *>(entity)) {
                human->Update(tickInterval);
            }
        });
    }
} // namespace HogwartsMP::Core::Modules
