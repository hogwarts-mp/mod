#pragma once

#include "shared/modules/appearance.hpp"

#include <networking/replication/network_entity.h>

#include <mafianet/BitStream.h>

#include <cstdint>

namespace HogwartsMP::Shared::RPC {
    namespace Replication = Framework::Networking::Replication;

    // Owner client -> server: my worn appearance (CCD). Server sanitizes, stores it on the sender's
    // HumanEntity, re-broadcasts as AppearanceUpdate.
    struct SetAppearance {
        static constexpr const char *kIdentifier = "HogwartsMP::SetAppearance";

        Modules::CcdProfile ccd;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            Replication::FieldSerializer fs(bs, write);
            Modules::SerializeCcd(fs, ccd);
        }
    };

    // Server -> clients: a player's appearance (CCD) by network id (the construction snapshot covers new
    // streamers). The receiver applies it to that entity's proxy.
    struct AppearanceUpdate {
        static constexpr const char *kIdentifier = "HogwartsMP::AppearanceUpdate";

        uint64_t networkId = 0;
        Modules::CcdProfile ccd;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, networkId);
            Replication::FieldSerializer fs(bs, write);
            Modules::SerializeCcd(fs, ccd);
        }
    };
} // namespace HogwartsMP::Shared::RPC
