#pragma once

#include "a_base_character.h"
#include "aactor.h"

#include "../components/u_bt_custom_component.h"
#include "../components/u_blackboard_component.h"
#include "../components/u_biped_state_component.h"
#include "../components/u_context_filter_component.h"
#include "../components/u_rpg_ability_component.h"
#include "../components/u_toolset_component.h"
#include "../components/u_cognition_stimuli_source_component.h"
#include "../components/u_facial_component.h"
#include "../components/u_animation_component.h"

#include "../containers/f_gameplay_property_float.h"

#include "../delegates/f_script_multicast_delegate.h"

#include "../enums/generic.h"

#include "../types/uclass.h"

namespace SDK {
    class ABiped_Character : public ABase_Character {
      public:
        char pad1[0x8];              
        char pad2[0x8];              
        UToolSetComponent *ToolSetComponent;
        UCognitionStimuliSourceComponent *CognitionStimuliSourceComponent;                 
        UBTCustomComponent *BehaviorComp_Primary;       
        UBTCustomComponent *BehaviorComp_Secondary;     
        UBlackboardComponent *BlackboardComponent;           
        UBipedStateComponent *ObjectStateComponent;     
        UContextFilterComponent *ContextFilterComponent;
        URPGAbilityComponent *RPGAbilityComponent;      
        UFacialComponent *FacialComponent;              
        UAnimationComponent *AnimationComponent;        
        float MAX_HEIGHT_FOR_CLIMB;                           
        float MAX_HEIGHT_FOR_HANG;                            
        float MAX_HEIGHT_FOR_JUMPWAIST;                       
        char pad3[0x4];                          
        AActor *ledgeActor;                             
        float CrouchCapsuleHalfHeight;                        
        float CrouchCapsuleRadius;                            
        float CrouchStartCapsuleBlendDuration;                
        float CrouchEndCapsuleBlendDuration;                  
        EnumGeneric ParryState;                               
        char pad4[0x7];                          
        FScriptMulticastDelegate OnSpellCast;           
        FScriptMulticastDelegate OnLumosStart;          
        FScriptMulticastDelegate OnLumosEnd;            
        FScriptMulticastDelegate OnAccioStart;          
        FScriptMulticastDelegate OnAccioEnd;            
        FScriptMulticastDelegate OnProtegoStart;        
        FScriptMulticastDelegate OnProtegoEnd;          
        FScriptMulticastDelegate OnProtegoDeflected;    
        FScriptMulticastDelegate OnDisillusionmentStart;
        FScriptMulticastDelegate OnDisillusionmentEnd;  
        unsigned char SpellLoadOutData[0x0030];               
        int32_t SpellCastHandle;                              
        char pad5[0x4];                          
        FGameplayProperty_Float DoubleItemAbilityDurationChanceModifier;
        FGameplayProperty_Float RefreshAllCooldownChanceModifier;       
        bool bIgnoreFallDamage;                                                
        char pad6[0x3];                                           
        float MaxAirHeight;                                                    
        float LastGroundHeight;                                                
        char pad7[0x4];                                           
        UClass *m_studentActorClassM;                                    
        UClass *m_studentActorClassF;                                    
        char pad8[0x170];                                         
    };
}
