// SPDX-License-Identifier: Apache-2.0
#pragma once

#define NOMINMAX
#include <windows.h>

#include "v1_transport_pcm_session.h"

struct native_pcm_source;

namespace native_ldac::agent {

class V1TransportNativePcmSource final : public V1TransportPcmSource {
public:
    explicit V1TransportNativePcmSource(
        HANDLE cancel_event = nullptr,
        HANDLE graceful_event = nullptr,
        bool apply_endpoint_volume = true,
        HANDLE single_gain_ready_event = nullptr,
        unsigned sample_rate_hz = 0u,
        unsigned bits_per_sample = 0u)
        : cancel_event_(cancel_event),
          graceful_event_(graceful_event),
          apply_endpoint_volume_(apply_endpoint_volume),
          single_gain_ready_event_(single_gain_ready_event),
          requested_sample_rate_hz_(sample_rate_hz),
          requested_bits_per_sample_(bits_per_sample) {}
    ~V1TransportNativePcmSource() override;

    bool Prepare(V1TransportPcmFormat* format,
                 std::uint32_t timeout_ms,
                 std::uint32_t* error) override;
    V1TransportPcmReadDisposition ReadFrames(
        float* interleaved_stereo,
        std::size_t requested_frames,
        std::uint32_t timeout_ms,
        std::size_t* frames_read,
        std::uint32_t* error) override;
    bool QueryFormat(V1TransportPcmFormat* format,
                     std::uint32_t* error) override;
    bool QuerySnapshot(V1TransportPcmSnapshot* snapshot,
                       std::uint32_t* error) override;
    bool WaitUntilSample(std::uint64_t sample_offset,
                         unsigned sample_rate_hz,
                         std::uint32_t* error) override;
    bool ResetPacing(std::uint32_t* error) override;
    bool Release(std::uint32_t* error) override;

private:
    void RefreshEndpointVolumePolicy();

    native_pcm_source* source_ = nullptr;
    HANDLE cancel_event_ = nullptr;
    HANDLE graceful_event_ = nullptr;
    LARGE_INTEGER pacing_start_ = {};
    LARGE_INTEGER pacing_frequency_ = {};
    std::uint64_t pacing_frame_base_ = 0u;
    bool pacing_started_ = false;
    bool lease_acquired_ = false;
    bool apply_endpoint_volume_ = true;
    HANDLE single_gain_ready_event_ = nullptr;
    unsigned requested_sample_rate_hz_ = 0u;
    unsigned requested_bits_per_sample_ = 0u;
};

}  // namespace native_ldac::agent
