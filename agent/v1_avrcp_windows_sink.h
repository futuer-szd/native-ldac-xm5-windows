// SPDX-License-Identifier: Apache-2.0
#pragma once

#define NOMINMAX
#include <windows.h>

#include <endpointvolume.h>
#include <mmdeviceapi.h>

#include <cstdint>
#include <atomic>
#include <string>

#include "v1_avrcp_action_executor.h"

namespace native_ldac::agent {

// Optional Bluetooth write back-end for the Windows sink. The executor tool
// and the V1 daily host bridge their own observer driver handle here so the
// sink itself stays free of device-interface knowledge.
struct V1AvrcpBluetoothWriter {
    virtual ~V1AvrcpBluetoothWriter() = default;
    virtual bool WriteAvrcp(ULONG pdu,
                            ULONG response,
                            const UCHAR* parameters,
                            ULONG parameter_size) = 0;
};

// Applies authorized mapper decisions to the Windows side: endpoint volume
// writes (IAudioEndpointVolume) and media-key injection, plus the optional
// AVRCP write back-end for SendXm5Volume and playback-status notifications.
// Without apply_ this is a dry run: decisions are printed, nothing is
// written. Shared by the v1_avrcp_action_executor tool and the daily host.
class V1AvrcpWindowsSink final : public V1AvrcpActionSink,
                                 public IAudioEndpointVolumeCallback {
public:
    explicit V1AvrcpWindowsSink(bool apply,
                                V1AvrcpBluetoothWriter* writer = nullptr,
                                bool native_ldac_endpoint_only = false);
    ~V1AvrcpWindowsSink() override;

    V1AvrcpWindowsSink(const V1AvrcpWindowsSink&) = delete;
    V1AvrcpWindowsSink& operator=(const V1AvrcpWindowsSink&) = delete;

    void SetMediaKeyDiagnostics(bool enabled);

    bool Handle(const V1AvrcpActionSet& actions) override;
    void RetryPendingWrites() override;
    bool QueryWindowsVolume(AvrcpWindowsVolume* volume) override;
    bool WindowsVolumeNotificationsActive() const override;
    bool ConsumeWindowsVolumeChange(AvrcpWindowsVolume* volume) override;
    HANDLE volume_change_event() const { return volume_change_event_; }
    bool WindowsVolumeEndpointReady() const {
        return volume_ != nullptr &&
            (!native_ldac_endpoint_only_ || exact_endpoint_bound_);
    }
    std::uint64_t volume_endpoint_bind_count() const {
        return volume_endpoint_bind_count_;
    }
    std::uint64_t volume_endpoint_rebind_count() const {
        return volume_endpoint_rebind_count_;
    }
    std::uint64_t volume_endpoint_bind_failure_count() const {
        return volume_endpoint_bind_failure_count_;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,
                                             void** object) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    HRESULT STDMETHODCALLTYPE OnNotify(
        PAUDIO_VOLUME_NOTIFICATION_DATA notification) override;

private:
    bool EnsureVolume();
    bool FindUniqueNativeLdacEndpoint(IMMDevice** device,
                                      std::wstring* endpoint_id);
    bool BindVolumeEndpoint(IMMDevice* device,
                            const std::wstring& endpoint_id,
                            bool exact_endpoint);
    void ReleaseVolumeEndpoint();
    void PublishCurrentVolume();
    bool SetWindowsVolume(std::uint8_t percent, bool muted);
    bool InjectVirtualKey(std::uint16_t vk,
                          UINT* sent_count,
                          DWORD* error);

    bool apply_;
    bool native_ldac_endpoint_only_ = false;
    bool exact_endpoint_bound_ = false;
    bool media_key_diagnostics_ = false;
    V1AvrcpBluetoothWriter* writer_ = nullptr;
    IMMDeviceEnumerator* enumerator_ = nullptr;
    IMMDevice* device_ = nullptr;
    IAudioEndpointVolume* volume_ = nullptr;
    bool notification_registered_ = false;
    std::wstring bound_endpoint_id_;
    ULONGLONG next_volume_bind_tick_ = 0u;
    ULONGLONG next_volume_validation_tick_ = 0u;
    std::uint64_t volume_endpoint_bind_count_ = 0u;
    std::uint64_t volume_endpoint_rebind_count_ = 0u;
    std::uint64_t volume_endpoint_bind_failure_count_ = 0u;
    HANDLE volume_change_event_ = nullptr;
    std::atomic<ULONG> callback_references_{1u};
    std::atomic<std::uint32_t> pending_volume_{0u};
    std::atomic<std::uint64_t> published_volume_sequence_{0u};
    std::uint64_t consumed_volume_sequence_ = 0u;
    bool pending_xm5_volume_valid_ = false;
    std::uint8_t pending_xm5_volume_ = 0u;
    bool pending_playback_status_valid_ = false;
    std::uint8_t pending_playback_status_ = 0u;
    bool pending_windows_volume_valid_ = false;
    std::uint8_t pending_windows_volume_percent_ = 0u;
    bool pending_windows_volume_muted_ = false;
};

}  // namespace native_ldac::agent
