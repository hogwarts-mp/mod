#pragma once

#include "shared/game/weather.h"
#include "shared/rpc/set_weather.h"

#include <mafianet/BitStream.h>

#include <string>

MODULE(rpc, {
    using namespace HogwartsMP::Shared;

    IT("round-trips weather state through a bitstream", {
        RPC::SetWeather out {};
        out.data.weather    = "Stormy_01";
        out.data.timeHour   = 21;
        out.data.timeMinute = 45;
        out.data.dateDay    = 31;
        out.data.dateMonth  = 10;
        out.data.season     = SEASON_AUTUMN;

        MafiaNet::BitStream bs;
        out.Serialize(&bs, true);

        RPC::SetWeather in {};
        in.Serialize(&bs, false);
        STREQUALS(in.data.weather.c_str(), "Stormy_01");
        EQUALS(in.data.timeHour, 21);
        EQUALS(in.data.timeMinute, 45);
        EQUALS(in.data.dateDay, 31);
        EQUALS(in.data.dateMonth, 10);
        EQUALS(in.data.season, static_cast<uint8_t>(SEASON_AUTUMN));
    });
});
