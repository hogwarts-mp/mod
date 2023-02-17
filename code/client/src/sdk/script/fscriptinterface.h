#pragma once

#include "../types/uobject.h"

namespace SDK {
    class FScriptInterface {
      private:
        UObject *ObjectPointer;
        void *InterfacePointer;

      public:
        UObject *GetObject() const {
            return ObjectPointer;
        }

        UObject *&GetObjectRef() {
            return ObjectPointer;
        }

        void *GetInterface() const {
            return ObjectPointer != nullptr ? InterfacePointer : nullptr;
        }
    };
}
