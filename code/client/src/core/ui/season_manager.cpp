#include "season_manager.h"

namespace HogwartsMP::Core::UI {
    void SeasonManager::SetSeason(SDK::ESeasonEnum season) {
        SDK::SetSeason(season);
    }

    void SeasonManager::SetRandomSeason() {
        const std::vector<SDK::ESeasonEnum> seasonList{SDK::ESeasonEnum::Season_Fall, SDK::ESeasonEnum::Season_Winter, SDK::ESeasonEnum::Season_Spring, SDK::ESeasonEnum::Season_Summer};
        const int index = rand() % seasonList.size();
        SetSeason(seasonList[index]);
    }

    void SeasonManager::AdvanceHours(int hours) {
        SDK::AdvanceHours(hours);
    }
} // namespace HogwartsMP::Core::UI
