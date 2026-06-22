#include "teleport_manager.h"

#include "core/teleport.h"

#include <imgui/imgui.h>

#include <algorithm>
#include <vector>

namespace HogwartsMP::Core::UI {
    void TeleportManager::Update() {
        static int selectedLocation = 6; // default FT_CentralHogsmeade

        // const char* view of the shared destination list, built once for ImGui's ListBox. The backing
        // strings live for the process (static in FastTravelLocations), so the pointers stay valid.
        static const std::vector<const char *> items = [] {
            std::vector<const char *> v;
            auto fastTravelLocations = HogwartsMP::Core::FastTravelLocations();
            v.resize(fastTravelLocations.size());
            std::ranges::transform(fastTravelLocations, v.begin(), [](const auto sv) {
                return sv.data();
            });
            return v;
        }();

        ImGui::SetNextWindowSize(ImVec2(470, 240), ImGuiCond_FirstUseEver);
        ImGui::Begin("Teleport manager");
        {
            ImGui::ListBox("Locations", &selectedLocation, items.data(), static_cast<int>(items.size()), 10);
            if (ImGui::Button("Teleport")) {
                TeleportTo(items[selectedLocation]);
            }
        }
        ImGui::End();
    }

    void TeleportManager::TeleportTo(const std::string &name) {
        HogwartsMP::Core::FastTravelTo(name);
    }
} // namespace HogwartsMP::Core::UI
