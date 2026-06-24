#pragma once

#include <cstdint>

namespace SDK {
    template <typename ElementType>
    class TSetElement {
      public:
        ElementType Value; 
        int32_t HashNextId;
        int32_t HashIndex; 
    };
}
