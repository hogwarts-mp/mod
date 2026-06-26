#pragma once

#include "shared/modules/appearance.hpp"

namespace HogwartsMP::Core::AppearanceDump {
    // Read the local player's CustomizableCharacterComponent -> CacheCCD into the portable CcdProfile
    // (gender/scale/bone-scales + CharacterItems + Outfits). Game thread only; false if no pawn/CCC/CCD.
    bool BuildLocalCcd(Shared::Modules::CcdProfile &out);

    // Thread-safe request setter; the actual dump happens on the next engine
    // tick via ProcessPending (game thread required: GObjects walk and
    // ProcessEvent are not safe off-thread).
    void RequestDump();

    // Game-thread pump — Playground_Tick calls this once per engine tick.
    void ProcessPending();
} // namespace HogwartsMP::Core::AppearanceDump
