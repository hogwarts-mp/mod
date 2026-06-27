// On-foot locomotion clip selection for remote-avatar proxies — see proxy_locomotion.h.

#include <utils/safe_win32.h>

#include <logging/logger.h>

#include "proxy_locomotion.h"
#include "sdk/reflection/ue4_reflection.h"

#include <algorithm>
#include <unordered_map>

namespace {
    using namespace HogwartsMP::Core::UE4;
    using HogwartsMP::Core::ProxyLocomotion::Gait;

    // Runtime object path per gait. All from the player Hu_BM_* family (the base biped skeleton —
    // master-poses onto the student outfit). Idle must NOT be a gendered student clip (StuF/StuM): the
    // proxy can be either gender, and a mismatched student idle fails to retarget → A-pose. The Hu_BM_*
    // clips retarget onto both.
    const std::unordered_map<Gait, const wchar_t *> GAIT_PATHS = {
        {Gait::Idle, L"/Game/Animation/Human/Hu_BM_Idle_Casual_Loop_anm.Hu_BM_Idle_Casual_Loop_anm"},
        {Gait::Walk, L"/Game/Animation/Human/Hu_BM_Walk_Loop_Fwd_anm.Hu_BM_Walk_Loop_Fwd_anm"},
        {Gait::Run, L"/Game/Animation/Human/Hu_BM_Jog_Loop_Fwd_anm.Hu_BM_Jog_Loop_Fwd_anm"},
        {Gait::Sprint, L"/Game/Animation/Human/Hu_BM_Sprint_Loop_Fwd_anm.Hu_BM_Sprint_Loop_Fwd_anm"},
    };
    const std::unordered_map<Gait, const char *> GAIT_NAMES = {
        {Gait::Idle, "idle"}, {Gait::Walk, "walk"}, {Gait::Run, "run"}, {Gait::Sprint, "sprint"},
    };

    const wchar_t *AIR_PATH = L"/Game/Animation/Human/Hu_BM_Fall_Loop_v2_anm.Hu_BM_Fall_Loop_v2_anm";

    // 1D forward-locomotion blendspace (walk->jog->run by speed) on the Hu_BM base-biped skeleton.
    const wchar_t *MOVE_BLENDSPACE_PATH = L"/Game/Animation/Human/Hu_BM_MoveLoopFwd_Blendspace.Hu_BM_MoveLoopFwd_Blendspace";

    // Representative speed per gait (UE cm/s), measured off the local player's GetVelocity at each gait.
    // GaitForSpeed derives its band edges from the midpoints of these.
    const std::unordered_map<Gait, float> GAIT_SPEED = {
        {Gait::Idle, 0.f}, {Gait::Walk, 150.f}, {Gait::Run, 475.f}, {Gait::Sprint, 700.f},
    };

    // Crossfade window between locomotion assets (idle clip <-> move blendspace <-> fall clip).
    constexpr float kBlendInSec = 0.2f;
} // namespace

namespace HogwartsMP::Core::ProxyLocomotion {
    float SpeedForGait(Gait gait) {
        return GAIT_SPEED.at(gait);
    }

    Gait GaitForSpeed(float speedCmPerSec, Gait current) {
        // Band boundaries at the midpoints between adjacent GAIT_SPEED entries; the idle/walk boundary is
        // a hard 40 cm/s floor so a barely-moving avatar still reads as walking and the stopped-decay
        // (which drives speed to 0) can always reach idle.
        if (speedCmPerSec < 40.f) {
            return Gait::Idle;
        }
        const float bWalkRun   = (GAIT_SPEED.at(Gait::Walk) + GAIT_SPEED.at(Gait::Run)) * 0.5f;
        const float bRunSprint = (GAIT_SPEED.at(Gait::Run) + GAIT_SPEED.at(Gait::Sprint)) * 0.5f;
        constexpr float kHys   = 50.f; // sticky hysteresis — don't flip-flop on a boundary
        int g                  = std::max(static_cast<int>(current), static_cast<int>(Gait::Walk));
        if (g == static_cast<int>(Gait::Walk) && speedCmPerSec > bWalkRun + kHys) {
            g = static_cast<int>(Gait::Run);
        }
        if (g == static_cast<int>(Gait::Run) && speedCmPerSec > bRunSprint + kHys) {
            g = static_cast<int>(Gait::Sprint);
        }
        if (g == static_cast<int>(Gait::Sprint) && speedCmPerSec < bRunSprint - kHys) {
            g = static_cast<int>(Gait::Run);
        }
        if (g == static_cast<int>(Gait::Run) && speedCmPerSec < bWalkRun - kHys) {
            g = static_cast<int>(Gait::Walk);
        }
        return static_cast<Gait>(g);
    }

    const char *Name(Gait gait) {
        return GAIT_NAMES.at(gait);
    }

    void PlayGait(UObjectBase *skinComp, Gait gait) {
        auto *seq = LoadAnimSequence(GAIT_PATHS.at(gait));
        if (!seq) {
            Framework::Logging::GetLogger("Loco")->warn("gait {} clip failed to load", GAIT_NAMES.at(gait));
            return;
        }
        PlayAnimBlended(skinComp, seq, true, kBlendInSec);
    }

    void PlayAir(UObjectBase *skinComp) {
        auto *seq = LoadAnimSequence(AIR_PATH);
        if (!seq) {
            Framework::Logging::GetLogger("Loco")->warn("in-air clip failed to load");
            return;
        }
        PlayAnimBlended(skinComp, seq, true, kBlendInSec);
    }

    bool PlayMoveBlend(UObjectBase *skinComp) {
        if (!skinComp) {
            return false;
        }
        auto *bs = LoadAnimAsset(MOVE_BLENDSPACE_PATH);
        if (!bs) {
            Framework::Logging::GetLogger("Loco")->warn("move blendspace failed to load");
            return false;
        }
        PlayAnimBlended(skinComp, bs, true, kBlendInSec);
        return true;
    }

    bool DriveMoveBlend(UObjectBase *skinComp, float speedCmPerSec) {
        return SetBlendSpaceInputOnSkin(skinComp, speedCmPerSec, 0.f); // 1D: speed on X
    }
} // namespace HogwartsMP::Core::ProxyLocomotion
