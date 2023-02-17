#include <utils/safe_win32.h>

#include <MinHook.h>
#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking.h>

#include <logging/logger.h>

#include "../application.h"
#include "../../sdk/Headers/FActorSpawnParameters.h"

//typedef AActor *(__fastcall *UWorld__SpawnActor_t)(UWorld *world, UClass *Class, FTransform const *UserTransformPtr, const FActorSpawnParameters &SpawnParameters);
//UWorld__SpawnActor_t UWorld__SpawnActor = nullptr;
//
//AActor *__fastcall UWorld__SpawnActor_Hook(UWorld *world, UClass *Class, FTransform const *UserTransformPtr, const FActorSpawnParameters &SpawnParameters) {
//    // Framework::Logging::GetLogger("Hooks")->debug("Spawned actor class: {} name: {}", narrow(Class->GetFName().ToString()).c_str(), narrow(SpawnParameters.Name.ToString()));
//    return UWorld__SpawnActor(world, Class, UserTransformPtr, SpawnParameters);
//}
//
//static InitFunction init([]() {
//    // Grab the pointer to the UWorld
//    auto GWorld_Scan                  = hook::pattern("48 8B 1D ? ? ? ? 48 85 DB 74 3B 41 B0 01").get_first();
//    uint8_t *GWorld_Instruction_Bytes = reinterpret_cast<uint8_t *>(GWorld_Scan);
//    uint64_t GWorld_Addr              = reinterpret_cast<uint64_t>(GWorld_Instruction_Bytes + *(int32_t *)(GWorld_Instruction_Bytes + 3) + 7);
//    HogwartsMP::Core::gGlobals.world                            = (UWorld *)(GWorld_Addr);
//    Framework::Logging::GetLogger("Hooks")->info("Found UWorld pointer at {}", fmt::ptr(HogwartsMP::Core::gGlobals.world));
//
//    // Grab the pointer to the global object array
//    auto Obj_Array_Scan      = hook::pattern("48 8D 0D ? ? ? ? E8 ? ? ? ? 48 8D 8D A0 02 00 00").get_first();
//    uint8_t *Obj_Array_Bytes = reinterpret_cast<uint8_t *>(Obj_Array_Scan);
//    HogwartsMP::Core::gGlobals.objectArray = reinterpret_cast<FUObjectArray *>(Obj_Array_Bytes + *(int32_t *)(Obj_Array_Bytes + 3) + 7);
//    Framework::Logging::GetLogger("Hooks")->info("Found GObjectArray pointer at {}", fmt::ptr(HogwartsMP::Core::gGlobals.objectArray));
//
//    // Hook the SpawnActor method
//    auto UWorld__SpawnActor_Addr = reinterpret_cast<uint64_t>(hook::pattern("40 55 53 56 57 41 54 41 55 41 56 41 57 48 8D AC 24 08 FF FF FF 48 81 EC F8 01 00 00 48 8B 05 ? ? ? ? 48 33 C4 48 89 45").get_first());
//    MH_CreateHook((LPVOID)UWorld__SpawnActor_Addr, &UWorld__SpawnActor_Hook, (LPVOID *)&UWorld__SpawnActor);
//});
