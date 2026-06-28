#include <utils/safe_win32.h>

#include <MinHook.h>
#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking.h>

#include <logging/logger.h>

#include <atomic>
#include <cstdint>

#include "../aob_scan.h"
#include "population.h"

// The whole tiered ambient crowd (BP_Tier3_Character_C / Tier4_Actor / students) is driven by
// UPopulationManager's per-frame tick, FUN_14316dd90, which ticks its 4 async population jobs.
// void __fastcall(UPopulationManager* this, FTickCtx*). Address via the game_layout AOB, RVA fallback.

namespace HogwartsMP::Core::Hooks {
    namespace {
        constexpr uintptr_t kPopTick_RVA = 0x316DD90; // FUN_14316dd90 fallback (Steam build 20773316)

        using PopTick_t = void(__fastcall *)(void *, void *);
        PopTick_t s_orig = nullptr;

        // Default OFF (no-op the tick from boot) => areas load EMPTY.
        // Server owners can flip to ON for vanilla via SetAmbientPopulation.
        std::atomic<bool> s_enabled{false}; // true = vanilla population; false = no ambient NPCs

        auto Log() {
            return Framework::Logging::GetLogger("NpcLab");
        }

        void __fastcall PopTick_Hook(void *mgr, void *tickCtx) {
            if (!s_enabled.load(std::memory_order_relaxed)) {
                return; // skip the population tick entirely -> no flesh
            }
            s_orig(mgr, tickCtx);
        }
    } // namespace

    void SetAmbientPopulation(bool enabled) {
        s_enabled.store(enabled, std::memory_order_relaxed);
        Log()->info("AmbientPopulation: {}", enabled ? "ON (vanilla NPCs)" : "OFF (no ambient NPCs)");
    }

    void ToggleAmbientPopulation() {
        SetAmbientPopulation(!s_enabled.load(std::memory_order_relaxed));
    }

    bool IsAmbientPopulationEnabled() {
        return s_enabled.load(std::memory_order_relaxed);
    }
} // namespace HogwartsMP::Core::Hooks

static InitFunction init([]() {
    using namespace HogwartsMP::Core::Hooks;
    auto *target = HogwartsMP::Core::AobFirst(HogwartsMP::Game::gLayout.populationManagerTick);
    if (!target) {
        // AOB miss/ambiguous — fall back to the pinned RVA for the known build.
        target = reinterpret_cast<void *>(reinterpret_cast<uintptr_t>(GetModuleHandle(nullptr)) + kPopTick_RVA);
    }
    const MH_STATUS st = MH_CreateHook(target, reinterpret_cast<LPVOID>(&PopTick_Hook), reinterpret_cast<void **>(&s_orig));
    Framework::Logging::GetLogger("NpcLab")->info("AmbientPopulation: hooked PopulationManagerTick @ 0x{:x} status {} (default {})",
        reinterpret_cast<uintptr_t>(target), static_cast<int>(st), IsAmbientPopulationEnabled() ? "ON" : "OFF");
}, "AmbientPopulation");
