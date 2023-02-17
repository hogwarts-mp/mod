#pragma once

#include <utils/states/state.h>

namespace HogwartsMP::Core::States {
    class ShutdownState: public Framework::Utils::States::IState {
      public:
        ShutdownState();
        ~ShutdownState();

        virtual const char *GetName() const override;
        virtual int32_t GetId() const override;

        virtual bool OnEnter(Framework::Utils::States::Machine *) override;
        virtual bool OnExit(Framework::Utils::States::Machine *) override;

        virtual bool OnUpdate(Framework::Utils::States::Machine *) override;
    };
} // namespace MafiaMP::Core::States
