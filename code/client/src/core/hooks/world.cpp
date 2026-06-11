#include <utils/safe_win32.h>

#include <MinHook.h>
#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking.h>

#include <logging/logger.h>

#include "../application.h"
#include "../aob_scan.h"
#include "../../sdk/Headers/FActorSpawnParameters.h"

class FObjectInitializer;
typedef void *(__fastcall *UWorld__UWorld_t)(void *, const FObjectInitializer &);
UWorld__UWorld_t UWorld__UWorld_original = nullptr;
void *UWorld__UWorld(void *pThis, const FObjectInitializer &objectInitialiser) {
    // Framework::Logging::GetLogger("Hooks")->trace("Uworld::UWorld ({})", fmt::ptr(pThis));
    return UWorld__UWorld_original(pThis, objectInitialiser);
}

static InitFunction init([]() {
    using HogwartsMP::Core::AobFirst;
    using HogwartsMP::Game::gLayout;

    // Grab the pointer to the UWorld
    auto GWorld_Scan                  = AobFirst(gLayout.gWorld);
    uint8_t *GWorld_Instruction_Bytes = reinterpret_cast<uint8_t *>(GWorld_Scan);
    if (GWorld_Instruction_Bytes) {
        uint64_t GWorld_Addr            = reinterpret_cast<uint64_t>(GWorld_Instruction_Bytes + *(int32_t *)(GWorld_Instruction_Bytes + 3) + 7);
        HogwartsMP::Core::gGlobals.world = (SDK::UWorld **)(GWorld_Addr);
    }

    // Grab the pointer to the global object array.
    auto Obj_Array_Scan      = AobFirst(gLayout.gObjectArray);
    uint8_t *Obj_Array_Bytes = reinterpret_cast<uint8_t *>(Obj_Array_Scan);
    if (Obj_Array_Bytes) {
        HogwartsMP::Core::gGlobals.objectArray = reinterpret_cast<FUObjectArray *>(Obj_Array_Bytes + *(int32_t *)(Obj_Array_Bytes + 3) + 7);
    }

    // Hook world constructor (optional: trace-only hook, no consumer — world is
    // obtained via the GWorld scan above)
    const auto UWorld__UWorld_Addr = AobFirst(gLayout.uworldCtor);
    if (UWorld__UWorld_Addr) {
        MH_CreateHook((LPVOID)UWorld__UWorld_Addr, (PBYTE)UWorld__UWorld, reinterpret_cast<void **>(&UWorld__UWorld_original));
    }
},"World");
