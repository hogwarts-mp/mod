#pragma once

#include <cstdint>

#include "aplayercontroller.h"
#include "../types/uobject.h"

namespace SDK {
    class UPlayer: public UObject {
      public:
        char pad0[0x8];                           
        APlayerController *PlayerController;
        int32_t CurrentNetSpeed;                    
        int32_t ConfiguredInternetSpeed;
        int32_t ConfiguredLanSpeed;
        char pad1[0x4];
    };
}
