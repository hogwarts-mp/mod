#pragma once

#include <v8.h>

#include "entity.h"
#include "human.h"
#include "world.h"

namespace HogwartsMP::Scripting {
    class Builtins final {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global, v8::Local<v8::Object> frameworkObj) {
            if (!isolate || global.IsEmpty() || frameworkObj.IsEmpty()) {
                return;
            }

            // Register entity classes on the Framework object
            Scripting::Entity::Register(isolate, frameworkObj);
            Scripting::Human::Register(isolate, frameworkObj);

            // Register module singletons on global for direct access (World, Environment)
            Scripting::World::Register(isolate, global);
        }
    };
} // namespace HogwartsMP::Scripting
