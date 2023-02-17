#pragma once

#include <cstdint>

#include "../types/fuobjectitem.h"

namespace SDK {
    class UObject;
    class TUObjectArray {
      private:
        static const constexpr int32_t NumElementsPerChunk = 64 * 1024; 
        FUObjectItem **Objects;                                         
        FUObjectItem *PreAllocatedObjects;                              
      public:
        int32_t MaxElements; 
        int32_t NumElements; 
        int32_t MaxChunks;   
        int32_t NumChunks;   

      public:
        int32_t Count() const;
        int32_t Max() const;
        bool IsValidIndex(int32_t Index) const;
        FUObjectItem *GetObjectPtr(int32_t Index) const;
        UObject *GetByIndex(int32_t index) const;
        FUObjectItem *GetItemByIndex(int32_t index) const;
        UObject *operator[](int32_t i);
        const UObject *operator[](int32_t i) const;
    };
}
