#pragma once

#include "integrations/server/scripting/builtins/node/entity.h"
#include "scripting/engines/node/engine.h"
#include "scripting/engines/node/sdk.h"
#include "shared/modules/human_sync.hpp"

#include "scripting/module.h"

namespace HogwartsMP::Scripting {
    class Human final: public Framework::Integrations::Scripting::Entity {
      public:
        Human(flecs::entity_t ent): Entity(ent) {}

        static v8::Local<v8::Object> WrapHuman(Framework::Scripting::Engines::Node::Resource *res, flecs::entity e) {
            V8_RESOURCE_LOCK(res);
            return v8pp::class_<Scripting::Human>::create_object(res->GetIsolate(), e.id());
        }


        std::string ToString() const override {
            std::ostringstream ss;
            ss << "Human{ id: " << _ent.id() << " }";
            return ss.str();
        }

        void Destruct(v8::Isolate *isolate) {
            isolate->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(isolate, "Human object can not be destroyed!").ToLocalChecked()));
        }

        static void EventPlayerConnected(flecs::entity e) {
            Framework::CoreModules::GetScriptingModule()->ForEachResource([&](Framework::Scripting::Engines::IResource *resource) {
                auto nodeResource = reinterpret_cast<Framework::Scripting::Engines::Node::Resource *>(resource);
                auto playerObj = WrapHuman(nodeResource, e);
                nodeResource->InvokeEvent("playerConnected", playerObj);
            });
        }

        static void EventPlayerDisconnected(flecs::entity e) {
            Framework::CoreModules::GetScriptingModule()->ForEachResource([&](Framework::Scripting::Engines::IResource *resource) {
                auto nodeResource = reinterpret_cast<Framework::Scripting::Engines::Node::Resource *>(resource);
                auto playerObj = WrapHuman(nodeResource, e);
                nodeResource->InvokeEvent("playerDisconnected", playerObj);
            });
        }

        static void Register(v8::Isolate *isolate, v8pp::module *rootModule) {
            if (!rootModule) {
                return;
            }

            v8pp::class_<Human> cls(isolate);
            cls.inherit<Framework::Integrations::Scripting::Entity>();
            cls.function("destruct", &Human::Destruct);
            rootModule->class_("Human", cls);
        }
    };
} // namespace HogwartsMP::Scripting
