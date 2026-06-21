#include "key_value_store.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace HogwartsMP::Core::Storage {

    void KeyValueStore::Set(const std::string &key, std::string value) {
        _data[key] = std::move(value);
    }

    std::optional<std::string> KeyValueStore::Get(const std::string &key) const {
        const auto it = _data.find(key);
        if (it == _data.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    bool KeyValueStore::Has(const std::string &key) const {
        return _data.find(key) != _data.end();
    }

    bool KeyValueStore::Erase(const std::string &key) {
        return _data.erase(key) > 0;
    }

    void KeyValueStore::Clear() {
        _data.clear();
    }

    std::vector<std::string> KeyValueStore::Keys() const {
        std::vector<std::string> keys;
        keys.reserve(_data.size());
        for (const auto &[key, _] : _data) {
            keys.push_back(key);
        }
        return keys;
    }

    size_t KeyValueStore::Size() const {
        return _data.size();
    }

    bool KeyValueStore::Load() {
        return _filePath.empty() ? false : LoadFrom(_filePath);
    }

    bool KeyValueStore::Save() const {
        return _filePath.empty() ? false : SaveTo(_filePath);
    }

    bool KeyValueStore::LoadFrom(const std::string &path) {
        std::ifstream in(path);
        if (!in.is_open()) {
            return false;
        }

        nlohmann::json doc;
        try {
            in >> doc;
        }
        catch (const nlohmann::json::exception &) {
            return false;
        }
        if (!doc.is_object()) {
            return false;
        }

        // Only commit to the live map once parsing fully succeeds.
        std::unordered_map<std::string, std::string> loaded;
        for (auto it = doc.begin(); it != doc.end(); ++it) {
            // Values are stored as strings; coerce non-string JSON (e.g. a hand-edited file) via dump
            // so a stray number/bool doesn't drop the whole entry.
            loaded[it.key()] = it->is_string() ? it->get<std::string>() : it->dump();
        }
        _data = std::move(loaded);
        return true;
    }

    bool KeyValueStore::SaveTo(const std::string &path) const {
        nlohmann::json doc = nlohmann::json::object();
        for (const auto &[key, value] : _data) {
            doc[key] = value;
        }

        // Write to a temp file, then atomically rename it over the target. A crash, full disk, or
        // power loss mid-write then leaves the previous file fully intact instead of a truncated or
        // empty one — the old open-with-trunc approach could wipe the whole store on a failed write.
        const std::string tmpPath = path + ".tmp";
        {
            std::ofstream out(tmpPath, std::ios::trunc | std::ios::binary);
            if (!out.is_open()) {
                return false;
            }
            out << doc.dump(2);
            out.flush();
            if (!out.good()) {
                return false;
            }
        } // close the stream before renaming

        std::error_code ec;
        std::filesystem::rename(tmpPath, path, ec); // atomic on the same filesystem; replaces target
        if (ec) {
            std::filesystem::remove(tmpPath, ec); // best-effort cleanup; ignore failure
            return false;
        }
        return true;
    }

} // namespace HogwartsMP::Core::Storage
