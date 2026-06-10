#pragma once

#include <v8.h>
#include <v8pp/class.hpp>
#include <v8pp/convert.hpp>

#include "entity.h"

#include "shared/modules/human_sync.hpp"

#include <fmt/format.h>

#include <memory>
#include <string>

namespace HogwartsMP::Scripting {
    class Human final: public Entity {
      public:
        Human(flecs::entity_t ent): Entity(ent) {
            const auto humanData = _ent.try_get<Shared::Modules::HumanSync::UpdateData>();
            if (!humanData) {
                throw std::runtime_error(fmt::format("Entity handle '{}' is not a Human!", ent));
            }
        }

        Human(flecs::entity ent): Human(ent.id()) {}

        std::string ToString() const override;

        std::string GetNickname() const;

        void SendChat(std::string message);

        void Destroy();

        static void EventPlayerConnected(flecs::entity e);
        static void EventPlayerDisconnected(flecs::entity e);
        static void EventPlayerDied(flecs::entity e);

        static void Register(v8::Isolate *isolate, v8::Local<v8::Object> global);
        static v8pp::class_<Human> &GetClass(v8::Isolate *isolate);

      private:
        static std::unique_ptr<v8pp::class_<Human>> _class;
    };
} // namespace HogwartsMP::Scripting
