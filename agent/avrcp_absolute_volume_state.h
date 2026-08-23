// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace native_ldac::agent {

// This reducer is deliberately transport-free. Callers execute decisions via
// public Windows endpoint APIs and an independently authorized AVRCP backend.
enum class AvrcpAbsoluteVolumeSupport {
    Unknown,
    Unsupported,
    Supported,
};

enum class AvrcpXm5VolumeEvent {
    // Response/echo to a locally issued SET_ABSOLUTE_VOLUME command.
    CommandResponse,
    // An unsolicited remote-volume notification caused by the headset.
    RemoteNotification,
};

struct AvrcpWindowsVolume {
    // Integer Windows endpoint policy scale. Inputs above 100 are clamped.
    std::uint8_t percent = 0u;
    bool muted = true;
};

enum AvrcpAbsoluteVolumeAction : std::uint32_t {
    AvrcpVolumeActionNone = 0u,
    AvrcpVolumeActionSetWindowsEndpoint = 1u << 0u,
    AvrcpVolumeActionSendXm5AbsoluteVolume = 1u << 1u,
};

struct AvrcpAbsoluteVolumeDecision {
    std::uint32_t actions = AvrcpVolumeActionNone;
    // Valid only when AvrcpVolumeActionSetWindowsEndpoint is present.
    AvrcpWindowsVolume windows_volume{};
    // Valid only when AvrcpVolumeActionSendXm5AbsoluteVolume is present.
    std::uint8_t xm5_absolute_volume = 0u;
};

struct AvrcpAbsoluteVolumeState {
    std::uint64_t generation = 0u;
    bool session_active = false;
    AvrcpAbsoluteVolumeSupport support =
        AvrcpAbsoluteVolumeSupport::Unknown;

    // The observed Windows endpoint is the sole authoritative volume state.
    bool windows_volume_known = false;
    AvrcpWindowsVolume windows_volume{};

    bool first_session_sync_complete = false;
    bool outbound_echo_pending = false;
    std::uint8_t last_xm5_volume_sent = 0u;

    bool inbound_volume_seen = false;
    std::uint8_t last_xm5_volume_received = 0u;
    bool windows_update_pending = false;
    std::uint8_t pending_xm5_volume = 0u;
};

std::uint8_t WindowsPercentToXm5AbsoluteVolume(
    std::uint32_t windows_percent);

std::uint8_t Xm5AbsoluteVolumeToWindowsPercent(
    std::uint32_t xm5_absolute_volume);

std::uint8_t ProjectWindowsVolumeToXm5(
    const AvrcpWindowsVolume& volume);

AvrcpAbsoluteVolumeDecision BeginAvrcpAbsoluteVolumeSession(
    AvrcpAbsoluteVolumeState* state,
    std::uint64_t generation,
    AvrcpAbsoluteVolumeSupport support,
    const AvrcpWindowsVolume& observed_windows_volume);

AvrcpAbsoluteVolumeDecision SetAvrcpAbsoluteVolumeSupport(
    AvrcpAbsoluteVolumeState* state,
    std::uint64_t generation,
    AvrcpAbsoluteVolumeSupport support);

AvrcpAbsoluteVolumeDecision ObserveWindowsEndpointVolume(
    AvrcpAbsoluteVolumeState* state,
    std::uint64_t generation,
    const AvrcpWindowsVolume& observed_windows_volume);

// A RemoteNotification may request a Windows endpoint update. The caller must
// apply that decision and feed the resulting endpoint callback through
// ObserveWindowsEndpointVolume; this function never promotes the XM5 value to
// authoritative state directly. CommandResponse values are echo-only.
AvrcpAbsoluteVolumeDecision ObserveXm5AbsoluteVolume(
    AvrcpAbsoluteVolumeState* state,
    std::uint64_t generation,
    std::uint32_t xm5_absolute_volume,
    AvrcpXm5VolumeEvent event);

void EndAvrcpAbsoluteVolumeSession(AvrcpAbsoluteVolumeState* state,
                                   std::uint64_t generation);

bool HasAvrcpAbsoluteVolumeAction(
    const AvrcpAbsoluteVolumeDecision& decision,
    AvrcpAbsoluteVolumeAction action);

enum class AvrcpAbsoluteVolumeGateMode {
    ObserveOnly,
    Synchronize,
};

struct AvrcpAbsoluteVolumeGateDecision {
    AvrcpAbsoluteVolumeDecision volume{};
    std::uint64_t acl_generation = 0u;
    std::uint64_t owner_lease = 0u;
    std::uint64_t authorization_epoch = 0u;
};

struct AvrcpAbsoluteVolumeGateState {
    std::uint64_t acl_generation = 0u;
    bool acl_generation_current = false;
    std::uint64_t owner_lease = 0u;
    std::uint64_t authorization_epoch = 0u;
    AvrcpAbsoluteVolumeGateMode mode =
        AvrcpAbsoluteVolumeGateMode::ObserveOnly;
    AvrcpAbsoluteVolumeSupport observed_support =
        AvrcpAbsoluteVolumeSupport::Unknown;

    bool windows_volume_observed = false;
    AvrcpWindowsVolume last_windows_volume{};
    bool xm5_volume_observed = false;
    std::uint8_t last_xm5_volume = 0u;
    AvrcpXm5VolumeEvent last_xm5_event =
        AvrcpXm5VolumeEvent::RemoteNotification;

    AvrcpAbsoluteVolumeState reducer{};
};

// Starting a fresh ACL generation always resets the gate to ObserveOnly and
// drops its owner lease. No write permission survives a reconnect.
AvrcpAbsoluteVolumeGateDecision BeginAvrcpAbsoluteVolumeAclGeneration(
    AvrcpAbsoluteVolumeGateState* state,
    std::uint64_t acl_generation);

AvrcpAbsoluteVolumeGateDecision SetAvrcpAbsoluteVolumeGateMode(
    AvrcpAbsoluteVolumeGateState* state,
    std::uint64_t acl_generation,
    AvrcpAbsoluteVolumeGateMode mode);

AvrcpAbsoluteVolumeGateDecision AcquireAvrcpAbsoluteVolumeOwnerLease(
    AvrcpAbsoluteVolumeGateState* state,
    std::uint64_t acl_generation,
    std::uint64_t owner_lease);

AvrcpAbsoluteVolumeGateDecision RevokeAvrcpAbsoluteVolumeOwnerLease(
    AvrcpAbsoluteVolumeGateState* state,
    std::uint64_t acl_generation,
    std::uint64_t owner_lease);

AvrcpAbsoluteVolumeGateDecision ObserveAvrcpAbsoluteVolumeCapability(
    AvrcpAbsoluteVolumeGateState* state,
    std::uint64_t acl_generation,
    AvrcpAbsoluteVolumeSupport support);

AvrcpAbsoluteVolumeGateDecision ObserveWindowsEndpointVolumeThroughGate(
    AvrcpAbsoluteVolumeGateState* state,
    std::uint64_t acl_generation,
    const AvrcpWindowsVolume& observed_windows_volume);

AvrcpAbsoluteVolumeGateDecision ObserveXm5AbsoluteVolumeThroughGate(
    AvrcpAbsoluteVolumeGateState* state,
    std::uint64_t acl_generation,
    std::uint32_t xm5_absolute_volume,
    AvrcpXm5VolumeEvent event);

AvrcpAbsoluteVolumeGateDecision EndAvrcpAbsoluteVolumeAclGeneration(
    AvrcpAbsoluteVolumeGateState* state,
    std::uint64_t acl_generation);

// Execution layers must revalidate a queued decision immediately before
// writing. Revoking the lease, leaving Synchronize mode, losing capability,
// or changing ACL generation makes every older decision invalid.
bool IsAvrcpAbsoluteVolumeGateDecisionCurrent(
    const AvrcpAbsoluteVolumeGateState& state,
    const AvrcpAbsoluteVolumeGateDecision& decision);

bool HasAvrcpAbsoluteVolumeGateAction(
    const AvrcpAbsoluteVolumeGateDecision& decision,
    AvrcpAbsoluteVolumeAction action);

}  // namespace native_ldac::agent
