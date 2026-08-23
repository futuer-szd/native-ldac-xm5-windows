#include "v1_render_demand_tracker.h"

namespace native_ldac::agent {
namespace {

constexpr unsigned int kConfirmationSamples = 2u;

}  // namespace

void ResetV1RenderDemandTracker(V1RenderDemandTracker* tracker) {
    if (tracker == nullptr) {
        return;
    }
    *tracker = {};
}

V1RenderDemandTransition ObserveV1RenderDemand(
    V1RenderDemandTracker* tracker,
    bool stream_active) {
    if (tracker == nullptr) {
        return V1RenderDemandTransition::None;
    }
    if (stream_active == tracker->stable_running) {
        tracker->candidate_running = stream_active;
        tracker->candidate_samples = 0u;
        return V1RenderDemandTransition::None;
    }
    if (tracker->candidate_samples == 0u ||
        tracker->candidate_running != stream_active) {
        tracker->candidate_running = stream_active;
        tracker->candidate_samples = 1u;
        return V1RenderDemandTransition::None;
    }
    ++tracker->candidate_samples;
    if (tracker->candidate_samples < kConfirmationSamples) {
        return V1RenderDemandTransition::None;
    }
    tracker->stable_running = stream_active;
    tracker->candidate_samples = 0u;
    return stream_active ? V1RenderDemandTransition::Started
                         : V1RenderDemandTransition::Stopped;
}

}  // namespace native_ldac::agent
