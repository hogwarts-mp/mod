#include <utils/safe_win32.h>

#include <MinHook.h>
#include <logging/logger.h>
#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking.h>

#include <logging/logger.h>

#include "../application.h"

typedef void(__fastcall *FWindowsWindow__Initialize_t)(void *, void *, float **, HINSTANCE, void *, bool);
FWindowsWindow__Initialize_t FWindowsWindow__Initialize_original = nullptr;

void FWindowsWindow__Initialize_Hook(void *pThis, void *app, float **definitions, HINSTANCE inst, void *parent, bool showNow) {
    FWindowsWindow__Initialize_original(pThis, app, definitions, inst, parent, showNow);
    Framework::Logging::GetLogger("Hooks")->info("Main Window created");
}

static InitFunction init([]() {
    // Initialize our WindowsWindow Initialize method
    const auto FWindowsWindow__Initialize_Addr = hook::pattern("4C 8B DC 53 55 56 41 54 41 55 41 56").get_first();
    MH_CreateHook((LPVOID)FWindowsWindow__Initialize_Addr, (PBYTE)FWindowsWindow__Initialize_Hook, reinterpret_cast<void **>(&FWindowsWindow__Initialize_original));
});
