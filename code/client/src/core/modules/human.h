#pragma once

#include "shared/game/human.h"
#include "core/proxy_locomotion.h"
#include "core/snapshot_interpolator.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <chrono>
#include <cstdint>

class AActor;
class UObjectBase;

namespace HogwartsMP::Core::Modules {
    // Client-side human replica. The server creates a Shared::HumanEntity; the client reconstructs the
    // same type id into a ClientHuman. On construction it binds the local player (when we own it) or
    // spawns a student proxy (remote players and server NPCs). Per frame it pushes the local player's
    // transform upstream, or interpolates the proxy toward the replicated transform.
    class ClientHuman final: public Shared::HumanEntity {
      public:
        void OnConstructed() override;
        void DeallocReplica(MafiaNet::Connection_RM3 *sourceConnection) override;

        void Update(float tickInterval);

        bool IsLocal() const {
            return _isLocal;
        }

        // (Re)dress the proxy from this entity's ccd — called on spawn and on each AppearanceUpdate.
        void ApplyAppearance();

      private:
        void SpawnProxy();
        void UpdateLocal(float tickInterval);
        void UpdateRemote(float tickInterval);
        // Smooth ground speed from the per-packet position delta (for gait selection).
        void UpdatePacketSpeed(const glm::vec3 &moved, bool teleported);
        // Drive the on-foot pose from the smoothed speed + the synced InAir flag.
        void UpdateGait();
        // DIY-pak path: run ABP_RemoteAvatar (the packed 1D-blendspace AnimBP) on the body mesh and steer
        // its Speed/bInAir each frame, instead of the single-node ProxyLocomotion clips. Smooth blending.
        void UpdateGaitAbp();
        // Async-spawn readiness gate: true once CharacterMesh0's body mesh is built (driving before then
        // T-poses/crashes).
        bool ProxyReadyToDrive();

        bool _isLocal = false;

        // Remote avatar: the BP_RemoteAvatarCCC proxy. actorIndex guards the pointer against GC slot reuse
        // (cf. StudentProxy::ResolveAlive); ccc is its CustomizableCharacterComponent (the appearance
        // target); mesh is CharacterMesh0, the locomotion anim target.
        AActor *_actor      = nullptr;
        int32_t _actorIndex = -1;
        UObjectBase *_ccc   = nullptr;
        UObjectBase *_mesh  = nullptr;
        bool _proxyReady    = false;

        // Snapshot interpolation buffer: each fresh replicated transform is recorded with its arrival time;
        // the proxy renders at (now - bufferDelay) by lerping the two snapshots bracketing it — smooth
        // regardless of frame/packet rate, ~bufferDelay in the past. Replaces the framework error-chaser
        // Interpolator (which snaps+freezes per packet).
        SnapshotInterpolator _interp;

        // Last replicated transform, for fresh-packet detection by comparison (Deserialize updates
        // position/rotation in place with no callback).
        glm::vec3 _lastTarget    = glm::vec3(0.0f);
        glm::quat _lastTargetRot = glm::identity<glm::quat>();
        bool _hasTarget          = false;

        // On-foot locomotion: gait clip picked from the avatar's ground speed (derived from how far the
        // replicated position moved between packets; the body itself is moved by the snapshot buffer).
        ProxyLocomotion::Gait _gait = ProxyLocomotion::Gait::Idle;
        bool _gaitApplied           = false; // a discrete gait/idle clip is playing
        bool _airApplied            = false; // the in-air fall clip is playing (vs a ground gait)
        bool _moveBlendApplied      = false; // the move blendspace is playing (steered per frame)
        bool _blendUnavailable      = false; // latched if the ABP/blendspace path isn't available — single-node fallback
        bool _abpAssigned           = false; // ABP_RemoteAvatar assigned to the mesh once
        // Speed = EWMA Σdistance / Σtime across recent packets (per-packet arrival jitter cancels between
        // numerator and denominator). _abpSpeed eases _speed per-frame for the ABP input (no gait flicker).
        float _speed     = 0.0f;
        float _abpSpeed  = 0.0f;
        float _distAccum = 0.0f;
        float _timeAccum = 0.0f;
        float _intervalMs = 0.0f;
        std::chrono::steady_clock::time_point _lastPacketTime {};
        std::chrono::steady_clock::time_point _abpLastTick {};
        bool _havePacketTime = false;
        bool _abpTickInit    = false;

        // Local-player appearance send state: the CacheCCD pointer last harvested (rebuild detection) and
        // the content signature last sent (change detection).
        void *_lastCacheCcd = nullptr;
        uint64_t _apprSig   = 0;
    };

    // Owns the client-side Human type registration and the per-frame update fan-out, plus a pointer to
    // the local player's replica (set when an owned ClientHuman constructs).
    class Human {
      public:
        // Register ClientHuman with the EntityRegistry under the shared type name. Call once at init.
        static void Register();

        // Drive every ClientHuman's per-frame update. No-op until replication is active.
        static void UpdateAll(float tickInterval);

        static void SetLocal(ClientHuman *human) {
            _local = human;
        }
        static ClientHuman *GetLocal() {
            return _local;
        }

      private:
        static inline ClientHuman *_local = nullptr;
    };
} // namespace HogwartsMP::Core::Modules
