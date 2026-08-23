#pragma once

namespace native_ldac::agent {

enum class V1RenderDemandTransition {
    None,
    Started,
    Stopped,
};

struct V1RenderDemandTracker {
    bool stable_running = false;
    bool candidate_running = false;
    unsigned int candidate_samples = 0u;
};

void ResetV1RenderDemandTracker(V1RenderDemandTracker* tracker);

V1RenderDemandTransition ObserveV1RenderDemand(
    V1RenderDemandTracker* tracker,
    bool stream_active);

}  // namespace native_ldac::agent
