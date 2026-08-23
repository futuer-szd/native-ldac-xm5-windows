// SPDX-License-Identifier: Apache-2.0
#include "../v1_avrcp_handoff_state.h"

#include <cstdio>

namespace {

using native_ldac::agent::V1AvrcpHandoffInput;
using native_ldac::agent::V1AvrcpHandoffPhase;
using native_ldac::agent::V1AvrcpHandoffState;

int failures = 0;

void Check(bool condition, const char* message) {
    if (condition) return;
    ++failures;
    std::fprintf(stderr, "v1_avrcp_handoff_state_tests: %s\n", message);
}

void TestHappyPath() {
    V1AvrcpHandoffState state;
    V1AvrcpHandoffInput input;

    auto t = state.Step(input);
    Check(t.next == V1AvrcpHandoffPhase::MicrosoftHeld && !t.changed &&
              !t.request_stage_observer,
          "idle machine must stay MicrosoftHeld without actions");

    input.media_streaming = true;
    t = state.Step(input);
    Check(state.phase() == V1AvrcpHandoffPhase::HandoffPending &&
              t.request_stage_observer && !t.request_handoff_restart,
          "media start must request staging, not the restart itself");

    // Idempotent while staging is in flight.
    t = state.Step(input);
    Check(!t.changed && !t.request_stage_observer,
          "duplicate media start must not re-request staging");

    V1AvrcpHandoffInput done;
    done.media_streaming = true;
    done.handoff_restart_done = true;
    t = state.Step(done);
    Check(state.phase() == V1AvrcpHandoffPhase::ObserverActive &&
              t.notify_daily_active && state.handoff_restart_used(),
          "handoff restart completion must activate the observer window");

    // Duplicate restart outcome must be ignored.
    t = state.Step(done);
    Check(!t.changed, "duplicate handoff restart outcome must be ignored");

    V1AvrcpHandoffInput stopped;
    t = state.Step(stopped);
    Check(state.phase() == V1AvrcpHandoffPhase::RestorePending &&
              t.request_restore_restart,
          "media stop must request the Microsoft restore");

    V1AvrcpHandoffInput restored;
    restored.restore_restart_done = true;
    t = state.Step(restored);
    Check(state.phase() == V1AvrcpHandoffPhase::MicrosoftHeld &&
              state.restore_restart_used() && !state.degraded(),
          "restore completion must return to MicrosoftHeld cleanly");
}

void TestHandoffRestartFailure() {
    V1AvrcpHandoffState state;
    V1AvrcpHandoffInput input;
    input.media_streaming = true;
    (void)state.Step(input);
    Check(state.phase() == V1AvrcpHandoffPhase::HandoffPending,
          "precondition failed");

    V1AvrcpHandoffInput failed;
    failed.media_streaming = true;
    failed.handoff_restart_failed = true;
    auto t = state.Step(failed);
    Check(state.phase() == V1AvrcpHandoffPhase::RestorePending &&
              t.request_restore_restart && state.handoff_restart_used(),
          "handoff restart failure must converge to the restore path");

    // A late success must not flip the machine back.
    V1AvrcpHandoffInput late;
    late.media_streaming = true;
    late.handoff_restart_done = true;
    t = state.Step(late);
    Check(!t.changed && state.phase() == V1AvrcpHandoffPhase::RestorePending,
          "late handoff success must be ignored after the failure");
}

void TestOpenFailureAndRestoreFailure() {
    V1AvrcpHandoffState state;
    V1AvrcpHandoffInput input;
    input.media_streaming = true;
    (void)state.Step(input);
    V1AvrcpHandoffInput done;
    done.media_streaming = true;
    done.handoff_restart_done = true;
    (void)state.Step(done);
    Check(state.phase() == V1AvrcpHandoffPhase::ObserverActive,
          "precondition failed");

    V1AvrcpHandoffInput open_failed;
    open_failed.media_streaming = true;
    open_failed.observer_open_failed = true;
    auto t = state.Step(open_failed);
    Check(state.phase() == V1AvrcpHandoffPhase::RestorePending &&
              t.request_restore_restart,
          "observer OPEN failure must request the restore");

    V1AvrcpHandoffInput restore_failed;
    restore_failed.restore_restart_failed = true;
    t = state.Step(restore_failed);
    Check(state.phase() == V1AvrcpHandoffPhase::MicrosoftHeld &&
              state.degraded() && t.mark_degraded,
          "restore failure must mark the session degraded");

    // A new session may start but the degraded marker persists until the
    // operator runs the manual rollback tool.
    V1AvrcpHandoffInput next_session;
    next_session.media_streaming = true;
    t = state.Step(next_session);
    Check(state.phase() == V1AvrcpHandoffPhase::HandoffPending &&
              state.degraded() && t.request_stage_observer,
          "degraded state must allow a new session but stay marked");
}

void TestRestartBudget() {
    V1AvrcpHandoffState state;
    V1AvrcpHandoffInput input;
    input.media_streaming = true;
    (void)state.Step(input);
    Check(state.phase() == V1AvrcpHandoffPhase::HandoffPending &&
              !state.handoff_restart_used(),
          "budget precondition failed");

    // Repeated failure outcomes before the shell reports a result must not
    // consume extra budget or re-request anything.
    V1AvrcpHandoffInput failed;
    failed.media_streaming = true;
    failed.handoff_restart_failed = true;
    auto t = state.Step(failed);
    t = state.Step(failed);
    Check(state.phase() == V1AvrcpHandoffPhase::RestorePending &&
              state.handoff_restart_used() && !t.changed,
          "restart budget must be consumed exactly once");
}

}  // namespace

int main() {
    TestHappyPath();
    TestHandoffRestartFailure();
    TestOpenFailureAndRestoreFailure();
    TestRestartBudget();
    if (failures == 0) {
        std::puts("V1 AVRCP handoff state machine tests passed.");
    }
    return failures == 0 ? 0 : 1;
}
