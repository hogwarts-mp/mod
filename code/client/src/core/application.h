#pragma once

#include <memory>

#include <utils/safe_win32.h>

#include <integrations/client/instance.h>
#include <graphics/backend/d3d12.h>
#include "dev_features.h"
#include <utils/states/machine.h>
#include <utils/command_processor.h>

#include "ui/chat.h"
#include "ui/hud.h"
#include "ui/creator.h"

#include "game/game_input.h"

#include "UObject/Class.h"
#include "UObject/EnumProperty.h"
#include "UObject/UObjectArray.h"
#include "UObject/UnrealType.h"

#include "sdk/offsets/entities/ulocalplayer.h"
#include "sdk/offsets/entities/a_biped_player.h"
#include "sdk/offsets/game/ugameinstance.h"
#include "sdk/offsets/game/uworld.h"

namespace HogwartsMP::Core {
    class Application : public Framework::Integrations::Client::Instance {
      private:
        friend class DevFeatures;
        void InitNetworkingMessages();
        void InitRPCs();
        float _tickInterval = 0.01667f;

        std::shared_ptr<Framework::Utils::States::Machine> _stateMachine;
        std::shared_ptr<HogwartsMP::Game::GameInput> _input;
        std::shared_ptr<UI::Chat> _chat;
        std::shared_ptr<UI::Hud> _hud;
        std::shared_ptr<UI::Creator> _creator;
        std::shared_ptr<Framework::Utils::CommandProcessor> _commandProcessor;
        DevFeatures _devFeatures;

        int _controlsLocked = 0;

        int _lockControlsCounter   = 0;
        bool _lockControlsBypassed = false;

      private:
        void ProcessLockControls(bool lock);

      public:
        void PostInit() override;
        void PreShutdown() override;
        void PostUpdate() override;
        void PostRender() override;

        // Register HogwartsMP client-side scripting builtins onto the connection's V8 engine.
        void ModuleRegister(Framework::Scripting::Engine *engine) override;

        // Networking event hooks (framework dispatches these; no Set*Callback setters anymore).
        void OnConnectionFinalized(float serverTickRate) override;
        void OnConnectionClosed() override;
        void OnChatMessageReceived(const std::string &text) override;

        float GetTickInterval() const {
            return _tickInterval;
        }

        std::shared_ptr<Framework::Utils::States::Machine> GetStateMachine() const {
            return _stateMachine;
        }

        std::shared_ptr<Framework::Utils::CommandProcessor> GetCommandProcessor() const {
            return _commandProcessor;
        }

        std::shared_ptr<HogwartsMP::Game::GameInput> GetInput() const {
            return _input;
        }

        DevFeatures &GetDevFeatures() {
            return _devFeatures;
        }

        std::shared_ptr<UI::Chat> GetChat() const {
            return _chat;
        }

        std::shared_ptr<UI::Hud> GetHud() const {
            return _hud;
        }

        std::shared_ptr<UI::Creator> GetCreator() const {
            return _creator;
        }

        void LockControls(bool lock);
        bool AreControlsLocked() const {
            return _controlsLocked>0;
        }
        void ToggleLockControlsBypass();
        bool AreControlsLockedBypassed() const {
            return _lockControlsBypassed;
        }

        uint64_t GetLocalPlayerID();
    };

    struct Globals {
        Application *application                                                  = nullptr;
        HWND window                                                          = nullptr;
        ID3D12Device *device                                                      = nullptr;
        FUObjectArray *objectArray                                                = {nullptr};
        SDK::ULocalPlayer *localPlayer                                            = nullptr;
        SDK::ABiped_Player *localBipedPlayer                                      = nullptr;
        SDK::UWorld **world                                                             = nullptr;
    };

    extern Globals gGlobals;
    extern std::unique_ptr<Application> gApplication;

    // Re-derive the local player's pawn from the live world walk (SEH-guarded), returning nullptr when
    // there is no current pawn (menu/loading, or after teardown). Prefer this over the latched
    // gGlobals.localBipedPlayer in code that may run after the pawn is destroyed (e.g. client scripts):
    // that global is write-once and never cleared on teardown, so it dangles after fast-travel/disconnect.
    SDK::ABiped_Player *GetLiveLocalBiped();
}
