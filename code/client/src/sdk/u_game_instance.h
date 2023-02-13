#pragma once

#include "u_local_player.h"
#include "u_object.h"
#include "types/t_array.h"

namespace HogwartsMP::SDK {
    struct UGameInstance: UObject {
        char pad_28[0x10];                                 // 0x28(0x10)
        struct Types::TArray<ULocalPlayer *> LocalPlayers; // 0x38(0x10)
        struct UOnlineSession *OnlineSession;              // 0x48(0x08)
        struct Types::TArray<struct UObject *> ReferencedObjects; // 0x50(0x10)
        char pad_60[0x18];                                 // 0x60(0x18)
        char pad_61[0x10];                                 // 0x60(0x18); // 0x78(0x10)
        char pad_88[0x120];                                // 0x88(0x120)
    };
}
