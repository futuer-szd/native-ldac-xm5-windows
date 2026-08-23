// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace native_ldac::agent {

// Media-period AVRCP owner handoff state machine (resident service design,
// docs/VOLUME_SYNC_RESPIDENT_DESIGN.md section 3). The elevated handoff host
// drives the PnP side; this pure state machine decides which bounded PnP
// action to request next. Invariants:
//   - Microsoft AVRCP is the resident default owner;
//   - at most one handoff restart and one restore restart per media session;
//   - every failure path converges to restoring Microsoft AVRCP;
//   - a failed restore marks the session degraded and never auto-retries.
enum class V1AvrcpHandoffPhase : std::uint8_t {
    MicrosoftHeld = 0u,
    HandoffPending,
    ObserverActive,
    RestorePending,
};

struct V1AvrcpHandoffInput {
    // Media event from the daily host.
    bool media_streaming = false;
    // Handoff restart outcome (PnP shell reports exactly one of these per
    // session; the machine ignores duplicates).
    bool handoff_restart_done = false;
    bool handoff_restart_failed = false;
    // Observer activation outcome reported by the daily host after
    // BEGIN_OBSERVATION (single outbound AVCTP OPEN).
    bool observer_open_accepted = false;
    bool observer_open_failed = false;
    // Restore restart outcome.
    bool restore_restart_done = false;
    bool restore_restart_failed = false;
};

struct V1AvrcpHandoffTransition {
    V1AvrcpHandoffPhase next = V1AvrcpHandoffPhase::MicrosoftHeld;
    bool request_stage_observer = false;
    bool request_handoff_restart = false;
    bool notify_daily_active = false;
    bool request_restore_restart = false;
    bool mark_degraded = false;
    bool changed = false;
};

class V1AvrcpHandoffState {
public:
    // Feeds one event batch and returns the requested PnP/notify actions.
    V1AvrcpHandoffTransition Step(const V1AvrcpHandoffInput& input);

    V1AvrcpHandoffPhase phase() const { return phase_; }
    // True once a restore restart failed; the session stays degraded until
    // the operator runs the manual rollback tool.
    bool degraded() const { return degraded_; }
    // Session-local restart budget consumed so far.
    bool handoff_restart_used() const { return handoff_restart_used_; }
    bool restore_restart_used() const { return restore_restart_used_; }

private:
    V1AvrcpHandoffPhase phase_ = V1AvrcpHandoffPhase::MicrosoftHeld;
    bool degraded_ = false;
    bool handoff_restart_used_ = false;
    bool restore_restart_used_ = false;
};

}  // namespace native_ldac::agent
