#pragma once

#include "uworld.h"

namespace SDK {
    class ULevel: public UObject {
      public:
        char pad0[0x90];             
        UWorld *OwningWorld;    
        struct UModel *Model;          
        unsigned char pad1[0x238]; 
    };
}
