#pragma once

#include <vector>

#include <sdk/types/uclass.h>

namespace SDK {
    class UClass;
    class UFunction;
    class UObjectBase;

    enum class ESeasonEnum : unsigned char {
        Season_Invalid = 0,
        Season_Fall = 1,
        Season_Winter = 2,
        Season_Spring = 3,
        Season_Summer = 4,
        Season_MAX = 5
    };

    struct USeasonChanger_SetCurrentSeason_Params {
    public:
        ESeasonEnum NewSeason;
    };

    UClass *SeasonChanger();
    UFunction *SeasonChanger_SetCurrentSeason();
    void SetSeason(ESeasonEnum season);

    std::vector<SDK::UClass *> USchedulers(); 
    UFunction *UScheduler_AdvanceHours(); 
 
    struct UScheduler_AdvanceHours_Params 
    { 
    public: 
        int InHours; 
    }; 
 
    void AdvanceHours(int hours); 
}
