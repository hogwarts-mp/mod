#pragma once

#include "u_object.h"
#include "u_level.h"
#include "u_game_instance.h"

namespace HogwartsMP::SDK {
    struct UWorld: UObject {
        char pad_28[0x8];                         // 0x28(0x08)
        struct ULevel *PersistentLevel;           // 0x30(0x08)
        unsigned char padding1[0x2E8];            // 0x38(0x2E8)
        struct UGameInstance *OwningGameInstance; // 0x320(0x08)
        unsigned char padding2[0x6A8];            // 0x328(0x670)
    };

    static UWorld* GetMainWorld() {
        return *reinterpret_cast<UWorld **>(hook::baseAddressDifference + 0x928DB88);
    }
}
