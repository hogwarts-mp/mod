#include <utils/safe_win32.h>

#include "application.h"

#include <logging/logger.h>

#include "shared/version.h"
#include <cppfs/fs.h>
#include <utils/version.h>

#include "states/initialize.h"
#include "states/menu.h"
#include "states/shutdown.h"
#include "states/session_offline_debug.h"
#include "states/session_connection.h"
#include "states/session_connected.h"
#include "states/session_disconnection.h"
#include "states/states.h"

#include "shared/modules/human_sync.hpp"
#include "shared/modules/mod.hpp"

#include "shared/rpc/chat_message.h"

#include "world/game_rpc/set_transform.h"

#include "modules/human.h"

#include "external/imgui/widgets/corner_text.h"

#include "shared/modules/mod.hpp"
#include "shared/rpc/set_weather.h"
#include "shared/version.h"

#include "../sdk/game/ulevel.h"

#include "game_layout.h"
#include <cstddef>

namespace HogwartsMP::Core {
    // Compile-time guard: the SDK struct layouts (sdk/**) and the central offset
    // table (game_layout.h) must agree. The pads are computed from the table, so
    // this verifies the pad arithmetic actually lands each member at the intended
    // offset — a bad base/member size or a reordered field fails the build here
    // instead of crashing in-game.
    static_assert(offsetof(SDK::UWorld, PersistentLevel) == Game::Offset::UWorld_PersistentLevel,
                  "UWorld::PersistentLevel does not match game_layout offset table");
    static_assert(offsetof(SDK::UWorld, OwningGameInstance) == Game::Offset::UWorld_OwningGameInstance,
                  "UWorld::OwningGameInstance does not match game_layout offset table");
    static_assert(offsetof(SDK::UGameInstance, LocalPlayers) == Game::Offset::UGameInstance_LocalPlayers,
                  "UGameInstance::LocalPlayers does not match game_layout offset table");

    Globals gGlobals;
    std::unique_ptr<Application> gApplication = nullptr;

    void Application::PostInit() {
        // Create the state machine and initialize
        _stateMachine = std::make_shared<Framework::Utils::States::Machine>();
        _stateMachine->RegisterState<States::InitializeState>();
        _stateMachine->RegisterState<States::InMenuState>();
        _stateMachine->RegisterState<States::ShutdownState>();
        _stateMachine->RegisterState<States::SessionOfflineDebugState>();
        _stateMachine->RegisterState<States::SessionConnectionState>();
        _stateMachine->RegisterState<States::SessionConnectedState>();
        _stateMachine->RegisterState<States::SessionDisconnectionState>();

        // This must always be the last call
        _stateMachine->RequestNextState(States::StateIds::Initialize);

        _commandProcessor = std::make_shared<Framework::Utils::CommandProcessor>();
        _input            = std::make_shared<HogwartsMP::Game::GameInput>();
        _console          = std::make_shared<UI::Console>(_commandProcessor);
        _chat             = std::make_shared<UI::Chat>();

        _chat->SetOnMessageSentCallback([this](const std::string &msg) {
            const auto net = gApplication->GetNetworkingEngine()->GetNetworkClient();

            HogwartsMP::Shared::RPC::ChatMessage chatMessage {};
            chatMessage.FromParameters(msg);
            net->SendRPC(chatMessage, MafiaNet::UNASSIGNED_RAKNET_GUID);
        });

        // setup debug routines
        _devFeatures.Init();

        // Register client modules (sync)
        GetWorldEngine()->GetWorld()->import <Shared::Modules::Mod>();
        GetWorldEngine()->GetWorld()->import <Shared::Modules::HumanSync>();

        // Register client modules
        GetWorldEngine()->GetWorld()->import <Modules::Human>();

        GetWorldEngine()->SetOnEntityDestroyCallback([](flecs::entity e) {
            const auto ekind = e.try_get<Shared::Modules::Mod::EntityKind>();
            switch (ekind->kind) {
                case Shared::Modules::Mod::MOD_PLAYER: Core::Modules::Human::Remove(e); break;
            }

            return true;
        });

        InitNetworkingMessages();
    }

    void Application::PreShutdown() {}

    namespace {
        struct PlayerGrab {
            // Step reached: 4=GameInstance, 5=LocalPlayer, 6=PlayerController, 7=Character.
            // Negative => access violation right after step -(N)-1 (stale offset).
            int step;
            SDK::ULocalPlayer *lp;
            SDK::ABiped_Player *biped;
        };
        // SEH-guarded walk *GWorld -> PersistentLevel -> OwningWorld ->
        // OwningGameInstance -> LocalPlayers[0] -> PlayerController -> Character.
        // Guarded so a still-stale struct offset reports its step instead of
        // crashing. Only pointer reads inside __try (no C++ objects → no unwind).
        static PlayerGrab SafeGrabLocalPlayer(SDK::UWorld **gworld) {
            PlayerGrab r{};
            __try {
                if (!gworld) return r;
                auto *world = *gworld;
                if (!world) return r;
                r.step = 1;
                auto *pl = world->PersistentLevel;
                if (!pl) return r;
                r.step = 2;
                auto *ow = pl->OwningWorld;
                if (!ow) return r;
                r.step = 3;
                auto *gi = ow->OwningGameInstance;
                if (!gi) return r;
                r.step = 4;
                auto *lp = gi->LocalPlayers.Data[0];
                if (!lp) return r;
                r.lp   = lp;
                r.step = 5;
                auto *pc = lp->PlayerController;
                if (!pc) return r;
                r.step = 6;
                auto *ch = pc->Character;
                if (!ch) return r;
                r.biped = reinterpret_cast<SDK::ABiped_Player *>(ch);
                r.step  = 7;
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                r.step = -(r.step + 1);
            }
            return r;
        }
    } // namespace

    void Application::PostUpdate() {
        if (_stateMachine) {
            _stateMachine->Update();
        }

        // If we don't have the local player yet, we try to grab it at each tick until we have it
        // This should be part of a hook "once map loaded" or "once local player created"
        if (!gGlobals.localPlayer) {
            if (!gGlobals.world) {
                return;
            }

            const auto grab = SafeGrabLocalPlayer(gGlobals.world);
            if (!grab.lp) {
                // Not ready yet (world/menu still loading). Warn once if a stale
                // offset faulted upstream of LocalPlayer, so it's visible.
                static int s_warned = 0;
                if (grab.step < 0 && s_warned < 3) {
                    Framework::Logging::GetLogger("Application")->warn("Local-player grab faulted at step {} (stale offset upstream of LocalPlayer)", grab.step);
                    ++s_warned;
                }
                return;
            }

            // Latch the local player. biped may be null if PlayerController /
            // Character offsets are still stale (grab.step < 7) — surfaced in the
            // log below for follow-up, but we stop retrying (no per-tick faults).
            gGlobals.localPlayer      = grab.lp;
            gGlobals.localBipedPlayer = grab.biped;
            Framework::Logging::GetLogger("Application")->info("Found local player {} (biped {}, grab reached step {}/7)", fmt::ptr(grab.lp), fmt::ptr(grab.biped), grab.step);
        }

        // Tick discord instance - Temporary
        const auto discordApi = Core::gApplication->GetPresence();
        if (discordApi && discordApi->IsInitialized()) {
            discordApi->SetPresence("Broomstick", "Flying around", discord::ActivityType::Playing);
        }

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
            DrawCornerText(CORNER_LEFT_BOTTOM, fmt::format("Connection: {}", connStateNames[static_cast<size_t>(connState)]));
            DrawCornerText(CORNER_LEFT_BOTTOM, fmt::format("Ping: {}", ping));
        });
#endif

        if (_input) {
            _input->Update();
        }
    }

    void Application::PostRender() {}

    void Application::InitNetworkingMessages() {
        SetOnConnectionFinalizedCallback([this](flecs::entity newPlayer, float tickInterval) {
            _tickInterval = tickInterval;
            _localPlayer = newPlayer;
            _stateMachine->RequestNextState(States::StateIds::SessionConnected);
            Core::Modules::Human::SetupLocalPlayer(this, newPlayer);

            Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->info("Connection established!");
        });

        SetOnConnectionClosedCallback([this]() {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->info("Connection lost!");
            _stateMachine->RequestNextState(States::StateIds::SessionDisconnection);
        });

        InitRPCs();

        Modules::Human::SetupMessages(this);

        Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->info("Networking messages registered!");
    }

    uint64_t Application::GetLocalPlayerID() {
        if (!_localPlayer)
            return 0;

        const auto sid = _localPlayer.try_get<Framework::World::Modules::Base::ServerID>();
        return sid->id;
    }

    void Application::InitRPCs() {
        const auto net = GetNetworkingEngine()->GetNetworkClient();

        net->RegisterRPC<Shared::RPC::ChatMessage>([this](MafiaNet::RakNetGUID guid, Shared::RPC::ChatMessage *chatMessage) {
            if (!chatMessage->Valid())
                return;
            _chat->AddMessage(chatMessage->GetText());

            Framework::Logging::GetLogger("chat")->trace(chatMessage->GetText());
        });
        net->RegisterGameRPC<Framework::World::RPC::SetTransform>([this](MafiaNet::RakNetGUID guid, Framework::World::RPC::SetTransform *msg) {
            if (!msg->Valid()) {
                return;
            }
            const auto e = GetWorldEngine()->GetEntityByServerID(msg->GetServerID());
            if (!e.is_alive()) {
                return;
            }
            const auto ekind = e.try_get<Shared::Modules::Mod::EntityKind>();
            switch (ekind->kind) {
                case Shared::Modules::Mod::MOD_PLAYER: Core::Modules::Human::UpdateTransform(e); break;
            }
        });

        net->RegisterRPC<Shared::RPC::SetWeather>([this](MafiaNet::RakNetGUID guid, Shared::RPC::SetWeather *msg) {
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Sync Weather!");
        });
    }

    void Application::ProcessLockControls(bool lock) {
        //Game::Helpers::Controls::Lock(lock);

        GetImGUI()->ShowCursor(lock);
    }

    void Application::LockControls(bool lock) {
        if (lock) _controlsLocked++;
        else _controlsLocked = std::max(--_controlsLocked, 0);

        if (_controlsLocked) {

            // Enable cursor
            ProcessLockControls(true);
        }
        else {

            // Disable cursor
            ProcessLockControls(false);
            _lockControlsBypassed = false;
        }
    }

    void Application::ToggleLockControlsBypass() {
        if (!AreControlsLocked()) {
            Framework::Logging::GetLogger("Application")->error("[ToggleLockControlsBypass] Controls are not locked.");
            return;
        }

        ProcessLockControls(_lockControlsBypassed);
        _lockControlsBypassed = !_lockControlsBypassed;
    }
}
