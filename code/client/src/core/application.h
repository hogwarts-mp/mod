#pragma once

#include <memory>

#include <utils/safe_win32.h>

#include <integrations/client/instance.h>

namespace HogwartsMP::Core {
    class Application : public Framework::Integrations::Client::Instance {
      public:
        bool PostInit() override;
        bool PreShutdown() override;
        void PostUpdate() override;
    };
    
    extern std::unique_ptr<Application> gApplication;
}
