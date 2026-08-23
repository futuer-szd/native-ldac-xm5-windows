// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <vector>

namespace native_ldac::agent {

enum class V1NativeLdacVolumeEndpointIdentity : std::uint8_t {
    Absent = 0u,
    Untrusted,
    Matched,
    Ambiguous,
};

struct V1NativeLdacVolumeEndpointCandidate {
    bool active = false;
    bool native_ldac_name = false;
    bool container_readable = false;
    bool container_matches = false;
};

V1NativeLdacVolumeEndpointIdentity
EvaluateV1NativeLdacVolumeEndpointIdentity(
    const std::vector<V1NativeLdacVolumeEndpointCandidate>& candidates);

}  // namespace native_ldac::agent
