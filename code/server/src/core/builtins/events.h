#pragma once

#include <v8.h>

#include <functional>
#include <string>
#include <vector>

namespace HogwartsMP::Scripting {
    using EventArgsBuilder = std::function<void(v8::Isolate *, v8::Local<v8::Context>, std::vector<v8::Local<v8::Value>> &)>;

    /**
     * Emit an event from native code into the server's JS resources.
     * Acquires the engine's isolate/context (with locking) and forwards the
     * args produced by buildArgs to Events::EmitReserved. No-op when the
     * scripting engine is not available or not initialized.
     */
    void EmitServerEvent(const std::string &eventName, const EventArgsBuilder &buildArgs);

    /**
     * Number of JS listeners currently registered for the given event,
     * or 0 when the scripting engine is unavailable.
     */
    size_t GetServerEventListenerCount(const std::string &eventName);
} // namespace HogwartsMP::Scripting
