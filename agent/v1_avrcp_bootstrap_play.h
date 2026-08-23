// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

#include "v1_media_session_eligibility.h"

namespace native_ldac::agent {

constexpr std::uint64_t kV1AvrcpBootstrapPlayDelayMs = 200u;

enum class V1AvrcpBootstrapPlayDecision : std::uint8_t {
    None = 0u,
    Scheduled,
    MicrosoftHandled,
    Cancelled,
    InjectPlay,
};

struct V1AvrcpBootstrapPlayState {
    bool pending = false;
    std::uint64_t acl_generation = 0u;
    std::uint64_t deadline_ms = 0u;
    std::uint64_t scheduled_count = 0u;
    std::uint64_t microsoft_handled_count = 0u;
    std::uint64_t fallback_injected_count = 0u;
    std::uint64_t cancelled_count = 0u;
};

void ResetV1AvrcpBootstrapPlay(
    V1AvrcpBootstrapPlayState* state,
    std::uint64_t acl_generation);

V1AvrcpBootstrapPlayDecision ObserveV1AvrcpBootstrapPlayGesture(
    V1AvrcpBootstrapPlayState* state,
    const V1MediaSessionSnapshot& media,
    bool play_like_press,
    std::uint64_t now_ms);

V1AvrcpBootstrapPlayDecision ReconcileV1AvrcpBootstrapPlay(
    V1AvrcpBootstrapPlayState* state,
    const V1MediaSessionSnapshot& media,
    std::uint64_t now_ms);

}  // namespace native_ldac::agent
