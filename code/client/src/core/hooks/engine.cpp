#include <utils/safe_win32.h>

#include <MinHook.h>
#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking.h>
#include <logging/logger.h>
#include "../application.h"

typedef void(__fastcall *EngineTick__Hook_t)(void);
EngineTick__Hook_t EngineTick__Hook_original = nullptr;

void EngineTick__Hook() {
    EngineTick__Hook_original();
    Framework::Logging::GetLogger("test")->info("hey i'm the ticker");
    if (HogwartsMP::Core::gApplication && HogwartsMP::Core::gApplication->IsInitialized()) {
        HogwartsMP::Core::gApplication->Update();
    }
}

static InitFunction init([]() {
    // Initialize our tick method
    const auto EngineTick__Addr = hook::get_opcode_address("E8 ? ? ? ? 80 3D ? ? ? ? ? 74 EB");
    MH_CreateHook((LPVOID)EngineTick__Addr, (PBYTE)EngineTick__Hook, reinterpret_cast<void **>(&EngineTick__Hook_original));
});
