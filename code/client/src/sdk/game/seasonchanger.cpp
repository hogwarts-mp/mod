#include "seasonchanger.h"
#include <UObject/UObjectBase.h>

UObjectBase *find_uobject(const char *obj_full_name);
std::vector<UObjectBase *> find_uobjects(const char *obj_full_name);

SDK::UClass *SDK::SeasonChanger() {
    SDK::UClass *seasonChanger = reinterpret_cast<SDK::UClass *>(find_uobject("Class /Script/Phoenix.SeasonChanger"));
    if (!seasonChanger) {
        return nullptr;
    }
    return seasonChanger;
}

SDK::UFunction *SDK::SeasonChanger_SetCurrentSeason() {
    SDK::UFunction *setCurrentSeason = reinterpret_cast<SDK::UFunction *>(find_uobject("Function /Script/Phoenix.SeasonChanger.SetCurrentSeason"));
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

SDK::UClass *SDK::UScheduler() {
    auto schedulers = find_uobjects("Class /Script/GameScheduler.Scheduler");
    if (schedulers.empty()) {
        return nullptr;
    }
    return (SDK::UClass *)schedulers.back();
}

SDK::UFunction *SDK::UScheduler_AdvanceHours() {
    SDK::UFunction *fn = (SDK::UFunction *)find_uobject("Function /Script/GameScheduler.Scheduler.AdvanceHours");
    if (!fn) {
        return nullptr;
    }
    return fn;
}

void SDK::AdvanceHours(int hours) {
    auto scheduler = UScheduler();
    auto advanceHours = UScheduler_AdvanceHours();
    if (scheduler || !advanceHours)
        return;

    UScheduler_AdvanceHours_Params params{hours};
    scheduler->ProcessEvent(advanceHours, &params);
}
