#pragma once

#include "ufield.h"

namespace SDK {
    class UStruct: public UField {
      public:
        UStruct *SuperField;
        UField *Children;
        int32_t PropertySize;
        int32_t MinAlignment;
        char pad[0x40];
    };
}
