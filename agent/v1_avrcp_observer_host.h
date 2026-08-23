// SPDX-License-Identifier: Apache-2.0
#pragma once

#define NOMINMAX
#include <windows.h>

#include <cstdint>

#include "nativeldac_avrcp_observer_ioctl.h"
#include "v1_avrcp_action_executor.h"
#include "v1_avrcp_windows_sink.h"

namespace native_ldac::agent {

// Small transport boundary around the observer device interface. Keeping the
// actual DeviceIoControl calls behind this interface makes the daily lifecycle
// testable without a Bluetooth PDO or a driver installation.
class V1AvrcpObserverIo {
public:
    virtual ~V1AvrcpObserverIo() = default;

    virtual bool OpenReadOnly(DWORD* error) = 0;
    // Phase B write mode: opens with GENERIC_READ | GENERIC_WRITE so the
    // host can bridge SEND_COMMAND writes to the media-scoped lease.
    virtual bool OpenReadWrite(DWORD* error) = 0;
    virtual void Close() = 0;
    virtual bool GetVersion(NLD_AVRCP_OBSERVER_ABI_VERSION* version,
                            DWORD* error) = 0;
    virtual bool GetStatus(NLD_AVRCP_OBSERVER_STATUS* status,
                           DWORD* error) = 0;
    virtual bool BeginObservation(DWORD* error) = 0;
    // Submits one bounded AVRCP write (SetAbsoluteVolume / playback status)
    // through the observer driver. Requires the write-mode open.
    virtual bool SendCommand(ULONG pdu,
                             ULONG response,
                             const UCHAR* parameters,
                             ULONG parameter_size,
                             DWORD* error) = 0;
    // Returns false with ERROR_NO_MORE_ITEMS when the event queue is empty.
    virtual bool Dequeue(NLD_AVRCP_OBSERVER_EVENT* event,
                         DWORD* error) = 0;
    // Write bridge for the media-scoped Windows sink; nullptr unless the io
    // also implements the Bluetooth writer (production io in write mode).
    virtual V1AvrcpBluetoothWriter* AsWriter() { return nullptr; }
};

// Production implementation. It opens exactly one present observer interface
// with GENERIC_READ; the Phase B host does not expose a write path.
class V1AvrcpObserverWin32Io final : public V1AvrcpObserverIo,
                                     public V1AvrcpBluetoothWriter {
public:
    ~V1AvrcpObserverWin32Io() override;

    bool OpenReadOnly(DWORD* error) override;
    bool OpenReadWrite(DWORD* error) override;
    void Close() override;
    bool GetVersion(NLD_AVRCP_OBSERVER_ABI_VERSION* version,
                    DWORD* error) override;
    bool GetStatus(NLD_AVRCP_OBSERVER_STATUS* status,
                   DWORD* error) override;
    bool BeginObservation(DWORD* error) override;
    bool SendCommand(ULONG pdu,
                     ULONG response,
                     const UCHAR* parameters,
                     ULONG parameter_size,
                     DWORD* error) override;
    bool Dequeue(NLD_AVRCP_OBSERVER_EVENT* event,
                 DWORD* error) override;
    // Bridges WriteAvrcp to SendCommand for the Windows sink.
    bool WriteAvrcp(ULONG pdu,
                    ULONG response,
                    const UCHAR* parameters,
                    ULONG parameter_size) override;
    V1AvrcpBluetoothWriter* AsWriter() override { return this; }

private:
    bool OpenImpl(DWORD access, DWORD* error);
    HANDLE handle_ = INVALID_HANDLE_VALUE;
};

enum class V1AvrcpObserverActivationResult : std::uint8_t {
    Active,
    Pending,
    Unavailable,
    Incompatible,
    Failed,
};

struct V1AvrcpObserverHostStats {
    std::uint64_t media_sessions_started = 0u;
    std::uint64_t media_sessions_ended = 0u;
    std::uint64_t interface_open_attempts = 0u;
    std::uint64_t interface_open_failures = 0u;
    std::uint64_t version_failures = 0u;
    std::uint64_t status_failures = 0u;
    std::uint64_t activation_requests = 0u;
    std::uint64_t activation_accepted = 0u;
    std::uint64_t activation_rejected = 0u;
    std::uint64_t dequeued_events = 0u;
    std::uint64_t ignored_events = 0u;
    std::uint64_t mapper_errors = 0u;
    std::uint64_t windows_volume_notifications = 0u;
    std::uint64_t windows_volume_polls = 0u;
};

// Owns the media-scoped mapper lease, but intentionally does not own the
// Bluetooth profile lifetime. A PDO/PnP generation can contain multiple media
// sessions: the first active session performs BEGIN_OBSERVATION; later ones
// reuse the already-open observer and acquire a fresh mapper lease.
class V1AvrcpObserverHost {
public:
    explicit V1AvrcpObserverHost(V1AvrcpObserverIo* io,
                                 bool write_enabled = false);

    V1AvrcpObserverHost(const V1AvrcpObserverHost&) = delete;
    V1AvrcpObserverHost& operator=(const V1AvrcpObserverHost&) = delete;

    V1AvrcpObserverActivationResult BeginMediaSession(
        const V1AvrcpReplayOptions& options,
        V1AvrcpActionSink* sink,
        DWORD* error);
    // Drains queued observer events and polls the Windows-volume source only
    // while the media-scoped lease is active.
    bool Poll(DWORD* error);
    void SetMediaSessionSnapshot(const V1MediaSessionSnapshot& snapshot);
    void EndMediaSession();
    // Closes the current observer PDO handle after an owner restore while
    // preserving the physical ACL generation and its one-time XM5 authority.
    void ReleaseTransport();
    void Close();

    // Returns the io as a Bluetooth writer when the host was constructed in
    // write mode (the production io bridges to SEND_COMMAND); otherwise
    // nullptr. The media-scoped Windows sink uses this to push
    // SendXm5Volume and playback status through the observer driver.
    V1AvrcpBluetoothWriter* writer();

    bool media_session_active() const { return media_session_active_; }
    bool observer_open() const { return observer_open_; }
    bool observation_requested() const { return observation_requested_; }
    bool control_channel_ready() const { return control_channel_ready_; }
    bool single_gain_ready() const {
        return control_channel_ready_ && headset_initial_sync_complete();
    }
    ULONG status_flags() const { return status_flags_; }
    LONG last_protocol_status() const { return last_protocol_status_; }
    LONG last_open_status() const { return last_open_status_; }
    std::uint64_t observer_generation() const { return observer_generation_; }
    std::uint64_t physical_acl_generation() const {
        return mapper_.acl_generation_current ? mapper_.acl_generation : 0u;
    }
    bool headset_initial_sync_complete() const {
        return mapper_.acl_generation_current && mapper_.xm5_volume_seen;
    }
    const V1AvrcpObserverHostStats& stats() const { return stats_; }

private:
    bool EnsureOpened(DWORD* error,
                      V1AvrcpObserverActivationResult* result);
    bool ReadStatus(NLD_AVRCP_OBSERVER_STATUS* status, DWORD* error);
    bool EnsureMapperLease();
    bool PreparePhysicalMapper(DWORD* error);
    bool FeedDriverEvent(const NLD_AVRCP_OBSERVER_EVENT& event);
    void EndMapperGeneration();
    std::uint64_t NextOwnerLease();

    V1AvrcpObserverIo* io_ = nullptr;
    bool write_enabled_ = false;
    V1AvrcpReplayOptions options_{};
    V1AvrcpActionSink* sink_ = nullptr;
    V1AvrcpControlMapperState mapper_{};
    V1AvrcpReplayStats mapper_stats_{};
    V1AvrcpObserverHostStats stats_{};
    bool observer_open_ = false;
    bool observation_requested_ = false;
    bool activation_terminal_failure_ = false;
    DWORD activation_failure_error_ = ERROR_SUCCESS;
    bool media_session_active_ = false;
    bool control_channel_ready_ = false;
    ULONG status_flags_ = 0u;
    LONG last_protocol_status_ = 0;
    LONG last_open_status_ = 0;
    std::uint64_t observer_generation_ = 0u;
    std::uint64_t owner_lease_ = 0u;
    ULONGLONG last_windows_volume_poll_tick_ = 0u;
};

}  // namespace native_ldac::agent
