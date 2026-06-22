#pragma once

#include <integrations/server/instance.h>

#include "shared/game/weather.h"

#include <cstdint>
#include <string>
#include <unordered_map>

namespace HogwartsMP {
    class Server: public Framework::Integrations::Server::Instance {
      private:
        static inline Framework::Scripting::Engine *_scriptingEngine;

        // Server-authoritative environment state (replaces the old flecs Mod::Weather singleton).
        Shared::WeatherState _weather;

        // Maps a connected player's NetworkID -> its stable identity. Kept
        // server-side only and NOT on the replicated entity, so a player's identity is never leaked
        // to other clients. Backs the per-player Storage exposed via Human.getData/setData.
        std::unordered_map<uint64_t, std::string> _playerIdentities;

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

        // Stable per-player identity, keyed by NetworkID. Set on connect, cleared
        // on disconnect. Returns "" for an unknown id (e.g. a server NPC, or not yet connected).
        void SetPlayerIdentity(uint64_t networkId, std::string identity) {
            _playerIdentities[networkId] = std::move(identity);
        }
        void ClearPlayerIdentity(uint64_t networkId) {
            _playerIdentities.erase(networkId);
        }
        std::string GetPlayerIdentity(uint64_t networkId) const {
            const auto it = _playerIdentities.find(networkId);
            return it != _playerIdentities.end() ? it->second : std::string();
        }

        void ModuleRegister(Framework::Scripting::Engine *engine) override;

        static inline Server *_serverRef = nullptr;

        static Framework::Scripting::Engine *GetScriptingEngine() {
            return _scriptingEngine;
        }
    };
} // namespace HogwartsMP
