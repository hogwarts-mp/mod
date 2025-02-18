#pragma once

#include <sol/sol.hpp>

#include "player.h"

#include "shared/rpc/chat_message.h"
#include "shared/rpc/set_weather.h"
#include "shared/modules/mod.hpp"

#include "core/server.h"
#include "core_modules.h"

namespace HogwartsMP::Scripting {
    class World final {
      public:
        static void SendChatMessage(std::string message, Human *human) {
            if (human) {
                const auto ent = human->GetHandle();
                const auto str   = ent.get<Framework::World::Modules::Base::Streamer>();
                if (!str)
                    return;
                FW_SEND_COMPONENT_RPC_TO(Shared::RPC::ChatMessage, SLNet::RakNetGUID(str->guid), message);
            }
        }

        static void SetWeather(std::string weatherSetName) {
            auto world = Framework::CoreModules::GetWorldEngine()->GetWorld();

            auto weather = world->get_mut<Shared::Modules::Mod::Weather>();
            weather->weather = weatherSetName;
            FW_SEND_COMPONENT_RPC(HogwartsMP::Shared::RPC::SetWeather, *weather);
        }

        static void SetTimeofDay(uint8_t timeHour, uint8_t timeMinute) {
            auto world = Framework::CoreModules::GetWorldEngine()->GetWorld();

            auto weather = world->get_mut<Shared::Modules::Mod::Weather>();
            weather->timeHour = timeHour;
            weather->timeMinute = timeMinute;
            FW_SEND_COMPONENT_RPC(HogwartsMP::Shared::RPC::SetWeather, *weather);
        }

        static void SetDate(uint8_t day, uint8_t month) {
            auto world = Framework::CoreModules::GetWorldEngine()->GetWorld();

            auto weather = world->get_mut<Shared::Modules::Mod::Weather>();
            weather->dateDay = day;
            weather->dateMonth = month;
            FW_SEND_COMPONENT_RPC(HogwartsMP::Shared::RPC::SetWeather, *weather);
        }

        static void SetSeason(Shared::Modules::Mod::SeasonKind season) {
            auto world = Framework::CoreModules::GetWorldEngine()->GetWorld();

            auto weather = world->get_mut<Shared::Modules::Mod::Weather>();
            weather->season = season;
            FW_SEND_COMPONENT_RPC(HogwartsMP::Shared::RPC::SetWeather, *weather);
        }

        static void BroadcastMessage(std::string message) {
            FW_SEND_COMPONENT_RPC(Shared::RPC::ChatMessage, message);
        }

        static void OnChatMessage(flecs::entity e, std::string message) {
            const auto engine = HogwartsMP::Server::GetScriptingEngine();
            engine->InvokeEvent("onChatMessage", Human(e), message);
        }

        static void OnChatCommand(flecs::entity e, std::string message, std::string command, std::vector<std::string> args) {
            const auto engine = HogwartsMP::Server::GetScriptingEngine();
            engine->InvokeEvent("onChatCommand", Human(e), message, command, args);
        }

        static void Register(sol::state& luaEngine) {
            // Create the main World type
            sol::usertype<World> worldType = luaEngine.new_usertype<World>("World");
            worldType["broadcastMessage"] = &World::BroadcastMessage;
            worldType["sendChatMessage"] = &World::SendChatMessage;

            // Create Environment namespace/table
            sol::table environment = luaEngine.create_table("Environment");
            environment["setWeather"] = &World::SetWeather;
            environment["setTime"] = &World::SetTimeofDay;
            environment["setDate"] = &World::SetDate;
            environment["setSeason"] = &World::SetSeason;
        }
    };
} // namespace HogwartsMP::Scripting
