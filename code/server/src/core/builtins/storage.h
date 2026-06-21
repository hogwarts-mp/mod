#pragma once

#include <v8.h>
#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include "core/storage/key_value_store.h"

#include <logging/logger.h>

#include <string>
#include <vector>

namespace HogwartsMP::Scripting {
    // The `Storage` global: a persistent key/value store for gamemode scripts. Values are strings —
    // scripts wrap structured data with JSON.stringify / JSON.parse. Backed by a single process-wide
    // KeyValueStore persisted to STORAGE_FILE next to the server's working directory; every write is
    // flushed immediately so state survives a restart or crash.
    //
    // JS surface:
    //   Storage.set(key, value)   persist a string value (overwrites)
    //   Storage.get(key)          -> string | undefined
    //   Storage.has(key)          -> boolean
    //   Storage.delete(key)       -> boolean (true if a key was removed)
    //   Storage.keys()            -> string[]
    //
    // v1 is a single global namespace. Per-player persistence is intentionally deferred: it needs a
    // stable player identity that survives reconnect (no account system exists yet), so scripts that
    // want per-player data should key by their own stable id for now (e.g. "house:" + accountId).
    class Storage final {
      public:
        static constexpr const char *STORAGE_FILE = "storage.json";

        // Process-wide store backing the JS global, lazily loaded from disk on first use.
        static Core::Storage::KeyValueStore &Store() {
            static Core::Storage::KeyValueStore store = [] {
                Core::Storage::KeyValueStore s(STORAGE_FILE);
                s.Load(); // missing file is fine — starts empty
                return s;
            }();
            return store;
        }

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
            if (!isolate || global.IsEmpty()) {
                return;
            }
            auto ctx = isolate->GetCurrentContext();

            v8pp::module storageModule(isolate);
            storageModule.function("set", &Storage::JsSet);
            storageModule.function("has", &Storage::Has);
            storageModule.function("delete", &Storage::Delete);
            storageModule.function("keys", &Storage::Keys);
            auto storageObj = storageModule.new_instance();
            // get returns undefined for a missing key, so it needs raw isolate access rather than a
            // typed v8pp function (which can't express the optional return).
            storageObj->Set(ctx, v8pp::to_v8(isolate, "get"),
                            v8::FunctionTemplate::New(isolate, &Storage::JsGet)->GetFunction(ctx).ToLocalChecked())
                .Check();
            global->Set(ctx, v8pp::to_v8(isolate, "Storage"), storageObj).Check();
        }

      private:
        static void JsSet(std::string key, std::string value) {
            Store().Set(key, std::move(value));
            if (!Store().Save()) {
                Framework::Logging::GetLogger("Scripting")->warn("Storage.set('{}') failed to persist to {}", key, STORAGE_FILE);
            }
        }

        static void JsGet(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            if (info.Length() < 1 || !info[0]->IsString()) {
                isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "get(key) requires a string key")));
                return;
            }
            const auto key   = v8pp::from_v8<std::string>(isolate, info[0]);
            const auto value = Store().Get(key);
            if (value) {
                info.GetReturnValue().Set(v8pp::to_v8(isolate, *value));
            }
            else {
                info.GetReturnValue().SetUndefined();
            }
        }

        static bool Has(std::string key) {
            return Store().Has(key);
        }

        static bool Delete(std::string key) {
            const bool erased = Store().Erase(key);
            if (erased && !Store().Save()) {
                Framework::Logging::GetLogger("Scripting")->warn("Storage.delete('{}') failed to persist to {}", key, STORAGE_FILE);
            }
            return erased;
        }

        static std::vector<std::string> Keys() {
            return Store().Keys();
        }
    };
} // namespace HogwartsMP::Scripting
