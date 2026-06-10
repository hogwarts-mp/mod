#pragma once

#include <utils/safe_win32.h>

#include <input/input.h>

namespace HogwartsMP::Game {
    class GameInput: public Framework::Input::IInput {
      public:
        GameInput() {}
        ~GameInput() = default;

        void Update() override;

        void GetMousePosition(int &x, int &y) const override {};
        void SetMousePosition(int x, int y) override {};
        void SetMouseVisible(bool visible) override {};
        bool IsMouseVisible() const override {
            return false;
        };
        void SetMouseLocked(bool locked) override {};
        bool IsMouseLocked() const override {
            return false;
        };
        void SetInputLocked(bool locked) override;
        bool IsInputLocked() const override;

        bool IsKeyDown(int key) const override {
            return _keysDown[MapKey(key)];
        };
        bool IsKeyUp(int key) const override {
            return !_keysDown[MapKey(key)];
        };
        bool IsKeyPressed(int key) const override {
            return _keysPressed[MapKey(key)];
        };
        bool IsKeyReleased(int key) const override {
            return _keysReleased[MapKey(key)];
        };

        bool IsMouseButtonDown(int button) const override {
            return false;
        };
        bool IsMouseButtonUp(int button) const override {
            return false;
        };
        bool IsMouseButtonPressed(int button) const override {
            return false;
        };
        bool IsMouseButtonReleased(int button) const override {
            return false;
        };

        uint32_t MapKey(uint32_t key) const override;

        void ProcessEvent(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

      private:
        bool _keysDown[256]     = {false};
        bool _keysPressed[256]  = {false};
        bool _keysReleased[256] = {false};
    };
} // namespace HogwartsMP::Game
