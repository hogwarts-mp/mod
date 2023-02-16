#include <utils/safe_win32.h>

#include <MinHook.h>
#include <logging/logger.h>
#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking.h>

#include <logging/logger.h>

#include "../application.h"

typedef void(__fastcall *FWindowsWindow__Initialize_t)(void *, void *, float **, HINSTANCE, void *, bool);
FWindowsWindow__Initialize_t FWindowsWindow__Initialize_original = nullptr;

typedef void(__fastcall *FWindowsApplication__ProcessMessage_t)(void *, HWND hwnd, uint32_t msg, WPARAM wParam, LPARAM lParam);
FWindowsApplication__ProcessMessage_t FWindowsApplication__ProcessMessage_original = nullptr;

void FWindowsApplication__ProcessMessage_Hook(void* pThis, HWND hwnd, uint32_t msg, WPARAM wParam, LPARAM lParam) {
    FWindowsApplication__ProcessMessage_original(pThis, hwnd, msg, wParam, lParam);
}

void FWindowsWindow__Initialize_Hook(void *pThis, void *app, float **definitions, HINSTANCE inst, void *parent, bool showNow) {
    FWindowsWindow__Initialize_original(pThis, app, definitions, inst, parent, showNow);

    // Acquire the windows and patch the title
    const HWND hWnd = *(HWND*)((DWORD*)pThis+0x28);
    HogwartsMP::Core::gGlobals.window = hWnd;
    SetWindowTextA(hWnd, "Hogwarts: Advanced Multiplayer Edition");

    Framework::Logging::GetLogger("Hooks")->info("Main Window created at {} (show now {})", fmt::ptr(hWnd), showNow ? "yes" : "no");
}

static InitFunction init([]() {
    // Initialize our WindowsWindow Initialize method
    const auto FWindowsWindow__Initialize_Addr = hook::pattern("4C 8B DC 53 55 56 41 54 41 55 41 56").get_first();
    MH_CreateHook((LPVOID)FWindowsWindow__Initialize_Addr, (PBYTE)FWindowsWindow__Initialize_Hook, reinterpret_cast<void **>(&FWindowsWindow__Initialize_original));

    // Initialize our FWindowsApplication ProcessMessage method
    const auto FWindowsApplication__ProcessMessage_Addr = hook::get_opcode_address("E8 ? ? ? ? 48 8B 5C 24 ? 48 8B 6C 24 ? 48 8B 74 24 ? 48 98");
    MH_CreateHook((LPVOID)FWindowsApplication__ProcessMessage_Addr, (PBYTE)FWindowsApplication__ProcessMessage_Hook, reinterpret_cast<void **>(&FWindowsApplication__ProcessMessage_original));
});
