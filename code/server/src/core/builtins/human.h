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
