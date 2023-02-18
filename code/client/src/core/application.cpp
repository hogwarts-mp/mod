#include "application.h"

#include <logging/logger.h>

#include "shared/version.h"
#include <cppfs/fs.h>
#include <utils/version.h>

#include "states/initialize.h"
#include "states/menu.h"
#include "states/shutdown.h"
#include "states/states.h"

#include "shared/modules/human_sync.hpp"
#include "shared/modules/mod.hpp"

#include "shared/rpc/chat_message.h"
#include "world/game_rpc/set_transform.h"

#include "modules/human.h"

#include "external/imgui/widgets/corner_text.h"

#include "shared/version.h"


namespace HogwartsMP::Core {
    Globals gGlobals;
    std::unique_ptr<Application> gApplication = nullptr;

    bool Application::PostInit() {
        // Create the state machine and initialize
        _stateMachine = std::make_shared<Framework::Utils::States::Machine>();
        _stateMachine->RegisterState<States::InitializeState>();
        _stateMachine->RegisterState<States::InMenuState>();
        _stateMachine->RegisterState<States::ShutdownState>();

        // This must always be the last call
        _stateMachine->RequestNextState(States::StateIds::Initialize);

        _commandProcessor = std::make_shared<Framework::Utils::CommandProcessor>();
        _input            = std::make_shared<HogwartsMP::Game::GameInput>();
        _console          = std::make_shared<UI::HogwartsConsole>(_commandProcessor, _input);
        _chat             = std::make_shared<UI::Chat>();

        _chat->SetOnMessageSentCallback([this](const std::string &msg) {
            const auto net = gApplication->GetNetworkingEngine()->GetNetworkClient();

            HogwartsMP::Shared::RPC::ChatMessage chatMessage {};
            chatMessage.FromParameters(msg);
            net->SendRPC(chatMessage, SLNet::UNASSIGNED_RAKNET_GUID);
        });

        // setup debug routines
        _devFeatures.Init();

        // Register client modules (sync)
        GetWorldEngine()->GetWorld()->import <Shared::Modules::Mod>();
        GetWorldEngine()->GetWorld()->import <Shared::Modules::HumanSync>();

        // Register client modules
        GetWorldEngine()->GetWorld()->import <Modules::Human>();

        GetWorldEngine()->SetOnEntityDestroyCallback([](flecs::entity e) {
            const auto ekind = e.get<Shared::Modules::Mod::EntityKind>();
            switch (ekind->kind) {
                case Shared::Modules::Mod::MOD_PLAYER: Core::Modules::Human::Remove(e); break;
            }

            return true;
        });

        InitNetworkingMessages();

        return true;
    }

    bool Application::PreShutdown() {
        return true;
    }

    void Application::PostUpdate() {
        if (_stateMachine) {
            _stateMachine->Update();
        }

        // Tick discord instance - Temporary
        const auto discordApi = Core::gApplication->GetPresence();
        if (discordApi && discordApi->IsInitialized()) {
            discordApi->SetPresence("Broomstick", "Flying around", discord::ActivityType::Playing);
        }

        // todo move to state later
        gApplication->GetImGUI()->PushWidget([]() {
            if (!gApplication->GetDevConsole()->IsOpen()){
                gApplication->GetChat()->Update();
            }
        });

        #if 1
        Core::gApplication->GetImGUI()->PushWidget([&]() {
            using namespace Framework::External::ImGUI::Widgets;
            const auto networkClient = Core::gApplication->GetNetworkingEngine()->GetNetworkClient();
            const auto connState     = networkClient->GetConnectionState();
            const auto ping          = networkClient->GetPing();

            _console->Update();
            _devFeatures.Update();

            if (_input->IsKeyPressed(FW_KEY_F8)) {
                _console->Toggle();
            }

            const char *connStateNames[] = {"Connecting", "Online", "Offline"};

            // versioning
            DrawCornerText(CORNER_RIGHT_TOP, "Hogwarts Legacy Multiplayer");
            DrawCornerText(CORNER_RIGHT_TOP, fmt::format("Framework version: {} ({})", Framework::Utils::Version::rel, Framework::Utils::Version::git));
            DrawCornerText(CORNER_RIGHT_TOP, fmt::format("HogwartsMP version: {} ({})", HogwartsMP::Version::rel, HogwartsMP::Version::git));

            // connection details
            DrawCornerText(CORNER_LEFT_BOTTOM, fmt::format("Connection: {}", connStateNames[connState]));
            DrawCornerText(CORNER_LEFT_BOTTOM, fmt::format("Ping: {}", ping));
        });
#endif

        if (_input) {
            _input->Update();
        }
    }

    void Application::InitNetworkingMessages() {
        SetOnConnectionFinalizedCallback([this](flecs::entity newPlayer, float tickInterval) {
            _tickInterval = tickInterval;
            _localPlayer = newPlayer;
            Core::Modules::Human::SetupLocalPlayer(this, newPlayer);

            Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->info("Connection established!");
        });

        SetOnConnectionClosedCallback([this]() {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->info("Connection lost!");
        });

        InitRPCs();

        Modules::Human::SetupMessages(this);

        Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->info("Networking messages registered!");
    }

    uint64_t Application::GetLocalPlayerID() {
        if (!_localPlayer)
            return 0;

        const auto sid = _localPlayer.get<Framework::World::Modules::Base::ServerID>();
        return sid->id;
    }

    void Application::InitRPCs() {
        const auto net = GetNetworkingEngine()->GetNetworkClient();

        net->RegisterRPC<Shared::RPC::ChatMessage>([this](SLNet::RakNetGUID guid, Shared::RPC::ChatMessage *chatMessage) {
            if (!chatMessage->Valid())
                return;
            _chat->AddMessage(chatMessage->GetText());

            Framework::Logging::GetLogger("chat")->trace(chatMessage->GetText());
        });
        net->RegisterGameRPC<Framework::World::RPC::SetTransform>([this](SLNet::RakNetGUID guid, Framework::World::RPC::SetTransform *msg) {
            if (!msg->Valid()) {
                return;
            }
            const auto e = GetWorldEngine()->GetEntityByServerID(msg->GetServerID());
            if (!e.is_alive()) {
                return;
            }
            const auto ekind = e.get<Shared::Modules::Mod::EntityKind>();
            switch (ekind->kind) {
                case Shared::Modules::Mod::MOD_PLAYER: Core::Modules::Human::UpdateTransform(e); break;
            }
        });
    }

    void Application::LockControls(bool lock) {
        if (lock) {
            // Lock game controls
            // Game::Helpers::Controls::Lock(true);

            // Enable cursor
            GetImGUI()->ShowCursor(true);
        }
        else {
            // Unlock game controls
            // Game::Helpers::Controls::Lock(false);

            // Disable cursor
            GetImGUI()->ShowCursor(false);
        }

        _controlsLocked = lock;
    }
}
