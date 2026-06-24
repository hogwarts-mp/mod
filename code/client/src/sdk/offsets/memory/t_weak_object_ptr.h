#pragma once

#include "fweakobjectptr.h"

namespace SDK {
    template <class T, class TWeakObjectPtrBase = FWeakObjectPtr>
    class TWeakObjectPtr: public TWeakObjectPtrBase {
      public:
        T *Get() const {
            return (T *)TWeakObjectPtrBase::Get();
        }

        T &operator*() const {
            return *Get();
        }

        T *operator->() const {
            return Get();
        }

        bool IsValid() {
            return TWeakObjectPtrBase::IsValid();
        }
    };
}
