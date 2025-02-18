#include <utils/safe_win32.h>

#include <MinHook.h>
#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking.h>

#include <logging/logger.h>

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
    // Hook local player constructor
    const auto ULocalPlayer__ULocalPlayer_Addr = hook::get_opcode_address("E9 ? ? ? ? C3 66 66 66 2E 0F 1F 84 00 00 00 00 00 48 8D 64 24 D8 41 54 F7 1C 24");
    MH_CreateHook((LPVOID)ULocalPlayer__ULocalPlayer_Addr, (PBYTE)ULocalPlayer__ULocalPlayer, reinterpret_cast<void **>(&ULocalPlayer__ULocalPlayer_original));

    // Hook local player constructor
    const auto APlayerController__APlayerController_Addr = hook::get_opcode_address("E9 ? ? ? ? C3 85 C0 3C 88");
    MH_CreateHook((LPVOID)APlayerController__APlayerController_Addr, (PBYTE)APlayerController__APlayerController, reinterpret_cast<void **>(&APlayerController__APlayerController_original));
},"LocalPlayer");
