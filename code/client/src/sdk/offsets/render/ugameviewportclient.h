#pragma once

#include <cstdint>

#include "uconsole.h"

#include "../containers/tarray.h"

namespace SDK {
    struct FDebugDisplayProperty {};
    class UGameInstance;
    class UWorld;
    class UGameViewportClient {
      public:
        UConsole *ViewportConsole;                              // 0040 - 0048
        TArray<FDebugDisplayProperty> DebugProperties;          // 0048 - 0058
        char pad0[0x10];                                        // 0058 - 0068
        int32_t MaxSplitscreenPlayers;                          // 0068 - 006C
        char pad1[0xC];                                         // 006C - 0078
        UWorld *World;                                          // 0078 - 0080
        UGameInstance *GameInstance;                            // 0080 - 0088
    };
}
