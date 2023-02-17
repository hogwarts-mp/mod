#pragma once

#include "aactor.h"
#include "apawn.h"
#include "acharacter.h"

namespace SDK {
    class AController: public AActor {
      public:
        char pad0[0x8];               
        struct APlayerState *PlayerState;
        char pad1[0x8];               
        char OnInstigatedAnyDamage[0x10];
        char StateName[0x08];
        APawn *Pawn;                        
        char pad2[0x8];                         
        ACharacter *Character;              
        struct USceneComponent *TransformComponent;
        char pad3[0x18];                        
        char ControlRotation[0x0c];
        char bAttachToPawn : 1;
        char pad4: 7;    
        char pad5[0x3];     
    };
}
