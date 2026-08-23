// SPDX-License-Identifier: Apache-2.0
#include "v1_avrcp_windows_sink.h"

#include <initguid.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <propvarutil.h>

#include <algorithm>
#include <cstdio>
#include <vector>

#include "nativeldac_remote_container.h"
#include "v1_native_ldac_volume_endpoint_selector.h"

namespace native_ldac::agent {

V1AvrcpWindowsSink::V1AvrcpWindowsSink(bool apply,
                                       V1AvrcpBluetoothWriter* writer,
                                       bool native_ldac_endpoint_only)
    : apply_(apply),
      native_ldac_endpoint_only_(native_ldac_endpoint_only),
      writer_(writer),
      volume_change_event_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}

V1AvrcpWindowsSink::~V1AvrcpWindowsSink() {
    ReleaseVolumeEndpoint();
    if (enumerator_ != nullptr) enumerator_->Release();
    if (volume_change_event_ != nullptr) {
        CloseHandle(volume_change_event_);
    }
}

void V1AvrcpWindowsSink::SetMediaKeyDiagnostics(bool enabled) {
    media_key_diagnostics_ = enabled;
}

bool V1AvrcpWindowsSink::Handle(const V1AvrcpActionSet& actions) {
    bool ok = true;
    if (V1AvrcpHasAction(actions, V1AvrcpActionSetWindowsVolume)) {
        const std::uint8_t percent = actions.windows_volume.percent;
        const bool muted = actions.windows_volume.muted;
        std::printf("action set-windows-volume percent=%u muted=%s\n",
                    percent, muted ? "yes" : "no");
        if (apply_ && !SetWindowsVolume(percent, muted)) {
            pending_windows_volume_valid_ = true;
            pending_windows_volume_percent_ = percent;
            pending_windows_volume_muted_ = muted;
            ok = false;
        } else {
            pending_windows_volume_valid_ = false;
        }
    }
    const V1AvrcpAction key_actions[] = {
        V1AvrcpActionStepVolumeUp,
        V1AvrcpActionStepVolumeDown,
        V1AvrcpActionToggleMute,
        V1AvrcpActionMediaPlay,
        V1AvrcpActionMediaPause,
        V1AvrcpActionMediaPlayPause,
        V1AvrcpActionMediaStop,
        V1AvrcpActionMediaNextTrack,
        V1AvrcpActionMediaPreviousTrack,
    };
    for (const V1AvrcpAction action : key_actions) {
        if (!V1AvrcpHasAction(actions, action)) continue;
        const std::uint16_t vk = V1AvrcpVirtualKeyForAction(action);
        std::printf("action inject vk=0x%04X action=%lu\n",
                    vk, static_cast<unsigned long>(action));
        if (apply_) {
            UINT sent_count = 0u;
            DWORD input_error = ERROR_SUCCESS;
            const bool injected = vk != 0u &&
                InjectVirtualKey(vk, &sent_count, &input_error);
            if (media_key_diagnostics_) {
                std::printf(
                    "diagnostic: SendInput vk=0x%04X sent=%u requested=2 "
                    "error=%lu\n",
                    vk,
                    sent_count,
                    static_cast<unsigned long>(input_error));
            }
            if (!injected) ok = false;
        }
    }
    if (V1AvrcpHasAction(actions, V1AvrcpActionSendXm5Volume)) {
        const std::uint8_t volume = actions.xm5_absolute_volume;
        if (apply_ && writer_ != nullptr &&
            writer_->WriteAvrcp(0x50u, 0u, &volume, 1u)) {
            pending_xm5_volume_valid_ = false;
            std::printf("action send-xm5-volume value=%u (sent)\n", volume);
        } else {
            if (apply_ && writer_ != nullptr) {
                pending_xm5_volume_valid_ = true;
                pending_xm5_volume_ = volume;
            }
            std::printf(
                "action send-xm5-volume value=%u (pending; no writer)\n",
                volume);
        }
    }
    if (V1AvrcpHasAction(actions, V1AvrcpActionNotifyPlaybackStatus)) {
        const std::uint8_t status =
            actions.playback_after == V1AvrcpPlaybackState::Playing
                ? 0x01u
                : (actions.playback_after == V1AvrcpPlaybackState::Paused
                       ? 0x02u
                       : 0x00u);
        const std::uint8_t params[2] = {0x01u, status};
        if (apply_ && writer_ != nullptr &&
            writer_->WriteAvrcp(0x31u, 0x0Du, params, sizeof(params))) {
            pending_playback_status_valid_ = false;
            std::printf(
                "action notify-playback-status=%u "
                "(queued; transaction-aware)\n",
                status);
        } else {
            if (apply_ && writer_ != nullptr) {
                pending_playback_status_valid_ = true;
                pending_playback_status_ = status;
            }
            std::printf(
                "action notify-playback-status=%u "
                "(pending; writer-unavailable-or-busy)\n",
                status);
        }
    }
    return ok;
}

void V1AvrcpWindowsSink::RetryPendingWrites() {
    if (!apply_) {
        return;
    }
    if (pending_windows_volume_valid_ && EnsureVolume() &&
        SetWindowsVolume(pending_windows_volume_percent_,
                         pending_windows_volume_muted_)) {
        pending_windows_volume_valid_ = false;
    }
    if (writer_ == nullptr ||
        (!pending_xm5_volume_valid_ && !pending_playback_status_valid_)) {
        return;
    }
    if (pending_xm5_volume_valid_) {
        const std::uint8_t volume = pending_xm5_volume_;
        if (writer_->WriteAvrcp(0x50u, 0u, &volume, 1u)) {
            pending_xm5_volume_valid_ = false;
            std::printf(
                "action send-xm5-volume value=%u (sent; retry)\n", volume);
        }
    }
    if (pending_playback_status_valid_) {
        const std::uint8_t status = pending_playback_status_;
        const std::uint8_t params[2] = {0x01u, status};
        if (writer_->WriteAvrcp(0x31u, 0x0Du, params, sizeof(params))) {
            pending_playback_status_valid_ = false;
            std::printf(
                "action notify-playback-status=%u "
                "(queued; retry)\n",
                status);
        }
    }
}

bool V1AvrcpWindowsSink::QueryWindowsVolume(AvrcpWindowsVolume* volume) {
    if (volume == nullptr || !EnsureVolume()) return false;
    float scalar = 0.0f;
    BOOL muted = FALSE;
    if (FAILED(volume_->GetMasterVolumeLevelScalar(&scalar)) ||
        FAILED(volume_->GetMute(&muted))) {
        return false;
    }
    const float percent = std::min<float>(scalar * 100.0f, 100.0f);
    volume->percent = static_cast<std::uint8_t>(percent + 0.5f);
    volume->muted = muted != FALSE;
    return true;
}

bool V1AvrcpWindowsSink::WindowsVolumeNotificationsActive() const {
    return notification_registered_;
}

bool V1AvrcpWindowsSink::ConsumeWindowsVolumeChange(
    AvrcpWindowsVolume* volume) {
    if (volume == nullptr) return false;
    (void)EnsureVolume();
    if (volume_change_event_ != nullptr) {
        (void)ResetEvent(volume_change_event_);
    }
    const std::uint64_t sequence =
        published_volume_sequence_.load(std::memory_order_acquire);
    if (sequence == consumed_volume_sequence_) return false;
    const std::uint32_t packed =
        pending_volume_.load(std::memory_order_relaxed);
    volume->percent = static_cast<std::uint8_t>(packed & 0xFFu);
    volume->muted = (packed & 0x100u) != 0u;
    consumed_volume_sequence_ = sequence;
    return true;
}

HRESULT STDMETHODCALLTYPE V1AvrcpWindowsSink::QueryInterface(
    REFIID iid,
    void** object) {
    if (object == nullptr) return E_POINTER;
    *object = nullptr;
    if (iid == __uuidof(IUnknown) ||
        iid == __uuidof(IAudioEndpointVolumeCallback)) {
        *object = static_cast<IAudioEndpointVolumeCallback*>(this);
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE V1AvrcpWindowsSink::AddRef() {
    return callback_references_.fetch_add(1u, std::memory_order_relaxed) + 1u;
}

ULONG STDMETHODCALLTYPE V1AvrcpWindowsSink::Release() {
    const ULONG previous =
        callback_references_.fetch_sub(1u, std::memory_order_relaxed);
    return previous > 0u ? previous - 1u : 0u;
}

HRESULT STDMETHODCALLTYPE V1AvrcpWindowsSink::OnNotify(
    PAUDIO_VOLUME_NOTIFICATION_DATA notification) {
    if (notification == nullptr) return E_POINTER;
    const float percent =
        std::min<float>(notification->fMasterVolume * 100.0f, 100.0f);
    const std::uint32_t packed =
        static_cast<std::uint32_t>(percent + 0.5f) |
        (notification->bMuted != FALSE ? 0x100u : 0u);
    pending_volume_.store(packed, std::memory_order_relaxed);
    published_volume_sequence_.fetch_add(1u, std::memory_order_release);
    if (volume_change_event_ != nullptr) {
        (void)SetEvent(volume_change_event_);
    }
    return S_OK;
}

bool V1AvrcpWindowsSink::EnsureVolume() {
    const ULONGLONG now = GetTickCount64();
    if (enumerator_ == nullptr) {
        const HRESULT create_hr = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_INPROC_SERVER,
            __uuidof(IMMDeviceEnumerator),
            reinterpret_cast<void**>(&enumerator_));
        if (FAILED(create_hr) || enumerator_ == nullptr) {
            ++volume_endpoint_bind_failure_count_;
            std::fprintf(stderr,
                         "MMDeviceEnumerator create failed 0x%08lX\n",
                         create_hr);
            return false;
        }
    }

    if (!native_ldac_endpoint_only_) {
        if (volume_ != nullptr) return true;
        IMMDevice* selected = nullptr;
        const HRESULT default_hr = enumerator_->GetDefaultAudioEndpoint(
            eRender, eConsole, &selected);
        if (FAILED(default_hr) || selected == nullptr) {
            ++volume_endpoint_bind_failure_count_;
            return false;
        }
        LPWSTR id = nullptr;
        std::wstring endpoint_id;
        if (SUCCEEDED(selected->GetId(&id)) && id != nullptr) {
            endpoint_id = id;
        }
        if (id != nullptr) CoTaskMemFree(id);
        return BindVolumeEndpoint(selected, endpoint_id, false);
    }

    if (volume_ != nullptr && now < next_volume_validation_tick_) {
        return exact_endpoint_bound_;
    }
    if (volume_ == nullptr && now < next_volume_bind_tick_) {
        return false;
    }
    next_volume_validation_tick_ = now + 500u;
    next_volume_bind_tick_ = now + 500u;

    IMMDevice* selected = nullptr;
    std::wstring endpoint_id;
    if (!FindUniqueNativeLdacEndpoint(&selected, &endpoint_id)) {
        if (volume_ != nullptr) ReleaseVolumeEndpoint();
        ++volume_endpoint_bind_failure_count_;
        return false;
    }
    if (volume_ != nullptr && exact_endpoint_bound_ &&
        endpoint_id == bound_endpoint_id_) {
        selected->Release();
        return true;
    }
    return BindVolumeEndpoint(selected, endpoint_id, true);
}

bool V1AvrcpWindowsSink::FindUniqueNativeLdacEndpoint(
    IMMDevice** device,
    std::wstring* endpoint_id) {
    if (device == nullptr || endpoint_id == nullptr || enumerator_ == nullptr) {
        return false;
    }
    *device = nullptr;
    endpoint_id->clear();
    IMMDeviceCollection* collection = nullptr;
    HRESULT hr = enumerator_->EnumAudioEndpoints(
        eRender, DEVICE_STATE_ACTIVE, &collection);
    if (FAILED(hr) || collection == nullptr) return false;

    UINT count = 0u;
    if (FAILED(collection->GetCount(&count))) {
        collection->Release();
        return false;
    }
    std::vector<V1NativeLdacVolumeEndpointCandidate> identities;
    identities.reserve(count);
    IMMDevice* selected = nullptr;
    for (UINT index = 0u; index < count; ++index) {
        IMMDevice* candidate = nullptr;
        IPropertyStore* properties = nullptr;
        PROPVARIANT name;
        PROPVARIANT container;
        PropVariantInit(&name);
        PropVariantInit(&container);
        V1NativeLdacVolumeEndpointCandidate identity{};
        identity.active = true;
        if (SUCCEEDED(collection->Item(index, &candidate)) &&
            candidate != nullptr &&
            SUCCEEDED(candidate->OpenPropertyStore(STGM_READ, &properties)) &&
            properties != nullptr) {
            if (SUCCEEDED(properties->GetValue(
                    PKEY_Device_FriendlyName, &name)) &&
                name.vt == VT_LPWSTR && name.pwszVal != nullptr) {
                identity.native_ldac_name =
                    wcsstr(name.pwszVal, L"Native LDAC") != nullptr;
            }
            if (SUCCEEDED(properties->GetValue(
                    PKEY_Device_ContainerId, &container)) &&
                container.vt == VT_CLSID && container.puuid != nullptr) {
                identity.container_readable = true;
                identity.container_matches = IsEqualGUID(
                    *container.puuid, NativeLdacRemoteContainerId) != FALSE;
            }
        }
        identities.push_back(identity);
        if (identity.native_ldac_name && identity.container_readable &&
            identity.container_matches) {
            if (selected == nullptr) {
                selected = candidate;
                candidate = nullptr;
            }
        }
        PropVariantClear(&name);
        PropVariantClear(&container);
        if (properties != nullptr) properties->Release();
        if (candidate != nullptr) candidate->Release();
    }
    collection->Release();
    if (EvaluateV1NativeLdacVolumeEndpointIdentity(identities) !=
            V1NativeLdacVolumeEndpointIdentity::Matched ||
        selected == nullptr) {
        if (selected != nullptr) selected->Release();
        return false;
    }
    LPWSTR id = nullptr;
    hr = selected->GetId(&id);
    if (FAILED(hr) || id == nullptr) {
        if (id != nullptr) CoTaskMemFree(id);
        selected->Release();
        return false;
    }
    *endpoint_id = id;
    CoTaskMemFree(id);
    *device = selected;
    return true;
}

bool V1AvrcpWindowsSink::BindVolumeEndpoint(
    IMMDevice* device,
    const std::wstring& endpoint_id,
    bool exact_endpoint) {
    if (device == nullptr) return false;
    IAudioEndpointVolume* new_volume = nullptr;
    HRESULT hr = device->Activate(
        __uuidof(IAudioEndpointVolume),
        CLSCTX_INPROC_SERVER,
        nullptr,
        reinterpret_cast<void**>(&new_volume));
    if (FAILED(hr) || new_volume == nullptr) {
        device->Release();
        ++volume_endpoint_bind_failure_count_;
        return false;
    }
    hr = new_volume->RegisterControlChangeNotify(this);
    if (FAILED(hr)) {
        new_volume->Release();
        device->Release();
        ++volume_endpoint_bind_failure_count_;
        return false;
    }

    const bool rebound = volume_endpoint_bind_count_ != 0u &&
        endpoint_id != bound_endpoint_id_;
    ReleaseVolumeEndpoint();
    device_ = device;
    volume_ = new_volume;
    notification_registered_ = true;
    exact_endpoint_bound_ = exact_endpoint;
    bound_endpoint_id_ = endpoint_id;
    ++volume_endpoint_bind_count_;
    if (rebound) ++volume_endpoint_rebind_count_;
    std::wprintf(
        L"V1 Windows volume endpoint bound exact=%ls rebind=%llu id=%ls.\n",
        exact_endpoint ? L"yes" : L"no",
        static_cast<unsigned long long>(volume_endpoint_rebind_count_),
        bound_endpoint_id_.c_str());
    PublishCurrentVolume();
    return true;
}

void V1AvrcpWindowsSink::ReleaseVolumeEndpoint() {
    if (notification_registered_ && volume_ != nullptr) {
        (void)volume_->UnregisterControlChangeNotify(this);
    }
    notification_registered_ = false;
    if (volume_ != nullptr) volume_->Release();
    if (device_ != nullptr) device_->Release();
    volume_ = nullptr;
    device_ = nullptr;
    exact_endpoint_bound_ = false;
    bound_endpoint_id_.clear();
}

void V1AvrcpWindowsSink::PublishCurrentVolume() {
    if (volume_ == nullptr) return;
    float scalar = 0.0f;
    BOOL muted = FALSE;
    if (FAILED(volume_->GetMasterVolumeLevelScalar(&scalar)) ||
        FAILED(volume_->GetMute(&muted))) {
        return;
    }
    const float percent = std::min<float>(scalar * 100.0f, 100.0f);
    pending_volume_.store(
        static_cast<std::uint32_t>(percent + 0.5f) |
            (muted != FALSE ? 0x100u : 0u),
        std::memory_order_relaxed);
    published_volume_sequence_.fetch_add(1u, std::memory_order_release);
    if (volume_change_event_ != nullptr) (void)SetEvent(volume_change_event_);
}

bool V1AvrcpWindowsSink::SetWindowsVolume(std::uint8_t percent, bool muted) {
    if (!EnsureVolume()) return false;
    const float scalar =
        static_cast<float>(std::min<std::uint32_t>(percent, 100u)) / 100.0f;
    HRESULT hr = volume_->SetMasterVolumeLevelScalar(scalar, nullptr);
    if (FAILED(hr)) {
        std::fprintf(stderr,
                     "SetMasterVolumeLevelScalar failed 0x%08lX\n", hr);
        return false;
    }
    hr = volume_->SetMute(muted ? TRUE : FALSE, nullptr);
    if (FAILED(hr)) {
        std::fprintf(stderr, "SetMute failed 0x%08lX\n", hr);
        return false;
    }
    float readback = 0.0f;
    BOOL readback_muted = FALSE;
    if (SUCCEEDED(volume_->GetMasterVolumeLevelScalar(&readback)) &&
        SUCCEEDED(volume_->GetMute(&readback_muted))) {
        std::printf(
            "  applied scalar=%.4f readback scalar=%.4f (%u%% %s)\n",
            scalar,
            readback,
            static_cast<unsigned>(
                std::min<float>(readback * 100.0f, 100.0f) + 0.5f),
            readback_muted ? "muted" : "unmuted");
    }
    return true;
}

bool V1AvrcpWindowsSink::InjectVirtualKey(std::uint16_t vk,
                                          UINT* sent_count,
                                          DWORD* error) {
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = vk;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = vk;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    if (sent_count != nullptr) *sent_count = 0u;
    if (error != nullptr) *error = ERROR_SUCCESS;
    SetLastError(ERROR_SUCCESS);
    const UINT sent = SendInput(2u, inputs, sizeof(INPUT));
    const DWORD last_error = sent == 2u ? ERROR_SUCCESS : GetLastError();
    if (sent_count != nullptr) *sent_count = sent;
    if (error != nullptr) *error = last_error;
    return sent == 2u;
}

}  // namespace native_ldac::agent
