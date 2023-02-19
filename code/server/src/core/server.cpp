#include "server.h"

#include "shared/modules/human_sync.hpp"

#include "modules/human.h"

#include "shared/rpc/chat_message.h"

#include <fmt/format.h>

#include "builtins/builtins.h"

namespace HogwartsMP {
    void Server::PostInit() {
        _serverRef = this;
        InitNetworkingMessages();

        // Setup ECS modules (sync)
        GetWorldEngine()->GetWorld()->import<Shared::Modules::HumanSync>();

        // Setup ECS modules
        GetWorldEngine()->GetWorld()->import <Core::Modules::Human>();
    }

    void Server::PostUpdate() {}

    void Server::PreShutdown() {}

    void Server::InitNetworkingMessages() {
        const auto net = GetNetworkingEngine()->GetNetworkServer();

        SetOnPlayerConnectCallback([this, net](flecs::entity player, uint64_t guid) {
            (void)(guid);
            // Create the networked human entity
            Core::Modules::Human::Create(net, player);

            // Broadcast chat message
            const auto st  = player.get<Framework::World::Modules::Base::Streamer>();
            const auto msg = fmt::format("Player {} has joined the session!", st->nickname);
            BroadcastChatMessage(msg);

            Scripting::Human::EventPlayerConnected(player);
        });

        SetOnPlayerDisconnectCallback([this](flecs::entity player, uint64_t) {
            const auto st  = player.get<Framework::World::Modules::Base::Streamer>();
            const auto msg = fmt::format("Player {} has left the session!", st->nickname);
            BroadcastChatMessage(msg);

            Scripting::Human::EventPlayerDisconnected(player);
        });

        InitRPCs();

        Core::Modules::Human::SetupMessages(this->GetWorldEngine(), net);

        // TODO: Register custom networking messages

        Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->info("Networking messages registered!");
    }

    void Server::ModuleRegister(Framework::Scripting::Engines::SDKRegisterWrapper sdk) {
        if (sdk.GetKind() != Framework::Scripting::ENGINE_NODE)
            return;

        const auto nodeSDK = sdk.GetNodeSDK();
        Scripting::Builtins::Register(nodeSDK->GetIsolate(), nodeSDK->GetModule());
    }

    void Server::BroadcastChatMessage(const std::string &msg) {
        FW_SEND_COMPONENT_RPC(Shared::RPC::ChatMessage, msg);
    }
    void Server::InitRPCs() {
        const auto net = GetNetworkingEngine()->GetNetworkServer();
        net->RegisterRPC<Shared::RPC::ChatMessage>([this](SLNet::RakNetGUID guid, Shared::RPC::ChatMessage *chatMessage) {
            if (!chatMessage->Valid())
                return;

            const auto ent = GetWorldEngine()->GetEntityByGUID(guid.g);
            if (!ent.is_alive())
                return;

            const auto text = chatMessage->GetText();
            if (text[0] == '/') {
                const auto command  = text.substr(1, text.find(' ') - 1);
                const auto argsPart = text.substr(text.find(' ') + 1);
                std::vector<std::string> args;
                std::string arg;
                std::istringstream iss(argsPart);
                while (iss >> arg) {
                    args.push_back(arg);
                }
                Scripting::World::OnChatCommand(ent, text, command, args);
            } else {
                Scripting::World::OnChatMessage(ent, text);
            }
        });
    }
} // namespace HogwartsMP
