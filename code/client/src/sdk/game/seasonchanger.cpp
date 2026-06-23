#include "seasonchanger.h"

#include "sdk/reflection/ue4_reflection.h"

using HogwartsMP::Core::UE4::FindUClass;
using HogwartsMP::Core::UE4::FindUFunction;
using HogwartsMP::Core::UE4::FindUObjects;

void SDK::SetSeason(ESeasonEnum season) {
    auto *seasonChanger   = FindUClass("Class /Script/Phoenix.SeasonChanger");
    auto *setCurrentSeason = FindUFunction("Function /Script/Phoenix.SeasonChanger.SetCurrentSeason");
    if (!seasonChanger || !setCurrentSeason)
        return;

    USeasonChanger_SetCurrentSeason_Params params{season};
    seasonChanger->ProcessEvent(setCurrentSeason, &params);
}

void SDK::AdvanceHours(int hours) {
    auto *advanceHours = FindUFunction("Function /Script/GameScheduler.Scheduler.AdvanceHours");
    if (!advanceHours)
        return;

    // Every active Scheduler instance gets advanced.
    for (auto *scheduler : FindUObjects("Class /Script/GameScheduler.Scheduler")) {
        UScheduler_AdvanceHours_Params params{hours};
        static_cast<UObject *>(scheduler)->ProcessEvent(advanceHours, &params);
    }
}
