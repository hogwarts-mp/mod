#include "server.h"

namespace HogwartsMP {

    void Server::PostInit() {
        _serverRef = this;
        InitNetworkingMessages();
    }

    void Server::PostUpdate() {}

    void Server::PreShutdown() {}

    void Server::InitNetworkingMessages() {
        const auto net = GetNetworkingEngine()->GetNetworkServer();

        SetOnPlayerConnectCallback([this, net](flecs::entity player, uint64_t guid) {
            // TODO: implement
        });
             
        SetOnPlayerDisconnectCallback([this](flecs::entity player, uint64_t) {
            // TODO: implement
        });

        InitRPCs();

        // TODO: Register custom networking messages

        Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->info("Networking messages registered!");
    }

    void Server::ModuleRegister(Framework::Scripting::Engines::SDKRegisterWrapper sdk) {
        if (sdk.GetKind() != Framework::Scripting::ENGINE_NODE)
            return;

        const auto nodeSDK = sdk.GetNodeSDK();
        // TODO: register scripting builtins layer
    }

    void Server::BroadcastChatMessage(flecs::entity ent, const std::string &msg) {
    }
    void Server::InitRPCs() {
    }
} // namespace HogwartsMP
