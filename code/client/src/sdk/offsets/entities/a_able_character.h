#pragma once

#include "a_animation_architect_character.h"

#include "../components/u_ragdoll_control_component.h"
#include "../components/u_abl_ability_component.h"

#include "../managers/u_abl_physical_body_manager.h"

namespace SDK {
    class AAble_Character : public AAnimationArchitect_Character {
      public:
        char pad0[0x8];                              
        char pad1[0x8];                              
        UAblAbilityComponent *AblAbilityComponent;         
        URagdollControlComponent *RagdollControlComponent;
        char pad2[0x18];                   
        UAblPhysicalBodyManager *PhysicalBodyManager;
        char pad3[0x8];      
    };
}
