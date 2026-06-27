#pragma once

// APPEND-ONLY: wire mount ids are 1-based indices
// into kMountClasses (id 0 = default broom / unknown). Do NOT reorder/insert. Names are the runtime
// class FNames (asset name + "_C"). The proxy can only spawn an allowlisted broom class.

#include <array>
#include <cstdint>
#include <string_view>

namespace HogwartsMP::Shared::Modules {
    inline constexpr auto kMountClasses = std::to_array<std::string_view>({
        "BP_FlyingBroomCapsule_C",
        "BP_FlyingBroomCapsule_Aeromancer_C",
        "BP_FlyingBroomCapsule_BrightSpark_C",
        "BP_FlyingBroomCapsule_DarkWizard1_C",
        "BP_FlyingBroomCapsule_DarkWizard2_C",
        "BP_FlyingBroomCapsule_EmberDash_C",
        "BP_FlyingBroomCapsule_FamilyAntique_C",
        "BP_FlyingBroomCapsule_FlyingClass1_C",
        "BP_FlyingBroomCapsule_FlyingClass2_C",
        "BP_FlyingBroomCapsule_FlyingClass3_C",
        "BP_FlyingBroomCapsule_House_C",
        "BP_FlyingBroomCapsule_LicketySwift_C",
        "BP_FlyingBroomCapsule_MoonTrimmer_C",
        "BP_FlyingBroomCapsule_NightDancer_C",
        "BP_FlyingBroomCapsule_SilverArrow_C",
        "BP_FlyingBroomCapsule_SkyScythe_C",
        "BP_FlyingBroomCapsule_WildFire_C",
        "BP_FlyingBroomCapsule_WindWisp_C",
        "BP_FlyingBroomCapsule_YewWeaver_C",
    });

    // Runtime broom class FName -> 1-based wire id (0 if not allowlisted / default broom).
    inline uint8_t MountClassId(std::string_view className) {
        if (className.empty()) {
            return 0;
        }
        for (std::size_t i = 0; i < kMountClasses.size(); ++i) {
            if (kMountClasses[i] == className) {
                return static_cast<uint8_t>(i + 1);
            }
        }
        return 0;
    }

    // Wire id -> broom class FName ("" for 0/out of range = default broom; null-terminated literal,
    // safe to pass to the C-string FindResidentClassByName).
    inline const char *MountClassName(uint8_t id) {
        return (id >= 1 && id <= kMountClasses.size()) ? kMountClasses[id - 1].data() : "";
    }
} // namespace HogwartsMP::Shared::Modules
