// SPDX-License-Identifier: Apache-2.0
#include "v1_native_ldac_volume_endpoint_selector.h"

namespace native_ldac::agent {

V1NativeLdacVolumeEndpointIdentity
EvaluateV1NativeLdacVolumeEndpointIdentity(
    const std::vector<V1NativeLdacVolumeEndpointCandidate>& candidates) {
    std::uint32_t named = 0u;
    std::uint32_t matched = 0u;
    for (const auto& candidate : candidates) {
        if (!candidate.active || !candidate.native_ldac_name) continue;
        ++named;
        if (candidate.container_readable && candidate.container_matches) {
            ++matched;
        }
    }
    if (named == 1u && matched == 1u) {
        return V1NativeLdacVolumeEndpointIdentity::Matched;
    }
    if (named > 1u || matched > 1u) {
        return V1NativeLdacVolumeEndpointIdentity::Ambiguous;
    }
    return named == 0u
        ? V1NativeLdacVolumeEndpointIdentity::Absent
        : V1NativeLdacVolumeEndpointIdentity::Untrusted;
}

}  // namespace native_ldac::agent
