#pragma once

namespace HogwartsMP::SDK::Types {
    template <class T>
    class TArray {
      public:
        T *Data;
        int Count;
        int Max;
    };
}
