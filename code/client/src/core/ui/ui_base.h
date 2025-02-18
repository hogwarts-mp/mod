#pragma once

#include <external/imgui/widgets/ui_base.h>

namespace HogwartsMP::Core::UI {
    class UIBase: virtual public Framework::External::ImGUI::Widgets::UIBase {
      protected:
        virtual bool AreControlsLocked() const override;

        virtual void LockControls(bool lock) const override;

      public:
        UIBase(): Framework::External::ImGUI::Widgets::UIBase() {};
    };
} // namespace HogwartsMP::Core::UI
