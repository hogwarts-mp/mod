#pragma once

#include "fscriptinterface.h"

namespace SDK {
    template <class InterfaceType>
    class TScriptInterface: public FScriptInterface {
      public:
        InterfaceType *operator->() const {
            return (InterfaceType *)GetInterface();
        }

        InterfaceType &operator*() const {
            return *((InterfaceType *)GetInterface());
        }

        operator bool() const {
            return GetInterface() != nullptr;
        }
    };
}
