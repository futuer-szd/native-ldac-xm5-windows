#include "v1_transport_open_stability.h"

namespace native_ldac::agent {

void CancelV1TransportOpenStability(
    V1TransportOpenStabilityGate* gate) {
    if (gate == nullptr) {
        return;
    }
    const std::uint32_t required_ms = gate->required_ms;
    *gate = {};
    gate->required_ms = required_ms;
}

void ArmV1TransportOpenStability(
    V1TransportOpenStabilityGate* gate,
    std::uint32_t required_ms,
    std::uint64_t acl_generation,
    bool render_running,
    std::uint64_t render_epoch,
    std::uint64_t now_ms) {
    if (gate == nullptr) {
        return;
    }
    *gate = {};
    gate->required_ms = required_ms;
    gate->acl_generation = acl_generation;
    gate->pending = true;
    if (render_running) {
        gate->render_epoch = render_epoch;
        gate->running_since_ms = now_ms;
        gate->observing_running = true;
    }
}

V1TransportOpenStabilityObservation ObserveV1TransportOpenStability(
    V1TransportOpenStabilityGate* gate,
    std::uint64_t acl_generation,
    bool render_running,
    std::uint64_t render_epoch,
    std::uint64_t now_ms) {
    if (gate == nullptr || !gate->pending) {
        return V1TransportOpenStabilityObservation::None;
    }
    if (acl_generation != gate->acl_generation) {
        CancelV1TransportOpenStability(gate);
        return V1TransportOpenStabilityObservation::Cancelled;
    }
    if (!render_running) {
        if (!gate->observing_running) {
            return V1TransportOpenStabilityObservation::None;
        }
        gate->observing_running = false;
        gate->render_epoch = 0u;
        gate->running_since_ms = 0u;
        return V1TransportOpenStabilityObservation::Reset;
    }
    if (!gate->observing_running) {
        gate->observing_running = true;
        gate->render_epoch = render_epoch;
        gate->running_since_ms = now_ms;
        return V1TransportOpenStabilityObservation::None;
    }
    if (render_epoch != gate->render_epoch ||
        now_ms < gate->running_since_ms) {
        gate->render_epoch = render_epoch;
        gate->running_since_ms = now_ms;
        return V1TransportOpenStabilityObservation::Reset;
    }
    if (now_ms - gate->running_since_ms < gate->required_ms) {
        return V1TransportOpenStabilityObservation::None;
    }
    gate->pending = false;
    gate->observing_running = false;
    return V1TransportOpenStabilityObservation::Ready;
}

}  // namespace native_ldac::agent
