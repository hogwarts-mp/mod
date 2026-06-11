#include "events.h"

#include "core/server.h"

#include <integrations/server/scripting/module.h>
#include <scripting/node_engine.h>
#include <scripting/resource/resource_manager.h>

namespace HogwartsMP::Scripting {

    void EmitServerEvent(const std::string &eventName, const EventArgsBuilder &buildArgs) {
        const auto server = HogwartsMP::Server::_serverRef;
        if (!server)
            return;

        const auto scriptingModule = server->GetScriptingModule();
        if (!scriptingModule)
            return;

        auto *engine          = scriptingModule->GetEngine();
        auto *resourceManager = scriptingModule->GetResourceManager();
        if (!engine || !resourceManager || !engine->IsInitialized())
            return;

        v8::Isolate *isolate = engine->GetIsolate();
        v8::Locker locker(isolate);
        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);
        v8::Local<v8::Context> context = engine->GetContext();
        v8::Context::Scope contextScope(context);

        std::vector<v8::Local<v8::Value>> args;
        buildArgs(isolate, context, args);

        resourceManager->GetEvents().EmitReserved(isolate, context, eventName, args);
    }

    size_t GetServerEventListenerCount(const std::string &eventName) {
        const auto server = HogwartsMP::Server::_serverRef;
        if (!server)
            return 0;

        const auto scriptingModule = server->GetScriptingModule();
        if (!scriptingModule)
            return 0;

        auto *resourceManager = scriptingModule->GetResourceManager();
        if (!resourceManager)
            return 0;

        return resourceManager->GetEvents().GetListenerCount(eventName);
    }

} // namespace HogwartsMP::Scripting
