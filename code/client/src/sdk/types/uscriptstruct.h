#pragma once

#include "ustruct.h"

namespace SDK {
    class UScriptStruct: public UStruct {
      public:
        char pad0[0x10];
    };
}
