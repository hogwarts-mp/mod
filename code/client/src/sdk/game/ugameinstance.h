#pragma once

#include "../entities/ulocalplayer.h"
#include "../containers/tarray.h"
#include "../types/uobject.h"

namespace SDK {
    class UGameInstance: public UObject {
      public:
        char pad0[0x10];                                 
        TArray<ULocalPlayer *> LocalPlayers; 
        struct UOnlineSession *OnlineSession;              
        TArray<UObject *> ReferencedObjects; 
        char pad1[0x18];                                 
        char pad2[0x10];                                 
        char pad3[0x120];                                
    };
}
