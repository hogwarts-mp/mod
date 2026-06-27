#include "game_input.h"

#include "core/application.h"

namespace HogwartsMP::Game {
    void GameInput::Update() {
        for (int i = 0; i < 256; i++) {
            _keysPressed[i]  = false;
            _keysReleased[i] = false;
        }
    }

    uint32_t GameInput::MapKey(uint32_t key) const {
        return key; // we map WndProc VK keys 1:1 to our input keys
    }

    void GameInput::ProcessEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        switch (msg) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            // Count only the down-transition (filters auto-repeat): the game
            // re-dispatches held keys, which would re-fire F8/Enter every frame.
            if (!_keysDown[wParam]) {
                _keysPressed[wParam] = true;
            }
            _keysDown[wParam] = true;
            break;
        case WM_KEYUP:
        case WM_SYSKEYUP:
            _keysDown[wParam]     = false;
            _keysReleased[wParam] = true;
            break;
        default: break;
        }
    }

    bool GameInput::IsInputLocked() const {
        return Core::gApplication->AreControlsLocked();
    }

    void GameInput::SetInputLocked(bool locked) {
        Core::gApplication->LockControls(locked);
    }
} // namespace HogwartsMP::Game
