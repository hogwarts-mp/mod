#pragma once

#include <v8.h>

#include <scripting/builtins/entity.h>

#include "human.h"
#include "storage.h"
#include "world.h"

namespace HogwartsMP::Scripting {
    class Builtins final {
      public:
        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global, v8::Local<v8::Object> frameworkObj) {
            if (!isolate || global.IsEmpty() || frameworkObj.IsEmpty()) {
                return;
            }

            // Base entity handle (framework, header-only). Human inherits its Player subclass.
            Framework::Scripting::Builtins::Entity::Register(isolate, frameworkObj);
            Scripting::Human::Register(isolate, frameworkObj);

            // Register module singletons on global for direct access (World, Environment, Storage).
            Scripting::World::Register(isolate, global);
            Scripting::Storage::Register(isolate, global);
        }
    };
} // namespace HogwartsMP::Scripting
