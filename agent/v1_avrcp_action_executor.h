// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>
#include <string>

#include "avrcp_absolute_volume_state.h"
#include "v1_avrcp_action_mapper.h"

namespace native_ldac::agent {

// Receives authorized, non-empty mapper decisions. The caller only invokes
// Handle for decisions whose authorization is current at decision time;
// implementations are still expected to revalidate before any system write.
struct V1AvrcpActionSink {
    virtual ~V1AvrcpActionSink() = default;
    virtual bool Handle(const V1AvrcpActionSet& actions) = 0;
    // Optional: reads back the current Windows endpoint volume after an
    // applied SetWindowsVolume. Return false when unsupported.
    virtual bool QueryWindowsVolume(AvrcpWindowsVolume* volume) {
        (void)volume;
        return false;
    }
    // Optional event-driven endpoint-volume source. Implementations publish
    // callbacks into a thread-safe snapshot; the observer host consumes it on
    // its own state-machine thread.
    virtual bool WindowsVolumeNotificationsActive() const { return false; }
    virtual bool ConsumeWindowsVolumeChange(AvrcpWindowsVolume* volume) {
        (void)volume;
        return false;
    }
    // Optional retry hook for sinks that temporarily could not submit an
    // authorized AVRCP write while the observer channel was opening.
    virtual void RetryPendingWrites() {}
};

struct V1AvrcpReplayOptions {
    std::uint64_t acl_generation = 1u;
    std::uint64_t owner_lease = 1u;
    bool volume_sync = true;
    bool media_routing = true;
    bool headset_preferred = true;
    AvrcpWindowsVolume initial_windows_volume{50u, false};
    V1MediaSessionSnapshot media_session{};
};

struct V1AvrcpReplayStats {
    std::uint64_t lines = 0u;
    std::uint64_t recognized_events = 0u;
    std::uint64_t ignored_lines = 0u;
    std::uint64_t action_sets = 0u;
    std::uint64_t sink_accepted = 0u;
    std::uint64_t generation_ended = 0u;
    std::uint64_t errors = 0u;
};

// Transport-free representation of one observer event. Live drivers and log
// replay parsers both convert to this before feeding the mapper.
struct V1AvrcpObservedEvent {
    enum class Kind : std::uint8_t {
        GenerationStarted,
        GenerationEnded,
        VolumeCapability,
        AbsoluteVolume,
        PassThrough,
        SetAbsoluteVolumeCommand,
    };
    Kind kind = Kind::GenerationStarted;
    std::uint64_t generation = 0u;
    std::uint32_t flags = 0u;
    std::uint32_t value = 0u;
    AvrcpXm5VolumeEvent volume_event =
        AvrcpXm5VolumeEvent::RemoteNotification;
};

// Feeds one observed event into an active mapper session and dispatches
// authorized decisions to the sink. GenerationStarted performs the full
// session setup (control mode, owner lease, initial Windows volume).
// Returns false only on a fatal sequence error.
bool V1AvrcpFeedEvent(V1AvrcpControlMapperState* mapper,
                      const V1AvrcpObservedEvent& event,
                      const V1AvrcpReplayOptions& options,
                      V1AvrcpActionSink* sink,
                      V1AvrcpReplayStats* stats);

// Replays the v1_avrcp_observer_probe event log through the control mapper.
// Events are applied in order; only decisions that are authorized_current at
// decision time reach the sink. Returns false on a fatal sequence error.
// Dispatches an authorized action set to the sink and, for applied
// SetWindowsVolume, reads the endpoint back and feeds it to the mapper so
// the resulting echo push (SendXm5Volume) also reaches the sink.
void V1AvrcpDispatchAuthorizedActions(
    V1AvrcpControlMapperState* mapper,
    const V1AvrcpActionSet& actions,
    V1AvrcpActionSink* sink,
    V1AvrcpReplayStats* stats);

bool V1AvrcpRunReplay(const std::string& log_text,
                      const V1AvrcpReplayOptions& options,
                      V1AvrcpActionSink* sink,
                      V1AvrcpReplayStats* stats);

// Replays the v1_avrcp_filter_probe decoded trace through the control
// mapper. The filter probe prints one "decoded sequence=... kind=..." line
// per decoded AVRCP event captured on the Microsoft-preserving upper filter;
// this parser converts those lines into the shared observer vocabulary.
//
// The filter trace has no generation markers and is scoped to one bounded
// observation window, so the replay synthesizes GenerationStarted (with
// options.acl_generation) before the first event and ends the generation
// when the trace is exhausted, mirroring V1AvrcpRunReplay. Volume events are
// classified by their AVRCP response: CHANGED (0x0D) is a headset-initiated
// remote notification, INTERIM (0x0F) is a command response (adoption or a
// reply to Microsoft's SetAbsoluteVolume write). Vendor-command,
// write-response and protocol-error lines carry no mapper-relevant payload
// in this surface (Microsoft owns the PC-to-XM5 write path) and are counted
// as ignored. Returns false only on a fatal sequence error.
bool V1AvrcpRunFilterReplay(const std::string& log_text,
                            const V1AvrcpReplayOptions& options,
                            V1AvrcpActionSink* sink,
                            V1AvrcpReplayStats* stats);

// Virtual key used by executors for a single volume/media action; 0 if the
// action has no key (for example SendXm5Volume, which needs an AVRCP write).
std::uint16_t V1AvrcpVirtualKeyForAction(V1AvrcpAction action);

}  // namespace native_ldac::agent
