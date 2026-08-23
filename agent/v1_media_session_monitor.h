// SPDX-License-Identifier: Apache-2.0
#pragma once

#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <memory>

#include "v1_media_session_eligibility.h"

namespace native_ldac::agent {

// Event-driven read-only adapter over Windows Global System Media Transport
// Controls. WinRT callbacks publish a small snapshot; the daily host remains
// the sole consumer and owner of AVRCP state transitions.
class V1MediaSessionMonitor {
public:
    V1MediaSessionMonitor();
    ~V1MediaSessionMonitor();

    V1MediaSessionMonitor(const V1MediaSessionMonitor&) = delete;
    V1MediaSessionMonitor& operator=(const V1MediaSessionMonitor&) = delete;

    bool Start(DWORD* error);
    void Stop();
    V1MediaSessionSnapshot Snapshot(std::uint64_t acl_generation) const;
    bool ready() const;
    DWORD last_error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace native_ldac::agent
