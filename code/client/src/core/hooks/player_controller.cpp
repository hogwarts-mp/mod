#include <utils/safe_win32.h>

#include <MinHook.h>
#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking.h>

#include <logging/logger.h>

#include "../aob_scan.h"

enum EndPlayReason {
    /** When the Actor or Component is explicitly destroyed. */
    Destroyed,
    /** When the world is being unloaded for a level transition. */
    LevelTransition,
    /** When the world is being unloaded because PIE is ending. */
    EndPlayInEditor,
    /** When the level it is a member of is streamed out. */
    RemovedFromWorld,
    /** When the application is being exited. */
    Quit,
};

typedef void(__fastcall *APlayerController_BeginPlay_t)(void *);
APlayerController_BeginPlay_t APlayerController_BeginPlay_original = nullptr;
void APlayerController_BeginPlay_Hook(void *pThis) {
    Framework::Logging::GetLogger("Hooks")->info("APlayerController::BeginPlay");
    APlayerController_BeginPlay_original(pThis);
}

typedef void(__fastcall *APlayerController_EndPlay_t)(void *, EndPlayReason);
APlayerController_EndPlay_t APlayerController_EndPlay_original = nullptr;
void APlayerController_EndPlay_Hook(void *pThis, EndPlayReason reason) {
    Framework::Logging::GetLogger("Hooks")->info("APlayerController::EndPlay with reason {}", reason);
    APlayerController_EndPlay_original(pThis, reason);
}

static InitFunction init([]() {
    using HogwartsMP::Core::AobFirst;
    using HogwartsMP::Game::gLayout;

    // Hook player controller begin/end play (optional: logging-only, no
    // consumer yet — wire to a consumer when MP join/leave/respawn lands)
    const auto APlayerController_BeginPlay_Addr = reinterpret_cast<uint64_t>(AobFirst(gLayout.apcBeginPlay));
    if (APlayerController_BeginPlay_Addr) {
        MH_CreateHook((LPVOID)APlayerController_BeginPlay_Addr, (PBYTE)APlayerController_BeginPlay_Hook, reinterpret_cast<void **>(&APlayerController_BeginPlay_original));
    }

    const auto APlayerController_EndPlay_Addr = reinterpret_cast<uint64_t>(AobFirst(gLayout.apcEndPlay));
    if (APlayerController_EndPlay_Addr) {
        MH_CreateHook((LPVOID)APlayerController_EndPlay_Addr, (PBYTE)APlayerController_EndPlay_Hook, reinterpret_cast<void **>(&APlayerController_EndPlay_original));
    }
},"PlayerController");
