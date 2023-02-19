#pragma once

#include "scripting/engines/node/engine.h"
#include "scripting/engines/node/sdk.h"

#include "player.h"
#include "world.h"

namespace HogwartsMP::Scripting {
    class Builtins final {
      public:
        static void Register(v8::Isolate *isolate, v8pp::module *rootModule) {
            Scripting::World::Register(isolate, rootModule);
            Scripting::Human::Register(isolate, rootModule);
        }
    };
} // namespace HogwartsMP::Scripting
