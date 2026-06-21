#pragma once

#include "core/storage/key_value_store.h"

#include <cstdio>

MODULE(storage, {
    using HogwartsMP::Core::Storage::KeyValueStore;

    IT("returns nullopt for a missing key", {
        KeyValueStore store;
        EQUALS(store.Has("missing"), false);
        EQUALS(store.Get("missing").has_value(), false);
        EQUALS(store.Size(), (size_t)0);
    });

    IT("stores and reads back a value", {
        KeyValueStore store;
        store.Set("house", "Ravenclaw");
        EQUALS(store.Has("house"), true);
        EQUALS(store.Get("house").has_value(), true);
        STREQUALS(store.Get("house").value().c_str(), "Ravenclaw");
        EQUALS(store.Size(), (size_t)1);
    });

    IT("overwrites an existing key without growing", {
        KeyValueStore store;
        store.Set("points", "10");
        store.Set("points", "25");
        STREQUALS(store.Get("points").value().c_str(), "25");
        EQUALS(store.Size(), (size_t)1);
    });

    IT("erases a key and reports whether it existed", {
        KeyValueStore store;
        store.Set("k", "v");
        EQUALS(store.Erase("k"), true);
        EQUALS(store.Has("k"), false);
        EQUALS(store.Erase("k"), false); // already gone
    });

    IT("clears all entries", {
        KeyValueStore store;
        store.Set("a", "1");
        store.Set("b", "2");
        store.Clear();
        EQUALS(store.Size(), (size_t)0);
    });

    IT("round-trips through a file", {
        const char *path = "test_kv_store.json";
        std::remove(path);

        KeyValueStore writer;
        writer.Set("house", "Slytherin");
        writer.Set("points", "42");
        EQUALS(writer.SaveTo(path), true);

        KeyValueStore reader;
        EQUALS(reader.LoadFrom(path), true);
        EQUALS(reader.Size(), (size_t)2);
        STREQUALS(reader.Get("house").value().c_str(), "Slytherin");
        STREQUALS(reader.Get("points").value().c_str(), "42");

        std::remove(path);
    });

    IT("load replaces prior contents and missing files fail cleanly", {
        const char *path = "test_kv_store_missing.json";
        std::remove(path); // ensure absent

        KeyValueStore store;
        store.Set("stale", "x");
        EQUALS(store.LoadFrom(path), false); // missing file
        EQUALS(store.Has("stale"), true);    // unchanged on failure
    });

    IT("uses the configured path for Load/Save and no-ops without one", {
        KeyValueStore noPath;
        EQUALS(noPath.Save(), false);
        EQUALS(noPath.Load(), false);

        const char *path = "test_kv_store_pathed.json";
        std::remove(path);
        KeyValueStore store(path);
        store.Set("k", "v");
        EQUALS(store.Save(), true);

        KeyValueStore reload(path);
        EQUALS(reload.Load(), true);
        STREQUALS(reload.Get("k").value().c_str(), "v");

        std::remove(path);
    });
});
