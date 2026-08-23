// SPDX-License-Identifier: Apache-2.0
#include "v1_hfp_render_endpoint_monitor.h"

#include <propkeydef.h>
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <propvarutil.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "nativeldac_remote_container.h"

namespace native_ldac::agent {
namespace {

bool Contains(const std::wstring& value, const wchar_t* needle) {
    return needle != nullptr && value.find(needle) != std::wstring::npos;
}

DWORD ErrorFromHresult(HRESULT result) {
    return HRESULT_FACILITY(result) == FACILITY_WIN32
        ? HRESULT_CODE(result)
        : static_cast<DWORD>(result);
}

}  // namespace

struct V1HfpRenderEndpointMonitor::Impl final : IMMNotificationClient {
    mutable std::mutex mutex;
    std::thread worker;
    HANDLE stop_event = nullptr;
    HANDLE refresh_event = nullptr;
    std::atomic<ULONG> references{1u};
    IMMDeviceEnumerator* enumerator = nullptr;
    V1HfpRenderEndpointSnapshot snapshot{};
    bool monitor_ready = false;
    DWORD error = ERROR_SUCCESS;
    bool registered = false;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,
                                             void** object) override {
        if (object == nullptr) return E_POINTER;
        *object = nullptr;
        if (iid != __uuidof(IUnknown) &&
            iid != __uuidof(IMMNotificationClient)) {
            return E_NOINTERFACE;
        }
        *object = static_cast<IMMNotificationClient*>(this);
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

    void Signal() {
        if (refresh_event != nullptr) (void)SetEvent(refresh_event);
    }

    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override {
        Signal();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override {
        Signal();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override {
        Signal();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow,
                                                     ERole,
                                                     LPCWSTR) override {
        if (flow == eRender || flow == eAll) Signal();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(
        LPCWSTR,
        const PROPERTYKEY) override {
        Signal();
        return S_OK;
    }

    void Publish(V1HfpRenderEndpointIdentity identity, DWORD publish_error) {
        std::lock_guard<std::mutex> lock(mutex);
        snapshot = BuildV1HfpRenderEndpointSnapshot(
            identity, snapshot.sequence + 1u);
        error = publish_error;
        monitor_ready = publish_error == ERROR_SUCCESS;
    }

    void Refresh() {
        IMMDeviceCollection* collection = nullptr;
        HRESULT result = enumerator->EnumAudioEndpoints(
            eRender, DEVICE_STATE_ACTIVE, &collection);
        if (FAILED(result) || collection == nullptr) {
            Publish(V1HfpRenderEndpointIdentity::Absent,
                    ErrorFromHresult(result));
            return;
        }
        UINT count = 0u;
        result = collection->GetCount(&count);
        if (FAILED(result)) {
            collection->Release();
            Publish(V1HfpRenderEndpointIdentity::Absent,
                    ErrorFromHresult(result));
            return;
        }
        std::vector<V1HfpRenderEndpointCandidate> candidates;
        candidates.reserve(count);
        for (UINT index = 0u; index < count; ++index) {
            IMMDevice* device = nullptr;
            if (FAILED(collection->Item(index, &device)) ||
                device == nullptr) {
                continue;
            }
            V1HfpRenderEndpointCandidate candidate;
            candidate.active = true;
            IPropertyStore* properties = nullptr;
            if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &properties)) &&
                properties != nullptr) {
                PROPVARIANT value;
                PropVariantInit(&value);
                std::wstring name;
                if (SUCCEEDED(properties->GetValue(
                        PKEY_Device_FriendlyName, &value)) &&
                    value.vt == VT_LPWSTR && value.pwszVal != nullptr) {
                    name = value.pwszVal;
                }
                PropVariantClear(&value);
                candidate.xm5_name = Contains(name, L"WH-1000XM5");
                candidate.native_ldac_name = Contains(name, L"Native LDAC");
                PropVariantInit(&value);
                if (SUCCEEDED(properties->GetValue(
                        PKEY_Device_ContainerId, &value)) &&
                    value.vt == VT_CLSID && value.puuid != nullptr) {
                    candidate.container_readable = true;
                    candidate.container_matches = IsEqualGUID(
                        *value.puuid, NativeLdacRemoteContainerId) != FALSE;
                }
                PropVariantClear(&value);
                properties->Release();
            }
            candidates.push_back(candidate);
            device->Release();
        }
        collection->Release();
        Publish(EvaluateV1HfpRenderEndpointIdentity(candidates),
                ERROR_SUCCESS);
    }

    void Run() {
        HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(result)) {
            Publish(V1HfpRenderEndpointIdentity::Absent,
                    ErrorFromHresult(result));
            return;
        }
        result = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_INPROC_SERVER,
            __uuidof(IMMDeviceEnumerator),
            reinterpret_cast<void**>(&enumerator));
        if (SUCCEEDED(result) && enumerator != nullptr) {
            result = enumerator->RegisterEndpointNotificationCallback(this);
            registered = SUCCEEDED(result);
        }
        if (FAILED(result) || enumerator == nullptr) {
            Publish(V1HfpRenderEndpointIdentity::Absent,
                    ErrorFromHresult(result));
        } else {
            Refresh();
            HANDLE handles[] = {stop_event, refresh_event};
            for (;;) {
                const DWORD wait =
                    WaitForMultipleObjects(2u, handles, FALSE, INFINITE);
                if (wait == WAIT_OBJECT_0) break;
                if (wait != WAIT_OBJECT_0 + 1u) {
                    Publish(V1HfpRenderEndpointIdentity::Absent,
                            GetLastError());
                    break;
                }
                Refresh();
            }
        }
        if (registered && enumerator != nullptr) {
            (void)enumerator->UnregisterEndpointNotificationCallback(this);
        }
        registered = false;
        if (enumerator != nullptr) {
            enumerator->Release();
            enumerator = nullptr;
        }
        CoUninitialize();
    }
};

V1HfpRenderEndpointMonitor::V1HfpRenderEndpointMonitor()
    : impl_(std::make_unique<Impl>()) {}

V1HfpRenderEndpointMonitor::~V1HfpRenderEndpointMonitor() {
    Stop();
}

bool V1HfpRenderEndpointMonitor::Start(DWORD* error) {
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

void V1HfpRenderEndpointMonitor::Stop() {
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

V1HfpRenderEndpointSnapshot V1HfpRenderEndpointMonitor::Snapshot() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->snapshot;
}

bool V1HfpRenderEndpointMonitor::ready() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->monitor_ready;
}

DWORD V1HfpRenderEndpointMonitor::last_error() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->error;
}

}  // namespace native_ldac::agent
