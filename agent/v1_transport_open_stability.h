#pragma once

#include <cstdint>

namespace native_ldac::agent {

enum class V1TransportOpenStabilityObservation {
    None,
    Reset,
    Ready,
    Cancelled,
};

struct V1TransportOpenStabilityGate {
    std::uint32_t required_ms = 0u;
    std::uint64_t acl_generation = 0u;
    std::uint64_t render_epoch = 0u;
    std::uint64_t running_since_ms = 0u;
    bool pending = false;
    bool observing_running = false;
};

void ArmV1TransportOpenStability(
    V1TransportOpenStabilityGate* gate,
    std::uint32_t required_ms,
    std::uint64_t acl_generation,
    bool render_running,
    std::uint64_t render_epoch,
    std::uint64_t now_ms);

void CancelV1TransportOpenStability(
    V1TransportOpenStabilityGate* gate);

V1TransportOpenStabilityObservation ObserveV1TransportOpenStability(
    V1TransportOpenStabilityGate* gate,
    std::uint64_t acl_generation,
    bool render_running,
    std::uint64_t render_epoch,
    std::uint64_t now_ms);

}  // namespace native_ldac::agent
