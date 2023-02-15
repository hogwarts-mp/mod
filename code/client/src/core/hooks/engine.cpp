#include <utils/safe_win32.h>

#include <MinHook.h>
#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking.h>

#include "../application.h"

typedef char *(__fastcall *GetNarrowWinMainCommandLine_t)();
GetNarrowWinMainCommandLine_t GetNarrowWinMainCommandLine_original = nullptr;
char *GetNarrowWinMainCommandLine() {
    // Create our core module application
    HogwartsMP::Core::gApplication.reset(new HogwartsMP::Core::Application);
    if (HogwartsMP::Core::gApplication && !HogwartsMP::Core::gApplication->IsInitialized()) {
        Framework::Integrations::Client::InstanceOptions opts;
        opts.discordAppId = 763114144454672444;
        opts.useRenderer  = false;
        opts.usePresence  = true;
        opts.useImGUI     = false;

        HogwartsMP::Core::gApplication->Init(opts);
        HogwartsMP::Core::gApplication->PostUpdate();
    }
    return GetNarrowWinMainCommandLine_original();
}

static InitFunction init([]() {
    // Initialize the main application after Denuvo actually finished unpacking / unciphering the game
    auto handle = LoadLibrary(TEXT("api-ms-win-crt-runtime-l1-1-0.dll"));
    if (handle) {
        auto procAddr = GetProcAddress((HMODULE)handle, "_get_narrow_winmain_command_line");
        if (procAddr) {
            MH_CreateHook(procAddr, GetNarrowWinMainCommandLine, reinterpret_cast<void **>(&GetNarrowWinMainCommandLine_original));
        }
    }
});
