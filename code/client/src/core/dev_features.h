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

namespace HogwartsMP::Core {
    class DevFeatures final {
      public:
        DevFeatures();
        void Init();
        void Update();
        void Shutdown();

      private:
        void SetupCommands();
        void SetupMenuBar();
        void Disconnect();
        void CrashMe();
        void BreakMe();
        void CloseGame();
    };
} // namespace HogwartsMP::Core
