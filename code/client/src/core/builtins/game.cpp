#include "game.h"

#include "core/application.h"

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
    } // namespace

    void ClientGame::Register(v8::Isolate *isolate, v8::Local<v8::Object> global) {
        if (!isolate || global.IsEmpty()) {
            return;
        }
        auto ctx = isolate->GetCurrentContext();

        v8pp::module gameModule(isolate);
        gameModule.function("notify", &Notify);
        global->Set(ctx, v8pp::to_v8(isolate, "Game"), gameModule.new_instance()).Check();
    }
} // namespace HogwartsMP::Scripting
