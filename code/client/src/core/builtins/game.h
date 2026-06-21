#pragma once

#include <v8.h>

namespace HogwartsMP::Scripting {
    // Client-side game builtins, registered onto the V8 global by Application::ModuleRegister when a
    // connection's scripting engine comes up. This is the seam where client JS reaches the live game.
    // Currently exposes:
    //   Game.notify(text)            - append a line to the local chat UI
    //   Game.emitServer(name, json)  - send a named event up to the server's scripts
    //   LocalPlayer.getPosition()    - { x, y, z } | null (the local pawn's world location)
    //   LocalPlayer.getRotation()    - { pitch, yaw, roll } degrees | null
    // Future additions (more reads, HUD, and eventually a guarded reflection bridge) hang off here.
    class ClientGame final {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
    };
} // namespace HogwartsMP::Scripting
