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

#include "substrate_loader.h"

#include "builtins/game.h"
#include <core_modules.h>
#include <networking/replication/replication_manager.h>
#include <scripting/engine.h>

#include "shared/rpc/set_appearance.h"
#include "shared/rpc/set_weather.h"

#include "sdk/offsets/game/seasonchanger.h"
#include "sdk/reflection/ue4_reflection.h"
#include "shared/version.h"

#include "../sdk/offsets/game/ulevel.h"

#include "game_layout.h"
#include <cstddef>
#include <filesystem>

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
        _chat             = std::make_shared<UI::Chat>();
        _hud              = std::make_shared<UI::Hud>();
        _creator          = std::make_shared<UI::Creator>();

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

    namespace {
        // Directory of HogwartsMPClient.dll (not the game exe) — CEF cache/logs
        // and cef_subprocess.exe live next to our binaries, not in the game dir.
        static std::string GetClientModuleDir() {
            static const int s_anchor = 0;
            HMODULE selfModule        = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(&s_anchor), &selfModule);
            wchar_t path[MAX_PATH] = {};
            GetModuleFileNameW(selfModule, path, MAX_PATH);
            return std::filesystem::path(path).parent_path().string();
        }

        // Defined further down (next to the env state it drives); forward-declared so PostUpdate
        // can call it.
        void ApplyEnvIfReady();
    } // namespace

    void Application::PostUpdate() {
        // Bring up CEF once the renderer is live (game thread: CefInitialize binds
        // its pump to the calling thread). One attempt only — can't re-init CEF.
        const auto webManager = GetWebManager();
        if (webManager && !webManager->IsInitialized() && GetRenderer() && GetRenderer()->IsInitialized() && gGlobals.window) {
            static bool s_webInitAttempted = false;
            if (!s_webInitAttempted) {
                s_webInitAttempted = true;

                // Diagnostic kill-switch: drop a "disable_web" file next to the
                // client DLL to skip CEF entirely.
                if (std::filesystem::exists(std::filesystem::path(GetClientModuleDir()) / "disable_web")) {
                    Framework::Logging::GetLogger("Web")->info("disable_web flag present, skipping web manager init");
                }
                else {
                    RECT rc {};
                    GetClientRect(gGlobals.window, &rc);
                    Framework::GUI::ViewportConfiguration viewport {static_cast<int>(rc.right - rc.left), static_cast<int>(rc.bottom - rc.top)};
                    if (!webManager->Init(GetClientModuleDir(), viewport, GetRenderer())) {
                        Framework::Logging::GetLogger("Web")->error("Web manager initialization failed, web UI disabled");
                    }
                    else {
                        Framework::Logging::GetLogger("Web")->info("Web manager initialized ({}x{})", viewport.width, viewport.height);
                    }
                }
            }
        }

        // Blit web views through the ImGui frame. Pushed before the local-player
        // early-returns below so web UI also renders in the main menu.
        if (webManager && webManager->IsInitialized()) {
            GetImGUI()->PushWidget([webManager]() {
                webManager->SubmitImGuiDraws();
            });
        }

        if (_stateMachine) {
            _stateMachine->Update();
        }

        // Drive replicated humans: push the local player's transform upstream and interpolate remote
        // proxies. No-op until replication is active.
        Core::Modules::Human::UpdateAll(_tickInterval);

        // Apply any pending server env (time/season/weather) once the world is live — covers the
        // on-join push arriving before the pawn/Scheduler exist. No-op when nothing is pending.
        ApplyEnvIfReady();

        // HUD on the game thread (NOT an ImGui widget), above the local-player
        // early-returns so it also works in the menu.
        if (_hud) {
            _hud->Update();
        }
        if (_creator) {
            _creator->Update();
        }

        // Edge-detect hotkeys before _input->Update() clears the edges (CEF's
        // mid-tick pump would otherwise clear them first).
        if (_input) {
            if (_hud && _input->IsKeyPressed(FW_KEY_F8)) {
                _hud->ToggleDevMenu();
            }
            if (_creator && _input->IsKeyPressed(FW_KEY_F5)) {
                _creator->Toggle();
            }
            _input->Update();
        }

        // Boot straight into the world. Unconditional: the menu is itself a world with a local
        // player, so this must NOT sit inside the !world/!localPlayer guards below.
        SubstrateLoader::TryAutoLoad();

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

    namespace {
        // Wire SeasonKind {Spring=0,Summer=1,Autumn=2,Winter=3} -> the game's ESeasonEnum
        // {Invalid=0,Fall=1,Winter=2,Spring=3,Summer=4}. Different ordering — map, don't cast.
        SDK::ESeasonEnum MapSeason(uint8_t kind) {
            switch (kind) {
            case Shared::SEASON_SPRING: return SDK::ESeasonEnum::Season_Spring;
            case Shared::SEASON_SUMMER: return SDK::ESeasonEnum::Season_Summer;
            case Shared::SEASON_AUTUMN: return SDK::ESeasonEnum::Season_Fall;
            case Shared::SEASON_WINTER: return SDK::ESeasonEnum::Season_Winter;
            default: return SDK::ESeasonEnum::Season_Summer;
            }
        }

        // Cache the server env and apply once the world is live (ApplyEnvIfReady) — the SetWeather RPC
        // (esp. the on-join push) can arrive before the pawn/Scheduler exist. g_envApplied latches until
        // the next SetWeather; fine since env changes re-broadcast (reset on world teardown if levels transition).
        Shared::WeatherState g_env;
        bool g_hasEnv     = false;
        bool g_envApplied = false;

        // Apply the cached env once the pawn AND a live Scheduler exist, so time/season/weather
        // land together. Cheap + no-op when nothing is pending (the common case).
        void ApplyEnvIfReady() {
            if (!g_hasEnv || g_envApplied) {
                return;
            }
            auto *pawn = GetLiveLocalBiped();
            if (!pawn || HogwartsMP::Core::UE4::FindInstancesOfClass("Class /Script/GameScheduler.Scheduler").empty()) {
                return; // not in-world / world managers not up yet
            }
            SDK::SetCurrentTime(g_env.timeHour, g_env.timeMinute, 0);
            SDK::SetSeason(MapSeason(g_env.season));
            SDK::SetOverrideWeather(pawn, g_env.weather);
            g_envApplied = true;
        }
    } // namespace

    void Application::InitRPCs() {
        const auto net = GetNetworkingEngine()->GetNetworkClient();

        // Server-authoritative environment. Cache it and apply on the next frame we're in-world
        // (ApplyEnvIfReady, driven from Update) — the apply drives the game via reflection and
        // needs live game objects that may not exist yet when this arrives (the on-join push).
        net->RegisterRPC<Shared::RPC::SetWeather>([this](const Shared::RPC::SetWeather &msg, MafiaNet::Packet *) {
            const auto &env = msg.data;
            Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("Env sync -> {:02}:{:02}, season {}, weather '{}'", env.timeHour, env.timeMinute, env.season, env.weather);

            g_env        = env;
            g_hasEnv     = true;
            g_envApplied = false; // re-apply this (new) state once in-world
        });

        // Live appearance change: store it on the replica and (re)dress the proxy.
        net->RegisterRPC<Shared::RPC::AppearanceUpdate>([](const Shared::RPC::AppearanceUpdate &msg, MafiaNet::Packet *) {
            auto *repl  = Framework::CoreModules::GetReplication();
            auto *human = repl ? repl->GetEntity<Core::Modules::ClientHuman>(msg.networkId) : nullptr;
            if (!human) {
                return;
            }
            human->ccd = msg.ccd;
            human->ApplyAppearance();
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
