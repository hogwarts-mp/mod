#include <utils/safe_win32.h>

#include <MinHook.h>
#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking.h>

#include <logging/logger.h>

#include "../application.h"
#include "../../sdk/Headers/FActorSpawnParameters.h"

static InitFunction init([]() {
    // Grab the pointer to the UWorld
    auto GWorld_Scan                  = hook::pattern("48 8B 1D ? ? ? ? 48 85 DB 74 3B 41 B0 01").get_first();
    uint8_t *GWorld_Instruction_Bytes = reinterpret_cast<uint8_t *>(GWorld_Scan);
    uint64_t GWorld_Addr              = reinterpret_cast<uint64_t>(GWorld_Instruction_Bytes + *(int32_t *)(GWorld_Instruction_Bytes + 3) + 7);
    HogwartsMP::Core::gGlobals.world                            = (SDK::UWorld **)(GWorld_Addr);
    Framework::Logging::GetLogger("Hooks")->info("Found UWorld pointer at {}", fmt::ptr(HogwartsMP::Core::gGlobals.world));

    // Grab the pointer to the global object array
    auto Obj_Array_Scan      = hook::pattern("48 8D 0D ? ? ? ? E8 ? ? ? ? 48 8D 8D A0 02 00 00").get_first();
    uint8_t *Obj_Array_Bytes = reinterpret_cast<uint8_t *>(Obj_Array_Scan);
    HogwartsMP::Core::gGlobals.objectArray = reinterpret_cast<FUObjectArray *>(Obj_Array_Bytes + *(int32_t *)(Obj_Array_Bytes + 3) + 7);
    Framework::Logging::GetLogger("Hooks")->info("Found GObjectArray pointer at {}", fmt::ptr(HogwartsMP::Core::gGlobals.objectArray));
});
