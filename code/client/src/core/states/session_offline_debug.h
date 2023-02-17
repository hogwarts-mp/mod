#pragma once

#include <utils/states/state.h>

namespace HogwartsMP::Core::States {
    class SessionOfflineDebugState: public Framework::Utils::States::IState {
      public:
        SessionOfflineDebugState();
        ~SessionOfflineDebugState();

        virtual const char *GetName() const override;
        virtual int32_t GetId() const override;

        virtual bool OnEnter(Framework::Utils::States::Machine *) override;
        virtual bool OnExit(Framework::Utils::States::Machine *) override;

        virtual bool OnUpdate(Framework::Utils::States::Machine *) override;
    };
}
