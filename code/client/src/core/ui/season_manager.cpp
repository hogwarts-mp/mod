
#include "season_manager.h"

#include "core/application.h"

#include <imgui/imgui.h>
#include <integrations/shared/modules/mod.hpp>

#include <utils/string_utils.h>
#include <logging/logger.h>
#include <shared/modules/mod.hpp>

const std::vector<HogwartsMP::Shared::Modules::Mod::SeasonKind> seasons = {
    HogwartsMP::Shared::Modules::Mod::SEASON_SPRING,
    HogwartsMP::Shared::Modules::Mod::SEASON_SUMMER,
    HogwartsMP::Shared::Modules::Mod::SEASON_AUTUMN,
    HogwartsMP::Shared::Modules::Mod::SEASON_WINTER
};

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

    void SeasonManager::SetRandomSeason() {
        UClass* seasonChanger = (UClass*)find_uobject("Class /Script/Phoenix.SeasonChanger");
        if (!seasonChanger) {
            Framework::Logging::GetLogger("SeasonManager")->error("Set Season failed: {}", "SeasonChanger Class");
            return;
        }

        UFunction *setCurrentSeason = (UFunction *)find_uobject("Function /Script/Phoenix.SeasonChanger.SetCurrentSeason");
        if (!setCurrentSeason) {
            Framework::Logging::GetLogger("SeasonManager")->error("Set Season failed: {}", "SetCurrentSeason Function");
            return;
        }

        const std::vector<ESeasonEnum> seasonList{ESeasonEnum::Season_Fall, ESeasonEnum::Season_Winter, ESeasonEnum::Season_Spring, ESeasonEnum::Season_Summer};

        const int index = rand() % seasonList.size();

        USeasonChanger_SetCurrentSeason_Params params{seasonList[index]};
        seasonChanger->ProcessEvent(setCurrentSeason, &params);

        Framework::Logging::GetLogger("SeasonManager")->info("Set Season to {} !", seasonList[index]);
    }
} // namespace HogwartsMP::Core::UI
