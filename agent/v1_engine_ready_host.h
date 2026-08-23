#pragma once

#define NOMINMAX
#include <windows.h>

#include <cstdint>
#include <string>

namespace native_ldac::agent {

enum class V1EngineStopMode {
    LocalOnly,
    GracefulTransport,
    CancelTransport,
};

enum class V1TransportWorkerEvent {
    None,
    CapabilitiesDiscovered,
    MediaStarted,
    MediaStopped,
    RetryableOpenFailure,
    MediaFailed,
};

class V1EngineReadyHost {
public:
    V1EngineReadyHost() = default;
    V1EngineReadyHost(const V1EngineReadyHost&) = delete;
    V1EngineReadyHost& operator=(const V1EngineReadyHost&) = delete;
    ~V1EngineReadyHost();

    bool Start(const std::wstring& executable,
               std::uint64_t generation,
               bool ignore_stop,
               DWORD* error);
    bool StartTransportWorker(const std::wstring& executable,
                              std::uint64_t generation,
                              bool ignore_stop,
                              DWORD* error,
                              bool apply_endpoint_volume = true,
                              const std::wstring& quality = L"hq",
                              const std::wstring& channel_mode = L"stereo",
                              unsigned sample_rate_hz = 48000u,
                              unsigned bits_per_sample = 16u);
    bool StartTransportDiscoveryWorker(const std::wstring& executable,
                                       std::uint64_t generation,
                                       const std::wstring& result_path,
                                       DWORD* error,
                                       bool apply_endpoint_volume = true,
                                       const std::wstring& quality = L"hq",
                                       const std::wstring& channel_mode =
                                           L"stereo",
                                       unsigned sample_rate_hz = 48000u,
                                       unsigned bits_per_sample = 16u);
    bool PollReady(bool* ready, DWORD* error);
    bool AuthorizeTransportOpen(DWORD* error);
    // Fail-safe single-gain control. The worker applies the Windows endpoint
    // gain until this manual-reset event is signaled, and restores it if the
    // AVRCP control channel later loses readiness.
    bool SetSingleGainReady(bool ready, DWORD* error);
    bool PollTransportEvent(V1TransportWorkerEvent* event,
                            DWORD* error);
    bool PollExited(bool* exited, DWORD* exit_code, DWORD* error) const;
    bool Stop(DWORD timeout_ms, DWORD* exit_code, DWORD* error);
    bool Stop(V1EngineStopMode mode,
              DWORD timeout_ms,
              DWORD* exit_code,
              DWORD* error);
    void Close();

    bool active() const { return process_ != nullptr; }
    bool ready_observed() const { return ready_observed_; }
    bool transport_worker_enabled() const {
        return transport_worker_enabled_;
    }
    bool transport_open_authorized() const {
        return transport_open_authorized_;
    }
    bool single_gain_fail_safe_enabled() const {
        return single_gain_ready_event_ != nullptr;
    }
    bool last_transport_stop_acknowledged() const {
        return last_transport_stop_acknowledged_;
    }
    DWORD process_id() const { return process_id_; }

private:
    bool StartInternal(const std::wstring& executable,
                       std::uint64_t generation,
                       bool ignore_stop,
                       bool transport_worker,
                       bool apply_endpoint_volume,
                       const std::wstring& transport_result_path,
                       const std::wstring& quality,
                       const std::wstring& channel_mode,
                       unsigned sample_rate_hz,
                       unsigned bits_per_sample,
                       DWORD* error);

    HANDLE job_ = nullptr;
    HANDLE process_ = nullptr;
    HANDLE ready_event_ = nullptr;
    HANDLE stop_event_ = nullptr;
    HANDLE transport_open_event_ = nullptr;
    HANDLE capabilities_discovered_event_ = nullptr;
    HANDLE media_started_event_ = nullptr;
    HANDLE media_stopped_event_ = nullptr;
    HANDLE media_failed_event_ = nullptr;
    HANDLE retryable_open_failure_event_ = nullptr;
    HANDLE graceful_transport_stop_event_ = nullptr;
    HANDLE cancel_transport_event_ = nullptr;
    HANDLE single_gain_ready_event_ = nullptr;
    DWORD process_id_ = 0u;
    bool ready_observed_ = false;
    bool transport_worker_enabled_ = false;
    bool transport_open_authorized_ = false;
    bool capabilities_discovered_observed_ = false;
    bool media_started_observed_ = false;
    bool media_stopped_observed_ = false;
    bool media_failed_observed_ = false;
    bool retryable_open_failure_observed_ = false;
    bool last_transport_stop_acknowledged_ = false;
};

}  // namespace native_ldac::agent
