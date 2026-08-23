// SPDX-License-Identifier: Apache-2.0
#include "v1_avrcp_filter_host.h"

#include <cstdio>

namespace native_ldac::agent {
namespace {

void StoreError(DWORD value, DWORD* error) {
    if (error != nullptr) *error = value;
}

bool Ioctl(HANDLE handle, DWORD code, void* input, DWORD input_size,
           void* output, DWORD output_size, DWORD* bytes, DWORD* error) {
    if (DeviceIoControl(handle, code, input, input_size, output, output_size,
                        bytes, nullptr) == FALSE) {
        StoreError(GetLastError(), error);
        return false;
    }
    StoreError(ERROR_SUCCESS, error);
    return true;
}

}  // namespace

V1AvrcpFilterHost::~V1AvrcpFilterHost() { Close(); }

bool V1AvrcpFilterHost::Open(DWORD* error) {
    Close();
    handle_ = CreateFileW(
        L"\\\\.\\NativeLdacAvrcpIoFilter",
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
        StoreError(GetLastError(), error);
        return false;
    }
    NLD_AVRCP_FILTER_ABI_VERSION version{};
    DWORD bytes = 0u;
    if (!Ioctl(handle_, IOCTL_NLD_AVRCP_FILTER_GET_VERSION,
               nullptr, 0u, &version, sizeof(version), &bytes, error) ||
        bytes != sizeof(version) || version.Major != 0u ||
        version.Minor < 2u ||
        (version.Flags & NLD_AVRCP_FILTER_VERSION_ABSOLUTE_VOLUME_WRITE) == 0u) {
        const DWORD failure = error != nullptr ? *error : ERROR_REVISION_MISMATCH;
        Close();
        StoreError(failure == ERROR_SUCCESS ? ERROR_REVISION_MISMATCH : failure,
                   error);
        return false;
    }
    StoreError(ERROR_SUCCESS, error);
    return true;
}

void V1AvrcpFilterHost::Close() {
    if (handle_ != INVALID_HANDLE_VALUE) {
        CloseHandle(handle_);
        handle_ = INVALID_HANDLE_VALUE;
    }
}

bool V1AvrcpFilterHost::SetAbsoluteVolume(std::uint8_t volume,
                                           DWORD* error) {
    if (handle_ == INVALID_HANDLE_VALUE || volume > 127u) {
        StoreError(ERROR_INVALID_PARAMETER, error);
        return false;
    }
    NLD_AVRCP_FILTER_SET_VOLUME_REQUEST request{};
    request.Size = sizeof(request);
    request.Version = NLD_AVRCP_FILTER_WRITE_ABI_VERSION;
    request.Volume = volume;
    DWORD bytes = 0u;
    return Ioctl(handle_, IOCTL_NLD_AVRCP_FILTER_SET_ABSOLUTE_VOLUME,
                 &request, sizeof(request), nullptr, 0u, &bytes, error);
}

bool V1AvrcpFilterHost::Dequeue(avrcp_observer_event* event, DWORD* error) {
    if (handle_ == INVALID_HANDLE_VALUE || event == nullptr) {
        StoreError(ERROR_INVALID_PARAMETER, error);
        return false;
    }
    for (;;) {
        NLD_AVRCP_FILTER_EVENT trace{};
        DWORD bytes = 0u;
        if (!Ioctl(handle_, IOCTL_NLD_AVRCP_FILTER_DEQUEUE_EVENT,
                   nullptr, 0u, &trace, sizeof(trace), &bytes, error)) {
            return false;
        }
        if (trace.Type != NldAvrcpFilterEventCompletion ||
            (trace.Flags & NLD_AVRCP_FILTER_EVENT_OUTPUT_PREFIX) == 0u ||
            trace.RawSize == 0u) {
            continue;
        }
        const auto layout = V1AvrcpFilterDetectLayout(
            trace.RawPrefix, trace.RawSize);
        if (layout == V1AvrcpFilterPayloadLayout::None) continue;
        if (V1AvrcpFilterDecodePacket(
                trace.RawPrefix, trace.RawSize, layout, event) == AVRCP_OK) {
            StoreError(ERROR_SUCCESS, error);
            return true;
        }
    }
}

bool V1AvrcpFilterHost::WriteAvrcp(ULONG pdu, ULONG response,
                                    const UCHAR* parameters,
                                    ULONG parameter_size) {
    if (pdu != AVRCP_PDU_SET_ABSOLUTE_VOLUME || response != 0u ||
        parameters == nullptr || parameter_size != 1u ||
        parameters[0] > 127u) {
        return false;
    }
    DWORD error = ERROR_SUCCESS;
    return SetAbsoluteVolume(parameters[0], &error);
}

bool V1AvrcpFilterHost::BeginSession(
    std::uint64_t generation,
    const V1MediaSessionSnapshot& media,
    V1AvrcpActionSink* sink,
    DWORD* error) {
    if (generation == 0u || sink == nullptr ||
        (session_active_ && mapper_.acl_generation != generation)) {
        StoreError(ERROR_INVALID_PARAMETER, error);
        return false;
    }
    if (!IsOpen() && !Open(error)) return false;
    const bool new_generation = !mapper_.acl_generation_current ||
        mapper_.acl_generation != generation;
    if (new_generation) {
        ResetV1AvrcpBootstrapPlay(&bootstrap_play_, generation);
        // The filter queue has no ACL-generation field. Starting a new
        // physical connection must discard any residual decoded event before
        // accepting live gestures for the new generation.
        for (unsigned int index = 0u; index < 128u; ++index) {
            avrcp_observer_event stale{};
            DWORD drain_error = ERROR_SUCCESS;
            if (Dequeue(&stale, &drain_error)) continue;
            if (drain_error != ERROR_NO_MORE_ITEMS) {
                StoreError(drain_error, error);
                return false;
            }
            break;
        }
    }
    if (mapper_.acl_generation_current &&
        mapper_.acl_generation != generation) {
        // EndSession intentionally preserves authority across multiple media
        // sessions on one physical ACL. A different generation is a real
        // reconnect and must start with a fresh mapper and XM5 authority.
        (void)V1AvrcpEndAclGeneration(
            &mapper_, mapper_.acl_generation);
    }
    if (!mapper_.acl_generation_current) {
        const auto begun = V1AvrcpBeginAclGeneration(&mapper_, generation);
        if (begun.acl_generation != generation) {
            StoreError(ERROR_INVALID_STATE, error);
            return false;
        }
    }
    sink_ = sink;
    options_ = V1AvrcpReplayOptions{};
    options_.acl_generation = generation;
    options_.volume_sync = true;
    // Microsoft remains the AVRCP function driver and is the sole media-key
    // executor. The filter mapper owns volume only; a delayed Paused->PLAY
    // fallback below is the one narrowly scoped exception.
    options_.media_routing = false;
    options_.headset_preferred = true;
    options_.media_session = media;
    // The filter host initializes the mapper directly instead of feeding a
    // GenerationStarted replay event. Preserve the same headset-authority
    // contract here: suppress PC-to-XM5 writes until the first absolute
    // volume event from this physical ACL generation has been adopted.
    mapper_.headset_preferred = options_.headset_preferred;
    ++owner_lease_;
    if (owner_lease_ == 0u) ++owner_lease_;
    (void)V1AvrcpSetControlMode(&mapper_, generation, true, false);
    (void)V1AvrcpAcquireOwnerLease(&mapper_, generation, owner_lease_);
    AvrcpWindowsVolume current{};
    if (sink_->QueryWindowsVolume(&current)) {
        (void)V1AvrcpObserveWindowsVolume(&mapper_, generation, current);
    }
    const auto actions = V1AvrcpSetMediaSessionSnapshot(&mapper_, media);
    V1AvrcpDispatchAuthorizedActions(&mapper_, actions, sink_, &stats_);
    session_active_ = true;
    StoreError(ERROR_SUCCESS, error);
    return true;
}

bool V1AvrcpFilterHost::Poll(DWORD* error) {
    if (!session_active_ || sink_ == nullptr) {
        StoreError(ERROR_SUCCESS, error);
        return true;
    }
    for (unsigned int index = 0u; index < 128u; ++index) {
        avrcp_observer_event decoded{};
        DWORD dequeue_error = ERROR_SUCCESS;
        if (!Dequeue(&decoded, &dequeue_error)) {
            if (dequeue_error == ERROR_NO_MORE_ITEMS) break;
            StoreError(dequeue_error, error);
            return false;
        }
        V1AvrcpObservedEvent observed{};
        observed.generation = mapper_.acl_generation;
        if (decoded.kind == AVRCP_OBSERVER_EVENT_VOLUME_CAPABILITY) {
            observed.kind = V1AvrcpObservedEvent::Kind::VolumeCapability;
            observed.value = decoded.volume_supported;
        } else if (decoded.kind == AVRCP_OBSERVER_EVENT_VOLUME_CHANGED) {
            observed.kind = V1AvrcpObservedEvent::Kind::AbsoluteVolume;
            observed.value = decoded.absolute_volume;
            observed.volume_event =
                decoded.response_code == AVRCP_RESPONSE_CHANGED
                    ? AvrcpXm5VolumeEvent::RemoteNotification
                    : AvrcpXm5VolumeEvent::CommandResponse;
        } else if (decoded.kind == AVRCP_OBSERVER_EVENT_PASS_THROUGH &&
                   decoded.response_code == AVRCP_CTYPE_CONTROL) {
            if (decoded.released == 0u) {
                std::printf(
                    "V1 daily Microsoft-owned AVRCP media key observed "
                    "operation=0x%02X.\n",
                    decoded.operation_id);
            }
            const bool play_like_press = decoded.released == 0u &&
                (decoded.operation_id == AVRCP_OPERATION_PLAY ||
                 decoded.operation_id == AVRCP_OPERATION_PAUSE);
            const auto bootstrap_decision =
                ObserveV1AvrcpBootstrapPlayGesture(
                    &bootstrap_play_, options_.media_session,
                    play_like_press, GetTickCount64());
            if (bootstrap_decision ==
                V1AvrcpBootstrapPlayDecision::Scheduled) {
                std::printf(
                    "V1 daily bootstrap PLAY arbitration scheduled; "
                    "waiting for Microsoft.\n");
            }
            observed.kind = V1AvrcpObservedEvent::Kind::PassThrough;
            observed.value = decoded.operation_id;
            observed.flags = decoded.released != 0u
                ? NLD_AVRCP_EVENT_FLAG_RELEASED
                : 0u;
        } else {
            continue;
        }
        if (!V1AvrcpFeedEvent(&mapper_, observed, options_, sink_, &stats_)) {
            StoreError(ERROR_INVALID_DATA, error);
            return false;
        }
    }
    const auto bootstrap_decision = ReconcileV1AvrcpBootstrapPlay(
        &bootstrap_play_, options_.media_session, GetTickCount64());
    if (bootstrap_decision ==
        V1AvrcpBootstrapPlayDecision::MicrosoftHandled) {
        std::printf(
            "V1 daily bootstrap PLAY was handled by Microsoft; "
            "fallback suppressed.\n");
    } else if (bootstrap_decision ==
               V1AvrcpBootstrapPlayDecision::InjectPlay) {
        V1AvrcpActionSet fallback{};
        fallback.actions = V1AvrcpActionMediaPlay;
        fallback.acl_generation = mapper_.acl_generation;
        fallback.authorized_current =
            mapper_.acl_generation_current && mapper_.owner_lease != 0u;
        fallback.event_observed = true;
        fallback.playback_after = V1AvrcpPlaybackState::Playing;
        fallback.playback_changed = true;
        V1AvrcpDispatchAuthorizedActions(
            &mapper_, fallback, sink_, &stats_);
        std::printf(
            "V1 daily bootstrap PLAY fallback injected after Microsoft "
            "left the paused session unchanged.\n");
    }
    sink_->RetryPendingWrites();
    AvrcpWindowsVolume notified{};
    if (sink_->ConsumeWindowsVolumeChange(&notified)) {
        const auto actions = V1AvrcpObserveWindowsVolume(
            &mapper_, mapper_.acl_generation, notified);
        V1AvrcpDispatchAuthorizedActions(
            &mapper_, actions, sink_, &stats_);
    }
    StoreError(ERROR_SUCCESS, error);
    return true;
}

void V1AvrcpFilterHost::SetMediaSessionSnapshot(
    const V1MediaSessionSnapshot& media) {
    options_.media_session = media;
    if (session_active_) {
        const auto actions = V1AvrcpSetMediaSessionSnapshot(&mapper_, media);
        V1AvrcpDispatchAuthorizedActions(
            &mapper_, actions, sink_, &stats_);
    }
}

void V1AvrcpFilterHost::EndSession() {
    if (session_active_ && mapper_.acl_generation_current) {
        (void)V1AvrcpRevokeOwnerLease(
            &mapper_, mapper_.acl_generation, owner_lease_);
    }
    session_active_ = false;
    sink_ = nullptr;
    ResetV1AvrcpBootstrapPlay(&bootstrap_play_, 0u);
}

}  // namespace native_ldac::agent
