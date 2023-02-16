#pragma once

#include <memory>

#include <utils/safe_win32.h>

#include <integrations/client/instance.h>

namespace HogwartsMP::Core {
    class Application : public Framework::Integrations::Client::Instance {
      private:
        void InitNetworkingMessages();
        void InitRPCs();
        flecs::entity _localPlayer;
        float _tickInterval = 0.01667f;

      public:
        bool PostInit() override;
        bool PreShutdown() override;
        void PostUpdate() override;

        uint64_t GetLocalPlayerID();
    };

    extern std::unique_ptr<Application> gApplication;
}
