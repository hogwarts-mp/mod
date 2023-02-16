#pragma once

#include <memory>

#include <utils/safe_win32.h>

#include <integrations/client/instance.h>
#include <graphics/backend/d3d12.h>

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

    struct Globals {
        Application *application                                                  = nullptr;
        HWND window                                                          = nullptr;
        IDXGISwapChain *swapChain                                            = nullptr;
        ID3D12Device *device                                                      = nullptr;
    };

    extern Globals gGlobals;
    extern std::unique_ptr<Application> gApplication;
}
