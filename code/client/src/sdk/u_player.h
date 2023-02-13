#pragma once

#include "u_object.h"

namespace HogwartsMP::SDK {
    // Class Engine.Player
    // Size: 0x48 (Inherited: 0x28)
    struct UPlayer: UObject {
        char pad_28[0x8];                           // 0x28(0x08)
        struct APlayerController *PlayerController; // 0x30(0x08)
        int32_t CurrentNetSpeed;                    // 0x38(0x04)
        int32_t ConfiguredInternetSpeed;            // 0x3c(0x04)
        int32_t ConfiguredLanSpeed;                 // 0x40(0x04)
        char pad_44[0x4];                           // 0x44(0x04)
    };
}
