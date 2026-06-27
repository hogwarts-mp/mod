#include "human.h"

#include "events.h"
#include "storage.h"

#include "core/server.h"

#include "shared/game/human.h"
#include "shared/rpc/set_appearance.h"

#include <core_modules.h>
#include <integrations/shared/rpc/emit_lua_event.h>
#include <networking/network_peer.h>
#include <networking/replication/replication_manager.h>
#include <networking/rpc/chat_message.h>

#include <logging/logger.h>

#include <mafianet/types.h>

#include <sstream>

namespace HogwartsMP::Scripting {

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

        // Storage key for a player's namespaced data, or "" when the entity has no stable identity
        // (a server NPC, or identity not yet recorded). hwid-backed via the server map today; the
        // identity source can change later without touching the script-facing API.
        std::string PlayerDataKey(uint64_t networkId, const std::string &userKey) {
            auto *server = Server::_serverRef;
            if (!server) {
                return "";
            }
            const std::string id = server->GetPlayerIdentity(networkId);
            return id.empty() ? std::string() : "player:" + id + ":" + userKey;
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

    void Human::Emit(std::string eventName, std::string payloadJson) {
        const auto *human = ResolveHuman(GetId());
        if (!human) {
            return;
        }
        auto *peer = Framework::CoreModules::GetNetworkPeer();
        if (!peer) {
            return;
        }
        // Delivered to this player's client scripts as Core.Events.on(eventName, payload); the client
        // JSON.parses payloadJson into the single handler arg, so callers pass JSON text (e.g.
        // JSON.stringify(obj)). Empty payload -> the handler is called with no argument.
        Framework::Integrations::Shared::RPC::EmitLuaEvent ev;
        ev.FromParameters(eventName, payloadJson);
        peer->SendRPC(ev, MafiaNet::ToGuid(human->ownerGUID));
    }

    void Human::SetData(std::string key, std::string value) {
        const std::string storageKey = PlayerDataKey(GetId(), key);
        if (storageKey.empty()) {
            return;
        }
        Storage::Store().Set(storageKey, std::move(value));
        if (!Storage::Store().Save()) {
            Framework::Logging::GetLogger("Scripting")->warn("player.setData('{}') failed to persist", key);
        }
    }

    bool Human::HasData(std::string key) {
        const std::string storageKey = PlayerDataKey(GetId(), key);
        return !storageKey.empty() && Storage::Store().Has(storageKey);
    }

    bool Human::DeleteData(std::string key) {
        const std::string storageKey = PlayerDataKey(GetId(), key);
        if (storageKey.empty()) {
            return false;
        }
        const bool erased = Storage::Store().Erase(storageKey);
        if (erased && !Storage::Store().Save()) {
            Framework::Logging::GetLogger("Scripting")->warn("player.deleteData('{}') failed to persist", key);
        }
        return erased;
    }

    void Human::JsGetData(const v8::FunctionCallbackInfo<v8::Value> &info) {
        auto *isolate = info.GetIsolate();
        if (info.Length() < 1 || !info[0]->IsString()) {
            isolate->ThrowException(v8::Exception::TypeError(v8pp::to_v8(isolate, "getData(key) requires a string key")));
            return;
        }
        auto *self = v8pp::class_<Human>::unwrap_object(isolate, info.This());
        if (!self) {
            info.GetReturnValue().SetUndefined();
            return;
        }
        const auto key               = v8pp::from_v8<std::string>(isolate, info[0]);
        const std::string storageKey = PlayerDataKey(self->GetId(), key);
        if (storageKey.empty()) {
            info.GetReturnValue().SetUndefined();
            return;
        }
        const auto value = Storage::Store().Get(storageKey);
        if (value) {
            info.GetReturnValue().Set(v8pp::to_v8(isolate, *value));
        }
        else {
            info.GetReturnValue().SetUndefined();
        }
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

    // Copy another human's worn CCD onto this one (sanitized) and broadcast it — used by the /mirrornpcs dev
    // command to clone a player's look onto NPCs. Rides future construction snapshots; AppearanceUpdate
    // dresses peers already streaming this entity.
    void Human::MirrorAppearanceFrom(double sourceNetworkId) {
        auto *target = ResolveHuman(GetId());
        auto *source = ResolveHuman(static_cast<uint64_t>(sourceNetworkId));
        if (!target || !source) {
            return;
        }
        auto ccd = source->ccd;
        Shared::Modules::SanitizeCcd(ccd);
        target->ccd = ccd;
        auto *peer  = Framework::CoreModules::GetNetworkPeer();
        if (!peer) {
            return;
        }
        Shared::RPC::AppearanceUpdate upd;
        upd.networkId = target->GetNetworkID();
        upd.ccd       = std::move(ccd);
        peer->BroadcastRPC(upd);
    }

    void Human::SetInAir(bool inAir) {
        if (auto *e = ResolveHuman(GetId())) {
            e->SetFlag(Shared::Modules::HumanSync::InAir, inAir);
        }
    }

    v8pp::class_<Human> &Human::GetClass(v8::Isolate *isolate) {
        auto it = _classes.find(isolate);
        if (it != _classes.end()) {
            return *it->second;
        }

        // v8pp inherit<Player> requires Player (and its Entity base) registered first.
        Framework::Scripting::Builtins::Player::GetClass(isolate);

        auto &cls = _classes[isolate];
        cls = std::make_unique<v8pp::class_<Human>>(isolate);
        cls->inherit<Framework::Scripting::Builtins::Player>()
            .auto_wrap_objects(true)
            .ctor<uint64_t>()
            .function("toString", &Human::ToString)
            .function("sendChat", &Human::SendChat)
            .function("setInAir", &Human::SetInAir)
            .function("emit", &Human::Emit)
            .function("setData", &Human::SetData)
            .function("hasData", &Human::HasData)
            .function("deleteData", &Human::DeleteData)
            .function("destroy", &Human::Destroy)
            .function("mirrorAppearanceFrom", &Human::MirrorAppearanceFrom);

        auto protoTemplate = cls->class_function_template()->PrototypeTemplate();

        // nickname (read-only)
        protoTemplate->SetNativeDataProperty(
            v8pp::to_v8(isolate, "nickname").As<v8::Name>(),
            [](v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value> &info) {
                auto *self = v8pp::class_<Human>::unwrap_object(info.GetIsolate(), info.This());
                if (self) info.GetReturnValue().Set(v8pp::to_v8(info.GetIsolate(), self->GetNickname()));
            });

        // getData returns undefined for a missing key, so it's a raw FunctionTemplate (like Storage.get)
        // rather than a typed v8pp function.
        protoTemplate->Set(
            v8pp::to_v8(isolate, "getData").As<v8::Name>(),
            v8::FunctionTemplate::New(isolate, &Human::JsGetData));
        return *cls;
    }

    void Human::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        v8pp::class_<Human> &cls = GetClass(isolate);
        auto ctx                 = isolate->GetCurrentContext();
        global->Set(ctx, v8pp::to_v8(isolate, "Human"), cls.js_function_template()->GetFunction(ctx).ToLocalChecked()).Check();
    }

} // namespace HogwartsMP::Scripting
