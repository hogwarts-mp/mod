#pragma once

#include "ustruct.h"

namespace SDK {
    class UClass: public UStruct {
      public:
        unsigned char pad0[0x180];

      public:
        template <typename T>
        T *CreateDefaultObjectOfType() {
            return static_cast<T *>(CreateDefaultObject());
        }

        UObject *CreateDefaultObject();
        static UClass *StaticClass();
    };

}
