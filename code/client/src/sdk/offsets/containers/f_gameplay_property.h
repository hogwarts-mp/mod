#pragma once

#include "../containers/fname.h"
#include "../containers/tarray.h"
#include "../filters/u_mod_filter.h"
#include "../types/ufunction.h"

namespace SDK {
    class FGameplayProperty {
        FName TypeName;                                        
        FName UpdateFunctionName;                              
        UFunction *OnUpdatedCallback;                          
        bool bSkipUpdateOnTick;                                       
        char pad0[0x7];                                             
        TArray<UModFilter *> SupportedOnAddModFilters;  
        TArray<UModFilter *> SupportedOnApplyModFilters;
        char pad1[0x10];                                            
    };
}
