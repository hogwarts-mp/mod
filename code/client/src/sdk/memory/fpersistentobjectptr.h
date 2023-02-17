#pragma once

#include <cstdint>
#include "fweakobjectptr.h"

namespace SDK {
    template <typename TObjectID>
    class TPersistentObjectPtr {
      public:
        FWeakObjectPtr WeakPtr;
        int32_t TagAtLastTest;
        TObjectID ObjectID;
    };
}
