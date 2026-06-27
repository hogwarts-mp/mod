#pragma once

#include "shared/modules/appearance.hpp"
#include "shared/modules/human_sync.hpp"

#include <networking/replication/network_entity.h>

#include <mafianet/BitStream.h>

#include <cstdint>
#include <string>

namespace HogwartsMP::Shared {
    // Stable type name registered with the EntityRegistry on both peers. Its CRC32 is the wire type
    // id, so the server's HumanEntity and the client's ClientHuman reconstruct from the same id.
    inline constexpr const char *kHumanTypeName = "HogwartsMP::Human";

    // A networked human (player avatar or server-owned NPC). The transform (position/velocity/
    // rotation) and ownership ride the NetworkEntity base; only the spawn-time identity lives here and
    // is carried once in the construction snapshot.
    class HumanEntity : public Framework::Networking::Replication::NetworkEntity {
      public:
        // Model / appearance profile hash the proxy is built from.
        uint64_t spawnProfile = 0;
        // Display name, shown in chat and exposed to scripting.
        std::string nickname;
        // Per-tick boolean state (in-air/mounted/…), packed into one byte; written by the owning client,
        // relayed to everyone else. Its own delta Field so it can toggle without re-sending the string.
        uint8_t stateFlags = 0;
        // Per-tick string-ish state (broom/spell ids, aim pitch); written by the owning client, relayed.
        Modules::HumanSync::UpdateData data {};
        // Worn appearance, set server-side; rides the construction snapshot. MUST stay the trailing field
        // (SerializeCcd's containment relies on it).
        Modules::CcdProfile ccd;

        bool IsInAir() const {
            return (stateFlags & Modules::HumanSync::InAir) != 0;
        }
        bool IsMounted() const {
            return (stateFlags & Modules::HumanSync::Mounted) != 0;
        }
        bool IsCasting() const {
            return (stateFlags & Modules::HumanSync::Cast) != 0;
        }
        void SetFlag(Modules::HumanSync::StateFlag flag, bool on) {
            stateFlags = on ? (stateFlags | flag) : (stateFlags & ~flag);
        }

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(spawnProfile);
            fields.Field(nickname);
            Modules::SerializeCcd(fields, ccd);
        }

        void SerializeFields(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(stateFlags);
            fields.Field(data);
        }
    };
} // namespace HogwartsMP::Shared
