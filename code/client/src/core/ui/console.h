#pragma once

#include <utils/safe_win32.h>

#include <external/imgui/widgets/console.h>
#include <utils/command_processor.h>

namespace HogwartsMP::Core::UI {
    class HogwartsConsole: public Framework::External::ImGUI::Widgets::Console {
      public:
        HogwartsConsole(std::shared_ptr<Framework::Utils::CommandProcessor> commandProcessor, std::shared_ptr<Framework::Input::IInput> input);
        ~HogwartsConsole() = default;

        virtual void LockControls(bool lock) override;
    };
} // namespace HogwartsMP::Core::UI
