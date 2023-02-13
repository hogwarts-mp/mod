#pragma once

#include "u_player.h"

namespace HogwartsMP::SDK {
    // Class Engine.LocalPlayer
    // Size: 0x258 (Inherited: 0x48)
    struct ULocalPlayer: UPlayer {
        char pad_48[0x28];                                           // 0x48(0x28)
        struct UGameViewportClient *ViewportClient;                  // 0x70(0x08)
        char pad_78[0x1c];                                           // 0x78(0x1c)
        char AspectRatioAxisConstraint[0x01];                        // 0x94(0x01)
        char pad_95[0x3];                                            // 0x95(0x03)
        struct APlayerController *PendingLevelPlayerControllerClass; // 0x98(0x08)
        char bSentSplitJoin : 1;                                     // 0xa0(0x01)
        char pad_A0_1 : 7;                                           // 0xa0(0x01)
        char pad_A1[0x17];                                           // 0xa1(0x17)
        int32_t ControllerId;                                        // 0xb8(0x04)
        char pad_BC[0x19c];                                          // 0xbc(0x19c)
    };
}
