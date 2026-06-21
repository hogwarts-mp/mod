#pragma once

#include <v8.h>

namespace HogwartsMP::Scripting {
    // Client-side game builtins, registered onto the V8 global by Application::ModuleRegister when a
    // connection's scripting engine comes up. This is the seam where client JS reaches the live game;
    // v1 exposes a single safe, concrete call (Game.notify) to prove the path end-to-end. Future
    // additions (local-player reads, HUD, and eventually a guarded reflection bridge) hang off here.
    class ClientGame final {
      public:
        // Game.notify(text) — append a line to the local chat/notification UI.
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
    };
} // namespace HogwartsMP::Scripting
