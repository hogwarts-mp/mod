/*
 * MafiaHub OSS license
 * Copyright (c) 2022, MafiaHub. All rights reserved.
 *
 * This file comes from MafiaHub, hosted at https://github.com/MafiaHub/Framework.
 * See LICENSE file in the source repository for information regarding licensing.
 */

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ui/teleport_manager.h"
#include "ui/season_manager.h"

namespace HogwartsMP::Core {
    class DevFeatures final {
      private:

        bool _showTeleportManager {false};
        std::shared_ptr<UI::TeleportManager> _teleportManager {};
        std::shared_ptr<UI::SeasonManager> _seasonManager {};

      public:
        DevFeatures();
        void Init();
        void Update();
        void Shutdown();

        std::shared_ptr<UI::TeleportManager> GetTeleportManager() const {
            return _teleportManager;
        }

        std::shared_ptr<UI::SeasonManager> GetSeasonManager() const {
            return _seasonManager;
        }

      private:
        void SetupCommands();
        void SetupMenuBar();
        void Disconnect();
        void CrashMe();
        void BreakMe();
        void CloseGame();

        void ToggleTeleportManager();
    };
} // namespace HogwartsMP::Core
