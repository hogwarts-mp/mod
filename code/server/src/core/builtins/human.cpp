#include "human.h"

#include "events.h"

#include "shared/game/human.h"

#include <core_modules.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>
#include <networking/rpc/chat_message.h>

#include <logging/logger.h>

#include <mafianet/types.h>

#include <sstream>

namespace HogwartsMP::Scripting {

    std::unique_ptr<v8pp::class_<Human>> Human::_class;

    namespace {
        Shared::HumanEntity *ResolveHuman(uint64_t networkId) {
            auto *repl = Framework::CoreModules::GetReplication();
            return repl ? dynamic_cast<Shared::HumanEntity *>(repl->GetEntityByNetworkID(networkId)) : nullptr;
        }

        // Emit a player lifecycle event with a Human JS object argument.
        void EmitHumanEvent(uint64_t networkId, const std::string &eventName) {
            EmitServerEvent(eventName, [networkId](v8::Isolate *isolate, v8::Local<v8::Context>, std::vector<v8::Local<v8::Value>> &args) {
                args.push_back(v8pp::class_<Human>::create_object(isolate, networkId));
            });
        }
    } // namespace

    void Human::EventPlayerConnected(uint64_t networkId) {
        Framework::Logging::GetLogger("Scripting")->debug("Player connected: {}", networkId);
        EmitHumanEvent(networkId, "playerConnect");
    }

    void Human::EventPlayerDisconnected(uint64_t networkId) {
        Framework::Logging::GetLogger("Scripting")->debug("Player disconnected: {}", networkId);
        EmitHumanEvent(networkId, "playerDisconnect");
    }

    void Human::EventPlayerDied(uint64_t networkId) {
        Framework::Logging::GetLogger("Scripting")->debug("Player died: {}", networkId);
        EmitHumanEvent(networkId, "playerDied");
    }

    std::string Human::ToString() const {
        std::ostringstream ss;
        ss << "Human{ id: " << GetId() << " }";
        return ss.str();
    }

    std::string Human::GetNickname() const {
        const auto *human = ResolveHuman(GetId());
        return human ? human->nickname : "";
    }

    void Human::SendChat(std::string message) {
        const auto *human = ResolveHuman(GetId());
        if (!human) {
            return;
        }
        auto *peer = Framework::CoreModules::GetNetworkPeer();
        if (!peer) {
            return;
        }
        Framework::Networking::RPC::ChatMessage payload {std::move(message)};
        peer->SendRPC(payload, MafiaNet::ToGuid(human->ownerGUID));
    }

    void Human::Destroy() {
        auto *human = ResolveHuman(GetId());
        auto *repl  = Framework::CoreModules::GetReplication();
        if (!human || !repl) {
            return;
        }
        // Real players (owned) are torn down by the network layer on disconnect — leave those alone.
        // Server-owned entities (NPCs spawned via World.spawnHuman) must be removed explicitly;
        // DestroyEntity broadcasts the despawn to every client streaming them.
        if (human->ownerGUID == MafiaNet::UNASSIGNED_PEER_GUID) {
            repl->DestroyEntity(human);
        }
    }

    v8pp::class_<Human> &Human::GetClass(v8::Isolate *isolate) {
        if (!_class) {
            // v8pp inherit<Player> requires Player (and its Entity base) registered first.
            Framework::Scripting::Builtins::Player::GetClass(isolate);

            _class = std::make_unique<v8pp::class_<Human>>(isolate);
            _class->inherit<Framework::Scripting::Builtins::Player>()
                .auto_wrap_objects(true)
                .ctor<uint64_t>()
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
