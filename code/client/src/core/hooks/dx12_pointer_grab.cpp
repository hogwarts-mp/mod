#include <d3d12.h>
#include <dxgi1_4.h>
#include <atlbase.h>

static uint64_t* g_MethodsTable = NULL;

struct DX12Pointers {
    void* ID3D12CommandQueue__ExecuteCommandLists;
    void* IDXGISwapChain3__Present;
    void* IDXGISwapChain3__ResizeBuffers;
};

int Init() {
    WNDCLASSEX windowClass;
    windowClass.cbSize = sizeof(WNDCLASSEX);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = DefWindowProc;
    windowClass.cbClsExtra = 0;
    windowClass.cbWndExtra = 0;
    windowClass.hInstance = GetModuleHandle(NULL);
    windowClass.hIcon = NULL;
    windowClass.hCursor = NULL;
    windowClass.hbrBackground = NULL;
    windowClass.lpszMenuName = NULL;
    windowClass.lpszClassName = L"Fake Window";
    windowClass.hIconSm = NULL;

    ::RegisterClassEx(&windowClass);

    HWND window = ::CreateWindow(windowClass.lpszClassName, L"Fake DirectX Window", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, NULL, NULL, windowClass.hInstance, NULL);

    HMODULE libDXGI;
    HMODULE libD3D12;

    if ((libDXGI = ::GetModuleHandle(L"dxgi.dll")) == NULL) {
        ::DestroyWindow(window);
        ::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
        return -1;
    }

    if ((libD3D12 = ::GetModuleHandle(L"d3d12.dll")) == NULL) {
        ::DestroyWindow(window);
        ::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
        return -1;
    }

    void* CreateDXGIFactory;
    if ((CreateDXGIFactory = ::GetProcAddress(libDXGI, "CreateDXGIFactory")) == NULL) {
        ::DestroyWindow(window);
        ::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
        return -1;
    }

    CComPtr<IDXGIFactory> factory;
    if (((long(__stdcall*)(const IID&, void**))(CreateDXGIFactory))(__uuidof(IDXGIFactory), (void**)&factory) < 0) {
        ::DestroyWindow(window);
        ::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
        return -1;
    }

    CComPtr<IDXGIAdapter> adapter;
    if (factory->EnumAdapters(0, &adapter) == DXGI_ERROR_NOT_FOUND) {
        ::DestroyWindow(window);
        ::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
        return -1;
    }

    void* D3D12CreateDevice;
    if ((D3D12CreateDevice = ::GetProcAddress(libD3D12, "D3D12CreateDevice")) == NULL) {
        ::DestroyWindow(window);
        ::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
        return -1;
    }

    CComPtr<ID3D12Device> device;
    if (((long(__stdcall*)(IUnknown*, D3D_FEATURE_LEVEL, const IID&, void**))(D3D12CreateDevice))(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&device) < 0) {
        ::DestroyWindow(window);
        ::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
        return -1;
    }

    D3D12_COMMAND_QUEUE_DESC queueDesc;
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    queueDesc.Priority = 0;
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.NodeMask = 0;

    CComPtr<ID3D12CommandQueue> commandQueue;
    if (device->CreateCommandQueue(&queueDesc, __uuidof(ID3D12CommandQueue), (void**)&commandQueue) < 0) {
        ::DestroyWindow(window);
        ::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
        return -1;
    }

    CComPtr<ID3D12CommandAllocator> commandAllocator;
    if (device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&commandAllocator) < 0) {
        ::DestroyWindow(window);
        ::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
        return -1;
    }

    CComPtr<ID3D12GraphicsCommandList> commandList;
    if (device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, commandAllocator, NULL, __uuidof(ID3D12GraphicsCommandList), (void**)&commandList) < 0) {
        ::DestroyWindow(window);
        ::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
        return -1;
    }

    DXGI_RATIONAL refreshRate;
    refreshRate.Numerator = 60;
    refreshRate.Denominator = 1;

    DXGI_MODE_DESC bufferDesc;
    bufferDesc.Width = 100;
    bufferDesc.Height = 100;
    bufferDesc.RefreshRate = refreshRate;
    bufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    bufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    bufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

    DXGI_SAMPLE_DESC sampleDesc;
    sampleDesc.Count = 1;
    sampleDesc.Quality = 0;

    DXGI_SWAP_CHAIN_DESC swapChainDesc = {};
    swapChainDesc.BufferDesc = bufferDesc;
    swapChainDesc.SampleDesc = sampleDesc;
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = 2;
    swapChainDesc.OutputWindow = window;
    swapChainDesc.Windowed = 1;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

    CComPtr<IDXGISwapChain> swapChain;
    if (factory->CreateSwapChain(commandQueue, &swapChainDesc, &swapChain) < 0) {
        ::DestroyWindow(window);
        ::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
        return -1;
    }

    g_MethodsTable = (uint64_t*)::calloc(150, sizeof(uint64_t));
    ::memcpy(g_MethodsTable, *(uint64_t**)(void*)device, 44 * sizeof(uint64_t));
    ::memcpy(g_MethodsTable + 44, *(uint64_t**)(void*)commandQueue, 19 * sizeof(uint64_t));
    ::memcpy(g_MethodsTable + 44 + 19, *(uint64_t**)(void*)commandAllocator, 9 * sizeof(uint64_t));
    ::memcpy(g_MethodsTable + 44 + 19 + 9, *(uint64_t**)(void*)commandList, 60 * sizeof(uint64_t));
    ::memcpy(g_MethodsTable + 44 + 19 + 9 + 60, *(uint64_t**)(void*)swapChain, 18 * sizeof(uint64_t));

    ::DestroyWindow(window);
    ::UnregisterClass(windowClass.lpszClassName, windowClass.hInstance);
    return 0;
}

std::optional<DX12Pointers> GrabDX12Pointers() {
    const auto res = Init();
    if(res == -1) {
        return {};
    }

    return std::make_optional(DX12Pointers{
        (void*)g_MethodsTable[54],
        (void*)g_MethodsTable[140],
        (void*)g_MethodsTable[145]
    });
}