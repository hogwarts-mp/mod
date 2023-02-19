#pragma once

#include "ugameinstance.h"
#include "../types/uobject.h"

namespace SDK {
    class ULevel;
    class UWorld: public UObject {
      public:
        char pad0[0x8];                         
        ULevel *PersistentLevel;           
        unsigned char pad1[0x2E8];            
        UGameInstance *OwningGameInstance; 
        unsigned char pad2[0x6A8];            
    };
}
