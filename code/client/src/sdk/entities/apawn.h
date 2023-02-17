#pragma once

#include "aactor.h"

namespace SDK {
    class APawn: public AActor {
      public:
        unsigned char pad0[0x60];
    };
}
