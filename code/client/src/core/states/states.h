#pragma once

#include <cstdint>

namespace HogwartsMP::Core::States {
    enum StateIds : int32_t { Initialize, Menu, SessionConnection, SessionConnected, SessionDisconnection, SessionOfflineDebug, Shutdown };
}
