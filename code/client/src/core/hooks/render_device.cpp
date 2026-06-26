#include <utils/safe_win32.h>

#include <MinHook.h>
#include <logging/logger.h>
#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking.h>

#include <logging/logger.h>

#include "../application.h"
#include "../aob_scan.h"
#include "dx12_pointer_grab.cpp"

#include <imgui.h>

#include <mutex>
#include <string>

// Resolve a code address to "module.dll+0xoffset" (defined below)
static std::string ModuleForAddress(void* addr);

// Serializes the Present hook against ResizeBuffers (different threads → a
// mid-resize Present could use-after-free the back buffer).
static std::mutex g_renderResizeMutex;

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
        // Tear CEF down while the window's message pump is still alive — left to
        // process teardown its threads stall game exit ~4.5 min. WM_DESTROY (not
        // WM_CLOSE) is what actually fires here.
        if ((msg == WM_CLOSE || msg == WM_DESTROY) && hwnd == HogwartsMP::Core::gGlobals.window) {
            const auto webManager = app->GetWebManager();
            if (webManager && webManager->IsInitialized()) {
                webManager->Shutdown();
            }
        }

        app->GetInput()->ProcessEvent(hwnd, msg, wParam, lParam);

        const bool isKeyboardMsg = msg >= WM_KEYFIRST && msg <= WM_KEYLAST;
        const bool isMouseMsg    = msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST;
        if (isKeyboardMsg || isMouseMsg) {
            const auto webManager = app->GetWebManager();
            if (webManager && webManager->IsInitialized()) {
                if (isMouseMsg) {
                    webManager->ProcessMouseEvent(hwnd, msg, wParam, lParam);
                }
                else {
                    webManager->ProcessKeyboardEvent(hwnd, msg, wParam, lParam);
                }

                // A focused web view consumes the input — swallow it from the game.
                if (webManager->IsAnyViewFocused()) {
                    return;
                }
            }
        }

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
        auto &opts = app->GetOptions();

        // TEMP BOOTSTRAP (2026-06): the FWindowsWindow::Initialize and
        // FD3D12Adapter::CreateRootDevice AOBs are stale for this build and
        // their hooks never fire, so device + windowHandle are never set the
        // original way. Source them from the swapchain instead. REVERT this
        // block once those two render functions are re-derived (see
        // framework-mod-stale-native-layer memory) to restore original flow.
        if (!opts.rendererOptions.d3d12.device) {
            ID3D12Device *dev = nullptr;
            if (SUCCEEDED(pSwapChain->GetDevice(__uuidof(ID3D12Device), reinterpret_cast<void **>(&dev))) && dev) {
                opts.rendererOptions.d3d12.device = dev;
                HogwartsMP::Core::gGlobals.device  = dev;
                dev->Release(); // swapchain keeps the device alive; borrow the ptr
            }
        }
        if (!opts.rendererOptions.windowHandle) {
            DXGI_SWAP_CHAIN_DESC desc{};
            if (SUCCEEDED(pSwapChain->GetDesc(&desc)) && desc.OutputWindow) {
                opts.rendererOptions.windowHandle = desc.OutputWindow;
                HogwartsMP::Core::gGlobals.window  = desc.OutputWindow;
            }
        }

        if(opts.rendererOptions.d3d12.commandQueue && opts.rendererOptions.d3d12.device) {
            opts.rendererOptions.d3d12.swapchain = pSwapChain;

            if (!app->RenderInit()) {
                Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->error("Rendering subsystems failed to initialize");
            }

            ImGuiIO& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        }
    } else {
        std::lock_guard<std::mutex> lock(g_renderResizeMutex);

        auto* backend = renderer->GetD3D12Backend();

        // Swapchain can be replaced wholesale (fullscreen/mode change); just point
        // at the current one (Begin() re-acquires the back buffer each frame).
        if (pSwapChain != backend->GetSwapChain()) {
            backend->SetSwapChain(pSwapChain);

            // Keep the CEF viewport matched to the (possibly new) client size
            const auto webManager = app->GetWebManager();
            if (webManager && webManager->IsInitialized()) {
                DXGI_SWAP_CHAIN_DESC desc {};
                if (SUCCEEDED(pSwapChain->GetDesc(&desc))) {
                    webManager->Resize(static_cast<int>(desc.BufferDesc.Width), static_cast<int>(desc.BufferDesc.Height));
                }
            }
        }

        backend->Begin();
        // Upload web-view textures before the ImGui draw that samples them.
        app->GetWebManager()->Render();
        app->GetImGUI()->Render();
        backend->End();
    }

    return IDXGISwapChain3__Present_original(pSwapChain, SyncInterval, Flags);
}

long __fastcall IDXGISwapChain3__ResizeBuffers_Hook(IDXGISwapChain3* pSwapChain, UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
    auto* app = HogwartsMP::Core::gApplication.get();

    long hr;
    {
        // Hold the lock only across the resize so no Present frame is mid-render
        // while the game resizes (we cache nothing; the game owns the resize).
        std::lock_guard<std::mutex> lock(g_renderResizeMutex);
        hr = IDXGISwapChain3__ResizeBuffers_orignal(pSwapChain, BufferCount, Width, Height, NewFormat, SwapChainFlags);
    }

    // Match the CEF viewport to the new client size (outside the lock)
    if (SUCCEEDED(hr) && app) {
        const auto webManager = app->GetWebManager();
        if (webManager && webManager->IsInitialized()) {
            DXGI_SWAP_CHAIN_DESC desc {};
            if (SUCCEEDED(pSwapChain->GetDesc(&desc))) {
                webManager->Resize(static_cast<int>(desc.BufferDesc.Width), static_cast<int>(desc.BufferDesc.Height));
            }
        }
    }

    return hr;
}

void __fastcall ID3D12CommandQueue__ExecuteCommandLists_Hook(ID3D12CommandQueue* queue, UINT NumCommandLists, ID3D12CommandList* ppCommandLists) {

    auto &opts = HogwartsMP::Core::gApplication->GetOptions();
    if (!opts.rendererOptions.d3d12.commandQueue && queue->GetDesc().Type == D3D12_COMMAND_LIST_TYPE_DIRECT) {
        opts.rendererOptions.d3d12.commandQueue = queue;
    }

    ID3D12CommandQueue__ExecuteCommandLists_original(queue, NumCommandLists, ppCommandLists);
}

// "module.dll+0xoffset" for a code address — tells real dxgi.dll from a proxy
// (e.g. NVIDIA Streamline's sl.interposer.dll).
static std::string ModuleForAddress(void* addr) {
    HMODULE mod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCWSTR>(addr), &mod) || !mod) {
        return "<unknown>";
    }
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(mod, path, MAX_PATH);
    std::wstring wp(path);
    const auto slash = wp.find_last_of(L"\\/");
    const std::wstring base = (slash == std::wstring::npos) ? wp : wp.substr(slash + 1);
    char nameBuf[MAX_PATH] = {};
    WideCharToMultiByte(CP_UTF8, 0, base.c_str(), -1, nameBuf, sizeof(nameBuf), nullptr, nullptr);
    const auto off = reinterpret_cast<uintptr_t>(addr) - reinterpret_cast<uintptr_t>(mod);
    return fmt::format("{}+0x{:x}", nameBuf, off);
}

void HookDX12_Functions() {
    // One-shot: safe to call from the temporary EngineTick bootstrap and from
    // the (currently dead) FWindowsWindow::Initialize hook without double-hooking.
    static bool s_hooked = false;
    if (s_hooked) {
        return;
    }
    s_hooked = true;

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
    // Which module owns each hooked function? (real DXGI vs Streamline proxy)
    Framework::Logging::GetLogger(FRAMEWORK_INNER_CLIENT)->warn("DX12 hook modules -> ExecuteCommandLists: {} | Present: {} | ResizeBuffers: {}",
        ModuleForAddress(pointers.ID3D12CommandQueue__ExecuteCommandLists),
        ModuleForAddress(pointers.IDXGISwapChain3__Present),
        ModuleForAddress(pointers.IDXGISwapChain3__ResizeBuffers)
    );

    MH_CreateHook((LPVOID)pointers.IDXGISwapChain3__Present, (PBYTE)IDXGISwapChain3__Present_Hook, reinterpret_cast<void **>(&IDXGISwapChain3__Present_original));
    MH_CreateHook((LPVOID)pointers.IDXGISwapChain3__ResizeBuffers, (PBYTE)IDXGISwapChain3__ResizeBuffers_Hook, reinterpret_cast<void **>(&IDXGISwapChain3__ResizeBuffers_orignal));
    MH_CreateHook((LPVOID)pointers.ID3D12CommandQueue__ExecuteCommandLists, (PBYTE)ID3D12CommandQueue__ExecuteCommandLists_Hook, reinterpret_cast<void **>(&ID3D12CommandQueue__ExecuteCommandLists_original));
    MH_EnableHook(NULL);
}

// TEMP BOOTSTRAP (2026-06): kick the DX12/ImGui hookup from the working
// EngineTick hook, because the FWindowsWindow::Initialize hook that normally
// triggers it is on a stale AOB and never fires. Idempotent (HookDX12_Functions
// is one-shot). Remove once that render AOB is re-derived.
void EnsureDX12Hooked() {
    HookDX12_Functions();
}

/* ---------------------------------------------- */

void FWindowsWindow__Initialize_Hook(FDWindowsWindow *pThis, void *app, float **definitions, HINSTANCE inst, void *parent, bool showNow) {
    FWindowsWindow__Initialize_original(pThis, app, definitions, inst, parent, showNow);

    // Acquire the windows and patch the title
    HogwartsMP::Core::gGlobals.window = pThis->m_pMainWindow;

    // prepare the data
    auto &opts = HogwartsMP::Core::gApplication->GetOptions();
    opts.rendererOptions.d3d12.device = HogwartsMP::Core::gGlobals.device;
    opts.rendererOptions.windowHandle = HogwartsMP::Core::gGlobals.window;

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
    using HogwartsMP::Core::AobFirst;
    using HogwartsMP::Core::AobOpcodeAddr;
    using HogwartsMP::Game::gLayout;

    // NOTE: these two render AOBs currently match the WRONG functions on this
    // build (their handlers never fire); the overlay is brought up by the
    // EngineTick DX12 bootstrap instead. Kept wired for when they're re-derived.
    const auto FWindowsWindow__Initialize_Addr = AobFirst(gLayout.fwindowsWindowInitialize);
    if (FWindowsWindow__Initialize_Addr) {
        MH_CreateHook((LPVOID)FWindowsWindow__Initialize_Addr, (PBYTE)FWindowsWindow__Initialize_Hook, reinterpret_cast<void **>(&FWindowsWindow__Initialize_original));
    }

    const auto FWindowsApplication__ProcessMessage_Addr = AobOpcodeAddr(gLayout.fwindowsAppProcessMessage);
    if (FWindowsApplication__ProcessMessage_Addr) {
        MH_CreateHook((LPVOID)FWindowsApplication__ProcessMessage_Addr, (PBYTE)FWindowsApplication__ProcessMessage_Hook, reinterpret_cast<void **>(&FWindowsApplication__ProcessMessage_original));
    }

    const auto FD3D12Adapter__CreateRootDevice_Addr = AobFirst(gLayout.fd3d12CreateRootDevice);
    if (FD3D12Adapter__CreateRootDevice_Addr) {
        MH_CreateHook((LPVOID)FD3D12Adapter__CreateRootDevice_Addr, (PBYTE)FD3D12Adapter__CreateRootdevice_Hook, reinterpret_cast<void **>(&FD3D12Adapter__CreateRootdevice_original));
    }

    // Init our present hook
    // const auto FD3D12Viewport__PresentInternal_Addr = hook::pattern("89 54 24 10 4C 8B DC 57").get_first();
    // MH_CreateHook((LPVOID)FD3D12Viewport__PresentInternal_Addr, (PBYTE)FD3D12Viewport__PresentInternal_Hook, reinterpret_cast<void **>(&FD3D12Viewport__PresentInternal_original));

    // Initialize our Tick method
    //const auto FEngineLoop__BeginFrameRenderThread_Addr = hook::get_opcode_address("E8 ? ? ? ? EB 54 33 D2 48 8D 4D 50");
    //MH_CreateHook((LPVOID)FEngineLoop__BeginFrameRenderThread_Addr, (PBYTE)FEngineLoop__BeginFrameRenderThread_Hook, reinterpret_cast<void **>(&FEngineLoop__BeginFrameRenderThread_original));
},"Render_Device");
