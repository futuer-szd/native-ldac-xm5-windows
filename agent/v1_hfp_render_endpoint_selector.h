// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <vector>

namespace native_ldac::agent {

enum class V1HfpRenderEndpointIdentity : std::uint8_t {
    Absent = 0u,
    Untrusted,
    Matched,
    Ambiguous,
};

struct V1HfpRenderEndpointCandidate {
    bool active = false;
    bool xm5_name = false;
    bool native_ldac_name = false;
    bool container_readable = false;
    bool container_matches = false;
};

V1HfpRenderEndpointIdentity EvaluateV1HfpRenderEndpointIdentity(
    const std::vector<V1HfpRenderEndpointCandidate>& candidates);

struct V1HfpRenderEndpointSnapshot {
    V1HfpRenderEndpointIdentity identity =
        V1HfpRenderEndpointIdentity::Absent;
    bool endpoint_present = false;
    bool endpoint_matched = false;
    bool output_bridge_ready = false;
    std::uint64_t sequence = 0u;
};

// Pure endpoint identity and bridge readiness contract. Opening an audio
// client, changing the default endpoint, and changing endpoint formats belong
// to a later online-gated executor.
V1HfpRenderEndpointSnapshot BuildV1HfpRenderEndpointSnapshot(
    V1HfpRenderEndpointIdentity identity,
    std::uint64_t sequence);

}  // namespace native_ldac::agent
