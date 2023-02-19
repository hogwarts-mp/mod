#pragma once

#include "apawn.h"

namespace SDK {
    class ACharacter: public APawn {
      public:
        class USkeletalMeshComponent *Mesh;                   
        struct UCharacterMovementComponent *CharacterMovement; 
        struct UCapsuleComponent *CapsuleComponent;            
        char BasedMovement[0x30];
        char ReplicatedBasedMovement[0x30];
        float AnimRootMotionTranslationScale; 
        char BaseTranslationOffset[0x0c];
        char BaseRotationOffset[0x10];
        float ReplicatedServerLastTransformUpdateTimeStamp; 
        float ReplayLastTransformUpdateTimeStamp;           
        char ReplicatedMovementMode;                        
        bool bInBaseReplication;                            
        char pad0[0x2];                                  
        float CrouchedEyeHeight;                            
        char bIsCrouched : 1;                               
        char bProxyIsJumpForceApplied : 1;                  
        char bPressedJump : 1;                              
        char bClientUpdating : 1;                           
        char bClientWasFalling : 1;                         
        char bClientResimulateRootMotion : 1;               
        char bClientResimulateRootMotionSources : 1;        
        char bSimGravityDisabled : 1;                       
        char bClientCheckEncroachmentOnNetUpdate : 1;       
        char bServerMoveIgnoreRootMotion : 1;               
        char bWasJumping : 1;                               
        char pad1 : 5;                                 
        char pad2[0x2];                                  
        float JumpKeyHoldTime;                              
        float JumpForceTimeRemaining;                       
        float ProxyJumpForceStartedTime;                    
        float JumpMaxHoldTime;                              
        int32_t JumpMaxCount;                               
        int32_t JumpCurrentCount;                           
        int32_t JumpCurrentCountPreJump;                    
        char pad3[0x8];                                  
        char otherSize[0x168];                              

        void Jump();
    };
}
