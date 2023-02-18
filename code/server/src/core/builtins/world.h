#pragma once

#include "scripting/engines/node/engine.h"
#include "scripting/engines/node/sdk.h"

#include "core_modules.h"

namespace HogwartsMP::Scripting {
    class World final {
      public:
        static void Register(v8::Isolate *isolate, v8pp::module *rootModule) {
            // Create the environment namespace
            // v8pp::module environment(isolate);

            // Create the vehicle namespace
            // v8pp::module world(isolate);
        }
    };
} // namespace HogwartsMP::Scripting
