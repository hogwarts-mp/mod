#pragma once

#include <cstdint>

#include "apawn.h"
#include "acontroller.h"

#include "../containers/frotator.h"
#include "../containers/tarray.h"
#include "../containers/fvector.h"

namespace SDK {
    class UPlayer;
    class APlayerController: public AController {
       public:
        UPlayer *Player;                                                                    // 02C0 - 02C8                                                 
        APawn *AcknowledgedPawn;                                                            // 02C8 - 02D0                                              
        struct UInterpTrackInstDirector *ControllingDirTrackInst;                           // 02D0 - 02D8
        struct AHUD *MyHUD;                                                                 // 02D8 - 02E0
        struct APlayerCameraManager *PlayerCameraManager;                                   // 02E0 - 02E8
        struct APlayerCameraManager *PlayerCameraManagerClass;                              // 02E8 - 02F0
        bool bAutoManageActiveCameraTarget;                                                 // 02F0 - 02F1
        char pad0[0x3];                                                                     // 02F1 - 02F4
        FRotator TargetViewRotation;                                                        // 02F4 - 0300                                                       
        char pad1[0xc];                                                                     // 0300 - 030C
        float SmoothTargetViewRotationSpeed;                                                // 030C - 0310
        char pad2[0x8];                                                                     // 0310 - 0318
        TArray<AActor *> HiddenActors;                                                      // 0318 - 0320
        TArray<class UPrimitiveComponent> HiddenPrimitiveComponents;                        // 0320 - 0328
        char pad3[0x4];                                                           
        float LastSpectatorStateSynchTime;                                           
        FVector LastSpectatorSyncLocation;                                    
        FRotator LastSpectatorSyncRotation;                                   
        int32_t ClientCap;                                                           
        char pad4[0x4];                                                           
        struct UCheatManager *CheatManager;                                          
        struct UCheatManager *CheatClass;                                            
        struct UPlayerInput *PlayerInput;                                            
        TArray<struct FActiveForceFeedbackEffect> ActiveForceFeedbackEffects; 
        char pad5[0x80];                                                          
        char pad6 : 4;                                                          
        char bPlayerIsWaiting : 1;                                                   
        char pad7 : 3;                                                          
        char pad8[0x3];                                                           
        char NetPlayerIndex;                                                         
        char pad9[0x3b];                                                          
        struct UNetConnection *PendingSwapConnection;                                
        struct UNetConnection *NetConnection;                                        
        char pad10[0xc];                                                           
        float InputYawScale;                                                         
        float InputPitchScale;                                                       
        float InputRollScale;                                                        
        char bShowMouseCursor : 1;                                                   
        char bEnableClickEvents : 1;                                                 
        char bEnableTouchEvents : 1;                                                 
        char bEnableMouseOverEvents : 1;                                             
        char bEnableTouchOverEvents : 1;                                             
        char bForceFeedbackEnabled : 1;                                              
        char pad11 : 2;                                                          
        char pad12[0x3];                                                           
        float ForceFeedbackScale;                                                    
        float HapticFeedbackScale;                                                   
        char pad13[0x4];                                                           
        TArray<struct FKey> ClickEventKeys;                                   
        int MouseAndChannels;
        float HitResultTraceDistance;                        
        uint16_t SeamlessTravelCount;                        
        uint16_t LastCompletedSeamlessTravelCount;           
        char pad14[0x74];                                  
        struct UInputComponent *InactiveStateInputComponent; 
        char pad15: 2;                                  
        char bShouldPerformFullTickWhenPaused : 1;           
        char pad16 : 5;                                  
        char pad17[0x17];                                  
        struct UTouchInterface *CurrentTouchInterface;       
        char pad18[0x50];                                  
        struct ASpectatorPawn *SpectatorPawn;                
        char pad19[0x4];                                   
        bool bIsLocalPlayerController;                       
        char pad20[0x3];                                   
        FVector SpawnLocation;                        
        char pad21[0xc];                                   
    };
}
