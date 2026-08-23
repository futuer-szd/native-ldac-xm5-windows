// SPDX-License-Identifier: Apache-2.0
#include "v1_hfp_capture_monitor.h"

#include <audiopolicy.h>
#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <propvarutil.h>

#include <atomic>
#include <cwctype>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "nativeldac_remote_container.h"

namespace native_ldac::agent {
namespace {

DWORD ErrorFromHresult(HRESULT result) {
    if (HRESULT_FACILITY(result) == FACILITY_WIN32) {
        return HRESULT_CODE(result);
    }
    return static_cast<DWORD>(result);
}

bool ContainsXm5(const std::wstring& value) {
    constexpr wchar_t needle[] = L"WH-1000XM5";
    if (value.size() < std::size(needle) - 1u) return false;
    for (std::size_t start = 0u;
         start + std::size(needle) - 1u <= value.size();
         ++start) {
        bool equal = true;
        for (std::size_t index = 0u; index < std::size(needle) - 1u;
             ++index) {
            if (std::towupper(value[start + index]) != needle[index]) {
                equal = false;
                break;
            }
        }
        if (equal) return true;
    }
    return false;
}

struct EndpointCandidate {
    IMMDevice* device = nullptr;
    V1HfpCaptureEndpointCandidate identity{};
};

void ReleaseCandidates(std::vector<EndpointCandidate>* candidates) {
    if (candidates == nullptr) return;
    for (auto& candidate : *candidates) {
        if (candidate.device != nullptr) candidate.device->Release();
    }
    candidates->clear();
}

}  // namespace

V1HfpCaptureEndpointIdentity EvaluateV1HfpCaptureEndpointIdentity(
    const std::vector<V1HfpCaptureEndpointCandidate>& candidates) {
    std::uint32_t named = 0u;
    std::uint32_t matched = 0u;
    for (const auto& candidate : candidates) {
        if (!candidate.active || !candidate.xm5_name) continue;
        ++named;
        if (candidate.container_readable && candidate.container_matches) {
            ++matched;
        }
    }
    if (matched == 1u && named == 1u) {
        return V1HfpCaptureEndpointIdentity::Matched;
    }
    if (matched > 1u || named > 1u) {
        return V1HfpCaptureEndpointIdentity::Ambiguous;
    }
    return named == 0u ? V1HfpCaptureEndpointIdentity::Absent
                       : V1HfpCaptureEndpointIdentity::Untrusted;
}

struct V1HfpCaptureMonitor::Impl final : public IMMNotificationClient,
                                         public IAudioSessionNotification,
                                         public IAudioSessionEvents {
    mutable std::mutex mutex;
    std::thread worker;
    HANDLE stop_event = nullptr;
    HANDLE refresh_event = nullptr;
    std::atomic<ULONG> references{1u};
    V1HfpCaptureSnapshot snapshot{};
    bool ready = false;
    IMMDeviceEnumerator* device_enumerator = nullptr;
    IMMDevice* capture_device = nullptr;
    IAudioSessionManager2* session_manager = nullptr;
    std::vector<IAudioSessionControl*> sessions;
    bool device_notifications_registered = false;
    bool session_notifications_registered = false;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,
                                             void** object) override {
        if (object == nullptr) return E_POINTER;
        *object = nullptr;
        if (iid == __uuidof(IUnknown) ||
            iid == __uuidof(IMMNotificationClient)) {
            *object = static_cast<IMMNotificationClient*>(this);
        } else if (iid == __uuidof(IAudioSessionNotification)) {
            *object = static_cast<IAudioSessionNotification*>(this);
        } else if (iid == __uuidof(IAudioSessionEvents)) {
            *object = static_cast<IAudioSessionEvents*>(this);
        } else {
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return references.fetch_add(1u, std::memory_order_relaxed) + 1u;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG previous =
            references.fetch_sub(1u, std::memory_order_relaxed);
        return previous > 0u ? previous - 1u : 0u;
    }

    void SignalRefresh() {
        if (refresh_event != nullptr) (void)SetEvent(refresh_event);
    }

    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override {
        SignalRefresh();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override {
        SignalRefresh();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override {
        SignalRefresh();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow,
                                                     ERole,
                                                     LPCWSTR) override {
        if (flow == eCapture || flow == eAll) SignalRefresh();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(
        LPCWSTR,
        const PROPERTYKEY) override {
        SignalRefresh();
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnSessionCreated(IAudioSessionControl*) override {
        SignalRefresh();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDisplayNameChanged(LPCWSTR, LPCGUID) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnIconPathChanged(LPCWSTR, LPCGUID) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnSimpleVolumeChanged(float,
                                                    BOOL,
                                                    LPCGUID) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnChannelVolumeChanged(DWORD,
                                                     float[],
                                                     DWORD,
                                                     LPCGUID) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnGroupingParamChanged(LPCGUID,
                                                     LPCGUID) override {
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnStateChanged(AudioSessionState) override {
        SignalRefresh();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnSessionDisconnected(
        AudioSessionDisconnectReason) override {
        SignalRefresh();
        return S_OK;
    }

    void ReleaseSessionBinding() {
        for (IAudioSessionControl* session : sessions) {
            (void)session->UnregisterAudioSessionNotification(this);
            session->Release();
        }
        sessions.clear();
        if (session_notifications_registered && session_manager != nullptr) {
            (void)session_manager->UnregisterSessionNotification(this);
        }
        session_notifications_registered = false;
        if (session_manager != nullptr) {
            session_manager->Release();
            session_manager = nullptr;
        }
        if (capture_device != nullptr) {
            capture_device->Release();
            capture_device = nullptr;
        }
    }

    void Publish(V1HfpCaptureEndpointIdentity identity,
                 std::uint32_t active_sessions,
                 DWORD error) {
        std::lock_guard<std::mutex> lock(mutex);
        snapshot.endpoint_identity = identity;
        snapshot.endpoint_present =
            identity != V1HfpCaptureEndpointIdentity::Absent;
        snapshot.endpoint_matched =
            identity == V1HfpCaptureEndpointIdentity::Matched;
        snapshot.active_session_count = snapshot.endpoint_matched
            ? active_sessions
            : 0u;
        snapshot.capture_active = snapshot.endpoint_matched &&
            active_sessions != 0u;
        snapshot.last_error = error;
        ++snapshot.sequence;
        ready = error == ERROR_SUCCESS;
    }

    HRESULT EnumerateCandidates(std::vector<EndpointCandidate>* candidates) {
        if (candidates == nullptr) return E_INVALIDARG;
        IMMDeviceCollection* collection = nullptr;
        HRESULT result = device_enumerator->EnumAudioEndpoints(
            eCapture, DEVICE_STATE_ACTIVE, &collection);
        if (FAILED(result) || collection == nullptr) return result;
        UINT count = 0u;
        result = collection->GetCount(&count);
        if (FAILED(result)) {
            collection->Release();
            return result;
        }
        for (UINT index = 0u; index < count; ++index) {
            IMMDevice* device = nullptr;
            if (FAILED(collection->Item(index, &device)) ||
                device == nullptr) {
                continue;
            }
            EndpointCandidate candidate;
            candidate.device = device;
            candidate.identity.active = true;
            IPropertyStore* properties = nullptr;
            if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties)) &&
                properties != nullptr) {
                PROPVARIANT value;
                PropVariantInit(&value);
                if (SUCCEEDED(properties->GetValue(
                        PKEY_Device_FriendlyName, &value)) &&
                    value.vt == VT_LPWSTR && value.pwszVal != nullptr) {
                    candidate.identity.xm5_name = ContainsXm5(value.pwszVal);
                }
                PropVariantClear(&value);
                PropVariantInit(&value);
                if (SUCCEEDED(properties->GetValue(
                        PKEY_Device_ContainerId, &value)) &&
                    value.vt == VT_CLSID && value.puuid != nullptr) {
                    candidate.identity.container_readable = true;
                    candidate.identity.container_matches =
                        IsEqualGUID(*value.puuid,
                                    NativeLdacRemoteContainerId) != FALSE;
                }
                PropVariantClear(&value);
                properties->Release();
            }
            candidates->push_back(candidate);
        }
        collection->Release();
        return S_OK;
    }

    void Refresh() {
        ReleaseSessionBinding();
        std::vector<EndpointCandidate> candidates;
        const HRESULT enumerate_result = EnumerateCandidates(&candidates);
        if (FAILED(enumerate_result)) {
            ReleaseCandidates(&candidates);
            Publish(V1HfpCaptureEndpointIdentity::Absent,
                    0u,
                    ErrorFromHresult(enumerate_result));
            return;
        }
        std::vector<V1HfpCaptureEndpointCandidate> identities;
        identities.reserve(candidates.size());
        for (const auto& candidate : candidates) {
            identities.push_back(candidate.identity);
        }
        const auto identity =
            EvaluateV1HfpCaptureEndpointIdentity(identities);
        if (identity != V1HfpCaptureEndpointIdentity::Matched) {
            ReleaseCandidates(&candidates);
            Publish(identity, 0u, ERROR_SUCCESS);
            return;
        }
        for (auto& candidate : candidates) {
            if (candidate.identity.active && candidate.identity.xm5_name &&
                candidate.identity.container_readable &&
                candidate.identity.container_matches) {
                capture_device = candidate.device;
                candidate.device = nullptr;
                break;
            }
        }
        ReleaseCandidates(&candidates);
        if (capture_device == nullptr) {
            Publish(V1HfpCaptureEndpointIdentity::Untrusted,
                    0u,
                    ERROR_NOT_FOUND);
            return;
        }
        HRESULT result = capture_device->Activate(
            __uuidof(IAudioSessionManager2),
            CLSCTX_INPROC_SERVER,
            nullptr,
            reinterpret_cast<void**>(&session_manager));
        if (FAILED(result) || session_manager == nullptr) {
            Publish(identity, 0u, ErrorFromHresult(result));
            return;
        }
        result = session_manager->RegisterSessionNotification(this);
        if (FAILED(result)) {
            Publish(identity, 0u, ErrorFromHresult(result));
            return;
        }
        session_notifications_registered = true;
        IAudioSessionEnumerator* session_enumerator = nullptr;
        result = session_manager->GetSessionEnumerator(&session_enumerator);
        if (FAILED(result) || session_enumerator == nullptr) {
            Publish(identity, 0u, ErrorFromHresult(result));
            return;
        }
        int count = 0;
        result = session_enumerator->GetCount(&count);
        std::uint32_t active_sessions = 0u;
        if (SUCCEEDED(result)) {
            for (int index = 0; index < count; ++index) {
                IAudioSessionControl* session = nullptr;
                if (FAILED(session_enumerator->GetSession(index, &session)) ||
                    session == nullptr) {
                    continue;
                }
                AudioSessionState state = AudioSessionStateInactive;
                if (SUCCEEDED(session->GetState(&state)) &&
                    state == AudioSessionStateActive) {
                    ++active_sessions;
                }
                if (SUCCEEDED(
                        session->RegisterAudioSessionNotification(this))) {
                    sessions.push_back(session);
                } else {
                    session->Release();
                }
            }
        }
        session_enumerator->Release();
        if (FAILED(result)) {
            Publish(identity, 0u, ErrorFromHresult(result));
            return;
        }
        Publish(identity, active_sessions, ERROR_SUCCESS);
    }

    void Run() {
        const HRESULT initialize =
            CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(initialize)) {
            Publish(V1HfpCaptureEndpointIdentity::Absent,
                    0u,
                    ErrorFromHresult(initialize));
            return;
        }
        HRESULT result = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_INPROC_SERVER,
            __uuidof(IMMDeviceEnumerator),
            reinterpret_cast<void**>(&device_enumerator));
        if (SUCCEEDED(result) && device_enumerator != nullptr) {
            result = device_enumerator->RegisterEndpointNotificationCallback(
                this);
            if (SUCCEEDED(result)) device_notifications_registered = true;
        }
        if (FAILED(result) || device_enumerator == nullptr) {
            Publish(V1HfpCaptureEndpointIdentity::Absent,
                    0u,
                    ErrorFromHresult(result));
        } else {
            Refresh();
            HANDLE handles[] = {stop_event, refresh_event};
            for (;;) {
                const DWORD wait =
                    WaitForMultipleObjects(2u, handles, FALSE, INFINITE);
                if (wait == WAIT_OBJECT_0) break;
                if (wait != WAIT_OBJECT_0 + 1u) {
                    Publish(V1HfpCaptureEndpointIdentity::Absent,
                            0u,
                            GetLastError());
                    break;
                }
                Refresh();
            }
        }
        ReleaseSessionBinding();
        if (device_notifications_registered && device_enumerator != nullptr) {
            (void)device_enumerator->UnregisterEndpointNotificationCallback(
                this);
        }
        device_notifications_registered = false;
        if (device_enumerator != nullptr) {
            device_enumerator->Release();
            device_enumerator = nullptr;
        }
        CoUninitialize();
    }
};

V1HfpCaptureMonitor::V1HfpCaptureMonitor()
    : impl_(std::make_unique<Impl>()) {}

V1HfpCaptureMonitor::~V1HfpCaptureMonitor() {
    Stop();
}

bool V1HfpCaptureMonitor::Start(DWORD* error) {
    if (impl_->worker.joinable()) {
        if (error != nullptr) *error = ERROR_ALREADY_INITIALIZED;
        return false;
    }
    impl_->stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    impl_->refresh_event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (impl_->stop_event == nullptr || impl_->refresh_event == nullptr) {
        const DWORD create_error = GetLastError();
        Stop();
        if (error != nullptr) *error = create_error;
        return false;
    }
    try {
        impl_->worker = std::thread([this] { impl_->Run(); });
    } catch (...) {
        Stop();
        if (error != nullptr) *error = ERROR_NOT_ENOUGH_MEMORY;
        return false;
    }
    if (error != nullptr) *error = ERROR_SUCCESS;
    return true;
}

void V1HfpCaptureMonitor::Stop() {
    if (impl_->stop_event != nullptr) (void)SetEvent(impl_->stop_event);
    if (impl_->worker.joinable()) impl_->worker.join();
    if (impl_->refresh_event != nullptr) {
        CloseHandle(impl_->refresh_event);
        impl_->refresh_event = nullptr;
    }
    if (impl_->stop_event != nullptr) {
        CloseHandle(impl_->stop_event);
        impl_->stop_event = nullptr;
    }
}

V1HfpCaptureSnapshot V1HfpCaptureMonitor::Snapshot() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->snapshot;
}

bool V1HfpCaptureMonitor::ready() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->ready;
}

DWORD V1HfpCaptureMonitor::last_error() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->snapshot.last_error;
}

}  // namespace native_ldac::agent
