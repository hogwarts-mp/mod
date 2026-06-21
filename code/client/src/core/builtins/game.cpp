#include "game.h"

#include "core/application.h"
#include "core/ue4_reflection.h"

#include <integrations/shared/rpc/emit_lua_event.h>

#include <v8.h>
#include <v8pp/convert.hpp>
#include <v8pp/module.hpp>

#include <string>
#include <utility>

namespace HogwartsMP::Scripting {
    namespace {
        // Game.notify(text): show a line in the local chat UI. Safe and read-only w.r.t. game state —
        // it only touches the mod's own UI — which makes it the right first end-to-end smoke test for
        // client scripting (server/client JS can surface messages without any game-mutating power).
        void Notify(std::string text) {
            if (auto *app = HogwartsMP::Core::gApplication.get()) {
                if (auto chat = app->GetChat()) {
                    chat->AddMessage(std::move(text));
                }
            }
        }

        // Game.emitServer(name, payloadJson): send a named event up to the server's scripts. The
        // client has a single peer (the server), so BroadcastRPC reaches it. Server scripts receive it
        // as Core.Events.on(name, (player, payload) => ...); payloadJson is JSON.parsed there, so pass
        // JSON text (e.g. JSON.stringify(obj)).
        void EmitServer(std::string eventName, std::string payloadJson) {
            auto *app = HogwartsMP::Core::gApplication.get();
            if (!app) {
                return;
            }
            auto *engine = app->GetNetworkingEngine();
            auto *net    = engine ? engine->GetNetworkClient() : nullptr;
            if (!net) {
                return;
            }
            Framework::Integrations::Shared::RPC::EmitLuaEvent ev;
            ev.FromParameters(eventName, payloadJson);
            net->BroadcastRPC(ev);
        }

        // UFunction param blocks — layout must match the engine's K2_GetActor* signatures exactly.
        // float members match UE4's FVector/FRotator (Hogwarts Legacy is UE4.27);
        struct Vec3f {
            float X, Y, Z;
        };
        struct Rot3f {
            float Pitch, Yaw, Roll;
        };

        // The local player's pawn, or nullptr when there's no current pawn (loading/menu/torn down).
        // Re-derived live each call rather than read from gGlobals.localBipedPlayer: that latch is
        // write-once and never cleared on pawn destruction, so trusting it would let a script call
        // ProcessEvent on a dangling UObject (use-after-free) after fast-travel/menu/disconnect.
        void *LocalActor() {
            return HogwartsMP::Core::GetLiveLocalBiped();
        }

        // LocalPlayer.getPosition() -> { x, y, z } | null (null until the local pawn exists). Reads the
        // pawn's WORLD location through the game's own getter. Runs on the game thread (the scripting
        // engine ticks there), which CallUFunction requires.
        void JsGetPosition(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto ctx      = isolate->GetCurrentContext();
            void *actor   = LocalActor();
            if (!actor) {
                info.GetReturnValue().SetNull();
                return;
            }
            Vec3f loc {};
            if (!HogwartsMP::Core::UE4::CallUFunction(actor, "K2_GetActorLocation", &loc)) {
                info.GetReturnValue().SetNull(); // contract: null on failure, not a bogus {0,0,0}
                return;
            }
            auto obj = v8::Object::New(isolate);
            obj->Set(ctx, v8pp::to_v8(isolate, "x"), v8pp::to_v8(isolate, loc.X)).Check();
            obj->Set(ctx, v8pp::to_v8(isolate, "y"), v8pp::to_v8(isolate, loc.Y)).Check();
            obj->Set(ctx, v8pp::to_v8(isolate, "z"), v8pp::to_v8(isolate, loc.Z)).Check();
            info.GetReturnValue().Set(obj);
        }

        // LocalPlayer.getRotation() -> { pitch, yaw, roll } (degrees) | null.
        void JsGetRotation(const v8::FunctionCallbackInfo<v8::Value> &info) {
            auto *isolate = info.GetIsolate();
            auto ctx      = isolate->GetCurrentContext();
            void *actor   = LocalActor();
            if (!actor) {
                info.GetReturnValue().SetNull();
                return;
            }
            Rot3f rot {};
            if (!HogwartsMP::Core::UE4::CallUFunction(actor, "K2_GetActorRotation", &rot)) {
                info.GetReturnValue().SetNull(); // contract: null on failure, not a bogus {0,0,0}
                return;
            }
            auto obj = v8::Object::New(isolate);
            obj->Set(ctx, v8pp::to_v8(isolate, "pitch"), v8pp::to_v8(isolate, rot.Pitch)).Check();
            obj->Set(ctx, v8pp::to_v8(isolate, "yaw"), v8pp::to_v8(isolate, rot.Yaw)).Check();
            obj->Set(ctx, v8pp::to_v8(isolate, "roll"), v8pp::to_v8(isolate, rot.Roll)).Check();
            info.GetReturnValue().Set(obj);
        }
    } // namespace

    void ClientGame::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (!isolate || global.IsEmpty()) {
            return;
        }
        auto ctx = isolate->GetCurrentContext();

        v8pp::module gameModule(isolate);
        gameModule.function("notify", &Notify);
        gameModule.function("emitServer", &EmitServer);
        global->Set(ctx, v8pp::to_v8(isolate, "Game"), gameModule.new_instance()).Check();

        // LocalPlayer: read-only access to the local pawn's live transform. getPosition/getRotation
        // need the isolate + return objects, so they're raw FunctionTemplates rather than v8pp funcs.
        auto localPlayer = v8::Object::New(isolate);
        localPlayer->Set(ctx, v8pp::to_v8(isolate, "getPosition"),
                         v8::FunctionTemplate::New(isolate, &JsGetPosition)->GetFunction(ctx).ToLocalChecked())
            .Check();
        localPlayer->Set(ctx, v8pp::to_v8(isolate, "getRotation"),
                         v8::FunctionTemplate::New(isolate, &JsGetRotation)->GetFunction(ctx).ToLocalChecked())
            .Check();
        global->Set(ctx, v8pp::to_v8(isolate, "LocalPlayer"), localPlayer).Check();
    }
} // namespace HogwartsMP::Scripting
