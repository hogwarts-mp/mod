#pragma once

#include "aactor.h"

namespace SDK {
    class APawn {
      public:
        char pad0[0x158];               // 0000 - 0158
        USceneComponent *RootComponent; // 0158 - 0160
    };
}
