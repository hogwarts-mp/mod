#pragma once
#include <logging/logger.h>
#include "UObject/Class.h"
#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking_patterns.h>

typedef void (__fastcall *FName__ToString_t)(const FName *, FString &);
FName__ToString_t FName__ToString = nullptr;

FMalloc* gMalloc = nullptr;

void* FGenericPlatformString::Memcpy(void* Dest, const void* Src, SIZE_T Count) {
	return FMemory::Memcpy(Dest, Src, Count);
}

void FMemory::Free(void *mem) {
    gMalloc->Free(mem);
}

void* FMemory::Realloc(void* Original, SIZE_T Count, uint32 Alignment) {
    return gMalloc->Realloc(Original, Count, Alignment);
}

size_t FMemory::QuantizeSize(SIZE_T Count, uint32_t Alignment) {
    return gMalloc->QuantizeSize(Count, Alignment);
}

FString FName::ToString() const {
    FString out{};
    FName__ToString(this, out);
    return out;
}

static InitFunction init([]() {
    FName__ToString = reinterpret_cast<FName__ToString_t>(hook::pattern("48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 48 83 EC 20 8B 01 48 8B DA 8B F8 44 0F B7 C0 C1").get_first());

    auto GMalloc_Instruction = reinterpret_cast<uint64_t>(hook::pattern("48 8B 0D ? ? ? ? 48 85 C9 75 0C E8 ? ? ? ? 48 8B 0D ? ? ? ? 48 8B 01 48 8B D3 FF 50 ? 48 83 C4 20").get_first());
    uint8_t *GMalloc_Bytes   = reinterpret_cast<uint8_t *>(GMalloc_Instruction);
    gMalloc          = *reinterpret_cast<FMalloc**>(GMalloc_Bytes + *(int32_t *)(GMalloc_Bytes + 3) + 7);
    
    Framework::Logging::GetLogger("Hooks")->info("GMalloc {}", (void*)gMalloc);
});
