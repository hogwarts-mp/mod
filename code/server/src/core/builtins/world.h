#pragma once

#include "scripting/engines/node/engine.h"
#include "scripting/engines/node/sdk.h"

#include "player.h"

#include "shared/rpc/chat_message.h"
#include "shared/rpc/set_weather.h"
#include "shared/modules/mod.hpp"

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
            Framework::CoreModules::GetScriptingModule()->ForEachResource([&](Framework::Scripting::Engines::IResource *resource) {
                auto nodeResource = reinterpret_cast<Framework::Scripting::Engines::Node::Resource *>(resource);
                nodeResource->InvokeEvent("chatMessage", Human::WrapHuman(nodeResource, e), message);
            });
        }

        static void OnChatCommand(flecs::entity e, std::string message, std::string command, std::vector<std::string> args) {
            Framework::CoreModules::GetScriptingModule()->ForEachResource([&](Framework::Scripting::Engines::IResource *resource) {
                auto nodeResource = reinterpret_cast<Framework::Scripting::Engines::Node::Resource *>(resource);
                nodeResource->InvokeEvent("chatCommand", Human::WrapHuman(nodeResource, e), message, command, args);
            });
        }

        static void Register(v8::Isolate *isolate, v8pp::module *rootModule) {
            v8pp::module environment(isolate);
            environment.function("setWeather", &World::SetWeather);
            environment.function("setTime", &World::SetTimeofDay);
            environment.function("setDate", &World::SetDate);
            environment.function("setSeason", &World::SetSeason);
            rootModule->submodule("Environment", environment);

            rootModule->function("sendChatMessage", &World::SendChatMessage);
            rootModule->function("broadcastMessage", &World::BroadcastMessage);
        }
    };
} // namespace HogwartsMP::Scripting
