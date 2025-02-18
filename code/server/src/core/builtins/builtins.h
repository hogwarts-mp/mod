#pragma once

#include <sol/sol.hpp>

#include "scripting/server_engine.h"

#include "player.h"
#include "world.h"

namespace HogwartsMP::Scripting {
    class Builtins final {
      public:
        static void Register(sol::state &luaEngine) {
            Scripting::Human::Register(luaEngine);
            Scripting::World::Register(luaEngine);
        }
    };
} // namespace HogwartsMP::Scripting
