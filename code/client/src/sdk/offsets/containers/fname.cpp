#include "fname.h"

namespace SDK {
    FNamePool *FName::GNames = nullptr;

    int32_t FNameEntry::GetLength() const {
        return Header.Len;
    }

    bool FNameEntry::IsWide() const {
        return Header.bIsWide;
    }

    int32_t FNameEntry::GetId() const {
        throw std::exception("ERROR");
    }

    std::string FNameEntry::GetAnsiName() const {
        uint32_t len = GetLength();
        if (len > 1024)
            return "ERROR";
        return std::string((const char *)AnsiName, len);
    }

    std::wstring FNameEntry::GetWideName() const {
        uint32_t len = GetLength();
        return std::wstring((const wchar_t *)WideName, len);
    }

    std::string FNameEntry::GetName() const {
        return GetAnsiName();
    }

    int32_t FNameEntryAllocator::NumBlocks() const {
        return CurrentBlock + 1;
    }

    FNameEntry *FNameEntryAllocator::GetById(int32_t key) const {
        int block  = key >> 16;
        int offset = (uint16_t)key;
        if (!IsValidIndex(key, block, offset))
            return reinterpret_cast<FNameEntry *>(Blocks[0] + 0);
        return reinterpret_cast<FNameEntry *>(Blocks[block] + ((uint64_t)offset * Stride));
    }

    bool FNameEntryAllocator::IsValidIndex(int32_t key) const {
        uint32_t block  = key >> 16;
        uint16_t offset = key;
        return IsValidIndex(key, block, offset);
    }

    bool FNameEntryAllocator::IsValidIndex(int32_t key, uint32_t block, uint16_t offset) const {
        return (key >= 0 && block < static_cast<uint32_t>(NumBlocks()) && offset * Stride < MaxOffset);
    }

    int32_t FNamePool::Count() const {
        return AnsiCount;
    }

    bool FNamePool::IsValidIndex(int32_t index) const {
        return Allocator.IsValidIndex(static_cast<int32_t>(index));
    }

    FNameEntry *FNamePool::GetById(int32_t id) const {
        return Allocator.GetById(id);
    }

    FNameEntry *FNamePool::operator[](int32_t id) const {
        return GetById(id);
    }

    FName::FName() {
        ComparisonIndex = 0;
        Number          = 0;
    }

    FName::FName(int32_t i) {
        ComparisonIndex = i;
        Number          = 0;
    }

    FName::FName(const char *nameToFind) {
        Number = 0;
        static std::unordered_set<int> cache;
        for (auto i : cache) {
            if (GetGlobalNames()[i]->GetAnsiName() == nameToFind) {
                ComparisonIndex = i;
                return;
            }
        }
    }

    FName::FName(const wchar_t *nameToFind) {
        Number = 0;
        static std::unordered_set<int> cache;
        for (auto i : cache) {
            if (GetGlobalNames()[i]->GetWideName() == nameToFind) {
                ComparisonIndex = i;
                return;
            }
        }
    }

    FNamePool &FName::GetGlobalNames() {
        return *GNames;
    }

    std::string FName::GetNameA() const {
        return GetGlobalNames()[ComparisonIndex]->GetAnsiName();
    }

    std::wstring FName::GetNameW() const {
        return GetGlobalNames()[ComparisonIndex]->GetWideName();
    }

    std::string FName::GetName() const {
        return GetNameA();
    }
}
