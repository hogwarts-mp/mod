#pragma once

#include "ugameinstance.h"
#include "../types/uobject.h"

#include "core/game_layout.h"

namespace SDK {
    class ULevel;
    class UWorld: public UObject {
      public:
        // Pad sizes are computed from the central offset table
        // (core/game_layout.h) so this layout has a single source of truth.
        char pad0[HogwartsMP::Game::Offset::UWorld_PersistentLevel - sizeof(UObject)]; // -> PersistentLevel @ 0x30
        ULevel *PersistentLevel;
        unsigned char pad1[HogwartsMP::Game::Offset::UWorld_OwningGameInstance - HogwartsMP::Game::Offset::UWorld_PersistentLevel - sizeof(ULevel *)]; // -> OwningGameInstance @ 0x330
        UGameInstance *OwningGameInstance;
        unsigned char pad2[0x698];
    };
}
