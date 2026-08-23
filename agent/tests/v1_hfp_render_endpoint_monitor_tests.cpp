// SPDX-License-Identifier: Apache-2.0
#include "../v1_hfp_render_endpoint_monitor.h"

#include <cstdio>

int main() {
    native_ldac::agent::V1HfpRenderEndpointMonitor monitor;
    DWORD error = ERROR_SUCCESS;
    if (!monitor.Start(&error)) {
        std::fprintf(stderr,
                     "V1 HFP render endpoint monitor start failed: %lu\n",
                     error);
        return 1;
    }
    monitor.Stop();
    const auto snapshot = monitor.Snapshot();
    if (snapshot.sequence == 0u && monitor.ready()) {
        std::fprintf(stderr,
                     "V1 HFP render endpoint monitor published no snapshot.\n");
        return 1;
    }
    std::puts("V1 HFP render endpoint monitor lifecycle test passed.");
    return 0;
}
