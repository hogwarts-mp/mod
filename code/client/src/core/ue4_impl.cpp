#pragma once
#include <logging/logger.h>
#include "UObject/Class.h"
#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking_patterns.h>
#include "aob_scan.h"

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
    using HogwartsMP::Core::AobFirst;
    using HogwartsMP::Game::gLayout;

    FName__ToString = reinterpret_cast<FName__ToString_t>(AobFirst(gLayout.fnameToString));

    auto GMalloc_Instruction = reinterpret_cast<uint64_t>(AobFirst(gLayout.gMalloc));
    if (GMalloc_Instruction) {
        uint8_t *GMalloc_Bytes = reinterpret_cast<uint8_t *>(GMalloc_Instruction);
        gMalloc                = *reinterpret_cast<FMalloc **>(GMalloc_Bytes + *(int32_t *)(GMalloc_Bytes + 3) + 7);
    }

    Framework::Logging::GetLogger("Hooks")->info("GMalloc {}", (void*)gMalloc);
},"UE4_impl");
