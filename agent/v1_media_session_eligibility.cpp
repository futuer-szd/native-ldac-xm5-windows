// SPDX-License-Identifier: Apache-2.0
#include "v1_media_session_eligibility.h"

namespace native_ldac::agent {

V1MediaSessionEligibility EvaluateV1MediaSessionEligibility(
    const V1MediaSessionSnapshot& snapshot) {
    V1MediaSessionEligibility result;
    result.acl_generation = snapshot.acl_generation;
    if (snapshot.acl_generation == 0u ||
        snapshot.playback == V1MediaSessionPlayback::Absent ||
        snapshot.playback == V1MediaSessionPlayback::Stopped) {
        return result;
    }
    result.session_present = true;
    result.play_eligible =
        snapshot.playback == V1MediaSessionPlayback::Paused &&
        snapshot.play_enabled;
    result.pause_eligible =
        snapshot.playback == V1MediaSessionPlayback::Playing &&
        snapshot.pause_enabled;
    result.next_eligible = snapshot.next_enabled;
    result.previous_eligible = snapshot.previous_enabled;
    return result;
}

V1MediaSessionPlayback NormalizeV1MediaSessionPlayback(
    V1MediaSessionObservedPlayback observed,
    V1MediaSessionPlayback previous,
    bool same_session) {
    switch (observed) {
        case V1MediaSessionObservedPlayback::Playing:
            return V1MediaSessionPlayback::Playing;
        case V1MediaSessionObservedPlayback::Paused:
            return V1MediaSessionPlayback::Paused;
        case V1MediaSessionObservedPlayback::Changing:
            if (same_session &&
                (previous == V1MediaSessionPlayback::Playing ||
                 previous == V1MediaSessionPlayback::Paused)) {
                return previous;
            }
            return V1MediaSessionPlayback::Stopped;
        case V1MediaSessionObservedPlayback::Stopped:
        case V1MediaSessionObservedPlayback::Closed:
        default:
            return V1MediaSessionPlayback::Stopped;
    }
}

}  // namespace native_ldac::agent
