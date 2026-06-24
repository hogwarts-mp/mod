#pragma once

#include "a_able_character.h"
#include "aactor.h"

#include "data/u_ambulatory_data.h"

#include "../containers/fvector.h"

#include "../delegates/f_script_multicast_delegate.h"

#include "../events/u_ak_audio_event.h"
#include "../effects/u_foot_plant_effects.h"

#include "../components/u_primitive_component.h"

#include "../trackers/u_target_tracker.h"
#include "../trackers/u_speed_mode_tracker.h"

namespace SDK {
    class AAmbulatory_Character : public AAble_Character {
      public:
        char pad0[0x8];               
        char pad1[0x8];               
        UAkAudioEvent *AkEvent;             
        UFootPlantEffects *FootPlantEffects;
        bool bIsBiped;                            
        bool bUseTurnAssist;                      
        bool bAbstractMobility;                   
        bool bTestNewMobilityMotions;             
        bool bOnlyLockOnMode;                     
        char pad2[0x3];               
        float MinimumMovementSpeed;               
        float MaximumMovementSpeed;               
        float SlowWalkMovementSpeed;              
        float WalkMovementSpeed;                  
        float FastWalkMovementSpeed;              
        float JogMovementSpeed;                   
        float SprintMovementSpeed;                
        char pad3[0x4];               
        UAmbulatory_Data *AmbulatoryData;   
        char pad4[0x8];               
        unsigned char PathSpec[0x0050];
        float JumpStartHeight;                                     
        FVector JumpStartVelocity;                          
        char pad5[0x5];                                
        bool Debug;                                                
        char pad6[0x2];                                
        AActor *LadderActor;                                 
        char pad7[0x28];                               
        unsigned char WorldSpeedTracker[0x000C];                   
        char pad8[0x4];                                
        FScriptMulticastDelegate CharacterTeleportedDelegate;
        UTargetTracker *LookAtTargetTracker;                 
        UTargetTracker *FaceTargetTracker;                   
        UTargetTracker *AimAtTargetTracker;                  
        USpeedModeTracker *SpeedModeTracker;                 
        USpeedModeTracker *SpeedModifierTracker;             
        TArray<UPrimitiveComponent *> WadingWaterComponents; 
        char pad9[0x18];                               
        FVector FixedWorldDirection;                        
        char pad10[0x538];                              
        unsigned char LedgeComponent[0x8];                         
        char pad11[0xB8];                               
        bool m_navLinkUsingLedge;                                  
        unsigned char m_linkType;                                  
        char pad12[0x2];                                
        float m_traceWallRadius;                                   
        float m_traceWallDropRadius;                               
        float m_traceWallForward;                                  
        float m_traceWallHeightAdjust;                             
        float m_traceLedgeRadius;                                  
        float m_traceLedgeIntoWallAdjust;                          
        float m_traceLedgeUpAdjust;                                
        float m_traceLedgeDownCast;                                
    };
}
