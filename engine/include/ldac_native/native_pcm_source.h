// SPDX-License-Identifier: Apache-2.0
#ifndef LDAC_NATIVE_PCM_SOURCE_H
#define LDAC_NATIVE_PCM_SOURCE_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NATIVE_PCM_SOURCE_PATH_CAPACITY 1024u

typedef enum native_pcm_source_status {
    NATIVE_PCM_SOURCE_OK = 0,
    NATIVE_PCM_SOURCE_INVALID_ARGUMENT = -1,
    NATIVE_PCM_SOURCE_NO_MEMORY = -2,
    NATIVE_PCM_SOURCE_NOT_FOUND = -3,
    NATIVE_PCM_SOURCE_IO_ERROR = -4,
    NATIVE_PCM_SOURCE_UNSUPPORTED_FORMAT = -5,
    NATIVE_PCM_SOURCE_TIMEOUT = -6,
    NATIVE_PCM_SOURCE_UNSUPPORTED_PROPERTY = -7
} native_pcm_source_status;

typedef struct native_pcm_source native_pcm_source;

typedef enum native_pcm_link_state {
    NATIVE_PCM_LINK_DISCONNECTED = 0,
    NATIVE_PCM_LINK_CONNECTING = 1,
    NATIVE_PCM_LINK_CONNECTED = 2,
    NATIVE_PCM_LINK_STOPPING = 3
} native_pcm_link_state;

typedef struct native_pcm_link_snapshot {
    native_pcm_link_state state;
    uint64_t session_id;
    uint64_t update_sequence;
    uint64_t updated_interrupt_time_100ns;
} native_pcm_link_snapshot;

typedef struct native_pcm_source_snapshot {
    unsigned sample_rate_hz;
    unsigned channels;
    unsigned bits_per_sample;
    unsigned available_bytes;
    unsigned capacity_bytes;
    int stream_active;
    int discontinuity;
    uint64_t stream_epoch;
    uint64_t total_bytes_written;
    uint64_t total_bytes_read;
    uint64_t total_bytes_dropped;
    int volume_control_available;
    int muted;
    float volume_scalar;
    float volume_db;
} native_pcm_source_snapshot;

typedef struct native_pcm_preferred_format {
    unsigned sample_rate_hz;
    unsigned bits_per_sample;
    unsigned supported_sample_rates;
    unsigned supported_bits_per_sample;
    unsigned revision;
} native_pcm_preferred_format;

native_pcm_source_status native_pcm_source_create(native_pcm_source **out);
native_pcm_source_status native_pcm_source_create_for_interface(
    const wchar_t *interface_path,
    native_pcm_source **out);
void native_pcm_source_destroy(native_pcm_source *source);

/* Controls whether the endpoint master volume is applied to the PCM the
 * source returns. The single-gain volume-sync model keeps this disabled so
 * the headset absolute volume is the only attenuation in the chain; the
 * endpoint slider then remains a display/input channel only. Default: on. */
void native_pcm_source_set_apply_endpoint_volume(
    native_pcm_source *source,
    int apply);

native_pcm_source_status native_pcm_source_read_f32_stereo(
    native_pcm_source *source,
    float *interleaved_stereo,
    size_t requested_frames,
    unsigned timeout_ms,
    size_t *frames_read);

unsigned native_pcm_source_sample_rate_hz(const native_pcm_source *source);
unsigned native_pcm_source_channels(const native_pcm_source *source);
unsigned native_pcm_source_bits_per_sample(const native_pcm_source *source);
const wchar_t *native_pcm_source_interface_path(
    const native_pcm_source *source);
unsigned long native_pcm_source_last_error(const native_pcm_source *source);
native_pcm_source_status native_pcm_source_get_snapshot(
    native_pcm_source *source,
    native_pcm_source_snapshot *snapshot);
native_pcm_source_status native_pcm_source_acquire_consumer(
    native_pcm_source *source,
    uint64_t consumer_generation);
native_pcm_source_status native_pcm_source_release_consumer(
    native_pcm_source *source);
native_pcm_source_status native_pcm_source_report_link_state(
    native_pcm_source *source,
    native_pcm_link_state state,
    uint64_t session_id);
native_pcm_source_status native_pcm_source_get_link_state(
    native_pcm_source *source,
    native_pcm_link_snapshot *snapshot);
native_pcm_source_status native_pcm_source_get_preferred_format(
    native_pcm_source *source,
    native_pcm_preferred_format *format);
native_pcm_source_status native_pcm_source_set_preferred_format(
    native_pcm_source *source,
    unsigned sample_rate_hz,
    unsigned bits_per_sample,
    native_pcm_preferred_format *applied_format);

#ifdef __cplusplus
}
#endif

#endif
