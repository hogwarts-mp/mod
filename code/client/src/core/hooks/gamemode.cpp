#include <utils/safe_win32.h>

#include <MinHook.h>
#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking.h>

#include "../application.h"

typedef void(__fastcall *AGameMode_InitGameState_t)(void *);
AGameMode_InitGameState_t AGameMode_InitGameState_original = nullptr;
void AGameMode_InitGameState_Hook(void *pThis) {
    AGameMode_InitGameState_original(pThis);
}

static InitFunction init([]() {
    // Hook gamemode init function
    // const auto AGameMode_InitGameState_Addr = reinterpret_cast<uint64_t>(hook::pattern("40 53 48 83 EC 20 48 8B 41 10 48 8B D9 48 8B 91").get_first());
    // MH_CreateHook((LPVOID)AGameMode_InitGameState_Addr, (PBYTE)AGameMode_InitGameState_Hook, reinterpret_cast<void **>(&AGameMode_InitGameState_original));
},"Gamemode");
