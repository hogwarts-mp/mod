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

#include "modules/human.h"

#include "builtins/game.h"
#include <scripting/engine.h>

#include "external/imgui/widgets/corner_text.h"

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

        // Outgoing chat goes through the framework's built-in chat (server resolves the sender).
        _chat->SetOnMessageSentCallback([this](const std::string &msg) {
            SendChatMessage(msg);
        });

        // setup debug routines
        _devFeatures.Init();

        // Register the client-side Human network type (reconstructs server HumanEntity into ClientHuman).
        Core::Modules::Human::Register();

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

    SDK::ABiped_Player *GetLiveLocalBiped() {
        // Fresh each call: SafeGrabLocalPlayer walks *GWorld -> ... -> Character under SEH, so it
        // returns the current pawn (or nullptr) and never a stale/destroyed one. Callers that may run
        // after teardown (client scripts) use this instead of the never-cleared gGlobals latch.
        return SafeGrabLocalPlayer(gGlobals.world).biped;
    }

    void Application::PostUpdate() {
        if (_stateMachine) {
            _stateMachine->Update();
        }

        // Drive replicated humans: push the local player's transform upstream and interpolate remote
        // proxies. No-op until replication is active.
        Core::Modules::Human::UpdateAll(_tickInterval);

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
        // The local player's avatar arrives through replication; binding the local pawn happens in
        // ClientHuman::OnConstructed when we own the entity, so there's nothing entity-specific here.
        InitRPCs();

        Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->info("Networking messages registered!");
    }

    void Application::OnConnectionFinalized(float serverTickRate) {
        _tickInterval = serverTickRate;
        _stateMachine->RequestNextState(States::StateIds::SessionConnected);

        Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->info("Connection established!");
    }

    void Application::OnConnectionClosed() {
        Framework::Logging::GetLogger(FRAMEWORK_INNER_NETWORKING)->info("Connection lost!");
        _stateMachine->RequestNextState(States::StateIds::SessionDisconnection);
    }

    // Chat lines from the server are pushed to the chat UI.
    void Application::OnChatMessageReceived(const std::string &text) {
        _chat->AddMessage(text);
        Framework::Logging::GetLogger("chat")->trace(text);
    }

    // Register HogwartsMP client builtins onto the connection's V8 engine. The framework invokes this
    // from InitFrameworkSDK with no V8 scopes active, so we enter them ourselves (mirroring
    // ClientScriptingModule::RegisterFrameworkBindings) before touching the context.
    void Application::ModuleRegister(Framework::Scripting::Engine *engine) {
        if (!engine) {
            return;
        }
        v8::Isolate *isolate = engine->GetIsolate();
        if (!isolate) {
            return;
        }
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = engine->GetContext();
        if (context.IsEmpty()) {
            return;
        }
        v8::Context::Scope contextScope(context);

        Scripting::ClientGame::Register(isolate, context->Global());
        Framework::Logging::GetLogger("Scripting")->debug("Registered HogwartsMP client builtins");
    }

    uint64_t Application::GetLocalPlayerID() {
        auto *local = Core::Modules::Human::GetLocal();
        return local ? local->GetNetworkID() : 0;
    }

    void Application::InitRPCs() {
        const auto net = GetNetworkingEngine()->GetNetworkClient();

        net->RegisterRPC<Shared::RPC::SetWeather>([this](const Shared::RPC::SetWeather &msg, MafiaNet::Packet *) {
            // TODO: apply the environment state to the game (season/time/weather).
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Sync Weather! ({}, season {})", msg.data.weather, msg.data.season);
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
