// SPDX-License-Identifier: Apache-2.0
#include "v1_transport_pcm_source_adapter.h"

#include <algorithm>

#include "ldac_native/native_pcm_source.h"

namespace native_ldac::agent {
namespace {

void StoreError(std::uint32_t value, std::uint32_t* error) {
    if (error != nullptr) *error = value;
}

void CopyNativeSnapshot(const native_pcm_source_snapshot& native_snapshot,
                        V1TransportPcmSnapshot* snapshot) {
    if (snapshot == nullptr) return;
    snapshot->format.sample_rate_hz = native_snapshot.sample_rate_hz;
    snapshot->format.bits_per_sample = native_snapshot.bits_per_sample;
    snapshot->format.stream_epoch = native_snapshot.stream_epoch;
    snapshot->format.volume_control_available =
        native_snapshot.volume_control_available != 0;
    snapshot->format.muted = native_snapshot.muted != 0;
    snapshot->format.volume_scalar = native_snapshot.volume_scalar;
    snapshot->format.volume_db = native_snapshot.volume_db;
    snapshot->available_bytes = native_snapshot.available_bytes;
    snapshot->capacity_bytes = native_snapshot.capacity_bytes;
    snapshot->stream_active = native_snapshot.stream_active != 0;
    snapshot->discontinuity = native_snapshot.discontinuity != 0;
    snapshot->total_bytes_written = native_snapshot.total_bytes_written;
    snapshot->total_bytes_read = native_snapshot.total_bytes_read;
    snapshot->total_bytes_dropped = native_snapshot.total_bytes_dropped;
}

}  // namespace

V1TransportNativePcmSource::~V1TransportNativePcmSource() {
    std::uint32_t ignored = 0u;
    (void)Release(&ignored);
}

void V1TransportNativePcmSource::RefreshEndpointVolumePolicy() {
    if (single_gain_ready_event_ == nullptr) return;
    // Any wait error remains fail-safe: keep the Windows endpoint gain
    // applied until the parent positively signals AVRCP readiness.
    apply_endpoint_volume_ =
        WaitForSingleObject(single_gain_ready_event_, 0u) != WAIT_OBJECT_0;
    if (source_ != nullptr) {
        native_pcm_source_set_apply_endpoint_volume(
            source_, apply_endpoint_volume_);
    }
}

bool V1TransportNativePcmSource::Prepare(
    V1TransportPcmFormat* format,
    std::uint32_t timeout_ms,
    std::uint32_t* error) {
    if (format == nullptr || timeout_ms == 0u || source_ != nullptr) {
        StoreError(ERROR_INVALID_STATE, error);
        return false;
    }
    const auto create_status = native_pcm_source_create(&source_);
    if (create_status != NATIVE_PCM_SOURCE_OK || source_ == nullptr) {
        StoreError(source_ == nullptr ? ERROR_NOT_FOUND :
            native_pcm_source_last_error(source_), error);
        return false;
    }
    RefreshEndpointVolumePolicy();
    native_pcm_source_set_apply_endpoint_volume(
        source_, apply_endpoint_volume_);
    native_pcm_source_snapshot snapshot{};
    if (requested_sample_rate_hz_ != 0u && requested_bits_per_sample_ != 0u) {
        if (native_pcm_source_get_snapshot(source_, &snapshot) !=
            NATIVE_PCM_SOURCE_OK) {
            StoreError(native_pcm_source_last_error(source_), error);
            native_pcm_source_destroy(source_);
            source_ = nullptr;
            return false;
        }
        native_pcm_preferred_format before{};
        const auto before_status =
            native_pcm_source_get_preferred_format(source_, &before);
        if (before_status != NATIVE_PCM_SOURCE_OK) {
            StoreError(native_pcm_source_last_error(source_), error);
            native_pcm_source_destroy(source_);
            source_ = nullptr;
            return false;
        }
        if (snapshot.stream_active &&
            (snapshot.sample_rate_hz != requested_sample_rate_hz_ ||
             snapshot.bits_per_sample != requested_bits_per_sample_) &&
            before.sample_rate_hz == requested_sample_rate_hz_ &&
            before.bits_per_sample == requested_bits_per_sample_) {
            native_pcm_preferred_format reset{};
            const auto reset_status = native_pcm_source_set_preferred_format(
                source_, snapshot.sample_rate_hz, snapshot.bits_per_sample,
                &reset);
            if (reset_status != NATIVE_PCM_SOURCE_OK) {
                StoreError(native_pcm_source_last_error(source_), error);
                native_pcm_source_destroy(source_);
                source_ = nullptr;
                return false;
            }
        }
        native_pcm_preferred_format applied{};
        const auto format_status = native_pcm_source_set_preferred_format(
            source_, requested_sample_rate_hz_, requested_bits_per_sample_,
            &applied);
        if (format_status != NATIVE_PCM_SOURCE_OK ||
            applied.sample_rate_hz != requested_sample_rate_hz_ ||
            applied.bits_per_sample != requested_bits_per_sample_) {
            StoreError(native_pcm_source_last_error(source_), error);
            native_pcm_source_destroy(source_);
            source_ = nullptr;
            return false;
        }
    }
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    for (;;) {
        const auto snapshot_status =
            native_pcm_source_get_snapshot(source_, &snapshot);
        if (snapshot_status != NATIVE_PCM_SOURCE_OK) {
            StoreError(native_pcm_source_last_error(source_), error);
            native_pcm_source_destroy(source_);
            source_ = nullptr;
            return false;
        }
        if (snapshot.stream_active &&
            (requested_sample_rate_hz_ == 0u ||
             (snapshot.sample_rate_hz == requested_sample_rate_hz_ &&
              snapshot.bits_per_sample == requested_bits_per_sample_))) break;
        const ULONGLONG now = GetTickCount64();
        if (now >= deadline) {
            StoreError(WAIT_TIMEOUT, error);
            native_pcm_source_destroy(source_);
            source_ = nullptr;
            return false;
        }
        HANDLE events[2]{};
        DWORD count = 0u;
        if (cancel_event_ != nullptr) events[count++] = cancel_event_;
        if (graceful_event_ != nullptr) events[count++] = graceful_event_;
        const DWORD remaining = static_cast<DWORD>(
            std::min<ULONGLONG>(deadline - now, 25u));
        const DWORD wait = count == 0u
            ? WaitForSingleObject(GetCurrentProcess(), remaining)
            : WaitForMultipleObjects(count, events, FALSE, remaining);
        if (wait >= WAIT_OBJECT_0 && wait < WAIT_OBJECT_0 + count) {
            StoreError(ERROR_CANCELLED, error);
            native_pcm_source_destroy(source_);
            source_ = nullptr;
            return false;
        }
        if (wait != WAIT_TIMEOUT) {
            StoreError(GetLastError(), error);
            native_pcm_source_destroy(source_);
            source_ = nullptr;
            return false;
        }
    }
    const std::uint64_t generation =
        (static_cast<std::uint64_t>(GetCurrentProcessId()) << 32u) |
        (GetTickCount64() & 0xFFFFFFFFu);
    const auto lease_status =
        native_pcm_source_acquire_consumer(source_, generation);
    if (lease_status != NATIVE_PCM_SOURCE_OK) {
        StoreError(native_pcm_source_last_error(source_), error);
        native_pcm_source_destroy(source_);
        source_ = nullptr;
        return false;
    }
    lease_acquired_ = true;
    if (!QueryPerformanceFrequency(&pacing_frequency_)) {
        StoreError(GetLastError(), error);
        std::uint32_t ignored = 0u;
        (void)Release(&ignored);
        return false;
    }
    format->sample_rate_hz = snapshot.sample_rate_hz;
    format->bits_per_sample = snapshot.bits_per_sample;
    format->stream_epoch = snapshot.stream_epoch;
    format->volume_control_available =
        snapshot.volume_control_available != 0;
    format->muted = snapshot.muted != 0;
    format->volume_scalar = snapshot.volume_scalar;
    format->volume_db = snapshot.volume_db;
    StoreError(ERROR_SUCCESS, error);
    return true;
}

V1TransportPcmReadDisposition V1TransportNativePcmSource::ReadFrames(
    float* interleaved_stereo,
    std::size_t requested_frames,
    std::uint32_t timeout_ms,
    std::size_t* frames_read,
    std::uint32_t* error) {
    if (source_ == nullptr || !lease_acquired_) {
        StoreError(ERROR_INVALID_STATE, error);
        return V1TransportPcmReadDisposition::Failure;
    }
    RefreshEndpointVolumePolicy();
    const auto status = native_pcm_source_read_f32_stereo(
        source_, interleaved_stereo, requested_frames,
        timeout_ms, frames_read);
    if (status == NATIVE_PCM_SOURCE_OK) {
        StoreError(ERROR_SUCCESS, error);
        return V1TransportPcmReadDisposition::Data;
    }
    if (status == NATIVE_PCM_SOURCE_TIMEOUT) {
        V1TransportPcmSnapshot snapshot{};
        std::uint32_t snapshot_error = ERROR_SUCCESS;
        if (QuerySnapshot(&snapshot, &snapshot_error) &&
            !snapshot.stream_active) {
            StoreError(ERROR_NO_DATA, error);
            return V1TransportPcmReadDisposition::StreamStopped;
        }
        StoreError(WAIT_TIMEOUT, error);
        return V1TransportPcmReadDisposition::Timeout;
    }
    StoreError(native_pcm_source_last_error(source_), error);
    return V1TransportPcmReadDisposition::Failure;
}

bool V1TransportNativePcmSource::QueryFormat(
    V1TransportPcmFormat* format,
    std::uint32_t* error) {
    if (format == nullptr) {
        StoreError(ERROR_INVALID_PARAMETER, error);
        return false;
    }
    V1TransportPcmSnapshot snapshot{};
    if (!QuerySnapshot(&snapshot, error)) return false;
    *format = snapshot.format;
    return true;
}

bool V1TransportNativePcmSource::QuerySnapshot(
    V1TransportPcmSnapshot* snapshot,
    std::uint32_t* error) {
    if (source_ == nullptr || !lease_acquired_ || snapshot == nullptr) {
        StoreError(ERROR_INVALID_STATE, error);
        return false;
    }
    native_pcm_source_snapshot native_snapshot{};
    if (native_pcm_source_get_snapshot(source_, &native_snapshot) !=
        NATIVE_PCM_SOURCE_OK) {
        StoreError(native_pcm_source_last_error(source_), error);
        return false;
    }
    CopyNativeSnapshot(native_snapshot, snapshot);
    StoreError(ERROR_SUCCESS, error);
    return true;
}

bool V1TransportNativePcmSource::WaitUntilSample(
    std::uint64_t sample_offset,
    unsigned sample_rate_hz,
    std::uint32_t* error) {
    if (source_ == nullptr || sample_rate_hz == 0u ||
        pacing_frequency_.QuadPart <= 0) {
        StoreError(ERROR_INVALID_STATE, error);
        return false;
    }
    if (!pacing_started_) {
        if (!QueryPerformanceCounter(&pacing_start_)) {
            StoreError(GetLastError(), error);
            return false;
        }
        pacing_frame_base_ = sample_offset;
        pacing_started_ = true;
    }
    if (sample_offset < pacing_frame_base_) {
        StoreError(ERROR_INVALID_DATA, error);
        return false;
    }
    const LONGLONG target = pacing_start_.QuadPart +
        static_cast<LONGLONG>(
            (sample_offset - pacing_frame_base_) *
            static_cast<std::uint64_t>(pacing_frequency_.QuadPart) /
            sample_rate_hz);
    for (;;) {
        if (cancel_event_ != nullptr &&
            WaitForSingleObject(cancel_event_, 0u) == WAIT_OBJECT_0) {
            StoreError(ERROR_CANCELLED, error);
            return false;
        }
        LARGE_INTEGER now{};
        if (!QueryPerformanceCounter(&now)) {
            StoreError(GetLastError(), error);
            return false;
        }
        const LONGLONG remaining = target - now.QuadPart;
        if (remaining <= 0) {
            StoreError(ERROR_SUCCESS, error);
            return true;
        }
        const DWORD wait_ms = static_cast<DWORD>(
            remaining * 1000 / pacing_frequency_.QuadPart);
        if (wait_ms > 1u && cancel_event_ != nullptr) {
            const DWORD wait = WaitForSingleObject(
                cancel_event_, wait_ms - 1u);
            if (wait == WAIT_OBJECT_0) {
                StoreError(ERROR_CANCELLED, error);
                return false;
            }
            if (wait != WAIT_TIMEOUT) {
                StoreError(GetLastError(), error);
                return false;
            }
        } else {
            (void)SwitchToThread();
        }
    }
}

bool V1TransportNativePcmSource::ResetPacing(std::uint32_t* error) {
    if (source_ == nullptr || !lease_acquired_) {
        StoreError(ERROR_INVALID_STATE, error);
        return false;
    }
    pacing_started_ = false;
    pacing_start_ = {};
    pacing_frame_base_ = 0u;
    StoreError(ERROR_SUCCESS, error);
    return true;
}

bool V1TransportNativePcmSource::Release(std::uint32_t* error) {
    if (source_ == nullptr) {
        StoreError(ERROR_SUCCESS, error);
        return true;
    }
    bool released = true;
    std::uint32_t release_error = ERROR_SUCCESS;
    if (lease_acquired_ &&
        native_pcm_source_release_consumer(source_) !=
            NATIVE_PCM_SOURCE_OK) {
        released = false;
        release_error = native_pcm_source_last_error(source_);
    }
    lease_acquired_ = false;
    native_pcm_source_destroy(source_);
    source_ = nullptr;
    pacing_started_ = false;
    pacing_frame_base_ = 0u;
    StoreError(release_error, error);
    return released;
}

}  // namespace native_ldac::agent
