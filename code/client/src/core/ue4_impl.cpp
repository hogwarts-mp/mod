#pragma once
#include "UObject/Class.h"
#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking_patterns.h>

typedef void(__fastcall *FMemory__Free_t)(void*);
FMemory__Free_t FMemory__Free = nullptr;

typedef void (__fastcall *FName__ToString_t)(const FName *, FString &);
FName__ToString_t FName__ToString = nullptr;

void FMemory::Free(void *mem) {
    FMemory__Free(mem);
}

// void *FMemory::Realloc(void *mem, size_t size, uint32_t) {
//     return realloc(mem, size);
// }

FString FName::ToString() const {
    FString out{};
    FName__ToString(this, out);
    return out;
}

static InitFunction init([]() {
    FName__ToString = reinterpret_cast<FName__ToString_t>(hook::pattern("48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 48 83 EC 20 8B 01 48 8B DA 8B F8 44 0F B7 C0 C1").get_first());
    FMemory__Free = reinterpret_cast<FMemory__Free_t>(hook::pattern("48 85 C9 74 2E 53 48 83 EC 20 48 8B D9 48 8B ? ? ? ? ? 48 85 C9").get_first());
});
