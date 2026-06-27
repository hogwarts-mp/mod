#include <utils/safe_win32.h>

#include "human.h"

#include "core/appearance_dump.h"
#include "core/application.h"
#include "core/broom_experiment.h"
#include "core/ccd_wire.h"
#include "core/proxy_locomotion.h"
#include "core/student_proxy.h"
#include "sdk/natives/ue4_natives.h"
#include "sdk/reflection/ue4_reflection.h"

#include "shared/modules/mount_records.hpp"
#include "shared/rpc/set_appearance.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/replication/entity_registry.h>
#include <networking/replication/replication_manager.h>

#include <sdk/offsets/entities/uplayer.h>

#include "UObject/UObjectArray.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

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

    // Drive locomotion with our packed ABP_RemoteAvatar (a Speed-steered 1D blendspace) instead of the
    // single-node ProxyLocomotion clips — smooth gait blending. Flip false to force the tested fallback.
    constexpr bool kUseDiyAbp     = true;
    const wchar_t *kRemoteAbpPath = L"/Game/Avatar/ABP_RemoteAvatar.ABP_RemoteAvatar_C";

    // Pin a loaded asset into the GC root set so a cached UObject* can't dangle (the ABP class is cached
    // in a static; an unrooted cached pointer can be freed by a collection and then dereferenced).
    void RootObject(UObjectBase *obj) {
        auto *arr = HogwartsMP::Core::gGlobals.objectArray;
        if (!obj || !arr) {
            return;
        }
        auto *item = arr->IndexToObject(static_cast<int32_t>(obj->GetUniqueID()));
        if (item && item->Object == obj) {
            item->SetFlags(EInternalObjectFlags::RootSet);
        }
    }

    // The rotation the proxy should hold. BP_RemoteAvatarCCC bakes its own mesh orientation (the mesh
    // faces the actor's +X forward), so the actor faces the synced rotation directly. Single tuning seam
    // if a build's proxy turns out to need a mesh-yaw correction.
    glm::quat ProxyFacing(const glm::quat &synced) {
        return synced;
    }

    // Whether the pawn is airborne (jumping/falling) — drives the remote in-air anim. Reads
    // CharacterMovementComponent::IsFalling (true off the ground in either direction).
    bool DetectInAir(void *pawn) {
        static auto *cmcCls = FindUClass("Class /Script/Engine.CharacterMovementComponent");
        if (!cmcCls) {
            return false;
        }
        struct {
            UClass *ComponentClass;
            UObjectBase *ReturnValue;
        } get {cmcCls, nullptr};
        CallUFunction(pawn, "GetComponentByClass", &get);
        if (!get.ReturnValue) {
            return false;
        }
        struct {
            bool ReturnValue;
        } falling {false};
        CallUFunction(get.ReturnValue, "IsFalling", &falling);
        return falling.ReturnValue;
    }

    // Switch a skeletal mesh into AnimBlueprint mode running animClass. UE4.27 names it SetAnimClass; some
    // builds expose SetAnimInstanceClass — try both.
    bool SetAnimClassOn(void *comp, UObjectBase *animClass) {
        struct {
            UObjectBase *NewClass;
        } p {animClass};
        if (CallUFunction(comp, "SetAnimClass", &p)) {
            return true;
        }
        return CallUFunction(comp, "SetAnimInstanceClass", &p);
    }

    // The pawn's world velocity (UE cm/s) via the game's own getter — published while mounted so remotes
    // can dead-reckon the broom.
    Vec3f GetActorVelocity(void *actor) {
        Vec3f v {};
        CallUFunction(actor, "GetVelocity", &v);
        return v;
    }

    // Whether an attach-parent class is a rideable mount. The one place to extend for other mounts; the
    // matched class name is also the wire token (mapped to a mount-allowlist id) used to spawn the mount
    // on remote clients.
    bool IsMountClass(const std::string &className) {
        return className.find("FlyingBroom") != std::string::npos;
    }

    // Whether the pawn is mounted. While riding, the game attaches the pawn to the mount actor — the
    // attach parent's class is both the flag and the mount identity. On true, outName holds the mount
    // class short name (NUL-terminated, capped).
    bool DetectMount(void *pawn, char *outName, size_t cap) {
        struct {
            UObjectBase *ReturnValue;
        } parent {};
        CallUFunction(pawn, "GetAttachParentActor", &parent);
        if (!parent.ReturnValue) {
            return false;
        }
        const auto parentClass = narrow(parent.ReturnValue->GetClass()->GetFName());
        if (!IsMountClass(parentClass)) {
            return false;
        }
        std::strncpy(outName, parentClass.c_str(), cap - 1);
        outName[cap - 1] = '\0';
        return true;
    }

    // While mounted the rider is attached to the mount — the mount is the actor the sync drives.
    AActor *SyncTarget(AActor *actor, int32_t actorIndex, AActor *broom, int32_t broomIndex, bool mounted) {
        if (mounted) {
            if (auto *b = AliveActor(broom, broomIndex)) {
                return b;
            }
        }
        return AliveActor(actor, actorIndex);
    }

    const wchar_t *kProxyWandMesh = L"/Game/RiggedObjects/Props/Wands/CharacterWands/SK_Wands_Student01.SK_Wands_Student01";

    // Attach a wand to the proxy's hand. HL has no "wand drawn" state — the wand is always parented to the
    // body mesh (CharacterMesh0) at the "WandSocket" bone and just rides the arm pose. So we mirror that:
    // a SkeletalMeshActor holding the wand mesh, attached to the body mesh at WandSocket. Always on.
    // Returns the actor (null on failure). Same spawn+attach idiom as the broom.
    AActor *SpawnWandOnProxy(AActor *proxy, UObjectBase *mesh) {
        if (!proxy || !mesh || !GWorld || !*GWorld || !UWorld__SpawnActor) {
            return nullptr;
        }
        static auto *actorCls = FindUClass("Class /Script/Engine.SkeletalMeshActor");
        static auto *skmCls   = FindUClass("Class /Script/Engine.SkeletalMesh");
        if (!actorCls || !skmCls) {
            return nullptr;
        }
        static auto *wandMesh = []() -> UObjectBase * {
            // Pin into the GC root set on first load (a cached static dangles after a collection otherwise).
            auto *m = LoadObjectByPath(skmCls, kProxyWandMesh);
            RootObject(m);
            return m;
        }();
        if (!wandMesh) {
            return nullptr;
        }
        const Vec3f loc = GetActorPos(proxy);
        FVector pos {loc.X, loc.Y, loc.Z};
        FRotator rot {0.f, 0.f, 0.f};
        FActorSpawnParameters params {};
        params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto *wandActor = UWorld__SpawnActor(*GWorld, actorCls, &pos, &rot, params);
        if (!wandActor) {
            return nullptr;
        }
        struct {
            bool b;
        } noCol {false};
        CallUFunction(wandActor, "SetActorEnableCollision", &noCol); // don't shove the avatar around

        UObjectBase *comp = ReadObjectProperty(wandActor, "SkeletalMeshComponent");
        if (!comp) {
            auto *smcCls = FindUClass("Class /Script/Engine.SkeletalMeshComponent");
            struct {
                UClass *ComponentClass;
                UObjectBase *ReturnValue;
            } get {smcCls, nullptr};
            CallUFunction(wandActor, "GetComponentByClass", &get);
            comp = get.ReturnValue;
        }
        if (!comp) {
            // No mesh component to dress — destroy the actor rather than leave an empty one parked at the
            // proxy until it dies (near-impossible for a stock SkeletalMeshActor, but keep it strict).
            UWorld__DestroyActor(*GWorld, wandActor, false, true);
            return nullptr;
        }
        {
            struct {
                UObjectBase *NewMesh;
                bool bReinitPose;
            } sm {wandMesh, true};
            CallUFunction(comp, "SetSkeletalMesh", &sm);
        }
        // Attach to the body mesh at WandSocket (snap so it rides the hand pose).
        struct {
            UObjectBase *Parent;
            FName SocketName;
            uint8_t LocationRule;      // 2 = SnapToTarget
            uint8_t RotationRule;      // 2 = SnapToTarget
            uint8_t ScaleRule;         // 1 = KeepWorld
            bool bWeldSimulatedBodies; // false
        } att {mesh, MakeFName(L"WandSocket"), 2, 2, 1, false};
        CallUFunction(wandActor, "K2_AttachToComponent", &att);
        return wandActor;
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
        UObjectBase *ccc  = nullptr;
        UObjectBase *mesh = nullptr;
        auto *actor       = StudentProxy::SpawnProxy(position.x, position.y, position.z, 0.f, &ccc, &mesh);
        if (!actor) {
            Framework::Logging::GetLogger("Human")->error("Remote avatar spawn failed");
            return;
        }
        _actor         = actor;
        _actorIndex    = ObjectIndex(actor);
        _ccc           = ccc;
        _mesh          = mesh;
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

        // Publish mount state (rides the DeltaSerializer to everyone): the Mounted flag + which broom as a
        // 1-based allowlist id (0 = default/unknown). In-air is on-foot only — while mounted the broom
        // owns the pose and flying reads as "falling".
        char mountClass[64] = {};
        const bool mounted  = DetectMount(pc->Pawn, mountClass, sizeof(mountClass));
        SetFlag(Shared::Modules::HumanSync::Mounted, mounted);
        data.mountId = mounted ? Shared::Modules::MountClassId(mountClass) : 0;
        SetFlag(Shared::Modules::HumanSync::InAir, !mounted && DetectInAir(pc->Pawn));

        // World velocity — only while mounted (remotes dead-reckon the broom from it; the on-foot snapshot
        // path ignores it). Zeroed on foot so the value stops changing and its delta Field goes quiet.
        if (mounted) {
            const auto vel = GetActorVelocity(pc->Pawn);
            velocity       = {vel.X, vel.Y, vel.Z};
        }
        else {
            velocity = {0.f, 0.f, 0.f};
        }

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

    void ClientHuman::UpdateRemote(float) {
        // Mount/dismount transitions first (Mounted flag + data.mountId arrive via the DeltaSerializer):
        // the sync target switches between the rider and the broom.
        auto *rider = AliveActor(_actor, _actorIndex);
        if (IsMounted() && !_mounted && rider) {
            // Latch mounted *before* the spawn so a failed Mount() doesn't re-attempt (spawn + destroy a
            // broom) every tick. If it fails, _broom stays null and SyncTarget falls back to the rider.
            _mounted = true;
            if (auto *broom = BroomRider::Mount(_actor, _mesh, Shared::Modules::MountClassName(data.mountId))) {
                _broom      = broom;
                _broomIndex = ObjectIndex(broom);
            }
            _interp.Reset(); // fresh leg toward the new target (the broom)
        }
        // Dismount on un-mount OR when the rider was GC'd while mounted (else the broom is orphaned).
        else if (_mounted && (!IsMounted() || !rider)) {
            BroomRider::Dismount(rider, rider ? _mesh : nullptr, AliveActor(_broom, _broomIndex));
            _broom      = nullptr;
            _broomIndex = -1;
            _mounted    = false;
            _interp.Reset();
            // Re-assert on-foot locomotion: the broom mount swapped the mesh to single-node broom anims,
            // so clear the gait latches (incl. the ABP assignment) and let UpdateGait re-take it.
            _gaitApplied = _airApplied = _moveBlendApplied = _abpAssigned = false;
        }

        auto *target = SyncTarget(_actor, _actorIndex, _broom, _broomIndex, _mounted);
        if (!target) {
            return;
        }

        // A fresh replicated transform? Record a snapshot + feed the speed estimator. Detected by
        // comparison — Deserialize updates position/rotation in place with no callback. A teleport-sized
        // jump since the last packet snaps (clears the buffer) so we don't lerp across the gap.
        if (!_hasTarget || position != _lastTarget || rotation != _lastTargetRot) {
            const glm::vec3 moved = position - _lastTarget;
            const bool jumped     = _hasTarget && glm::dot(moved, moved) > 5000.f * 5000.f;
            UpdatePacketSpeed(moved, jumped || !_hasTarget);
            _interp.Push(position, ProxyFacing(rotation), jumped || !_hasTarget);
            _lastTarget    = position;
            _lastTargetRot = rotation; // raw synced rotation — fresh-packet detection compares the wire value
            _hasTarget     = true;
        }

        // Place the proxy each frame. Mounted: DEAD-RECKON (extrapolate from pos + synced velocity) so the
        // broom tracks ~now at flight speed instead of trailing ~bufferMs behind. On foot: snapshot
        // interpolation (smooth + exact). Either way the attached rider follows the driven actor.
        if (_mounted) {
            // Dead-reckon, but clamp the extrapolation age so a stalled sender (lag spike) freezes the
            // broom near its last point instead of flying it off along the stale velocity. KNOWN OFFSET:
            // the rider re-attaches at the broom's seat socket, so it rides ~one socket-height above the
            // synced point (the synced pos is the local rider's seat); small + cosmetic, tune via the seat
            // seam if it reads wrong.
            constexpr float kMaxExtrapMs = 250.f;
            const float ageS             = std::min(static_cast<float>(GetUpdateAge()), kMaxExtrapMs) / 1000.f;
            const glm::vec3 ep           = position + velocity * ageS;
            // The broom mesh forward sits 90° off its actor yaw, so the pair flies "sideways" (facing
            // radially out of a turn) if we apply the synced yaw raw. Offset the broom's yaw to align its
            // visual forward with the travel direction; the attached rider follows. TUNE in-game.
            constexpr float kBroomYawDeg = 90.f;
            Rot3f br                     = RotatorFromQuat(rotation);
            br.Yaw += kBroomYawDeg;
            TeleportActor(target, {ep.x, ep.y, ep.z}, br);
        }
        else {
            const float bufferMs = std::clamp(_intervalMs * 2.0f, 80.0f, 300.0f);
            glm::vec3 rp;
            glm::quat rr;
            if (_interp.Sample(rp, rr, bufferMs)) {
                TeleportActor(target, {rp.x, rp.y, rp.z}, RotatorFromQuat(rr));
            }
        }

        // A stopped avatar sends no position packets (delta compression), so the last speed would stick and
        // idle would keep playing the run/walk clip. Decay to standing after several missed sends (with
        // headroom over the measured interval so a jittery link doesn't zero a still-moving avatar).
        const float stopMs = std::max(300.f, _intervalMs * 3.f);
        if (_havePacketTime &&
            std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - _lastPacketTime).count() > stopMs) {
            _speed          = 0.0f;
            _distAccum      = 0.0f;
            _timeAccum      = 0.0f;
            _havePacketTime = false; // next packet is treated as the first — clean resume, no huge-dt sample
        }
        // Gait runs on foot only (the broom owns the mounted pose), and not before the async CCC mesh is built.
        if (!_mounted && ProxyReadyToDrive()) {
            UpdateGait();
        }
    }

    // Ground speed for gait selection from how far the replicated position moved between packets
    // (horizontal only). EWMA Σdistance / Σtime: numerator and denominator share the decay, so noisy
    // per-packet arrival time cancels (dividing a single distance by a single jittery dt flickers gait).
    void ClientHuman::UpdatePacketSpeed(const glm::vec3 &moved, bool teleported) {
        const auto now = std::chrono::steady_clock::now();
        if (teleported || !_havePacketTime) {
            _speed     = 0.0f;
            _distAccum = 0.0f;
            _timeAccum = 0.0f;
        }
        else {
            const float dt = std::chrono::duration<float>(now - _lastPacketTime).count();
            if (dt > 1e-3f && dt < 1.0f) {
                const float horiz      = std::sqrt(moved.x * moved.x + moved.y * moved.y);
                constexpr float kDecay = 0.8f; // ~5-packet window
                _distAccum             = _distAccum * kDecay + horiz;
                _timeAccum             = _timeAccum * kDecay + dt;
                if (_timeAccum > 1e-4f) {
                    _speed = _distAccum / _timeAccum;
                }
                const float intervalMs = dt * 1000.f;
                _intervalMs            = _intervalMs > 0.f ? _intervalMs + (intervalMs - _intervalMs) * 0.3f : intervalMs;
            }
        }
        _lastPacketTime = now;
        _havePacketTime = true;
    }

    // DIY-pak locomotion: assign ABP_RemoteAvatar to the body mesh once, then steer its Speed var by the
    // synced ground speed every frame (the blendspace blends idle→walk→jog→sprint). In-air rides the
    // synced InAir flag into bInAir. The writes are no-ops until the ABP ships the vars.
    void ClientHuman::UpdateGaitAbp() {
        if (!_abpAssigned) {
            static auto *clsMeta = FindUClass("Class /Script/CoreUObject.Class");
            static auto *abp     = []() -> UObjectBase * {
                auto *a = clsMeta ? LoadObjectByPath(clsMeta, kRemoteAbpPath) : nullptr;
                RootObject(a); // pin against GC — a cached static that isn't rooted can dangle
                return a;
            }();
            if (!abp) {
                _blendUnavailable = true; // pak/ABP missing — let UpdateGait fall back to single-node
                Framework::Logging::GetLogger("Human")->warn("ABP_RemoteAvatar load FAILED — single-node fallback");
                return;
            }
            if (!SetAnimClassOn(_mesh, abp)) {
                _blendUnavailable = true; // anim class couldn't be applied — use the known-good single-node path
                Framework::Logging::GetLogger("Human")->warn("SetAnimClass not reflected — single-node fallback");
                return;
            }
            _abpAssigned = true;
            Framework::Logging::GetLogger("Human")->info("ABP_RemoteAvatar assigned — locomotion via custom AnimBP");
        }

        // Ease the Speed fed to the blendspace per-FRAME toward the packet-stepped _speed (writing it raw
        // steps the input at the packet rate → gait flicker).
        const auto now = std::chrono::steady_clock::now();
        if (!_abpTickInit) {
            _abpLastTick = now;
            _abpTickInit = true;
        }
        const float dt = std::chrono::duration<float>(now - _abpLastTick).count();
        _abpLastTick   = now;
        const float k  = std::clamp(dt * 10.0f, 0.0f, 1.0f); // ~100ms follow
        _abpSpeed += (_speed - _abpSpeed) * k;

        if (auto *inst = ReadObjectProperty(_mesh, "AnimScriptInstance")) {
            SetFloatProperty(inst, "Speed", _abpSpeed);
            SetBoolProperty(inst, "bInAir", IsInAir());
        }
    }

    void ClientHuman::UpdateGait() {
        if (!_mesh) {
            return;
        }
        // DIY-pak AnimBP path (smooth blending). Falls through to single-node if the pak/ABP failed.
        if (kUseDiyAbp && !_blendUnavailable) {
            UpdateGaitAbp();
            return;
        }
        // Airborne: play the fall loop once and hold it until we land. Clear the ground latches so the
        // gait/blend re-asserts on landing.
        if (IsInAir()) {
            if (!_airApplied) {
                ProxyLocomotion::PlayAir(_mesh);
                _airApplied       = true;
                _gaitApplied      = false;
                _moveBlendApplied = false;
            }
            return;
        }

        const auto gait = ProxyLocomotion::GaitForSpeed(_speed, _gait);

        // Standing still (or no blendspace path): discrete clips. The 1D move blendspace bottoms out at a
        // slow walk, not a true stand, so idle always uses the clip.
        if (gait == ProxyLocomotion::Gait::Idle || _blendUnavailable) {
            if (!_gaitApplied || gait != _gait || _moveBlendApplied) {
                ProxyLocomotion::PlayGait(_mesh, gait);
                _gait             = gait;
                _gaitApplied      = true;
                _airApplied       = false;
                _moveBlendApplied = false;
            }
            return;
        }

        // Moving: play the blendspace once, then steer it by speed every frame.
        if (!_moveBlendApplied) {
            if (!ProxyLocomotion::PlayMoveBlend(_mesh)) {
                _blendUnavailable = true; // asset missing — fall back to discrete clips next frame
                return;
            }
            _moveBlendApplied = true;
            _gaitApplied      = false;
            _airApplied       = false;
        }
        if (!ProxyLocomotion::DriveMoveBlend(_mesh, _speed)) {
            _blendUnavailable = true; // not reflected here — latch the fallback, re-assert a clip next frame
            _moveBlendApplied = false;
        }
    }

    // Async-spawn readiness gate: the CCC build assigns CharacterMesh0's SkeletalMesh a few frames after
    // spawn; driving (graft / Speed) before then T-poses or crashes.
    bool ClientHuman::ProxyReadyToDrive() {
        if (_proxyReady) {
            return true;
        }
        if (!_mesh || !ReadObjectProperty(_mesh, "SkeletalMesh")) {
            return false;
        }
        _proxyReady = true;
        // The hand socket exists now — give the proxy its (always-on) wand.
        if (auto *wand = SpawnWandOnProxy(_actor, _mesh)) {
            _wand      = wand;
            _wandIndex = ObjectIndex(wand);
        }
        Framework::Logging::GetLogger("Human")->info("CCC proxy build complete — locomotion enabled");
        return true;
    }

    void ClientHuman::DeallocReplica(MafiaNet::Connection_RM3 *) {
        if (_isLocal) {
            if (Human::GetLocal() == this) {
                Human::SetLocal(nullptr);
            }
        }
        else {
            // Tear the broom + wand down first so they aren't orphaned when the proxy goes away.
            if (_mounted) {
                BroomRider::Dismount(AliveActor(_actor, _actorIndex), nullptr, AliveActor(_broom, _broomIndex));
            }
            StudentProxy::DestroyProxy(AliveActor(_wand, _wandIndex));
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
