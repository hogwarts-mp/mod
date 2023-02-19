#include "menu.h"
#include "states.h"
#include <utils/safe_win32.h>

#include <utils/states/machine.h>

#include <imgui/imgui.h>

#include "../application.h"

namespace HogwartsMP::Core::States {
    InMenuState::InMenuState() {}

    InMenuState::~InMenuState() {}

    int32_t InMenuState::GetId() const {
        return StateIds::Menu;
    }

    const char *InMenuState::GetName() const {
        return "InMenu";
    }

    bool InMenuState::OnEnter(Framework::Utils::States::Machine *) {
        _shouldDisplayWidget       = true;
        _shouldProceedConnection   = false;
        _shouldProceedOfflineDebug = false;

        // Set camera
        // Game::Helpers::Camera::SetPos({450.43698, -646.01941, 58.132675}, {-399.2962, -594.75391, 37.324718}, true);

        // Lock game controls
        // Game::Helpers::Controls::Lock(true);

        // Enable cursor
        gApplication->LockControls(true);
        return true;
    }

    bool InMenuState::OnExit(Framework::Utils::States::Machine *) {
        // Temp
        // Game::Helpers::Camera::ResetBehindPlayer();

        // Hide cursor
        gApplication->LockControls(false);
        return true;
    }

    bool InMenuState::OnUpdate(Framework::Utils::States::Machine *machine) {
        gApplication->GetImGUI()->PushWidget([this]() {
            if (!ImGui::Begin("Debug", &_shouldDisplayWidget, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::End();
                return;
            }

            bool isDiscordPresent = gApplication->GetPresence()->IsInitialized();

            ImGui::Text("Enter connection details:");
            static char serverIp[32] = "127.0.0.1";
            static char nickname[32] = "Player";
            ImGui::Text("Server IP: ");
            ImGui::SameLine();
            ImGui::InputText("##server_ip", serverIp, 32);

            if (!isDiscordPresent) {
                ImGui::Text("Nickname: ");
                ImGui::SameLine();
                ImGui::InputText("##nickname", nickname, 32);
            }
            else {
                discord::User currUser {};
                gApplication->GetPresence()->GetUserManager().GetCurrentUser(&currUser);
                strcpy(nickname, currUser.GetUsername());
                ImGui::Text("Nickname: %s (set via Discord)", nickname);
            }

            if (ImGui::Button("Connect")) {
                // Update the application state for further usage
                Framework::Integrations::Client::CurrentState newApplicationState = HogwartsMP::Core::gApplication->GetCurrentState();
                newApplicationState._host                                         = serverIp;
                newApplicationState._port                                         = 27015; // TODO: fix this
                newApplicationState._nickname                                     = nickname;
                HogwartsMP::Core::gApplication->SetCurrentState(newApplicationState);

                // Request transition to next state (session connection)
                _shouldProceedConnection = true;
            }

            ImGui::SameLine();

            if (ImGui::Button("Play Offline (debug)")) {
                _shouldProceedOfflineDebug = true;
            }

            ImGui::End();
        });
        if (_shouldProceedOfflineDebug) {
            machine->RequestNextState(StateIds::SessionOfflineDebug);
        }
        if (_shouldProceedConnection) {
            machine->RequestNextState(StateIds::SessionConnection);
        }
        return _shouldProceedConnection || _shouldProceedOfflineDebug;
    }
}
