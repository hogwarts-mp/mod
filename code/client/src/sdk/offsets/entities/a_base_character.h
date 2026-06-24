#pragma once

#include "a_ambulatory_character.h"

#include "u_phoenix_managed_character.h"

#include "info/u_character_state_info.h"

#include "../components/u_cloth_sit_component.h"
#include "../settings/u_traversal_settings.h"

namespace SDK {
    class ABase_Character : public AAmbulatory_Character {
      public:
        char pad0[0x40];                        
        UPhoenixManagedCharacter *ManagedCharacter;   
        unsigned char ScratchFXHandle[0x0008];              
        FScriptMulticastDelegate OnSpellSuccess;      
        UTraversalSettings *TraversalSettings;        
        float ShoulderFixupAlpha;                           
        float FadeOutAlphaFactor;                           
        UCharacterStateInfo *CachedCharacterStateInfo;
        char pad1[0x3D0];                       
        UClothSitComponent *ClothSitComponent;        
        char pad2[0x140];                      
        float DefaultMaxHeight;                             
        char pad3[0x404];                      
    };
}
