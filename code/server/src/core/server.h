#pragma once

#include <integrations/server/instance.h>

namespace HogwartsMP {
    class Server: public Framework::Integrations::Server::Instance {
      private:
        static inline Framework::Scripting::Engine *_scriptingEngine;
        void InitNetworkingMessages();

      public:
        void PostInit() override;

        void PostUpdate() override;

        void PreShutdown() override;

        void BroadcastChatMessage(const std::string &msg);

        void ModuleRegister(Framework::Scripting::Engine *engine) override;

        void InitRPCs();

        static inline Server *_serverRef = nullptr;

        static Framework::Scripting::Engine *GetScriptingEngine() {
            return _scriptingEngine;
        }
    };
} // namespace HogwartsMP
