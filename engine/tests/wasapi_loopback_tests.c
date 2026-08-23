// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>

#include "ldac_native/wasapi_loopback.h"

int main(void) {
    wasapi_loopback *source = NULL;
    wasapi_loopback_status status = wasapi_loopback_create(&source);
    if (status != WASAPI_LOOPBACK_OK || source == NULL) {
        fprintf(stderr,
                "WASAPI loopback initialization failed (status %d).\n",
                (int)status);
        return 1;
    }
    wprintf(L"WASAPI endpoint: %ls\n",
            wasapi_loopback_device_name(source));
    printf("Mix format: %u Hz, %u channel(s), %u bits\n",
           wasapi_loopback_sample_rate_hz(source),
           wasapi_loopback_source_channels(source),
           wasapi_loopback_bits_per_sample(source));
    if (wasapi_loopback_sample_rate_hz(source) == 0u ||
        wasapi_loopback_source_channels(source) == 0u ||
        wasapi_loopback_bits_per_sample(source) == 0u) {
        wasapi_loopback_destroy(source);
        return 2;
    }
    status = wasapi_loopback_start(source);
    if (status != WASAPI_LOOPBACK_OK) {
        fprintf(stderr,
                "WASAPI loopback start failed (status %d, HRESULT 0x%08lX).\n",
                (int)status,
                (unsigned long)wasapi_loopback_last_hresult(source));
        wasapi_loopback_destroy(source);
        return 3;
    }
    wasapi_loopback_stop(source);
    wasapi_loopback_destroy(source);
    return 0;
}
