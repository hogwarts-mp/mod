#include "server.h"

#include "shared/modules/human_sync.hpp"
#include "shared/modules/mod.hpp"

#include "modules/human.h"

#include "shared/chat_command.h"
#include "shared/rpc/chat_message.h"

#include "builtins/builtins.h"
#include "builtins/events.h"

#include <fmt/format.h>

#include <scripting/node_engine.h>
#include <v8pp/convert.hpp>

namespace HogwartsMP {
    void Server::PostInit() {
        _serverRef = this;
        InitNetworkingMessages();

        // Setup ECS modules (sync)
        GetWorldEngine()->GetWorld()->import<Shared::Modules::Mod>();
        GetWorldEngine()->GetWorld()->import<Shared::Modules::HumanSync>();

        // Setup ECS modules
        GetWorldEngine()->GetWorld()->import <Core::Modules::Human>();

        // Setup specific components - default values
        auto weather = GetWorldEngine()->GetWorld()->ensure<Shared::Modules::Mod::Weather>();
        weather.season = Shared::Modules::Mod::SeasonKind::SEASON_SUMMER;
        weather.weather = "Clear";
        weather.timeHour = 11;
        weather.timeMinute = 0;
        weather.dateDay = 12;
        weather.dateMonth = 6;
    }

    void Server::PostUpdate() {}

    void Server::PreShutdown() {}

    void Server::InitNetworkingMessages() {
        const auto net = GetNetworkingEngine()->GetNetworkServer();

        SetOnPlayerConnectCallback([this, net](flecs::entity player, uint64_t guid) {
            (void)(guid);
            // Create the networked human entity
            Core::Modules::Human::Create(net, player);

            // The framework's default streamer range is 100 units — ~1 m in
            // Hogwarts' cm scale, so nothing more than a step away ever streams.
            // Use a game-sized range (500 m) so other players/NPCs are visible.
            auto &streamer = player.ensure<Framework::World::Modules::Base::Streamer>();
            streamer.range = 50000.f;

            // Broadcast chat message
            const auto msg = fmt::format("Player {} has joined the session!", streamer.nickname);
            BroadcastChatMessage(msg);

            Scripting::Human::EventPlayerConnected(player);
        });

        SetOnPlayerDisconnectCallback([this](flecs::entity player, uint64_t) {
            const auto st  = player.try_get<Framework::World::Modules::Base::Streamer>();
            const auto msg = fmt::format("Player {} has left the session!", st->nickname);
            BroadcastChatMessage(msg);

            Scripting::Human::EventPlayerDisconnected(player);
        });

        InitRPCs();

        Core::Modules::Human::SetupMessages(this->GetWorldEngine(), net);

        // TODO: Register custom networking messages

        Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->info("Networking messages registered!");
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
        FW_SEND_COMPONENT_RPC(Shared::RPC::ChatMessage, msg);
    }
    void Server::InitRPCs() {
        const auto net = GetNetworkingEngine()->GetNetworkServer();
        net->RegisterRPC<Shared::RPC::ChatMessage>([this](MafiaNet::RakNetGUID guid, Shared::RPC::ChatMessage *chatMessage) {
            if (!chatMessage->Valid())
                return;

            const auto ent = GetWorldEngine()->GetEntityByGUID(guid.g);
            if (!ent.is_alive())
                return;

            const auto text   = chatMessage->GetText();
            const auto parsed = Shared::ParseChatCommand(text);
            if (parsed.isCommand) {
                Scripting::World::EventChatCommand(ent, text, parsed.command, parsed.args);
            }
            else if (Scripting::GetServerEventListenerCount("chatMessage") > 0) {
                Scripting::World::EventChatMessage(ent, text);
            }
            else {
                // No JS gamemode is handling chat - broadcast directly so chat still works
                const auto st = ent.try_get<Framework::World::Modules::Base::Streamer>();
                BroadcastChatMessage(fmt::format("{}: {}", st ? st->nickname : "Player", text));
            }
        });
    }
} // namespace HogwartsMP
