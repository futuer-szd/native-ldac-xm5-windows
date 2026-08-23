// SPDX-License-Identifier: Apache-2.0
#include "v1_avrcp_handoff_state.h"

namespace native_ldac::agent {

V1AvrcpHandoffTransition V1AvrcpHandoffState::Step(
    const V1AvrcpHandoffInput& input) {
    V1AvrcpHandoffTransition transition;
    transition.next = phase_;

    switch (phase_) {
        case V1AvrcpHandoffPhase::MicrosoftHeld:
            if (input.media_streaming) {
                phase_ = V1AvrcpHandoffPhase::HandoffPending;
                transition.next = phase_;
                transition.request_stage_observer = true;
                transition.changed = true;
            }
            break;

        case V1AvrcpHandoffPhase::HandoffPending:
            if (!handoff_restart_used_ && input.handoff_restart_done) {
                handoff_restart_used_ = true;
                phase_ = V1AvrcpHandoffPhase::ObserverActive;
                transition.next = phase_;
                transition.notify_daily_active = true;
                transition.changed = true;
            } else if (!handoff_restart_used_ &&
                       input.handoff_restart_failed) {
                // A failed handoff restart never retries; converge to the
                // restore path so Microsoft ownership is re-established.
                handoff_restart_used_ = true;
                phase_ = V1AvrcpHandoffPhase::RestorePending;
                transition.next = phase_;
                transition.request_restore_restart = true;
                transition.changed = true;
            }
            break;

        case V1AvrcpHandoffPhase::ObserverActive:
            if (!input.media_streaming || input.observer_open_failed) {
                // Media ended or the single outbound OPEN failed: the daily
                // host ends its lease first, then Microsoft is restored.
                phase_ = V1AvrcpHandoffPhase::RestorePending;
                transition.next = phase_;
                transition.request_restore_restart = true;
                transition.changed = true;
            }
            break;

        case V1AvrcpHandoffPhase::RestorePending:
            if (!restore_restart_used_ && input.restore_restart_done) {
                restore_restart_used_ = true;
                phase_ = V1AvrcpHandoffPhase::MicrosoftHeld;
                transition.next = phase_;
                transition.changed = true;
            } else if (!restore_restart_used_ &&
                       input.restore_restart_failed) {
                restore_restart_used_ = true;
                degraded_ = true;
                phase_ = V1AvrcpHandoffPhase::MicrosoftHeld;
                transition.next = phase_;
                transition.mark_degraded = true;
                transition.changed = true;
            }
            break;
    }
    return transition;
}

}  // namespace native_ldac::agent
