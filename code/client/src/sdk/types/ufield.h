#pragma once

#include "uobject.h"

namespace SDK {
    class UField: public UObject {
      public:
        UField *Next;
    };
}
