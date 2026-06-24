#pragma once

#include <cstdint>

#include "f_gameplay_property.h"

namespace SDK {
    class FGameplayProperty_Int : public FGameplayProperty {
      public:
        int32_t Current;                   
        int32_t Base;                      
        int32_t Min;                       
        int32_t Max;                       
        EnumGeneric RoundingType;          
        char pad0[0x1F];
    };
}
