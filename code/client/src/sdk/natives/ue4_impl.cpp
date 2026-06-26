#pragma once
#include <logging/logger.h>
#include "UObject/Class.h"
#include "UObject/UnrealType.h"
#include <utils/hooking/hook_function.h>
#include <utils/hooking/hooking_patterns.h>
#include "core/aob_scan.h"

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

// FScriptMapHelper::AddPair needs these: key hashing (forwards to the virtual) + the map allocator's
// growth (stock FMemory::Realloc — our runtime-built maps are never frozen).
uint32 FProperty::GetValueTypeHash(const void *Src) const {
    return GetValueTypeHashInternal(Src);
}

void FMemoryImageAllocatorBase::ResizeAllocation(int32 PreviousNumElements, int32 NumElements, SIZE_T NumBytesPerElement, uint32 Alignment) {
    (void)PreviousNumElements;
    FScriptContainerElement *LocalData = Data.Get();
    if (!Data.IsFrozen() && (LocalData || NumElements > 0)) {
        Data = (FScriptContainerElement *)FMemory::Realloc(LocalData, NumElements * NumBytesPerElement, Alignment);
    }
}

FString FName::ToString() const {
    FString out{};
    FName__ToString(this, out);
    return out;
}

// Normally lives in UnrealNames.cpp (not compiled here). Linker glue: FName's
// default ctor (FName() -> FName(NAME_None)) pulls in FNameEntryId::FromEName,
// which references this symbol. NOTE that FromEName short-circuits NAME_None
// itself (NameTypes.h), so this stub only ever RUNS for a non-None hardcoded
// EName — and entry 0 ("None") is then the WRONG answer. Nothing in the mod
// does that today; the log is here so it cannot start happening silently.
FNameEntryId FNameEntryId::FromValidEName(EName Ename) {
    Framework::Logging::GetLogger("Hooks")->error("FNameEntryId::FromValidEName({}) stub hit — returning the NAME_None entry, name resolution will be wrong", static_cast<int>(Ename));
    return FNameEntryId();
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
