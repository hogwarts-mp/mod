#include <utils/safe_win32.h>

#include <MinHook.h>
#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking.h>

#include <logging/logger.h>

#include "../application.h"

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

typedef void(__fastcall *APlayerController_TickActor_t)(void *, float, int, void *);
APlayerController_TickActor_t APlayerController_TickActor_original = nullptr;
void APlayerController_TickActor_Hook(void *pThis, float deltaSeconds, int tickType, void *callbackFnc) {
    APlayerController_TickActor_original(pThis, deltaSeconds, tickType, callbackFnc);

    if (!HogwartsMP::Core::gApplication || !HogwartsMP::Core::gApplication->IsInitialized()) {
        return;
    }

    HogwartsMP::Core::gApplication->Update();
}

static InitFunction init([]() {
    // Hook player controller begin play function
    const auto APlayerController_BeginPlay_Addr = reinterpret_cast<uint64_t>(hook::pattern("40 56 48 83 EC 40 48 89 7C 24 ?").get_first());
    MH_CreateHook((LPVOID)APlayerController_BeginPlay_Addr, (PBYTE)APlayerController_BeginPlay_Hook, reinterpret_cast<void **>(&APlayerController_BeginPlay_original));

    // Hook player controller begin play function
    const auto APlayerController_EndPlay_Addr = reinterpret_cast<uint64_t>(hook::pattern("48 89 5C 24 ? 48 89 74 24 ? 57 48 83 EC 30 48 8B B9 ? ? ? ? 8B F2").get_first());
    MH_CreateHook((LPVOID)APlayerController_EndPlay_Addr, (PBYTE)APlayerController_EndPlay_Hook, reinterpret_cast<void **>(&APlayerController_EndPlay_original));

    // Hook player controller tick player function
    const auto APlayerController_TickActor_Addr = reinterpret_cast<uint64_t>(hook::pattern("40 53 55 57 48 81 EC ? ? ? ? 48 8B F9").get_first());
    MH_CreateHook((LPVOID)APlayerController_TickActor_Addr, (PBYTE)APlayerController_TickActor_Hook, reinterpret_cast<void **>(&APlayerController_TickActor_original));
});
