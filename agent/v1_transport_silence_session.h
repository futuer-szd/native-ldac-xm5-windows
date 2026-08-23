// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "v1_transport_configuration_session.h"

namespace native_ldac::agent {

enum class V1TransportSilenceStage : std::uint32_t {
    None = 0, OpenSignaling, Negotiate, OpenMedia, Start,
    WriteSilence, Suspend, Close, CloseChannels, PreparePcm, ReleasePcm,
    WritePcm,
};

struct V1TransportSilenceOptions {
    std::uint32_t open_timeout_ms = 10000u;
    std::uint32_t exchange_timeout_ms = 5000u;
    std::uint32_t media_timeout_ms = 5000u;
    std::uint16_t preferred_media_mtu = 1000u;
    std::uint32_t packet_limit = 4u;
    unsigned preferred_sample_rate_hz = 96000u;
};

struct V1TransportMediaWriteDiagnostics {
    std::uint32_t query_attempts = 0u;
    std::uint32_t query_error = 0u;
    std::uint32_t query_bytes = 0u;
    std::uint32_t sequence = 0u;
    std::uint32_t operation = 0u;
    std::int32_t io_status = 0;
    std::int32_t brb_status = 0;
    std::uint32_t bluetooth_status = 0u;
    std::uint32_t requested_bytes = 0u;
    std::uint32_t brb_buffer_size = 0u;
    std::uint32_t remaining_bytes = 0u;
    std::uint32_t transfer_flags = 0u;
    bool available = false;
};

struct V1TransportSilenceResult {
    V1TransportConfigurationDisposition disposition =
        V1TransportConfigurationDisposition::InvalidConfiguration;
    V1TransportConfigurationDisposition primary_disposition =
        V1TransportConfigurationDisposition::InvalidConfiguration;
    V1TransportSilenceStage stage = V1TransportSilenceStage::None;
    std::uint32_t backend_error = 0u;
    std::uint32_t cleanup_error = 0u;
    std::int32_t protocol_error = 0;
    std::uint8_t remote_seid = 0u;
    ldac_configuration configuration = {};
    std::uint32_t open_attempts = 0u;
    std::uint32_t signaling_exchanges = 0u;
    std::uint32_t peer_signaling_commands_received = 0u;
    std::uint32_t peer_discover_commands_accepted = 0u;
    std::uint32_t peer_capability_commands_accepted = 0u;
    std::uint32_t peer_configuration_commands_rejected = 0u;
    std::uint32_t peer_close_commands_accepted = 0u;
    std::uint32_t peer_signaling_read_timeouts = 0u;
    std::uint32_t last_signaling_response_size = 0u;
    std::uint8_t last_signaling_tx_transaction_label = 0u;
    std::uint8_t last_signaling_tx_message_type = 0u;
    std::uint8_t last_signaling_tx_signal_id = 0u;
    std::uint8_t last_signaling_rx_transaction_label = 0u;
    std::uint8_t last_signaling_rx_message_type = 0u;
    std::uint8_t last_signaling_rx_signal_id = 0u;
    std::uint16_t incoming_mtu = 0u;
    std::uint16_t outgoing_mtu = 0u;
    std::uint32_t media_packets_written = 0u;
    std::uint32_t media_bytes_written = 0u;
    V1TransportOpenDiagnostics open_diagnostics = {};
    V1TransportMediaWriteDiagnostics media_write_diagnostics = {};
    bool signaling_opened = false;
    bool last_signaling_tx_header_available = false;
    bool last_signaling_rx_header_available = false;
    bool set_configuration_accepted = false;
    bool avdtp_open_accepted = false;
    bool media_opened = false;
    bool avdtp_start_accepted = false;
    bool avdtp_suspend_accepted = false;
    bool avdtp_close_accepted = false;
    bool ended_by_peer_close = false;
    bool remote_stream_cleanup_required = false;
    bool close_attempted = false;
    bool close_succeeded = false;
};

class V1TransportSilenceBackend
    : public V1TransportConfigurationBackend {
public:
    virtual bool WriteMedia(const std::uint8_t* packet,
                            std::size_t packet_size,
                            std::uint32_t timeout_ms,
                            std::uint32_t* error) = 0;
    virtual bool GetLastMediaWriteDiagnostics(
        V1TransportMediaWriteDiagnostics* diagnostics) {
        if (diagnostics == nullptr) return false;
        *diagnostics = {};
        return false;
    }
    virtual bool BeginPeerSignalingRead(
        std::uint32_t timeout_ms,
        std::uint32_t* error) = 0;
    virtual V1TransportSignalingReadDisposition PollPeerSignalingRead(
        std::uint8_t* packet,
        std::size_t packet_capacity,
        std::size_t* packet_size,
        std::uint32_t* error) = 0;
    virtual bool SendPeerSignalingResponse(
        const std::uint8_t* packet,
        std::size_t packet_size,
        std::uint32_t timeout_ms,
        std::uint32_t* error) = 0;
    virtual bool CancelPeerSignalingRead(std::uint32_t* error) = 0;
};

V1TransportSilenceResult RunV1TransportSilenceBurstOnce(
    V1TransportSilenceBackend* backend,
    const V1TransportSilenceOptions& options,
    V1TransportCancelProbe cancel_probe = nullptr,
    void* cancel_context = nullptr);

bool IsV1StrictlyRetryableRemoteNoResources(
    const V1TransportSilenceResult& result);

bool IsV1RemoteNoResourcesDiagnostic(
    const V1TransportOpenDiagnostics& diagnostics);

}  // namespace native_ldac::agent
