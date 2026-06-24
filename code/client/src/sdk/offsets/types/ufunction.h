#pragma once

#include <cstdint>

#include "ustruct.h"

namespace SDK {
    class UFunction: public UStruct {
      public:
        int32_t FunctionFlags;
        int16_t RepOffset;
        int8_t NumParms;
        unsigned char pad0[0x01];
        uint16_t ParmsSize;
        uint16_t ReturnValueOffset;
        uint16_t RPCId;
        uint16_t RPCResponseId;
        class FProperty *FirstPropertyToInit;
        class UFunction *EventGraphFunction;
        int32_t EventGraphCallOffset;
        unsigned char pad1[0x04];
        void *Func;
    };
}
