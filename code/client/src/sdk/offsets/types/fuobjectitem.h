#pragma once

#include <cstdint>

namespace SDK {
    class UObject;
    class FUObjectItem {
      public:
        UObject *Object;              
        int32_t Flags;                      
        int32_t ClusterIndex;               
        int32_t SerialNumber;               
        unsigned char pad0[0x04]; 

      public:
        bool IsUnreachable() const;
        bool IsPendingKill() const;
    };
}
