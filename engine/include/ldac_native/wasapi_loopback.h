// SPDX-License-Identifier: Apache-2.0
#ifndef LDAC_NATIVE_WASAPI_LOOPBACK_H
#define LDAC_NATIVE_WASAPI_LOOPBACK_H

#include <stddef.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WASAPI_LOOPBACK_DEVICE_NAME_CAPACITY 256u

typedef enum wasapi_loopback_status {
    WASAPI_LOOPBACK_OK = 0,
    WASAPI_LOOPBACK_INVALID_ARGUMENT = -1,
    WASAPI_LOOPBACK_NO_MEMORY = -2,
    WASAPI_LOOPBACK_COM_ERROR = -3,
    WASAPI_LOOPBACK_UNSUPPORTED_FORMAT = -4,
    WASAPI_LOOPBACK_TIMEOUT = -5
} wasapi_loopback_status;

typedef struct wasapi_loopback wasapi_loopback;

wasapi_loopback_status wasapi_loopback_create(wasapi_loopback **out);
void wasapi_loopback_destroy(wasapi_loopback *source);

wasapi_loopback_status wasapi_loopback_start(wasapi_loopback *source);
void wasapi_loopback_stop(wasapi_loopback *source);

wasapi_loopback_status wasapi_loopback_read_f32_stereo(
    wasapi_loopback *source,
    float *interleaved_stereo,
    size_t requested_frames,
    unsigned timeout_ms,
    size_t *frames_read);

unsigned wasapi_loopback_sample_rate_hz(const wasapi_loopback *source);
unsigned wasapi_loopback_source_channels(const wasapi_loopback *source);
unsigned wasapi_loopback_bits_per_sample(const wasapi_loopback *source);
const wchar_t *wasapi_loopback_device_name(const wasapi_loopback *source);
long wasapi_loopback_last_hresult(const wasapi_loopback *source);

#ifdef __cplusplus
}
#endif

#endif
