#pragma once

#include "shared/game/human.h"

#include <utils/interpolator.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

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

        bool _isLocal = false;

        // Remote avatar: the BP_RemoteAvatarCCC proxy. actorIndex guards the pointer against GC slot reuse
        // (cf. StudentProxy::ResolveAlive); ccc is its CustomizableCharacterComponent (the appearance target).
        AActor *_actor      = nullptr;
        int32_t _actorIndex = -1;
        UObjectBase *_ccc   = nullptr;

        Framework::Utils::Interpolator _interpolator = {};
        // Last replicated transform we set up an interpolation leg toward, so a fresh packet is
        // detected by comparison (there is no per-update callback).
        glm::vec3 _lastTarget    = glm::vec3(0.0f);
        glm::quat _lastTargetRot = glm::identity<glm::quat>();
        bool _hasTarget          = false;

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
