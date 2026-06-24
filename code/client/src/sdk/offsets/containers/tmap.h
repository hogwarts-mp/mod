#pragma once

#include <cstdint>

#include "t_set_element.h"
#include "tpair.h"

namespace SDK {
    template <typename Key, typename Value>
    class TMap {
      public:
        TArray<TSetElement<TPair<Key, Value>>> Data;
      private:
        uint8_t UnknownData01[0x04];          
        uint8_t UnknownData02[0x04];          
        uint8_t UnknownData03[0x08];          
        uint8_t UnknownData04[0x08];          
        uint8_t UnknownData_MayBeSize[0x04];  
        uint8_t UnknownData_MayBeFlag[0x04];  
        uint8_t UnknownData05[0x08];          
        uint8_t UnknownData06[0x08];          
        uint8_t UnknownData07[0x08];          
        uint8_t UnknownData_MayBeSize02[0x04];
        uint8_t UnknownData08[0x04];          
    };
}
