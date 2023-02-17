#pragma once

#include "../containers/fstring.h"
#include "../containers/tarray.h"
#include "../containers/tpair.h"
#include "ufield.h"

namespace SDK {
    class UEnum: public UField {
      public:
        FString CppType;                      
        TArray<TPair<FName, uint64_t>> Names; 
        __int64 CppForm;                      
    };
}
