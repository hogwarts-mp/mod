#pragma once

#include <cstdint>

#include "../containers/frotator.h"
#include "../entities/acharacter.h"

namespace SDK {
    enum class EMovementMode : uint8_t { MOVE_None = 0, MOVE_Walking = 1, MOVE_NavWalking = 2, MOVE_Falling = 3, MOVE_Swimming = 4, MOVE_Flying = 5, MOVE_Custom = 6, MOVE_MAX = 7 };

    class UCharacterMovementComponent {
        unsigned char inheritedSizePadding[0x150];
        char pad0[0x10];                     
        ACharacter *CharacterOwner;      
        float GravityScale;                     
        float MaxStepHeight;                    
        float JumpZVelocity;                    
        float JumpOffJumpZFactor;               
        float WalkableFloorAngle;               
        float WalkableFloorZ;                   
        char bMovementModeIsManaged : 1;        
        char bMovementModeCalledFromManager : 1;
        char pad_180_2 : 6;                     
        enum class EMovementMode MovementMode;  
        char CustomMovementMode;                
        char NetworkSmoothingMode[0x1];
        float GroundFriction;                                    
        char pad1[0x24];                                      
        float MaxWalkSpeed;                                      
        float MaxWalkSpeedCrouched;                              
        float MaxSwimSpeed;                                      
        float MaxFlySpeed;                                       
        float MaxCustomMovementSpeed;                            
        float MaxAcceleration;                                   
        float MinAnalogWalkSpeed;                                
        float BrakingFrictionFactor;                             
        float BrakingFriction;                                   
        float BrakingSubStepTime;                                
        float BrakingDecelerationWalking;                        
        float BrakingDecelerationFalling;                        
        float BrakingDecelerationSwimming;                       
        float BrakingDecelerationFlying;                         
        float AirControl;                                        
        float AirControlBoostMultiplier;                         
        float AirControlBoostVelocityThreshold;                  
        float FallingLateralFriction;                            
        float CrouchedHalfHeight;                                
        float Buoyancy;                                          
        float PerchRadiusThreshold;                              
        float PerchAdditionalHeight;                             
        struct FRotator RotationRate;                            
        char bUseSeparateBrakingFriction : 1;                    
        char bApplyGravityWhileJumping : 1;                      
        char bUseControllerDesiredRotation : 1;                  
        char bOrientRotationToMovement : 1;                      
        char bSweepWhileNavWalking : 1;                          
        char pad2 : 1;                                      
        char bMovementInProgress : 1;                            
        char bEnableScopedMovementUpdates : 1;                   
        char bEnableServerDualMoveScopedMovementUpdates : 1;     
        char bForceMaxAccel : 1;                                 
        char bRunPhysicsWithNoController : 1;                    
        char bForceNextFloorCheck : 1;                           
        char bShrinkProxyCapsule : 1;                            
        char bCanWalkOffLedges : 1;                              
        char bCanWalkOffLedgesWhenCrouching : 1;                 
        char pad3 : 1;                                      
        char pad4: 1;                                      
        char bNetworkSkipProxyPredictionOnNetUpdate : 1;         
        char bNetworkAlwaysReplicateTransformUpdateTimestamp : 1;
        char bDeferUpdateMoveComponent : 1;                      
        char bEnablePhysicsInteraction : 1;                      
        char bTouchForceScaledToMass : 1;                        
        char bPushForceScaledToMass : 1;                         
        char bPushForceUsingZOffset : 1;                         
        char bScalePushForceToVelocity : 1;                      
        char pad5 : 7;                                      
        char pad6[0x4];                                       
        class USceneComponent *DeferredUpdatedMoveComponent;    
        float MaxOutOfWaterStepHeight;                           
        float OutofWaterZ;                                       
        float Mass;                                              
        float StandingDownwardForceScale;                        
        float InitialPushForceFactor;                            
        float PushForceFactor;                                   
        float PushForcePointZOffsetFactor;                       
        float TouchForceFactor;                                  
        float MinTouchForce;                                     
        float MaxTouchForce;                                     
        float RepulsionForce;                                    
        struct FVector Acceleration;                             
        char pad7[0x8];                                       
        unsigned char LastUpdateRotation[0x10];
        struct FVector LastUpdateLocation;                          
        struct FVector LastUpdateVelocity;                          
        float ServerLastTransformUpdateTimeStamp;                   
        float ServerLastClientGoodMoveAckTime;                      
        float ServerLastClientAdjustmentTime;                       
        struct FVector PendingImpulseToApply;                       
        struct FVector PendingForceToApply;                         
        float AnalogInputModifier;                                  
        char pad8[0xc];                                          
        float MaxSimulationTimeStep;                                
        int MaxSimulationIterations;                                
        int MaxJumpApexAttemptsPerSimulation;                       
        float MaxDepenetrationWithGeometry;                         
        float MaxDepenetrationWithGeometryAsProxy;                  
        float MaxDepenetrationWithPawn;                             
        float MaxDepenetrationWithPawnAsProxy;                      
        float NetworkSimulatedSmoothLocationTime;                   
        float NetworkSimulatedSmoothRotationTime;                   
        float ListenServerNetworkSimulatedSmoothLocationTime;       
        float ListenServerNetworkSimulatedSmoothRotationTime;       
        float NetProxyShrinkRadius;                                 
        float NetProxyShrinkHalfHeight;                             
        float NetworkMaxSmoothUpdateDistance;                       
        float NetworkNoSmoothUpdateDistance;                        
        float NetworkMinTimeBetweenClientAckGoodMoves;              
        float NetworkMinTimeBetweenClientAdjustments;               
        float NetworkMinTimeBetweenClientAdjustmentsLargeCorrection;
        float NetworkLargeClientCorrectionDistance;                 
        float LedgeCheckThreshold;                                  
        float JumpOutOfWaterPitch;                                  
        unsigned char currentFloor[0x94];
        enum class EMovementMode DefaultLandMovementMode;      
        enum class EMovementMode DefaultWaterMovementMode;     
        enum class EMovementMode GroundMovementMode;           
        char bMaintainHorizontalGroundVelocity : 1;            
        char bImpartBaseVelocityX : 1;                         
        char bImpartBaseVelocityY : 1;                         
        char bImpartBaseVelocityZ : 1;                         
        char bImpartBaseAngularVelocity : 1;                   
        char bJustTeleported : 1;                              
        char bNetworkUpdateReceived : 1;                       
        char bNetworkMovementModeChanged : 1;                  
        char bIgnoreClientMovementErrorChecksAndCorrection : 1;
        char bServerAcceptClientAuthoritativePosition : 1;     
        char bNotifyApex : 1;                                  
        char bCheatFlying : 1;                                 
        char bWantsToCrouch : 1;                               
        char bCrouchMaintainsBaseLocation : 1;                 
        char bIgnoreBaseRotation : 1;                          
        char bFastAttachedMove : 1;                            
        char bAlwaysCheckFloor : 1;                            
        char bUseFlatBaseForFloorChecks : 1;                   
        char bPerformingJumpOff : 1;                           
        char bWantsToLeaveNavWalking : 1;                      
        char bUseRVOAvoidance : 1;                             
        char bRequestedMoveUseAcceleration : 1;                
        char pad9 : 1;                                    
        char bWasSimulatingRootMotion : 1;                     
        char bAllowPhysicsRotationDuringAnimRootMotion : 1;    
        char bHasRequestedVelocity : 1;                        
        char bRequestedMoveWithMaxSpeed : 1;                   
        char bWasAvoidanceUpdated : 1;                         
        char pad10: 2;                                    
        char bProjectNavMeshWalking : 1;                       
        char bProjectNavMeshOnBothWorldChannels : 1;           
        char pad11[0x11];                                    
        float AvoidanceConsiderationRadius;                    
        struct FVector RequestedVelocity;                      
        int32_t AvoidanceUID;                                  
        unsigned char avoidanceMasks[0xC];
        float AvoidanceWeight;                 
        struct FVector PendingLaunchVelocity;  
        char pad12[0xa4];                    
        float NavMeshProjectionInterval;       
        float NavMeshProjectionTimer;          
        float NavMeshProjectionInterpSpeed;    
        float NavMeshProjectionHeightScaleUp;  
        float NavMeshProjectionHeightScaleDown;
        float NavWalkingFloorDistTolerance;    
        char PostPhysicsTickFunction[0x48];    
        char pad13[0x18];                    
        float MinTimeBetweenTimeStampResets;   
        char pad14[0x4ac];                   
        unsigned char rootMotions[0x70];
        char pad15[0x98];                   
        char RootMotionParams[0x40];          
        struct FVector AnimRootMotionVelocity;
        char pad16[0x24];                   
    };
}