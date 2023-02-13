#pragma once

#include "u_object.h"

namespace HogwartsMP::SDK {
    struct UWorld;
    struct UModel;

    // Class Engine.Level
    // Size: 0x300 (Inherited: 0x28)
    struct ULevel: UObject {
        char pad_28[0x90];             // 0x28(0x90)
        struct UWorld *OwningWorld;    // 0xb8(0x08)
        struct UModel *Model;          // 0xc0(0x08)
        unsigned char padding1[0x238]; // 0xC8(0x238)
    };
}
