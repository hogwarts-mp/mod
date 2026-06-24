#pragma once

namespace SDK {
    template <class T>
    struct TArray {
        TArray(): Data(nullptr), Count(0), Max(0) {}

        T &operator[](int ArrayIdx) {
            return Data[ArrayIdx];
        }

        T &operator[](int ArrayIdx) const {
            return Data[ArrayIdx];
        }

        T *begin() {
            return reinterpret_cast<T *>(Count, Data);
        }

        T *end() {
            return reinterpret_cast<T *>(Count, Data + Count);
        }

        T *Data;
        int Count;
        int Max;
    };

}
