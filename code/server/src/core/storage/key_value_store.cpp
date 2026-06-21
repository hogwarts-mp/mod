#include "key_value_store.h"

#include <nlohmann/json.hpp>

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

        std::ofstream out(path, std::ios::trunc);
        if (!out.is_open()) {
            return false;
        }
        out << doc.dump(2);
        return out.good();
    }

} // namespace HogwartsMP::Core::Storage
