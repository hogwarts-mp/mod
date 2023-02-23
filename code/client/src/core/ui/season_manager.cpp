#include "season_manager.h"

#include "core/application.h"

#include <imgui/imgui.h>
#include <integrations/shared/modules/mod.hpp>

#include <utils/string_utils.h>
#include <logging/logger.h>
#include <shared/modules/mod.hpp>

// todo move to sdk
UObjectBase *find_uobject(const char *obj_full_name);

namespace HogwartsMP::Core::UI {
    void SeasonManager::Update() {
        static int selectedSeason = Shared::Modules::Mod::SEASON_WINTER;

        // imgui window with listbox
        ImGui::SetNextWindowSize(ImVec2(470, 240), ImGuiCond_FirstUseEver);
        ImGui::Begin("Season Manager");
        {
            if (ImGui::Button("Random Season")) {
                SetRandomSeason();
            }
            // ImGui::ListBox("Locations", &selectedSeason, seasons, IM_ARRAYSIZE(seasons), 5);
            // if (ImGui::Button("Change Season")) {
            //     auto season = seasons[selectedSeason];
            //     SetRandomSeason(season);
            // }
        }
        ImGui::End();
    }

    void SeasonManager::SetSeason(SDK::ESeasonEnum season) {
        SDK::SetSeason(season);
    }

    void SeasonManager::SetRandomSeason() {
        const std::vector<SDK::ESeasonEnum> seasonList{SDK::ESeasonEnum::Season_Fall, SDK::ESeasonEnum::Season_Winter, SDK::ESeasonEnum::Season_Spring, SDK::ESeasonEnum::Season_Summer};
        const int index = rand() % seasonList.size();
        SetSeason(seasonList[index]);
    }
} // namespace HogwartsMP::Core::UI
