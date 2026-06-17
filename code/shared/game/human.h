#pragma once

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

        void OnSerializeConstruction(Framework::Networking::Replication::FieldSerializer &fields) override {
            fields.Field(spawnProfile);
            fields.Field(nickname);
        }
    };
} // namespace HogwartsMP::Shared
