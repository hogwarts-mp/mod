#pragma once

#include <v8.h>
#include <v8pp/class.hpp>
#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include "events.h"
#include "human.h"

#include "core/modules/human.h"
#include "core/server.h"

#include "shared/game/weather.h"
#include "shared/rpc/set_weather.h"

#include <core_modules.h>
#include <logging/logger.h>
#include <networking/network_peer.h>
#include <networking/rpc/chat_message.h>

#include <cstdint>
#include <string>
#include <vector>

namespace HogwartsMP::Scripting {
    class World final {
      public:
        static void BroadcastMessage(std::string message) {
            auto *peer = Framework::CoreModules::GetNetworkPeer();
            if (!peer) {
                return;
            }
            Framework::Networking::RPC::ChatMessage payload {std::move(message)};
            peer->BroadcastRPC(payload);
        }

        static void SendChatMessage(Human *human, std::string message) {
            if (human) {
                human->SendChat(message);
            }
        }

        // World.spawnHuman(x, y, z) -> Human
        // Spawns a server-owned human (NPC) at a world position and returns its Human handle. Clients
        // render it via the student-proxy path like any other player. Despawn with human.destroy().
        static void JsSpawnHuman(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto ctx      = isolate->GetCurrentContext();
            if (info.Length() < 3 || !info[0]->IsNumber() || !info[1]->IsNumber() || !info[2]->IsNumber()) {
                isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "spawnHuman(x, y, z) requires 3 numbers")));
                return;
            }
            const float x = static_cast<float>(info[0]->NumberValue(ctx).FromMaybe(0.0));
            const float y = static_cast<float>(info[1]->NumberValue(ctx).FromMaybe(0.0));
            const float z = static_cast<float>(info[2]->NumberValue(ctx).FromMaybe(0.0));

            auto *server = Server::_serverRef;
            if (!server) {
                return;
            }
            auto *netEngine = server->GetNetworkingEngine();
            if (!netEngine) {
                return;
            }
            auto *repl  = netEngine->GetNetworkServer()->GetReplicationManager();
            auto *human = Core::Modules::Human::Spawn(repl, x, y, z);
            if (!human) {
                return;
            }
            info.GetReturnValue().Set(v8pp::class_<Human>::create_object(isolate, human->GetNetworkID()));
        }

        static void SetWeather(std::string weatherSetName) {
            if (auto *server = Server::_serverRef) {
                server->GetWeather().weather = std::move(weatherSetName);
                BroadcastWeather();
            }
        }

        static void SetTimeOfDay(int timeHour, int timeMinute) {
            if (auto *server = Server::_serverRef) {
                server->GetWeather().timeHour   = static_cast<uint8_t>(timeHour);
                server->GetWeather().timeMinute = static_cast<uint8_t>(timeMinute);
                BroadcastWeather();
            }
        }

        static void SetDate(int day, int month) {
            if (auto *server = Server::_serverRef) {
                server->GetWeather().dateDay   = static_cast<uint8_t>(day);
                server->GetWeather().dateMonth = static_cast<uint8_t>(month);
                BroadcastWeather();
            }
        }

        // Spring=0, Summer=1, Autumn=2, Winter=3
        static void SetSeason(int season) {
            if (auto *server = Server::_serverRef) {
                server->GetWeather().season = static_cast<uint8_t>(season);
                BroadcastWeather();
            }
        }

        static void EventChatMessage(uint64_t senderNetworkId, std::string message) {
            Framework::Logging::GetLogger("Scripting")->debug("Chat message from {}: {}", senderNetworkId, message);

            EmitServerEvent("chatMessage", [senderNetworkId, message](v8::Isolate *isolate, v8::Local<v8::Context>, std::vector<v8::Local<v8::Value>> &args) {
                args.push_back(v8pp::class_<Human>::create_object(isolate, senderNetworkId));
                args.push_back(v8pp::to_v8(isolate, message));
            });
        }

        static void EventChatCommand(uint64_t senderNetworkId, std::string message, std::string command, std::vector<std::string> commandArgs) {
            Framework::Logging::GetLogger("Scripting")->debug("Chat command from {}: /{} ({})", senderNetworkId, command, message);

            EmitServerEvent("chatCommand", [senderNetworkId, message, command, commandArgs](v8::Isolate *isolate, v8::Local<v8::Context> context, std::vector<v8::Local<v8::Value>> &args) {
                v8::Local<v8::Array> argsArray = v8::Array::New(isolate, static_cast<int>(commandArgs.size()));
                for (size_t i = 0; i < commandArgs.size(); ++i) {
                    argsArray->Set(context, static_cast<uint32_t>(i), v8pp::to_v8(isolate, commandArgs[i])).Check();
                }

                args.push_back(v8pp::class_<Human>::create_object(isolate, senderNetworkId));
                args.push_back(v8pp::to_v8(isolate, message));
                args.push_back(v8pp::to_v8(isolate, command));
                args.push_back(argsArray);
            });
        }

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
            if (!isolate || global.IsEmpty()) {
                return;
            }

            auto ctx = isolate->GetCurrentContext();

            v8pp::module worldModule(isolate);
            worldModule.function("broadcastMessage", &World::BroadcastMessage);
            worldModule.function("sendChatMessage", &World::SendChatMessage);
            auto worldObj = worldModule.new_instance();
            // spawnHuman needs the isolate + returns a wrapped object, so it's a raw FunctionTemplate
            // set on the module object rather than a typed v8pp function.
            worldObj->Set(ctx, v8pp::to_v8(isolate, "spawnHuman"),
                          v8::FunctionTemplate::New(isolate, &World::JsSpawnHuman)->GetFunction(ctx).ToLocalChecked())
                .Check();
            global->Set(ctx, v8pp::to_v8(isolate, "World"), worldObj).Check();

            v8pp::module envModule(isolate);
            envModule.function("setWeather", &World::SetWeather);
            envModule.function("setTime", &World::SetTimeOfDay);
            envModule.function("setDate", &World::SetDate);
            envModule.function("setSeason", &World::SetSeason);
            global->Set(ctx, v8pp::to_v8(isolate, "Environment"), envModule.new_instance()).Check();
        }

      private:
        // Push the server-authoritative environment state to every client.
        static void BroadcastWeather() {
            auto *server = Server::_serverRef;
            auto *peer   = Framework::CoreModules::GetNetworkPeer();
            if (!server || !peer) {
                return;
            }
            Shared::RPC::SetWeather payload;
            payload.data = server->GetWeather();
            peer->BroadcastRPC(payload);
        }
    };
} // namespace HogwartsMP::Scripting
