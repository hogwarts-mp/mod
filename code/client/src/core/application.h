#pragma once

#include <memory>

#include <utils/safe_win32.h>

#include <integrations/client/instance.h>
#include <graphics/backend/d3d12.h>
#include "dev_features.h"
#include <utils/states/machine.h>

#include "ui/chat.h"
#include "ui/console.h"

#include "game/game_input.h"

#include "UObject/Class.h"
#include "UObject/EnumProperty.h"
#include "UObject/UObjectArray.h"
#include "UObject/UnrealType.h"

#include "sdk/entities/ulocalplayer.h"
#include "sdk/game/ugameinstance.h"
#include "sdk/game/uworld.h"

namespace HogwartsMP::Core {
    class Application : public Framework::Integrations::Client::Instance {
      private:
        friend class DevFeatures;
        void InitNetworkingMessages();
        void InitRPCs();
        flecs::entity _localPlayer;
        float _tickInterval = 0.01667f;

        std::shared_ptr<Framework::Utils::States::Machine> _stateMachine;
        std::shared_ptr<HogwartsMP::Game::GameInput> _input;
        std::shared_ptr<UI::HogwartsConsole> _console;
        std::shared_ptr<UI::Chat> _chat;
        std::shared_ptr<Framework::Utils::CommandProcessor> _commandProcessor;
        DevFeatures _devFeatures;

        bool _controlsLocked = false;

      public:
        bool PostInit() override;
        bool PreShutdown() override;
        void PostUpdate() override;

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

        std::shared_ptr<UI::HogwartsConsole> GetDevConsole() const {
            return _console;
        }

        std::shared_ptr<UI::Chat> GetChat() const {
            return _chat;
        }

        void LockControls(bool lock);
        bool AreControlsLocked() const {
            return _controlsLocked;
        }

        uint64_t GetLocalPlayerID();
    };

    struct Globals {
        Application *application                                                  = nullptr;
        HWND window                                                          = nullptr;
        ID3D12Device *device                                                      = nullptr;
        FUObjectArray *objectArray                                                = {nullptr};
        SDK::ULocalPlayer *localPlayer                                            = nullptr;
        SDK::UWorld **world                                                             = nullptr;
    };

    extern Globals gGlobals;
    extern std::unique_ptr<Application> gApplication;
}
