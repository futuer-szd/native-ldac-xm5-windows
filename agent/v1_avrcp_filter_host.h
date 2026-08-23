// SPDX-License-Identifier: Apache-2.0
#pragma once

#define NOMINMAX
#include <windows.h>

#include <cstdint>

#include "nativeldac_avrcp_filter_ioctl.h"
#include "nativeldac_avrcp_observer_ioctl.h"
#include "v1_avrcp_bootstrap_play.h"
#include "v1_avrcp_action_mapper.h"
#include "v1_avrcp_action_executor.h"
#include "v1_avrcp_filter_decoder.h"
#include "v1_avrcp_windows_sink.h"

namespace native_ldac::agent {

class V1AvrcpFilterHost final : public V1AvrcpBluetoothWriter {
public:
    V1AvrcpFilterHost() = default;
    ~V1AvrcpFilterHost();
    V1AvrcpFilterHost(const V1AvrcpFilterHost&) = delete;
    V1AvrcpFilterHost& operator=(const V1AvrcpFilterHost&) = delete;

    bool Open(DWORD* error);
    void Close();
    bool IsOpen() const { return handle_ != INVALID_HANDLE_VALUE; }
    bool SetAbsoluteVolume(std::uint8_t volume, DWORD* error);
    bool Dequeue(avrcp_observer_event* event, DWORD* error);
    bool WriteAvrcp(ULONG pdu, ULONG response,
                    const UCHAR* parameters,
                    ULONG parameter_size) override;
    bool BeginSession(std::uint64_t generation,
                      const V1MediaSessionSnapshot& media,
                      V1AvrcpActionSink* sink,
                      DWORD* error);
    bool Poll(DWORD* error);
    void SetMediaSessionSnapshot(const V1MediaSessionSnapshot& media);
    void EndSession();
    bool session_active() const { return session_active_; }
    bool initial_volume_seen() const { return mapper_.xm5_volume_seen; }
    std::uint64_t bootstrap_play_scheduled_count() const {
        return bootstrap_play_.scheduled_count;
    }
    std::uint64_t bootstrap_play_microsoft_handled_count() const {
        return bootstrap_play_.microsoft_handled_count;
    }
    std::uint64_t bootstrap_play_fallback_injected_count() const {
        return bootstrap_play_.fallback_injected_count;
    }

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    V1AvrcpControlMapperState mapper_{};
    V1AvrcpReplayOptions options_{};
    V1AvrcpReplayStats stats_{};
    V1AvrcpBootstrapPlayState bootstrap_play_{};
    V1AvrcpActionSink* sink_ = nullptr;
    std::uint64_t owner_lease_ = 0u;
    bool session_active_ = false;
};

}  // namespace native_ldac::agent
