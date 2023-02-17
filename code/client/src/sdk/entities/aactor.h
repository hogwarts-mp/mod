#pragma once

#include "../types/uobject.h"

namespace SDK {
    class AActor: public UObject {
      public:
        unsigned char pad0[0x220];
    };
}
