#pragma once

#include "fquat.h"
#include "fvector.h"

namespace SDK {
    struct alignas(16) FTransform {
        FQuat Rotation;
        FVector Translation;
        char pad0[0x4];
        FVector Scale3D;
        char pad1[0x4];
    };
}
