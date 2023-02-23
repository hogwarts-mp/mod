#include "seasonchanger.h"

#include <logging/logger.h>

::UClass *SDK::SeasonChanger() {
    ::UClass *seasonChanger = (::UClass *)find_uobject("Class /Script/Phoenix.SeasonChanger");
    if (!seasonChanger) {
        return nullptr;
    }
    return seasonChanger;
}

::UFunction *SDK::SeasonChanger_SetCurrentSeason() {
    ::UFunction *setCurrentSeason = (::UFunction *)find_uobject("Function /Script/Phoenix.SeasonChanger.SetCurrentSeason");
    if (!setCurrentSeason) {
        return nullptr;
    }
    return setCurrentSeason;
}

void SDK::SetSeason(ESeasonEnum season) {
    auto seasonChanger = SeasonChanger();
    auto setCurrentSeason = SeasonChanger_SetCurrentSeason();
    if (!seasonChanger || !setCurrentSeason)
        return;

    const std::vector<ESeasonEnum> seasonList{ESeasonEnum::Season_Fall, ESeasonEnum::Season_Winter, ESeasonEnum::Season_Spring, ESeasonEnum::Season_Summer};

    USeasonChanger_SetCurrentSeason_Params params{season};
    seasonChanger->ProcessEvent(setCurrentSeason, &params);
}
