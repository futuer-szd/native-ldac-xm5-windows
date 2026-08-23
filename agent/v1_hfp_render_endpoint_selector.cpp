// SPDX-License-Identifier: Apache-2.0
#include "v1_hfp_render_endpoint_selector.h"

namespace native_ldac::agent {

V1HfpRenderEndpointIdentity EvaluateV1HfpRenderEndpointIdentity(
    const std::vector<V1HfpRenderEndpointCandidate>& candidates) {
    std::uint32_t named = 0u;
    std::uint32_t matched = 0u;
    for (const auto& candidate : candidates) {
        if (!candidate.active || !candidate.xm5_name ||
            candidate.native_ldac_name) {
            continue;
        }
        ++named;
        if (candidate.container_readable && candidate.container_matches) {
            ++matched;
        }
    }
    if (matched == 1u && named == 1u) {
        return V1HfpRenderEndpointIdentity::Matched;
    }
    if (matched > 1u || named > 1u) {
        return V1HfpRenderEndpointIdentity::Ambiguous;
    }
    return named == 0u ? V1HfpRenderEndpointIdentity::Absent
                       : V1HfpRenderEndpointIdentity::Untrusted;
}

V1HfpRenderEndpointSnapshot BuildV1HfpRenderEndpointSnapshot(
    V1HfpRenderEndpointIdentity identity,
    std::uint64_t sequence) {
    V1HfpRenderEndpointSnapshot snapshot;
    snapshot.identity = identity;
    snapshot.endpoint_present =
        identity != V1HfpRenderEndpointIdentity::Absent;
    snapshot.endpoint_matched =
        identity == V1HfpRenderEndpointIdentity::Matched;
    snapshot.output_bridge_ready = snapshot.endpoint_matched;
    snapshot.sequence = sequence;
    return snapshot;
}

}  // namespace native_ldac::agent
