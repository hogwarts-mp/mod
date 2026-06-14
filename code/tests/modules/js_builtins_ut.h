#pragma once

#include "scripting/node_engine.h"

#include "core/builtins/builtins.h"

MODULE(js_builtins, {
    using namespace Framework::Scripting;

    IT("registers World, Environment and entity classes into a JS context", {
        NodeEngine engine({});
        EQUALS(engine.Init(), ScriptingError::SCRIPTING_NONE);

        // V8 scopes must exit before Shutdown() is called
        {
            v8::Isolate *isolate = engine.GetIsolate();
            v8::Locker locker(isolate);
            v8::Isolate::Scope isolateScope(isolate);
            v8::HandleScope handleScope(isolate);
            v8::Local<v8::Context> context = engine.GetContext();
            v8::Context::Scope contextScope(context);

            v8::Local<v8::Object> global       = context->Global();
            v8::Local<v8::Object> frameworkObj = v8::Object::New(isolate);
            global->Set(context, v8pp::to_v8(isolate, "Framework"), frameworkObj).Check();

            HogwartsMP::Scripting::Builtins::Register(isolate, global, frameworkObj);

            // v8pp keeps two function templates per class: the JS-exposed constructor
            // (js_function_template) and an internal class template whose prototype
            // carries the registered methods. Wrapped instances created from C++
            // (create_object) receive the internal prototype, so method presence must
            // be probed there - it is not visible on Framework.Human.prototype.
            const auto exposeClassPrototype = [&](const char *name, v8::Local<v8::FunctionTemplate> tmpl) {
                v8::Local<v8::Function> fn = tmpl->GetFunction(context).ToLocalChecked();
                global->Set(context, v8pp::to_v8(isolate, name), fn).Check();
            };
            exposeClassPrototype("EntityImpl", Framework::Scripting::Builtins::Entity::GetClass(isolate).class_function_template());
            exposeClassPrototype("HumanImpl", HogwartsMP::Scripting::Human::GetClass(isolate).class_function_template());

            const auto evalBool = [&](const char *src) -> bool {
                v8::Local<v8::String> source = v8::String::NewFromUtf8(isolate, src).ToLocalChecked();
                v8::Local<v8::Script> script = v8::Script::Compile(context, source).ToLocalChecked();
                v8::Local<v8::Value> result  = script->Run(context).ToLocalChecked();
                return result->BooleanValue(isolate);
            };

            // Module singletons on the global object
            EQUALS(evalBool("typeof World.broadcastMessage === 'function'"), true);
            EQUALS(evalBool("typeof World.sendChatMessage === 'function'"), true);
            EQUALS(evalBool("typeof Environment.setWeather === 'function'"), true);
            EQUALS(evalBool("typeof Environment.setTime === 'function'"), true);
            EQUALS(evalBool("typeof Environment.setDate === 'function'"), true);
            EQUALS(evalBool("typeof Environment.setSeason === 'function'"), true);

            // Entity classes on the Framework object, with the inherit chain intact
            EQUALS(evalBool("typeof Framework.Entity === 'function'"), true);
            EQUALS(evalBool("typeof Framework.Human === 'function'"), true);
            EQUALS(evalBool("Framework.Human.prototype instanceof Framework.Entity"), true);

            // Methods and accessors on the instance-facing prototypes
            EQUALS(evalBool("typeof EntityImpl.prototype.toString === 'function'"), true);
            EQUALS(evalBool("Object.getOwnPropertyNames(EntityImpl.prototype).includes('position')"), true);
            EQUALS(evalBool("Object.getOwnPropertyNames(EntityImpl.prototype).includes('rotation')"), true);
            EQUALS(evalBool("typeof HumanImpl.prototype.sendChat === 'function'"), true);
            EQUALS(evalBool("typeof HumanImpl.prototype.destroy === 'function'"), true);
            EQUALS(evalBool("Object.getOwnPropertyNames(HumanImpl.prototype).includes('nickname')"), true);
        }

        engine.Shutdown();
    });
});
