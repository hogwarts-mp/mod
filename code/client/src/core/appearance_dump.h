#pragma once

namespace HogwartsMP::Core::AppearanceDump {
    // Thread-safe request setter; the actual dump happens on the next engine
    // tick via ProcessPending (game thread required: GObjects walk and
    // ProcessEvent are not safe off-thread).
    void RequestDump();

    // Game-thread pump — Playground_Tick calls this once per engine tick.
    void ProcessPending();
} // namespace HogwartsMP::Core::AppearanceDump
