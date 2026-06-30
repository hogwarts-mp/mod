#pragma once

namespace HogwartsMP::Core::UI {
    // Web-backed character creator overlay (creator.html). Owns its own CEF view
    // and the cc:* C++<->JS bridge that drives the local player's CCC live
    // (CharacterCreator::Request*). Toggled with F5. Drive from the game thread
    // (PostUpdate), like Hud/Chat.
    //
    // URL: HOGWARTSMP_CREATOR_URL env override (dev: a localhost dev server) else
    // the hosted Pages default — no code change between dev and ship.
    class Creator final {
      public:
        Creator() = default;

        // Ensure the view exists (lazily creates + wires the bridge).
        void Update();

        // Open/close the creator (focus + control lock + page open()/close()).
        void Toggle();
        void Open();
        void Close();
        bool IsOpen() const {
            return _open;
        }

        // Release input + hide the overlay (e.g. on disconnect).
        void Hide();

      private:
        void EnsureView();

        int _viewId     = -1;
        bool _pageReady = false;
        bool _open      = false;
        bool _locked    = false;
        // Set by cc:confirm; on close, an unconfirmed session rolls the appearance back.
        bool _confirmed = false;
    };
} // namespace HogwartsMP::Core::UI
