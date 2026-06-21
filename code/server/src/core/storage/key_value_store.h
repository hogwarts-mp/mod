#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace HogwartsMP::Core::Storage {
    // A small persistent key/value store for server scripts. Values are opaque strings; callers that
    // want structured data store JSON text (the JS Storage builtin pairs this with JSON.stringify /
    // JSON.parse). Backed by a flat JSON object on disk ({ "key": "value", ... }). Pure C++ with no
    // V8 dependency so it is unit-testable in isolation; the scripting binding wraps a process-wide
    // instance of it.
    class KeyValueStore final {
      public:
        KeyValueStore() = default;
        // Sets the backing file path but does not read it; call Load() to populate from disk.
        explicit KeyValueStore(std::string filePath): _filePath(std::move(filePath)) {}

        // --- In-memory access ---
        void Set(const std::string &key, std::string value);
        std::optional<std::string> Get(const std::string &key) const;
        bool Has(const std::string &key) const;
        // Removes a key; returns true if it existed.
        bool Erase(const std::string &key);
        void Clear();
        std::vector<std::string> Keys() const;
        size_t Size() const;

        // --- Persistence ---
        // Load()/Save() use the path set at construction (or via SetFilePath); they no-op and return
        // false when no path is set. LoadFrom/SaveTo take an explicit path. Load replaces the current
        // contents on success and leaves them untouched on a missing file or parse error (returns
        // false). Save returns false on any I/O error.
        bool Load();
        bool Save() const;
        bool LoadFrom(const std::string &path);
        bool SaveTo(const std::string &path) const;

        const std::string &FilePath() const {
            return _filePath;
        }
        void SetFilePath(std::string path) {
            _filePath = std::move(path);
        }

      private:
        std::unordered_map<std::string, std::string> _data;
        std::string _filePath;
    };
} // namespace HogwartsMP::Core::Storage
