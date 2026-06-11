#include <utils/safe_win32.h>

#include <MinHook.h>
#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking.h>

#include <logging/logger.h>

#include "../aob_scan.h"

class FObjectInitializer;
typedef void *(__fastcall *ULocalPlayer__ULocalPlayer_t)(void *, const FObjectInitializer&);
ULocalPlayer__ULocalPlayer_t ULocalPlayer__ULocalPlayer_original = nullptr;
void *ULocalPlayer__ULocalPlayer(void *pThis, const FObjectInitializer &objectInitialiser) {
    Framework::Logging::GetLogger("Hooks")->debug("ULocalPlayer::ULocalPlayer ({})", fmt::ptr(pThis));
    return ULocalPlayer__ULocalPlayer_original(pThis, objectInitialiser);
}

typedef void *(__fastcall *APlayerController__APlayerController_t)(void *, const FObjectInitializer &);
APlayerController__APlayerController_t APlayerController__APlayerController_original = nullptr;
void *APlayerController__APlayerController(void *pThis, const FObjectInitializer &objInit) {
    Framework::Logging::GetLogger("Hooks")->debug("APlayerController::APlayerController ({})", fmt::ptr(pThis));
    return APlayerController__APlayerController_original(pThis, objInit);
}

static InitFunction init([]() {
    using HogwartsMP::Core::AobOpcodeAddr;
    using HogwartsMP::Game::gLayout;

    // Hook local player / player controller constructors (optional:
    // logging-only, no consumer — local player is found via the GWorld chain)
    const auto ULocalPlayer__ULocalPlayer_Addr = AobOpcodeAddr(gLayout.ulocalplayerCtor);
    if (ULocalPlayer__ULocalPlayer_Addr) {
        MH_CreateHook((LPVOID)ULocalPlayer__ULocalPlayer_Addr, (PBYTE)ULocalPlayer__ULocalPlayer, reinterpret_cast<void **>(&ULocalPlayer__ULocalPlayer_original));
    }

    const auto APlayerController__APlayerController_Addr = AobOpcodeAddr(gLayout.apcCtor);
    if (APlayerController__APlayerController_Addr) {
        MH_CreateHook((LPVOID)APlayerController__APlayerController_Addr, (PBYTE)APlayerController__APlayerController, reinterpret_cast<void **>(&APlayerController__APlayerController_original));
    }
},"LocalPlayer");
