// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace native_ldac::agent {

enum class V1MediaSessionPlayback : std::uint8_t {
    Absent = 0u,
    Stopped,
    Playing,
    Paused,
};

enum class V1MediaSessionObservedPlayback : std::uint8_t {
    Changing = 0u,
    Stopped,
    Playing,
    Paused,
    Closed,
};

struct V1MediaSessionSnapshot {
    std::uint64_t acl_generation = 0u;
    V1MediaSessionPlayback playback = V1MediaSessionPlayback::Absent;
    bool play_enabled = false;
    bool pause_enabled = false;
    bool next_enabled = false;
    bool previous_enabled = false;
};

struct V1MediaSessionEligibility {
    bool session_present = false;
    bool play_eligible = false;
    bool pause_eligible = false;
    bool next_eligible = false;
    bool previous_eligible = false;
    std::uint64_t acl_generation = 0u;
};

// Converts an external Windows media-session snapshot into the narrow gate
// consumed by AVRCP routing. No session or capability means no media-key
// injection, even if the Bluetooth headset sends a valid PASS THROUGH event.
V1MediaSessionEligibility EvaluateV1MediaSessionEligibility(
    const V1MediaSessionSnapshot& snapshot);

// Keeps a transient GSMTC Changing state from tearing down a stable session.
// A new session or an unknown previous state remains fail-closed as Stopped.
V1MediaSessionPlayback NormalizeV1MediaSessionPlayback(
    V1MediaSessionObservedPlayback observed,
    V1MediaSessionPlayback previous,
    bool same_session);

}  // namespace native_ldac::agent
