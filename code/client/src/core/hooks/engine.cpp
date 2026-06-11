#include <utils/safe_win32.h>

#include <MinHook.h>
#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking.h>
#include <logging/logger.h>

#include "../application.h"
#include "../aob_scan.h"
#include "../playground.h"

typedef void(__fastcall *EngineTick__Hook_t)(void);
EngineTick__Hook_t EngineTick__Hook_original = nullptr;

// TEMP BOOTSTRAP (2026-06): the FWindowsWindow::Initialize AOB that normally
// kicks the DX12/ImGui hookup is stale on this build, so trigger it from here
// (idempotent). Remove once that render AOB is re-derived.
void EnsureDX12Hooked();

void EngineTick__Hook() {
    EngineTick__Hook_original();
    if (HogwartsMP::Core::gApplication && HogwartsMP::Core::gApplication->IsInitialized()) {
        EnsureDX12Hooked();
        Playground_Tick();
        HogwartsMP::Core::gApplication->Update();
    }
}

static InitFunction init([]() {
    // Initialize our tick method
    const auto EngineTick__Addr = HogwartsMP::Core::AobOpcodeAddr(HogwartsMP::Game::gLayout.engineTick);
    if (EngineTick__Addr) {
        MH_CreateHook((LPVOID)EngineTick__Addr, (PBYTE)EngineTick__Hook, reinterpret_cast<void **>(&EngineTick__Hook_original));
    }
},"Engine");

