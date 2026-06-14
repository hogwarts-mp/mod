#pragma once

#include <integrations/server/instance.h>

#include "shared/game/weather.h"

namespace HogwartsMP {
    class Server: public Framework::Integrations::Server::Instance {
      private:
        static inline Framework::Scripting::Engine *_scriptingEngine;

        // Server-authoritative environment state (replaces the old flecs Mod::Weather singleton).
        Shared::WeatherState _weather;

      public:
        void PostInit() override;

        void PostUpdate() override;

        void PreShutdown() override;

        // Networking event hooks (framework dispatches these; no Set*Callback setters anymore).
        void OnPlayerConnect(const Framework::Integrations::Server::PlayerConnectionData &data) override;
        void OnPlayerDisconnect(MafiaNet::PeerGuid guid) override;
        void OnChatMessage(uint64_t senderNetworkId, const std::string &text) override;
        void OnChatCommand(uint64_t senderNetworkId, const std::string &text, const std::string &command, const std::vector<std::string> &args) override;

        // Broadcast a chat line to every connected client (framework ChatMessage RPC).
        void BroadcastChatMessage(const std::string &msg);

        Shared::WeatherState &GetWeather() {
            return _weather;
        }

        void ModuleRegister(Framework::Scripting::Engine *engine) override;

        static inline Server *_serverRef = nullptr;

        static Framework::Scripting::Engine *GetScriptingEngine() {
            return _scriptingEngine;
        }
    };
} // namespace HogwartsMP
