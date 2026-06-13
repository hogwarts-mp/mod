#include "human.h"

#include "events.h"

#include "core/server.h"

#include "shared/rpc/chat_message.h"

#include <logging/logger.h>
#include <world/modules/base.hpp>
#include <world/server.h>

namespace HogwartsMP::Scripting {

    std::unique_ptr<v8pp::class_<Human>> Human::_class;

    namespace {
        // Emit a player lifecycle event with a Human JS object argument
        void EmitHumanEvent(flecs::entity e, const std::string &eventName) {
            EmitServerEvent(eventName, [e](v8::Isolate *isolate, v8::Local<v8::Context>, std::vector<v8::Local<v8::Value>> &args) {
                args.push_back(v8pp::class_<Human>::create_object(isolate, e));
            });
        }
    } // namespace

    void Human::EventPlayerConnected(flecs::entity e) {
        Framework::Logging::GetLogger("Scripting")->debug("Player connected: {}", e.id());
        EmitHumanEvent(e, "playerConnect");
    }

    void Human::EventPlayerDisconnected(flecs::entity e) {
        Framework::Logging::GetLogger("Scripting")->debug("Player disconnected: {}", e.id());
        EmitHumanEvent(e, "playerDisconnect");
    }

    void Human::EventPlayerDied(flecs::entity e) {
        Framework::Logging::GetLogger("Scripting")->debug("Player died: {}", e.id());
        EmitHumanEvent(e, "playerDied");
    }

    std::string Human::ToString() const {
        std::ostringstream ss;
        ss << "Human{ id: " << _ent.id() << " }";
        return ss.str();
    }

    std::string Human::GetNickname() const {
        const auto streamer = _ent.try_get<Framework::World::Modules::Base::Streamer>();
        if (streamer) {
            return streamer->nickname;
        }
        return "";
    }

    void Human::SendChat(std::string message) {
        const auto streamer = _ent.try_get<Framework::World::Modules::Base::Streamer>();
        if (streamer) {
            FW_SEND_COMPONENT_RPC_TO(Shared::RPC::ChatMessage, MafiaNet::RakNetGUID(streamer->guid), message);
        }
    }

    void Human::Destroy() {
        // Real players (owner != 0) are torn down by the network/game systems on
        // disconnect — leave those alone. Server-owned entities (NPCs spawned via
        // World.spawnHuman, owner 0) must be removed explicitly; RemoveEntity
        // fires the despawn to every client streaming them.
        const auto streamable = _ent.try_get<Framework::World::Modules::Base::Streamable>();
        if (streamable && streamable->owner == 0) {
            Framework::World::ServerEngine::RemoveEntity(_ent);
        }
    }

    v8pp::class_<Human> &Human::GetClass(v8::Isolate *isolate) {
        if (!_class) {
            _class = std::make_unique<v8pp::class_<Human>>(isolate);
            _class->inherit<Entity>()
                .auto_wrap_objects(true)
                .ctor<flecs::entity_t>()
                .function("toString", &Human::ToString)
                .function("sendChat", &Human::SendChat)
                .function("destroy", &Human::Destroy);

            auto protoTemplate = _class->class_function_template()->PrototypeTemplate();

            // nickname (read-only)
            protoTemplate->SetNativeDataProperty(
                v8pp::to_v8(isolate, "nickname").As<v8::Name>(),
                [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value> &info) {
                    auto *self = v8pp::class_<Human>::unwrap_object(info.GetIsolate(), info.This());
                    if (self) info.GetReturnValue().Set(v8pp::to_v8(info.GetIsolate(), self->GetNickname()));
                });
        }
        return *_class;
    }

    void Human::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        v8pp::class_<Human> &cls = GetClass(isolate);
        auto ctx                 = isolate->GetCurrentContext();
        global->Set(ctx, v8pp::to_v8(isolate, "Human"), cls.js_function_template()->GetFunction(ctx).ToLocalChecked()).Check();
    }

} // namespace HogwartsMP::Scripting
