#include <utils/safe_win32.h>

#include <MinHook.h>
#include <logging/logger.h>
#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking.h>

#include <logging/logger.h>

#include "../application.h"
#include "../dx_test.h"

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

class FRHICommandListImmediate;
typedef void(__fastcall *FEngineLoop__BeginFrameRenderThread_t)(void *, FRHICommandListImmediate&, uint64_t);
FEngineLoop__BeginFrameRenderThread_t FEngineLoop__BeginFrameRenderThread_original = nullptr;

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

    // prepare the data
    auto opts = HogwartsMP::Core::gApplication->GetOptions();
    opts->rendererOptions.d3d12.device = HogwartsMP::Core::gGlobals.device;
    opts->rendererOptions.windowHandle = HogwartsMP::Core::gGlobals.window;

    SetWindowTextA(pThis->m_pMainWindow, "Hogwarts: Advanced Multiplayer Edition");

    if (HogwartsMP::Core::gApplication->RenderInit() != Framework::Integrations::Client::ClientError::CLIENT_NONE) {
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Rendering subsystems failed to initialize");
    }

    Framework::Logging::GetLogger("Hooks")->info("Main Window created (show now {}) = {}", showNow ? "yes" : "no", fmt::ptr(pThis->m_pMainWindow));
    //HookDx();
}

void FD3D12Adapter__CreateRootdevice_Hook(FD3D12Adapter *pThis, bool withDebug) {
    FD3D12Adapter__CreateRootdevice_original(pThis, withDebug);
    HogwartsMP::Core::gGlobals.device = pThis->m_pDevice;
    Framework::Logging::GetLogger("Hooks")->info("D3D12 RootDevice created (with debug {}) = {}", withDebug ? "yes" : "no", fmt::ptr(pThis->m_pDevice));
}

typedef HRESULT(__fastcall *D3D12Viewport__PresentInternal_t)(void*, int32_t);
D3D12Viewport__PresentInternal_t FD3D12Viewport__PresentInternal_original = nullptr;
HRESULT __fastcall FD3D12Viewport__PresentInternal_Hook(void* _FD3D12Viewport, int32_t SwapInterval) {
    const auto app = HogwartsMP::Core::gApplication.get();
    if (app && app->IsInitialized()) {
        app->GetImGUI()->Render();
    }

    return FD3D12Viewport__PresentInternal_original(_FD3D12Viewport, SwapInterval);
}

static InitFunction init([]() {
    // Initialize our FWindowsWindow Initialize method
    const auto FWindowsWindow__Initialize_Addr = hook::pattern("4C 8B DC 53 55 56 41 54 41 55 41 56").get_first();
    MH_CreateHook((LPVOID)FWindowsWindow__Initialize_Addr, (PBYTE)FWindowsWindow__Initialize_Hook, reinterpret_cast<void **>(&FWindowsWindow__Initialize_original));

    // Initialize our FWindowsApplication ProcessMessage method
    const auto FWindowsApplication__ProcessMessage_Addr = hook::get_opcode_address("E8 ? ? ? ? 48 8B 5C 24 ? 48 8B 6C 24 ? 48 8B 74 24 ? 48 98");
    MH_CreateHook((LPVOID)FWindowsApplication__ProcessMessage_Addr, (PBYTE)FWindowsApplication__ProcessMessage_Hook, reinterpret_cast<void **>(&FWindowsApplication__ProcessMessage_original));

    // Initialize our CreateRootDevice method
    const auto FD3D12Adapter__CreateRootDevice_Addr = hook::pattern("48 89 5C 24 ? 55 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 ? ? ? ? 48 81 EC ? ? ? ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 44 0F B6 FA").get_first();
    MH_CreateHook((LPVOID)FD3D12Adapter__CreateRootDevice_Addr, (PBYTE)FD3D12Adapter__CreateRootdevice_Hook, reinterpret_cast<void **>(&FD3D12Adapter__CreateRootdevice_original));

    // Init our present hook
    const auto FD3D12Viewport__PresentInternal_Addr = hook::pattern("89 54 24 10 4C 8B DC 57").get_first();
    MH_CreateHook((LPVOID)FD3D12Viewport__PresentInternal_Addr, (PBYTE)FD3D12Viewport__PresentInternal_Hook, reinterpret_cast<void **>(&FD3D12Viewport__PresentInternal_original));
});
