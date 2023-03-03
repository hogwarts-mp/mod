#pragma once

#include "f_gameplay_property.h"

namespace SDK {
    class FGameplayProperty_Float : public FGameplayProperty {
      public:
        float Current;    
        float Base;       
        float Min;        
        float Max;        
        char pad0[0x30];
    };
}
