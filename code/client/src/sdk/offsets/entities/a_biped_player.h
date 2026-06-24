#pragma once

#include "a_biped_character.h"
#include "aactor.h"

#include "../components/u_conversation_component.h"
#include "../components/u_customizable_character_component.h"
#include "../components/u_toolset_component.h"
#include "../components/u_spline_component.h"
#include "../components/u_vegetation_interaction_component.h"
#include "../components/u_ak_component.h"

#include "../containers/u_curve_float.h"
#include "../containers/f_gameplay_property_float.h"
#include "../containers/f_gameplay_property_int.h"
#include "../containers/fvector.h"
#include "../containers/fname.h"
#include "../containers/tarray.h"
#include "../containers/tmap.h"

#include "../delegates/f_script_delegate.h"
#include "../delegates/f_script_multicast_delegate.h"

#include "../enums/generic.h"

#include "../events/u_ak_audio_event.h"

#include "../memory/t_weak_object_ptr.h"

#include "../types/uclass.h"

namespace SDK {
    class ABiped_Player : public ABiped_Character {
      public:
        char pad0[0x18];                            
        UConversationComponent *ConversationComponent;     
        UCustomizableCharacterComponent *Customization;    
        class ASocialReasoning *PlayerSocialReasoning;           
        char pad1[0x50];                            
        UToolSetComponent *InventoryToolSetComponent;      
        bool bAllowFastWalk;                                     
        char pad2[0x7];                             
        UCurveFloat *WalkSlowModeSpeedCurve;               
        UCurveFloat *WalkModeSpeedCurve;                   
        UCurveFloat *WalkOnlyModeSpeedCurve;               
        UCurveFloat *WalkFastModeSpeedCurve;               
        UCurveFloat *WalkFastOnlyModeSpeedCurve;           
        UCurveFloat *JogModeSpeedCurve;                    
        UCurveFloat *JogOnlyModeSpeedCurve;                
        UCurveFloat *SprintModeSpeedCurve;                 
        UCurveFloat *JogModeSpeedGovernorCurve;            
        UClass *GryffindorMaleBedAbility;                  
        UClass *GryffindorFemaleBedAbility;                
        UClass *RavenClawMaleBedAbility;                   
        UClass *RavenClawFemaleBedAbility;                 
        UClass *HufflePuffMaleBedAbility;                  
        UClass *HufflePuffFemaleBedAbility;                
        UClass *SlytherinMaleBedAbility;                   
        UClass *SlytherinFemaleBedAbility;                 
        UClass *AttackIndicatorActorClass;                 
        class APlayer_AttackIndicator *AttackIndicatorActor;     
        float InnerDeadZoneMin;                                  
        float InnerDeadZoneMax;                                  
        float OuterDeadZone;                                     
        EnumGeneric EdgeState;                                   
        char pad3[0x3];                             
        FVector EdgeLocation;                             
        FVector EdgeNormal;                               
        float EdgeDistanceToGround;                              
        float EdgeDistanceFromEdge;                              
        FVector EdgeImpulse;                              
        char pad4[0x4];                             
        USplineComponent *SyncToSplineComponent;           
        float SyncToSplineDirection;                             
        float SyncToSplineDistance;                              
        UClass *DefaultIdleAbility;                        
        int32_t MaxChompingCabbage;                              
        int32_t MaxVenomousTentacula;                            
        bool AllowPlayerCamContorlOnOtherActorCam;               
        bool InvertCameraPitchControl;                           
        bool InvertCameraYawControl;                             
        bool InvertMoveLeftRightControl;                         
        bool InvertMoveForwardBackControl;                       
        bool EnableFadeSystem;                                   
        char pad5[0x2];                             
        class AFieldGuideHotSpot *PlayerInThisFieldGuideHotSpot; 
        float FinisherDamage;                                    
        float ElderWandFinisherDamage;                           
        float MaxFocus;                                          
        int32_t InitialNumBarsFilled;                            
        float FinisherFocusCost;                                 
        float ManagedFinisherFocusCost;                          
        float PushFocusCost;                                     
        char pad6[0x4];                             
        unsigned char FocusMap[0x50];                            
        TArray<class UMultiFX2_Base *> FinisherAvailableFX;      
        float ComboResetTime;                                    
        float AdditionalComboResetTimeOnParry;                   
        int32_t CooldownPickupFirstTierSize;                     
        int32_t CooldownPickupTierSize;                          
        TArray<int32_t> CooldownPickupCountArray;                
        float ExtendedCombatTime;                                
        FScriptDelegate ClothTargetsStartDelegate;         
        FScriptDelegate ClothTargetsEndDelegate;           
        float HealthIncreasePerLevel;                            
        FName LeftFootSocketName;                          
        FName RightFootSocketName;                         
        UCurveFloat *LeftStickMagWalkingCurve;             
        UCurveFloat *LeftStickMagJoggingCurve;             
        float BulletTimeStartSeconds;                            
        char pad7[0x4];                             
        UCurveFloat *BulletTimeDilationCurve;              
        float BulletTimeDurationSeconds;                         
        float BulletTimeDialation;                               
        float CutBackToPlayerCamAfterTimeDialationSec;           
        char pad8[0x4];                             
        unsigned char DamageEffectBlendable[0x10];               
        class UMaterialInstanceDynamic *DamageEffectMaterialInstance;         
        UCurveFloat *DamageEffectCurve;                                 
        float DamageEffectDuration;                                           
        float DamageEffectScale;                                              
        TArray<class UMultiFX2_Base *> CriticalHealthFX;                      
        UCurveFloat *CriticalHealthPulseTimeCurve;                      
        TArray<class UMultiFX2_Base *> CriticalHealthPulseFX;                 
        float CriticalHealthFadeOutTime;                                      
        char pad9[0x4];                                          
        UClass *DamageDirectionClass;                                   
        float PercentHealthRecoverdPerSecond;                                 
        float SecondsAfterCombatBeforeRecoveryStarts;                         
        bool DoDamageOnNextLanding;                                           
        bool UseDodgeCamera;                                                  
        bool InHoverDrone;                                                    
        bool bLookAtHips;                                                     
        float cameraOffsetScale;                                              
        bool bLockOutPlayerCamControl;                                        
        bool bLockOutPlayerCamControlPitch;                                   
        char pad10[0xE];                                          
        TArray<struct FGearItem> CachedGearPieces;                            
        FGameplayProperty_Float OffenseStat;                           
        FGameplayProperty_Float DefenseStat;                           
        FGameplayProperty_Float CooldownStat;                          
        FGameplayProperty_Int InventorySizeModification;               
        FGameplayProperty_Float HiddenAfterTakedownTime;               
        FGameplayProperty_Float StatDamageMultiplier;                  
        FGameplayProperty_Float StatDamageReduction;                   
        FGameplayProperty_Float StatCooldownReduction;                 
        FGameplayProperty_Float GainKnowledgeModifier;                 
        FGameplayProperty_Float GainExperienceModifier;                
        FGameplayProperty_Float CompanionBondingPointsModifier;        
        FGameplayProperty_Float VendorPricesModifier;                  
        FGameplayProperty_Float EnemyEvadeChanceModifier;              
        FGameplayProperty_Float EnemyProtegoChanceModifier;            
        FGameplayProperty_Float EnemyAwarenessRateModifier;            
        FGameplayProperty_Float CriticalSuccessChanceModifier;         
        FGameplayProperty_Float LootDropCountModifier;                 
        FGameplayProperty_Float LootDropRareChanceModifier;            
        FGameplayProperty_Float StealRateModifier;                     
        FGameplayProperty_Float CaptureToolRangeModifier;              
        FGameplayProperty_Float CaptureToolDecayPenaltyModifier;       
        FGameplayProperty_Float CaptureToolCaptureSpeedModifier;       
        FGameplayProperty_Float CreatureAgroFleeDistanceModifier;      
        FGameplayProperty_Float FocusModifier;                         
        FGameplayProperty_Float FocusOnHitModifier;                    
        FGameplayProperty_Float UseConsumeableForFreeChance;           
        FGameplayProperty_Float UsePotionForFreeChance;                
        FGameplayProperty_Int ObscurePerkDistance;                     
        FGameplayProperty_Float MaliciousPerkMultiplier;               
        FGameplayProperty_Float ExpelliarmusLootDropChance;            
        FGameplayProperty_Int CombatAuxPerk;                           
        FGameplayProperty_Float CombatAuxPerkDuration;                 
        FGameplayProperty_Float CombatAuxPerkCooldown;                 
        char pad11[0x8];                                          
        FGameplayProperty_Int DirectAIPerk;                            
        FGameplayProperty_Int ExtendedCompanionInventoryCapacity;      
        FGameplayProperty_Float PlantDamageModifier;                   
        FGameplayProperty_Float VenomousTentaculaPoisonDamageModifier; 
        FGameplayProperty_Float VenomousTentaculaWeakenEnemyDuration;  
        FGameplayProperty_Float HealingCooldownGroupModifier;          
        TWeakObjectPtr<AActor> LastStupefyTarget;                       
        FGameplayProperty_Float CabbageVampireModifier;                
        FGameplayProperty_Float PlantDamageFocus;                      
        FGameplayProperty_Float PlayerStupifyPlantDamageModifier;      
        FGameplayProperty_Float MandrakeBonusReactionTime;             
        FGameplayProperty_Float PlantSummonHealthPercent;              
        FGameplayProperty_Float DifficultyAttackCooldownModifier;      
        FGameplayProperty_Float DisillusionmentModifier;               
        FGameplayProperty_Float CrimeSceneInvestigationModifier;       
        FGameplayProperty_Float SneakOScopeWindupModifier;             
        FGameplayProperty_Float SilencioRangeModifier;                 
        bool bAllowEdgeCheck;                                                 
        bool bInCombatMode;                                                   
        bool bInCombatModeDelayed;                                            
        char pad12[0x5];                                          
        AActor *FollowActor;                                            
        int32_t CautiousMode;                                                 
        char pad13[0x4];                                          
        FGameplayProperty_Int BonusAncientMagicBars;                   
        FGameplayProperty_Int BonusSpellLoadouts;                      
        char pad14[0x10];                                         
        bool InStealthMode;                                                   
        char pad15[0x7];                                          
        bool bIsInvisible;                                                    
        bool InCinematic;                                                     
        EnumGeneric WandLinkResult;                                           
        char pad16[0x1];                                          
        bool bHoldingProtegoParry;                                            
        char pad17[0x3];                                          
        UClass *EnemyDetectionFXClass;                                  
        UAkAudioEvent *EnemyDetectionSFX;                               
        UAkAudioEvent *EnemyAlertSFX;                                   
        UAkAudioEvent *EnemyAggroSFX;                                   
        char pad18[0x60];                                         
        FScriptMulticastDelegate OnSpellCooldownChanged;                
        FScriptMulticastDelegate OnFocusChanged;                        
        TMap<FName, struct FSpellCooldownData> CooldownMap;             
        FScriptMulticastDelegate OnStationInteractionExitComplete;      
        FVector DesiredFocusDirection;                                 
        bool bUseDesiredFocusDirection;                                       
        char pad19[0x3];                                          
        UVegetationInteractionComponent *VegetationInteraction;         
        FScriptMulticastDelegate OnTeleportTo;                          
        char pad20[0x8];                                          
        UClass *LastCriticalFinisher;                                   
        float TimeoutLookAtCameraDirection;                                   
        char pad21[0x26C];                                        
        UAkComponent *MotionListenerCameraOrientation;                  
        UAkComponent *MotionListenerPlayerOrientation;                  
        AActor *DamageDirectionActor;                                   
        char pad22[0x68];                                 
    };
}
