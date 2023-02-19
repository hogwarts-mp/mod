#pragma once

#include "../components/uscenecomponent.h"
#include "../types/uobject.h"

namespace SDK {
    class AActor: public UObject {
      public:
        char pad0[0x158];                   // 0000 - 0158
        USceneComponent *RootComponent;     // 0158 - 0160
        char pad1[0xC0];                    // 0160 - 0220
    };
}
