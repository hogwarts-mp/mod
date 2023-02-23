#pragma once

#include "utils/safe_win32.h"
#include <vector>
#include <sdk/Runtime/CoreUObject/Public/UObject/Class.h>

class UClass;
class UFunction;
class UObjectBase;

UObjectBase *find_uobject(const char *obj_full_name);

namespace SDK {
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

    ::UClass *SeasonChanger();
    ::UFunction *SeasonChanger_SetCurrentSeason();
    void SetSeason(ESeasonEnum season);
}
