#pragma once

#include "shared/game/weather.h"

#include <networking/rpc/rpc.h>

#include <mafianet/BitStream.h>

namespace HogwartsMP::Shared::RPC {
    // Server -> client environment sync. A plain RPC4 payload (see networking/rpc/rpc.h): a stable
    // identifier and a symmetric Serialize, dispatched to a C handler on the client.
    struct SetWeather {
        static constexpr const char *kIdentifier = "HogwartsMP::SetWeather";

        WeatherState data;

        void Serialize(MafiaNet::BitStream *bs, bool write) {
            bs->Serialize(write, data.timeHour);
            bs->Serialize(write, data.timeMinute);
            bs->Serialize(write, data.dateDay);
            bs->Serialize(write, data.dateMonth);
            bs->Serialize(write, data.weather);
            bs->Serialize(write, data.season);
        }
    };
} // namespace HogwartsMP::Shared::RPC
