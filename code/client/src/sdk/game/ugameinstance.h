#pragma once

#include "../entities/ulocalplayer.h"
#include "../containers/tarray.h"
#include "../types/uobject.h"

#include "../../core/game_layout.h"

namespace SDK {
    class UGameInstance: public UObject {
      public:
        // pad computed from the central offset table (core/game_layout.h)
        char pad0[HogwartsMP::Game::Offset::UGameInstance_LocalPlayers - sizeof(UObject)];
        TArray<ULocalPlayer *> LocalPlayers;
        struct UOnlineSession *OnlineSession;              
        TArray<UObject *> ReferencedObjects; 
        char pad1[0x18];                                 
        char pad2[0x10];                                 
        char pad3[0x120];                                
    };
}
