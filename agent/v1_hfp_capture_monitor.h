// SPDX-License-Identifier: Apache-2.0
#pragma once

#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace native_ldac::agent {

enum class V1HfpCaptureEndpointIdentity : std::uint8_t {
    Absent = 0u,
    Untrusted,
    Matched,
    Ambiguous,
};

struct V1HfpCaptureEndpointCandidate {
    bool active = false;
    bool xm5_name = false;
    bool container_readable = false;
    bool container_matches = false;
};

V1HfpCaptureEndpointIdentity EvaluateV1HfpCaptureEndpointIdentity(
    const std::vector<V1HfpCaptureEndpointCandidate>& candidates);

struct V1HfpCaptureSnapshot {
    V1HfpCaptureEndpointIdentity endpoint_identity =
        V1HfpCaptureEndpointIdentity::Absent;
    bool endpoint_present = false;
    bool endpoint_matched = false;
    bool capture_active = false;
    std::uint32_t active_session_count = 0u;
    std::uint64_t sequence = 0u;
    DWORD last_error = ERROR_SUCCESS;
};

// Read-only Core Audio monitor for the XM5 Hands-Free capture endpoint. The
// endpoint must match both the XM5 friendly name and the build-verified remote
// Container ID. Uncertain identity is deliberately reported inactive.
class V1HfpCaptureMonitor {
public:
    V1HfpCaptureMonitor();
    ~V1HfpCaptureMonitor();

    V1HfpCaptureMonitor(const V1HfpCaptureMonitor&) = delete;
    V1HfpCaptureMonitor& operator=(const V1HfpCaptureMonitor&) = delete;

    bool Start(DWORD* error);
    void Stop();
    V1HfpCaptureSnapshot Snapshot() const;
    bool ready() const;
    DWORD last_error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace native_ldac::agent
