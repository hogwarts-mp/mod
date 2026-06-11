#pragma once

// Non-aborting AOB scan helpers.
//
// hook::pattern(...).get_first() and hook::get_opcode_address(...) both call
// count(1) internally, which assert()s that the pattern matched EXACTLY once.
// On a game build the mod's patterns weren't written for, a stale pattern
// (0 or >1 matches) aborts the whole DLL load on the first bad scan — so we
// never learn which other patterns are also broken.
//
// These wrappers log the live match count for each named pattern (from the
// central core/game_layout.h table) and return the first match (or nullptr / 0)
// instead of aborting. So a stale pattern downgrades the whole-DLL abort to a
// single logged miss + a skipped hook, and one launch surfaces every stale AOB
// at once. `optional` selects [warn] (no consumer yet) vs [error] (critical).

#include <cstdint>

#include <logging/logger.h>
#include <utils/hooking/hooking_patterns.h>

#include "game_layout.h"

namespace HogwartsMP::Core {
    // `optional` selects the severity of a missed scan:
    //   false (default) = critical hook with a downstream consumer (find_uobject,
    //                     world access, spawn, tick) -> log [error], it's a real
    //                     break that must be re-derived.
    //   true            = no-consumer hook (logging/trace/detection or an
    //                     optional dev redirect) -> log [warn]; skipping it is
    //                     tolerated until a feature actually needs it.
    inline void *AobFirst(const char *name, const char *pat, bool optional = false) {
        hook::pattern p(pat);
        const size_t n  = p.size();
        const auto logger = Framework::Logging::GetLogger("AOB");
        if (n == 1) {
            logger->info("[{}] ok (1 match)", name);
            return p.get(0).get<void>(0);
        }
        // 0 matches = stale pattern; >1 = ambiguous. Either way refuse to
        // return an address — hooking an unverified match is worse than not
        // hooking at all. Callers null-check and skip.
        if (optional) {
            logger->warn("[{}] not hooked ({} matches) — optional, no consumer yet", name, n);
        }
        else {
            logger->error("[{}] STALE: {} matches (expected 1)", name, n);
        }
        return nullptr;
    }

    // rel32 call/jmp target resolver, mirroring hook::get_opcode_address but
    // non-aborting. Returns 0 on a missed pattern.
    inline uint64_t AobOpcodeAddr(const char *name, const char *pat, ptrdiff_t offset = 0, bool optional = false) {
        auto *res = static_cast<uint8_t *>(AobFirst(name, pat, optional));
        if (!res) {
            return 0;
        }
        res += offset;
        return reinterpret_cast<uint64_t>(res + *reinterpret_cast<int32_t *>(res + 1) + 5);
    }

    // GameLayout-driven overloads — preferred form. The pattern table
    // (core/game_layout.h) is the single source of truth.
    inline void *AobFirst(const Game::Aob &a) {
        return AobFirst(a.name, a.pattern, a.optional);
    }
    inline uint64_t AobOpcodeAddr(const Game::Aob &a, ptrdiff_t offset = 0) {
        return AobOpcodeAddr(a.name, a.pattern, offset, a.optional);
    }
} // namespace HogwartsMP::Core
