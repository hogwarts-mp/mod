#include "acharacter.h"

#include "../types/ufunction.h"
#include "../types/uobject.h"

namespace SDK {
    void ACharacter::Jump() {
        static auto fn = UObject::FindObject<UFunction>("Function Engine.Character.Jump");

        struct {} params;

        UObject::ProcessEvent(fn, &params);
    }
}
