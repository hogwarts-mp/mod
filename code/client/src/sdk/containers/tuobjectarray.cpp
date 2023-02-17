#include "tuobjectarray.h"
#include "../types/uobject.h"

namespace SDK {
    int32_t TUObjectArray::Count() const {
        return NumElements;
    }

    int32_t TUObjectArray::Max() const {
        return MaxElements;
    }

    bool TUObjectArray::IsValidIndex(int32_t Index) const {
        return Index < Count() && Index >= 0;
    }

    FUObjectItem *TUObjectArray::GetObjectPtr(int32_t Index) const {
        const int32_t ChunkIndex       = Index / NumElementsPerChunk;
        const int32_t WithinChunkIndex = Index % NumElementsPerChunk;
        if (!IsValidIndex(Index))
            return nullptr;
        if (ChunkIndex > NumChunks)
            return nullptr;
        if (Index > MaxElements)
            return nullptr;
        FUObjectItem *Chunk = Objects[ChunkIndex];
        if (!Chunk)
            return nullptr;
        return Chunk + WithinChunkIndex;
    }

    UObject *TUObjectArray::GetByIndex(int32_t index) const {
        FUObjectItem *ItemPtr = GetObjectPtr(index);
        if (!ItemPtr)
            return nullptr;
        return (*ItemPtr).Object;
    }

    FUObjectItem *TUObjectArray::GetItemByIndex(int32_t index) const {
        FUObjectItem *ItemPtr = GetObjectPtr(index);
        if (!ItemPtr)
            return nullptr;
        return ItemPtr;
    }

    UObject *TUObjectArray::operator[](int32_t i) {
        return GetByIndex(i);
    }

    const UObject *TUObjectArray::operator[](int32_t i) const {
        return GetByIndex(i);
    }
}
