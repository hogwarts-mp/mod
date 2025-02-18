#pragma once

#include <sol/sol.hpp>

#include "integrations/server/scripting/builtins/entity.h"

#include "shared/modules/human_sync.hpp"

namespace HogwartsMP::Scripting {
    class Human final: public Framework::Integrations::Scripting::Entity {
      public:
        Human(flecs::entity_t ent): Entity(ent) {
            const auto humanData = _ent.get<Shared::Modules::HumanSync::UpdateData>();

            if (!humanData) {
                throw std::runtime_error(fmt::format("Entity handle '{}' is not a Human!", ent));
            }
        }

        Human(flecs::entity ent): Human(ent.id()) {}

        std::string ToString() const override {
            std::ostringstream ss;
            ss << "Human{ id: " << _ent.id() << " }";
            return ss.str();
        }

        void Destory() {
            // Nothing should happen here, as the player entity is destroyed by the game and network systems
        }

        static void EventPlayerConnected(flecs::entity e) {
            const auto engine = HogwartsMP::Server::GetScriptingEngine();
            engine->InvokeEvent("onPlayerConnected", Human(e));
        }

        static void EventPlayerDisconnected(flecs::entity e) {
            const auto engine = HogwartsMP::Server::GetScriptingEngine();
            engine->InvokeEvent("onPlayerDisconnected", Human(e));
        }

        static void EventPlayerDied(flecs::entity e) {
            const auto engine = HogwartsMP::Server::GetScriptingEngine();
            engine->InvokeEvent("onPlayerDied", Human(e));
        }

        static void Register(sol::state& luaEngine) {
            // Create the Human usertype with inheritance from Entity
            auto humanType = luaEngine.new_usertype<Human>("Human",
                // Base class
                sol::base_classes, sol::bases<Framework::Integrations::Scripting::Entity>(),
        
                // Methods
                "destruct", &Human::Destory
            );
        }
    };
} // namespace HogwartsMP::Scripting
