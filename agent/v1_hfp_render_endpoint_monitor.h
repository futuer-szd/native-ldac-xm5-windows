// SPDX-License-Identifier: Apache-2.0
#pragma once

#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <memory>

#include "v1_hfp_render_endpoint_selector.h"

namespace native_ldac::agent {

class V1HfpRenderEndpointMonitor {
public:
    V1HfpRenderEndpointMonitor();
    ~V1HfpRenderEndpointMonitor();

    V1HfpRenderEndpointMonitor(const V1HfpRenderEndpointMonitor&) = delete;
    V1HfpRenderEndpointMonitor& operator=(
        const V1HfpRenderEndpointMonitor&) = delete;

    bool Start(DWORD* error);
    void Stop();
    V1HfpRenderEndpointSnapshot Snapshot() const;
    bool ready() const;
    DWORD last_error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace native_ldac::agent
