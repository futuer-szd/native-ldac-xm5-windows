// SPDX-License-Identifier: Apache-2.0
#include "v1_media_session_monitor.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>

#include <mutex>
#include <string>
#include <thread>

namespace native_ldac::agent {
namespace {

using winrt::Windows::Media::Control::
    GlobalSystemMediaTransportControlsSession;
using winrt::Windows::Media::Control::
    GlobalSystemMediaTransportControlsSessionManager;
using winrt::Windows::Media::Control::
    GlobalSystemMediaTransportControlsSessionPlaybackStatus;

V1MediaSessionObservedPlayback ConvertPlayback(
    GlobalSystemMediaTransportControlsSessionPlaybackStatus status) {
    switch (status) {
        case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing:
            return V1MediaSessionObservedPlayback::Playing;
        case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused:
            return V1MediaSessionObservedPlayback::Paused;
        case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Stopped:
            return V1MediaSessionObservedPlayback::Stopped;
        case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Closed:
            return V1MediaSessionObservedPlayback::Closed;
        case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Changing:
            return V1MediaSessionObservedPlayback::Changing;
        default:
            return V1MediaSessionObservedPlayback::Stopped;
    }
}

}  // namespace

struct V1MediaSessionMonitor::Impl {
    mutable std::mutex mutex;
    std::thread worker;
    HANDLE stop_event = nullptr;
    HANDLE refresh_event = nullptr;
    V1MediaSessionSnapshot snapshot{};
    std::wstring session_id;
    bool ready = false;
    DWORD last_error = ERROR_SUCCESS;

    void PublishAbsent(DWORD error) {
        std::lock_guard<std::mutex> lock(mutex);
        snapshot = {};
        session_id.clear();
        ready = error == ERROR_SUCCESS;
        last_error = error;
    }

    void Publish(const GlobalSystemMediaTransportControlsSession& session) {
        V1MediaSessionSnapshot next{};
        V1MediaSessionPlayback previous_playback =
            V1MediaSessionPlayback::Stopped;
        std::wstring previous_session_id;
        {
            std::lock_guard<std::mutex> lock(mutex);
            previous_playback = snapshot.playback;
            previous_session_id = session_id;
        }
        std::wstring next_session_id;
        if (session != nullptr) {
            const auto info = session.GetPlaybackInfo();
            const auto controls = info.Controls();
            const auto source_id = session.SourceAppUserModelId();
            next_session_id = std::wstring(source_id.c_str());
            const auto observed_playback =
                ConvertPlayback(info.PlaybackStatus());
            next.playback = NormalizeV1MediaSessionPlayback(
                observed_playback,
                previous_playback,
                !next_session_id.empty() &&
                    next_session_id == previous_session_id);
            next.play_enabled = controls.IsPlayEnabled();
            next.pause_enabled = controls.IsPauseEnabled();
            next.next_enabled = controls.IsNextEnabled();
            next.previous_enabled = controls.IsPreviousEnabled();
        }
        std::lock_guard<std::mutex> lock(mutex);
        snapshot = next;
        session_id = next_session_id;
        ready = true;
        last_error = ERROR_SUCCESS;
    }

    void Run() {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        GlobalSystemMediaTransportControlsSessionManager manager{nullptr};
        GlobalSystemMediaTransportControlsSession session{nullptr};
        winrt::event_token manager_token{};
        winrt::event_token session_token{};
        try {
            manager =
                GlobalSystemMediaTransportControlsSessionManager::
                    RequestAsync().get();
            manager_token = manager.CurrentSessionChanged(
                [this](const auto&, const auto&) {
                    (void)SetEvent(refresh_event);
                });
            for (;;) {
                if (session != nullptr && session_token.value != 0) {
                    session.PlaybackInfoChanged(session_token);
                    session_token = {};
                }
                session = manager.GetCurrentSession();
                if (session != nullptr) {
                    session_token = session.PlaybackInfoChanged(
                        [this](const auto&, const auto&) {
                            (void)SetEvent(refresh_event);
                        });
                }
                Publish(session);
                HANDLE handles[] = {stop_event, refresh_event};
                const DWORD wait =
                    WaitForMultipleObjects(2u, handles, FALSE, INFINITE);
                if (wait == WAIT_OBJECT_0) break;
                if (wait != WAIT_OBJECT_0 + 1u) {
                    PublishAbsent(GetLastError());
                    break;
                }
            }
        } catch (const winrt::hresult_error& failure) {
            PublishAbsent(static_cast<DWORD>(failure.code().value));
        } catch (...) {
            PublishAbsent(ERROR_UNHANDLED_EXCEPTION);
        }
        if (session != nullptr && session_token.value != 0) {
            session.PlaybackInfoChanged(session_token);
        }
        if (manager != nullptr && manager_token.value != 0) {
            manager.CurrentSessionChanged(manager_token);
        }
        winrt::uninit_apartment();
    }
};

V1MediaSessionMonitor::V1MediaSessionMonitor()
    : impl_(std::make_unique<Impl>()) {}

V1MediaSessionMonitor::~V1MediaSessionMonitor() {
    Stop();
}

bool V1MediaSessionMonitor::Start(DWORD* error) {
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

void V1MediaSessionMonitor::Stop() {
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

V1MediaSessionSnapshot V1MediaSessionMonitor::Snapshot(
    std::uint64_t acl_generation) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    V1MediaSessionSnapshot result = impl_->snapshot;
    result.acl_generation = acl_generation;
    return result;
}

bool V1MediaSessionMonitor::ready() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->ready;
}

DWORD V1MediaSessionMonitor::last_error() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->last_error;
}

}  // namespace native_ldac::agent
