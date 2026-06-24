#pragma once

#include "uactorcomponent.h"

#include "../containers/fvector.h"
#include "../containers/frotator.h"

namespace SDK {
    class USceneComponent {
      public:
        char pad0[0x1F0];
        FVector RelativeLocation;
        FRotator RelativeRotation;
        FVector RelativeScale3D;
    };
}
