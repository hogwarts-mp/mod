#include "server.h"

#include "modules/human.h"

#include "builtins/builtins.h"
#include "builtins/events.h"

#include "shared/game/human.h"

#include <core_modules.h>
#include <integrations/shared/rpc/emit_lua_event.h>
#include <networking/replication/replication_manager.h>
#include <networking/rpc/chat_message.h>

#include <mafianet/types.h>

#include <fmt/format.h>

#include <scripting/node_engine.h>
#include <v8pp/convert.hpp>

namespace HogwartsMP {
    void Server::PostInit() {
        _serverRef = this;

        // Register the networked entity types before any connection is accepted.
        Core::Modules::Human::Register();

        // Client -> server scripted events: a client's Game.emitServer sends EmitLuaEvent up; resolve
        // the sender to its avatar and dispatch into the server event bus as (player, payload).
        auto *net = GetNetworkingEngine()->GetNetworkServer();
        net->RegisterRPC<Framework::Integrations::Shared::RPC::EmitLuaEvent>(
            [this](const Framework::Integrations::Shared::RPC::EmitLuaEvent &payload, MafiaNet::Packet *packet) {
                const std::string name = payload.GetEventName();
                if (name.empty()) {
                    return;
                }
                auto *repl   = GetNetworkingEngine()->GetNetworkServer()->GetReplicationManager();
                auto *sender = repl ? repl->GetViewer(MafiaNet::ToPeerGuid(packet->guid)) : nullptr;
                if (!sender) {
                    return;
                }
                Scripting::World::EventClientEvent(sender->GetNetworkID(), name, payload.GetPayload());
            });

        Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->info("Networking messages registered!");
    }

    void Server::PostUpdate() {}

    void Server::PreShutdown() {}

    // A player joined: build its avatar (owned + viewer), announce it, and notify scripting. The
    // framework resolves nickname/hwid/slot and hands them in via PlayerConnectionData.
    void Server::OnPlayerConnect(const Framework::Integrations::Server::PlayerConnectionData &data) {
        auto *repl  = GetNetworkingEngine()->GetNetworkServer()->GetReplicationManager();
        auto *human = Core::Modules::Human::CreatePlayer(repl, data);
        if (!human) {
            return;
        }

        // Record the player's stable identity (server-side only) so scripts can persist per-player
        // data via Human.getData/setData. Eventually swap for a verified account id later
        // without touching the script API.
        SetPlayerIdentity(human->GetNetworkID(), data.hardwareID);

        BroadcastChatMessage(fmt::format("Player {} has joined the session!", data.nickname));
        Scripting::Human::EventPlayerConnected(human->GetNetworkID());
    }

    // A player left: the framework fires this while the avatar is still resolvable, just before
    // replication tears it down.
    void Server::OnPlayerDisconnect(MafiaNet::PeerGuid guid) {
        auto *repl  = GetNetworkingEngine()->GetNetworkServer()->GetReplicationManager();
        auto *human = repl ? dynamic_cast<Shared::HumanEntity *>(repl->GetViewer(guid)) : nullptr;
        const std::string nickname = human ? human->nickname : "Player";

        BroadcastChatMessage(fmt::format("Player {} has left the session!", nickname));
        if (human) {
            Scripting::Human::EventPlayerDisconnected(human->GetNetworkID());
            ClearPlayerIdentity(human->GetNetworkID());
        }
    }

    // Plain chat: dispatch to the JS gamemode if it's listening, otherwise echo "nick: text" so
    // chat still works without a gamemode. The sender is already resolved to its NetworkID.
    void Server::OnChatMessage(uint64_t senderNetworkId, const std::string &text) {
        if (Scripting::GetServerEventListenerCount("chatMessage") > 0) {
            Scripting::World::EventChatMessage(senderNetworkId, text);
            return;
        }
        auto *repl  = GetNetworkingEngine()->GetNetworkServer()->GetReplicationManager();
        auto *human = repl ? dynamic_cast<Shared::HumanEntity *>(repl->GetEntityByNetworkID(senderNetworkId)) : nullptr;
        BroadcastChatMessage(fmt::format("{}: {}", human ? human->nickname : "Player", text));
    }

    // Slash commands are pre-parsed by the framework; forward them to the JS gamemode.
    void Server::OnChatCommand(uint64_t senderNetworkId, const std::string &text, const std::string &command, const std::vector<std::string> &args) {
        Scripting::World::EventChatCommand(senderNetworkId, text, command, args);
    }

    void Server::ModuleRegister(Framework::Scripting::Engine *engine) {
        _scriptingEngine = engine;

        auto *nodeEngine = dynamic_cast<Framework::Scripting::NodeEngine *>(engine);
        if (!nodeEngine) {
            return;
        }

        v8::Isolate *isolate = nodeEngine->GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = nodeEngine->GetContext();
        v8::Context::Scope contextScope(context);

        v8::Local<v8::Object> global = context->Global();
        v8::Local<v8::Value> frameworkVal;
        if (global->Get(context, v8pp::to_v8(isolate, "Framework")).ToLocal(&frameworkVal) && frameworkVal->IsObject()) {
            Scripting::Builtins::Register(isolate, global, frameworkVal.As<v8::Object>());
        }
    }

    void Server::BroadcastChatMessage(const std::string &msg) {
        auto *net = GetNetworkingEngine()->GetNetworkServer();
        Framework::Networking::RPC::ChatMessage payload {msg};
        net->BroadcastRPC(payload);
    }
} // namespace HogwartsMP
