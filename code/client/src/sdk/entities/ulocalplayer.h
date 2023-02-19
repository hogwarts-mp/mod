#pragma once

#include "uplayer.h"
#include "aplayercontroller.h"

namespace SDK {
    class ULocalPlayer: public UPlayer {
      public:
        char pad0[0x28];                                           
        class UGameViewportClient *ViewportClient;    
        char pad1[0x1c];
        char AspectRatioAxisConstraint[0x01];                        
        char pad2[0x3];                                            
        APlayerController *PendingLevelPlayerControllerClass; 
        char bSentSplitJoin : 1;                                     
        char pad3 : 7;                                           
        char pad4[0x17];                                           
        int32_t ControllerId = -1;                                   
        char pad5[0x19c];                                          
    };
}
