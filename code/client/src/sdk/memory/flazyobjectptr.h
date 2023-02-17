#pragma once

#include "fpersistentobjectptr.h"
#include "funiqueobjectguid.h"

namespace SDK {
    class FLazyObjectPtr: public TPersistentObjectPtr<FUniqueObjectGuid> {};
}
