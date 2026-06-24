#pragma once

#include "../containers/tarray.h"
#include "../game/uworld.h"
#include "../game/ugameinstance.h"
#include "uscriptviewportclient.h"

namespace SDK {
    class UGameViewportClient: public UScriptViewportClient {
      public:
        unsigned char pad0[0x8];                 
        class UConsole *ViewportConsole;                     
        TArray<struct FDebugDisplayProperty> DebugProperties;
        unsigned char pad1[0x10];                
        int32_t MaxSplitscreenPlayers;                       
        unsigned char pad2[0xC];                 
        UWorld *World;                                 
        UGameInstance *GameInstance;                   
        unsigned char pad3[0x2D8];               
    };
}
