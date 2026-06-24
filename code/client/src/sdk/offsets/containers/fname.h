#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>

namespace SDK {
    class FNameEntryHeader {
      public:
        static const constexpr uint32_t ProbeHashBits = 5; 
        uint16_t bIsWide : 1;                              
        uint16_t LowercaseProbeHash: ProbeHashBits;        
        uint16_t Len : 10;                                 
    };

    class FNameEntry {
      public:
        FNameEntryHeader Header;
        union {
            char AnsiName[1024];
            wchar_t WideName[1024];
        };

      public:
        int32_t GetLength() const;
        bool IsWide() const;
        int32_t GetId() const;
        std::string GetAnsiName() const;
        std::wstring GetWideName() const;
        std::string GetName() const;
    };

    class FNameEntryAllocator {
      private:
        uint8_t FrwLock[0x8];
      public:
        static const constexpr int32_t Stride    = 0x02;               
        static const constexpr int32_t MaxOffset = Stride * (1 << 16);
        int32_t CurrentBlock;                                          
        int32_t CurrentByteCursor;                                     
        uint8_t *Blocks[8192];                                         

      public:
        int32_t NumBlocks() const;
        FNameEntry *GetById(int32_t key) const;
        bool IsValidIndex(int32_t key) const;
        bool IsValidIndex(int32_t key, uint32_t block, uint16_t offset) const;
    };

    class FNamePool {
      public:
        FNameEntryAllocator Allocator;
        int32_t AnsiCount;
        int32_t WideCount;

      public:
        FNameEntry *GetNext(uintptr_t &nextFNameAddress, uint32_t *comparisonId) const;
        int32_t Count() const;
        bool IsValidIndex(int32_t index) const;
        FNameEntry *GetById(int32_t id) const;
        FNameEntry *operator[](int32_t id) const;
    };

    class FName {
      public:
        static FNamePool *GNames;
        int32_t ComparisonIndex;
        int32_t Number;

      public:
        FName();
        FName(int32_t i);
        FName(const char *nameToFind);
        FName(const wchar_t *nameToFind);
        static FNamePool &GetGlobalNames();
        std::string GetNameA() const;
        std::wstring GetNameW() const;
        std::string GetName() const;
    };
}
