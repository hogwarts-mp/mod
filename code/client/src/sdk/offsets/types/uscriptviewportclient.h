#pragma once

#include "uobject.h"

namespace SDK {
    class UScriptViewportClient: public UObject {
      public:
        unsigned char pad0[0x10];
    };
}
