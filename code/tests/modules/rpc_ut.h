#pragma once

#include "shared/modules/mod.hpp"
#include "shared/rpc/chat_message.h"
#include "shared/rpc/set_weather.h"

#include <string>

MODULE(rpc, {
    using namespace HogwartsMP::Shared;

    IT("round-trips a chat message through a bitstream", {
        RPC::ChatMessage out {};
        out.FromParameters("Expecto Patronum!");
        EQUALS(out.Valid(), true);

        MafiaNet::BitStream bs;
        out.Serialize(&bs, true);

        RPC::ChatMessage in {};
        in.Serialize(&bs, false);
        STREQUALS(in.GetText().c_str(), "Expecto Patronum!");
        EQUALS(in.Valid(), true);
    });

    IT("rejects empty and oversized chat messages", {
        RPC::ChatMessage empty {};
        empty.FromParameters("");
        EQUALS(empty.Valid(), false);

        RPC::ChatMessage longest {};
        longest.FromParameters(std::string(1023, 'a'));
        EQUALS(longest.Valid(), true);

        RPC::ChatMessage oversized {};
        oversized.FromParameters(std::string(1024, 'a'));
        EQUALS(oversized.Valid(), false);
    });

    IT("round-trips weather state through a bitstream", {
        Modules::Mod::Weather weather {};
        weather.weather    = "Stormy_01";
        weather.timeHour   = 21;
        weather.timeMinute = 45;
        weather.dateDay    = 31;
        weather.dateMonth  = 10;
        weather.season     = Modules::Mod::SEASON_AUTUMN;

        RPC::SetWeather out {};
        out.FromParameters(weather);
        EQUALS(out.Valid(), true);

        MafiaNet::BitStream bs;
        out.Serialize(&bs, true);

        RPC::SetWeather in {};
        in.Serialize(&bs, false);
        STREQUALS(in.GetWeather().c_str(), "Stormy_01");
        EQUALS(in.GetTimeHour(), 21);
        EQUALS(in.GetTimeMinute(), 45);
        EQUALS(in.GetDateDay(), 31);
        EQUALS(in.GetDateMonth(), 10);
        EQUALS(in.GetSeason(), Modules::Mod::SEASON_AUTUMN);
        EQUALS(in.Valid(), true);
    });

    IT("rejects out-of-range weather values", {
        Modules::Mod::Weather weather {};
        weather.weather    = "Clear";
        weather.timeHour   = 11;
        weather.timeMinute = 0;
        weather.dateDay    = 12;
        weather.dateMonth  = 6;
        weather.season     = Modules::Mod::SEASON_SUMMER;

        {
            auto invalid     = weather;
            invalid.timeHour = 24;
            RPC::SetWeather rpc {};
            rpc.FromParameters(invalid);
            EQUALS(rpc.Valid(), false);
        }

        {
            auto invalid      = weather;
            invalid.dateMonth = 13;
            RPC::SetWeather rpc {};
            rpc.FromParameters(invalid);
            EQUALS(rpc.Valid(), false);
        }

        {
            auto invalid    = weather;
            invalid.weather = "";
            RPC::SetWeather rpc {};
            rpc.FromParameters(invalid);
            EQUALS(rpc.Valid(), false);
        }
    });
});
