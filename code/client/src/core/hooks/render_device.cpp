#include <utils/safe_win32.h>

#include <MinHook.h>
#include <logging/logger.h>
#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking.h>

#include <logging/logger.h>

#include "../application.h"
#include "dx12_pointer_grab.cpp"

#include <imgui.h>

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

        if (app->AreControlsLocked() && app->GetImGUI()->ProcessEvent(hwnd, msg, wParam, lParam) == Framework::External::ImGUI::InputState::BLOCK) {
            return;
        }
    }
    FWindowsApplication__ProcessMessage_original(pThis, hwnd, msg, wParam, lParam);
}

/* ------------- DX12 hooks section ------------- */
typedef long(__fastcall* IDXGISwapChain3__Present_t) (IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags);
IDXGISwapChain3__Present_t IDXGISwapChain3__Present_original = nullptr;

typedef long(__fastcall* IDXGISwapChain3__ResizeBuffers_t)(IDXGISwapChain3* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags);
static IDXGISwapChain3__ResizeBuffers_t IDXGISwapChain3__ResizeBuffers_orignal = nullptr;

typedef void(*ID3D12CommandQueue__ExecuteCommandLists_t)(ID3D12CommandQueue* queue, UINT NumCommandLists, ID3D12CommandList* ppCommandLists);
ID3D12CommandQueue__ExecuteCommandLists_t ID3D12CommandQueue__ExecuteCommandLists_original = nullptr;

long __fastcall IDXGISwapChain3__Present_Hook(IDXGISwapChain3* pSwapChain, UINT SyncInterval, UINT Flags) {
    auto* app = HogwartsMP::Core::gApplication.get();
    auto* renderer = app->GetRenderer();

    if(!renderer->IsInitialized()) {
        auto opts = app->GetOptions();
        if(opts->rendererOptions.d3d12.commandQueue) {
            opts->rendererOptions.d3d12.swapchain = pSwapChain;

            if (app->RenderInit() != Framework::Integrations::Client::ClientError::CLIENT_NONE) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Rendering subsystems failed to initialize");
            }

            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        }
    } else {
        renderer->GetD3D12Backend()->Begin();
        app->GetImGUI()->Render();
        renderer->GetD3D12Backend()->End();
    }

    return IDXGISwapChain3__Present_original(pSwapChain, SyncInterval, Flags);
}

long __fastcall IDXGISwapChain3__ResizeBuffers_Hook(IDXGISwapChain3* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
    return IDXGISwapChain3__ResizeBuffers_orignal(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
}

void __fastcall ID3D12CommandQueue__ExecuteCommandLists_Hook(ID3D12CommandQueue* queue, UINT NumCommandLists, ID3D12CommandList* ppCommandLists) {

    auto opts = HogwartsMP::Core::gApplication->GetOptions();
    if (!opts->rendererOptions.d3d12.commandQueue && queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
        opts->rendererOptions.d3d12.commandQueue = queue;
    }

    ID3D12CommandQueue__ExecuteCommandLists_original(queue, NumCommandLists, ppCommandLists);
}

void HookDX12_Functions() {
    auto pointersRes = GrabDX12Pointers();
    if(!pointersRes.has_value()) {
        Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Unable to grab DX12 pointers !");
        return;
    }

    auto pointers = pointersRes.value();
    Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->info("DX12 pointers ExecuteCommandLists: {} Present: {} ResizeBuffers: {}",
        pointers.ID3D12CommandQueue__ExecuteCommandLists,
        pointers.IDXGISwapChain3__Present,
        pointers.IDXGISwapChain3__ResizeBuffers
    );

    MH_CreateHook((LPVOID)pointers.IDXGISwapChain3__Present, (PBYTE)IDXGISwapChain3__Present_Hook, reinterpret_cast<void **>(&IDXGISwapChain3__Present_original));
    MH_CreateHook((LPVOID)pointers.IDXGISwapChain3__ResizeBuffers, (PBYTE)IDXGISwapChain3__ResizeBuffers_Hook, reinterpret_cast<void **>(&IDXGISwapChain3__ResizeBuffers_orignal));
    MH_CreateHook((LPVOID)pointers.ID3D12CommandQueue__ExecuteCommandLists, (PBYTE)ID3D12CommandQueue__ExecuteCommandLists_Hook, reinterpret_cast<void **>(&ID3D12CommandQueue__ExecuteCommandLists_original));
    MH_EnableHook(NULL);
}

/* ---------------------------------------------- */

void FWindowsWindow__Initialize_Hook(FDWindowsWindow *pThis, void *app, float **definitions, HINSTANCE inst, void *parent, bool showNow) {
    FWindowsWindow__Initialize_original(pThis, app, definitions, inst, parent, showNow);

    // Acquire the windows and patch the title
    HogwartsMP::Core::gGlobals.window = pThis->m_pMainWindow;

    // prepare the data
    auto opts = HogwartsMP::Core::gApplication->GetOptions();
    opts->rendererOptions.d3d12.device = HogwartsMP::Core::gGlobals.device;
    opts->rendererOptions.windowHandle = HogwartsMP::Core::gGlobals.window;

    SetWindowTextA(pThis->m_pMainWindow, "Hogwarts: Advanced Multiplayer Edition");

    HookDX12_Functions();
    Framework::Logging::GetLogger("Hooks")->info("Main Window created (show now {}) = {}", showNow ? "yes" : "no", fmt::ptr(pThis->m_pMainWindow));
}

void FD3D12Adapter__CreateRootdevice_Hook(FD3D12Adapter *pThis, bool withDebug) {
    FD3D12Adapter__CreateRootdevice_original(pThis, withDebug);
    HogwartsMP::Core::gGlobals.device = pThis->m_pDevice;
    Framework::Logging::GetLogger("Hooks")->info("D3D12 RootDevice created (with debug {}) = {}", withDebug ? "yes" : "no", fmt::ptr(pThis->m_pDevice));
}

// void FEngineLoop__BeginFrameRenderThread_Hook(void *pThis, FRHICommandListImmediate &cmdsList, uint64_t frameCount) {
//     FEngineLoop__BeginFrameRenderThread_original(pThis, cmdsList, frameCount);
//     //Framework::Logging::GetLogger("Hooks")->info(" Rendering thread tick");
// }

// typedef HRESULT(__fastcall *D3D12Viewport__PresentInternal_t)(void*, int32_t);
// D3D12Viewport__PresentInternal_t FD3D12Viewport__PresentInternal_original = nullptr;
// HRESULT __fastcall FD3D12Viewport__PresentInternal_Hook(void* _FD3D12Viewport, int32_t SwapInterval) {
//     const auto app = HogwartsMP::Core::gApplication.get();
//     if (app && app->IsInitialized()) {
//         app->GetImGUI()->Render();
//     }

//     return FD3D12Viewport__PresentInternal_original(_FD3D12Viewport, SwapInterval);
// }

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
    // const auto FD3D12Viewport__PresentInternal_Addr = hook::pattern("89 54 24 10 4C 8B DC 57").get_first();
    // MH_CreateHook((LPVOID)FD3D12Viewport__PresentInternal_Addr, (PBYTE)FD3D12Viewport__PresentInternal_Hook, reinterpret_cast<void **>(&FD3D12Viewport__PresentInternal_original));

    // Initialize our Tick method
    //const auto FEngineLoop__BeginFrameRenderThread_Addr = hook::get_opcode_address("E8 ? ? ? ? EB 54 33 D2 48 8D 4D 50");
    //MH_CreateHook((LPVOID)FEngineLoop__BeginFrameRenderThread_Addr, (PBYTE)FEngineLoop__BeginFrameRenderThread_Hook, reinterpret_cast<void **>(&FEngineLoop__BeginFrameRenderThread_original));
});
