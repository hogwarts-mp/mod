#include <utils/safe_win32.h>

#include <MinHook.h>
#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking.h>

#include "../application.h"

typedef void(__fastcall *APlayerController_BeginPlay_t)(void *);
APlayerController_BeginPlay_t APlayerController_BeginPlay_original = nullptr;
void APlayerController_BeginPlay_Hook(void *pThis) {
    APlayerController_BeginPlay_original(pThis);
}

typedef void(__fastcall *APlayerController_TickPlayer_t)(void *, float, int, void*);
APlayerController_TickPlayer_t APlayerController_TickPlayer_original = nullptr;
void APlayerController_TickPlayer_Hook(void *pThis, float deltaSeconds, int tickType, void *callbackFnc) {
    APlayerController_TickPlayer_original(pThis, deltaSeconds, tickType, callbackFnc);

    if (!HogwartsMP::Core::gApplication || !HogwartsMP::Core::gApplication->IsInitialized()) {
        return;
    }

    HogwartsMP::Core::gApplication->Update();
}

static InitFunction init([]() {
    // Hook player controller begin play function
    // const auto APlayerController_BeginPlay_Addr = reinterpret_cast<uint64_t>(hook::pattern("40 56 48 83 EC 40 48 89 7C 24 58 48 8B F1 E8").get_first());
    // MH_CreateHook((LPVOID)APlayerController_BeginPlay_Addr, (PBYTE)APlayerController_BeginPlay_Hook, reinterpret_cast<void **>(&APlayerController_BeginPlay_original));

    // Hook player controller tick player function
    // const auto APlayerController_TickPlayer_Addr = reinterpret_cast<uint64_t>(hook::pattern("40 53 55 57 48 81 EC A0 00 00 00 48 8B F9 44 0F").get_first());
    // MH_CreateHook((LPVOID)APlayerController_TickPlayer_Addr, (PBYTE)APlayerController_TickPlayer_Hook, reinterpret_cast<void **>(&APlayerController_TickPlayer_original));
});
