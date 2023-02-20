
/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include "shared/messages/messages.h"
#include "src/networking/rpc/rpc.h"

#include <string>

namespace HogwartsMP::Shared::RPC {
    class SetWeather final: public Framework::Networking::RPC::IRPC<SetWeather> {
      private:
        uint8_t timeHour;
        uint8_t timeMinute;
        uint8_t dateDay;
        uint8_t dateMonth;

        SLNet::RakString weather;
        Modules::Mod::SeasonKind season;
      public:
        void FromParameters(const Modules::Mod::Weather &weatherData) {
            timeHour = weatherData.timeHour;
            timeMinute = weatherData.timeMinute;
            dateDay = weatherData.dateDay;
            dateMonth = weatherData.dateMonth;
            weather = weatherData.weather.c_str();
            season = weatherData.season;
        }

        void Serialize(SLNet::BitStream *bs, bool write) override {
            bs->Serialize(write, timeHour);
            bs->Serialize(write, timeMinute);
            bs->Serialize(write, dateDay);
            bs->Serialize(write, dateMonth);
            bs->Serialize(write, weather);
            bs->Serialize(write, season);
        }

        bool Valid() const override {
            return timeHour < 24 && timeMinute < 60 && dateDay < 32 && dateMonth < 13 && !weather.IsEmpty() && weather.GetLength() < 1024 && season < 4;
        }

        uint8_t GetTimeHour() const {
            return timeHour;
        }

        uint8_t GetTimeMinute() const {
            return timeMinute;
        }

        uint8_t GetDateDay() const {
            return dateDay;
        }

        uint8_t GetDateMonth() const {
            return dateMonth;
        }

        std::string GetWeather() const {
            return weather.C_String();
        }

        Modules::Mod::SeasonKind GetSeason() const {
            return season;
        }
    };
} // namespace HogwartsMP::Shared::Messages::Human
