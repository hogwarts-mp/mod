#include <utils/safe_win32.h>
#include <MinHook.h>

#include <logging/logger.h>
#include <utils/string_utils.h>

#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking_patterns.h>

#include "core/application.h"

/* extern "C" void __declspec(dllexport) InitClient(const wchar_t *projectPath) {
    Framework::Logging::GetInstance()->SetLogName("HogwartsMP");
    Framework::Logging::GetInstance()->SetLogFolder(Framework::Utils::StringUtils::WideToNormal(projectPath) + "\\logs");

    // MH_Initialize();
    hook::set_base();

    // Entry point is handled by an InitFunction, so we just have to enable hooks and trigger the shits down here
    InitFunction::RunAll();
    MH_EnableHook(MH_ALL_HOOKS);
}*/

typedef char *(__fastcall *GetNarrowWinMainCommandLine_t)();
GetNarrowWinMainCommandLine_t GetNarrowWinMainCommandLine_original = nullptr;
char *GetNarrowWinMainCommandLine() {
    InitFunction::RunAll();
    MH_EnableHook(MH_ALL_HOOKS);

    // Create our core module application
    HogwartsMP::Core::gApplication.reset(new HogwartsMP::Core::Application);
    if (HogwartsMP::Core::gApplication && !HogwartsMP::Core::gApplication->IsInitialized()) {
        Framework::Integrations::Client::InstanceOptions opts;
        opts.discordAppId = 1075742680321114212;
        opts.useRenderer  = false;
        opts.usePresence  = true;
        opts.useImGUI     = false;

        HogwartsMP::Core::gApplication->Init(opts);
        HogwartsMP::Core::gApplication->Update();
    }
    return GetNarrowWinMainCommandLine_original();
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpReserved) {
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH: {
        AllocConsole();
        AttachConsole(GetCurrentProcessId());
        SetConsoleTitleW(L"HogwartsMP");
        
        MH_Initialize();

        auto base = GetModuleHandle(nullptr);
        hook::set_base(reinterpret_cast<uintptr_t>(base));

        // Initialize the main application after Denuvo actually finished unpacking / unciphering the game
        auto handle = LoadLibrary(TEXT("api-ms-win-crt-runtime-l1-1-0.dll"));
        if (handle) {
            auto procAddr = GetProcAddress((HMODULE)handle, "_get_narrow_winmain_command_line");
            if (procAddr) {
                MH_CreateHook(procAddr, GetNarrowWinMainCommandLine, reinterpret_cast<void **>(&GetNarrowWinMainCommandLine_original));
                MH_EnableHook(procAddr);
            }
        }
        
    } break;
    }

    return 1;
}
