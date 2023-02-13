#include <MinHook.h>

#include <logging/logger.h>
#include <utils/string_utils.h>

#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking_patterns.h>

#include "sdk/u_world.h"

void HackThread(HMODULE hModule) {
    MH_Initialize();
    uintptr_t baseAddress = (uintptr_t)GetModuleHandle("HogwartsLegacy.exe");
    Framework::Logging::GetInstance()->Get("test")->debug("Base address {}", baseAddress);
    hook::set_base(baseAddress);

    InitFunction::RunAll();
    MH_EnableHook(MH_ALL_HOOKS);

    // debug
    while (true) {
        if (GetAsyncKeyState(VK_F1) & 0x1) {
            auto *world = HogwartsMP::SDK::GetMainWorld();
            Framework::Logging::GetInstance()->Get("test")->debug("World ptr is {}", fmt::ptr(world));

            auto *persistentLvl = world->PersistentLevel;
            Framework::Logging::GetInstance()->Get("test")->debug("Persistent Level ptr is {}", fmt::ptr(persistentLvl));

            auto *owningWorld = persistentLvl->OwningWorld;
            Framework::Logging::GetInstance()->Get("test")->debug("Owning World ptr is {}", fmt::ptr(owningWorld));

            auto *owningGameInstance = owningWorld->OwningGameInstance;
            Framework::Logging::GetInstance()->Get("test")->debug("Owning Game Instance ptr is {}", fmt::ptr(owningGameInstance));

            // auto *localPlayer = owningGameInstance->LocalPlayers.Data[0];
            // Framework::Logging::GetInstance()->Get("test")->debug("LocalPlayer Instance ptr is {}", fmt::ptr(localPlayer));
        }
        Sleep(10);
    }
    

    // Entry point is handled by an InitFunction, so we just have to enable hooks and trigger the shits down here
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);

        AllocConsole();
        SetConsoleTitleW(L"HogwartsMP");

        Framework::Logging::GetInstance()->SetLogName("HogwartsMP");

        HANDLE handle = CreateThread(NULL, NULL, (LPTHREAD_START_ROUTINE)HackThread, hModule, NULL, NULL);
        
        break;
    }
    return 1;
}
