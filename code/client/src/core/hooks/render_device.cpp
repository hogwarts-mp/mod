#include <utils/safe_win32.h>

#include <MinHook.h>
#include <logging/logger.h>
#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking.h>

#include <logging/logger.h>

#include "../application.h"

class FD3D12Adapter {
  public:
    char pad0[0x18];
    ID3D12Device *m_pDevice;
};

class FDWindowsWindow {
  public:
    char pad0[0x28];
    HWND m_pMainWindow;
};

typedef void(__fastcall *FWindowsWindow__Initialize_t)(FDWindowsWindow *, void *, float **, HINSTANCE, void *, bool);
FWindowsWindow__Initialize_t FWindowsWindow__Initialize_original = nullptr;

typedef void(__fastcall *FWindowsApplication__ProcessMessage_t)(void *, HWND hwnd, uint32_t msg, WPARAM wParam, LPARAM lParam);
FWindowsApplication__ProcessMessage_t FWindowsApplication__ProcessMessage_original = nullptr;

typedef void(__fastcall *FD3D12Adapter__CreateRootdevice_t)(FD3D12Adapter *, bool);
FD3D12Adapter__CreateRootdevice_t FD3D12Adapter__CreateRootdevice_original = nullptr;

void FWindowsApplication__ProcessMessage_Hook(void* pThis, HWND hwnd, uint32_t msg, WPARAM wParam, LPARAM lParam) {
    const auto app = HogwartsMP::Core::gApplication.get();
    if (app && app->IsInitialized()) {
        app->GetInput()->ProcessEvent(hwnd, msg, wParam, lParam);

        if (app->GetImGUI()->ProcessEvent(hwnd, msg, wParam, lParam) == Framework::External::ImGUI::InputState::BLOCK) {
            return;
        }
    }
    FWindowsApplication__ProcessMessage_original(pThis, hwnd, msg, wParam, lParam);
}

void FWindowsWindow__Initialize_Hook(FDWindowsWindow *pThis, void *app, float **definitions, HINSTANCE inst, void *parent, bool showNow) {
    FWindowsWindow__Initialize_original(pThis, app, definitions, inst, parent, showNow);

    // Acquire the windows and patch the title
    HogwartsMP::Core::gGlobals.window = pThis->m_pMainWindow;
    SetWindowTextA(pThis->m_pMainWindow, "Hogwarts: Advanced Multiplayer Edition");

    Framework::Logging::GetLogger("Hooks")->info("Main Window created (show now {}) = {}", showNow ? "yes" : "no", fmt::ptr(pThis->m_pMainWindow));
}

void FD3D12Adapter__CreateRootdevice_Hook(FD3D12Adapter *pThis, bool withDebug) {
    FD3D12Adapter__CreateRootdevice_original(pThis, withDebug);
    Framework::Logging::GetLogger("Hooks")->info("D3D12 RootDevice created (with debug {}) = {}", withDebug ? "yes" : "no", fmt::ptr(pThis->m_pDevice));
}

static InitFunction init([]() {
    // Initialize our WindowsWindow Initialize method
    const auto FWindowsWindow__Initialize_Addr = hook::pattern("4C 8B DC 53 55 56 41 54 41 55 41 56").get_first();
    MH_CreateHook((LPVOID)FWindowsWindow__Initialize_Addr, (PBYTE)FWindowsWindow__Initialize_Hook, reinterpret_cast<void **>(&FWindowsWindow__Initialize_original));

    // Initialize our FWindowsApplication ProcessMessage method
    const auto FWindowsApplication__ProcessMessage_Addr = hook::get_opcode_address("E8 ? ? ? ? 48 8B 5C 24 ? 48 8B 6C 24 ? 48 8B 74 24 ? 48 98");
    MH_CreateHook((LPVOID)FWindowsApplication__ProcessMessage_Addr, (PBYTE)FWindowsApplication__ProcessMessage_Hook, reinterpret_cast<void **>(&FWindowsApplication__ProcessMessage_original));

    const auto FD3D12Adapter__CreateRootDevice_Addr = hook::pattern("48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 44 0F B6 FA").get_first();
    MH_CreateHook((LPVOID)FD3D12Adapter__CreateRootDevice_Addr, (PBYTE)FD3D12Adapter__CreateRootdevice_Hook, reinterpret_cast<void **>(&FD3D12Adapter__CreateRootdevice_original));
});
