#pragma once

// Snapshot (playback) interpolation for a networked transform: record timestamped (pos, rot) samples as
// they arrive, then render at (now - bufferDelay) by lerping the two samples that bracket that time.
// Smooth regardless of frame/packet rate and robust to packet-timing jitter, at the cost of showing the
// entity ~bufferDelay in the past — the Source/Quake approach. Contrast with extrapolation/dead-reckoning
// (predict forward from pos+velocity): zero added latency, but overshoots on direction changes — better
// for high-speed straight-line motion (e.g. brooms).
//
// Self-contained (glm + chrono + deque only), no engine deps, so it can be lifted into Framework::Utils
// later — just change the namespace.

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <chrono>
#include <deque>

namespace HogwartsMP::Core {
    class SnapshotInterpolator {
      public:
        using Clock = std::chrono::steady_clock;

        // Record a freshly replicated transform (once per received packet). snap=true clears the buffer
        // first (a teleport / stream-in), so the next Sample doesn't lerp across the gap.
        void Push(const glm::vec3 &pos, const glm::quat &rot, bool snap) {
            if (snap) {
                _samples.clear();
            }
            _samples.push_back({Clock::now(), pos, rot});
            while (_samples.size() > kMax) {
                _samples.pop_front();
            }
        }

        // Sample the render transform at (now - bufferDelayMs). Returns false if there are no samples yet.
        // Holds at the oldest sample until the buffer fills, and at the newest if starved (no extrapolation
        // — a stopped entity must not drift; use dead reckoning where forward prediction is wanted).
        bool Sample(glm::vec3 &outPos, glm::quat &outRot, float bufferDelayMs) const {
            if (_samples.empty()) {
                return false;
            }
            const auto renderT = Clock::now() - std::chrono::duration_cast<Clock::duration>(
                                                    std::chrono::duration<float, std::milli>(bufferDelayMs));
            if (renderT <= _samples.front().t) {
                outPos = _samples.front().pos;
                outRot = _samples.front().rot;
                return true;
            }
            if (renderT >= _samples.back().t) {
                outPos = _samples.back().pos;
                outRot = _samples.back().rot;
                return true;
            }
            for (size_t i = 0; i + 1 < _samples.size(); ++i) {
                const auto &a = _samples[i];
                const auto &b = _samples[i + 1];
                if (renderT >= a.t && renderT <= b.t) {
                    const float span  = std::chrono::duration<float, std::milli>(b.t - a.t).count();
                    const float alpha = span > 1e-3f ? std::chrono::duration<float, std::milli>(renderT - a.t).count() / span : 1.0f;
                    outPos            = glm::mix(a.pos, b.pos, alpha);
                    outRot            = glm::slerp(a.rot, b.rot, alpha);
                    return true;
                }
            }
            outPos = _samples.back().pos; // unreachable (renderT is between front and back)
            outRot = _samples.back().rot;
            return true;
        }

        void Reset() {
            _samples.clear();
        }

      private:
        struct Entry {
            Clock::time_point t;
            glm::vec3 pos;
            glm::quat rot;
        };
        std::deque<Entry> _samples;
        static constexpr size_t kMax = 16;
    };
} // namespace HogwartsMP::Core
