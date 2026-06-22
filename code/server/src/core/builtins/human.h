#pragma once

#include <v8.h>
#include <v8pp/class.hpp>
#include <v8pp/convert.hpp>

#include <scripting/builtins/player.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace HogwartsMP::Scripting {
    // A player avatar exposed to JS. Derives the framework Player handle (id/position/rotation/kick,
    // all resolved from the live NetworkEntity by NetworkID) and adds the Hogwarts-specific surface.
    class Human final: public Framework::Scripting::Builtins::Player {
      public:
        Human(uint64_t networkId): Framework::Scripting::Builtins::Player(networkId) {}

        std::string ToString() const override;

        std::string GetNickname() const;

        void SendChat(std::string message);

        // Emit a named event to this player's client scripts (Core.Events). payloadJson is sent as-is
        // and JSON.parsed on the client into the handler's single argument; pass JSON text.
        void Emit(std::string eventName, std::string payloadJson);

        // Per-player persistent data, keyed to the player's stable identity (survives reconnect), not
        // the network id. Backed by the Storage builtin's store, namespaced per player; values are
        // strings (use JSON.stringify/parse). No-op / undefined for entities without an identity (NPCs).
        void SetData(std::string key, std::string value);
        bool HasData(std::string key);
        bool DeleteData(std::string key);
        static void JsGetData(const v8::FunctionCallbackInfo<v8::Value> &info);

        void Destroy();

        static void EventPlayerConnected(uint64_t networkId);
        static void EventPlayerDisconnected(uint64_t networkId);
        static void EventPlayerDied(uint64_t networkId);

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
        static v8pp::class_<Human> &GetClass(v8::Isolate *isolate);

      private:
        // Keyed by isolate (like the framework's Entity/Player) so a second isolate in the same
        // process gets its own class rather than reusing one bound to a destroyed isolate.
        inline static std::unordered_map<v8::Isolate *, std::unique_ptr<v8pp::class_<Human>>> _classes;
    };
} // namespace HogwartsMP::Scripting
