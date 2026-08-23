// SPDX-License-Identifier: Apache-2.0
#include "../v1_native_ldac_volume_endpoint_selector.h"

#include <cstdio>

namespace {

using namespace native_ldac::agent;

int failures = 0;

void Check(bool condition, const char* message) {
    if (condition) return;
    ++failures;
    std::fprintf(stderr,
                 "v1_native_ldac_volume_endpoint_selector_tests: %s\n",
                 message);
}

V1NativeLdacVolumeEndpointCandidate Candidate(
    bool active, bool named, bool readable, bool matches) {
    return {active, named, readable, matches};
}

}  // namespace

int main() {
    Check(EvaluateV1NativeLdacVolumeEndpointIdentity({}) ==
              V1NativeLdacVolumeEndpointIdentity::Absent,
          "empty endpoint set was not absent");
    Check(EvaluateV1NativeLdacVolumeEndpointIdentity(
              {Candidate(true, true, false, false)}) ==
              V1NativeLdacVolumeEndpointIdentity::Untrusted,
          "name-only endpoint was trusted");
    Check(EvaluateV1NativeLdacVolumeEndpointIdentity(
              {Candidate(true, true, true, false)}) ==
              V1NativeLdacVolumeEndpointIdentity::Untrusted,
          "wrong-container endpoint was trusted");
    Check(EvaluateV1NativeLdacVolumeEndpointIdentity(
              {Candidate(true, true, true, true)}) ==
              V1NativeLdacVolumeEndpointIdentity::Matched,
          "exact Native LDAC endpoint was not selected");
    Check(EvaluateV1NativeLdacVolumeEndpointIdentity(
              {Candidate(true, true, true, true),
               Candidate(true, true, true, true)}) ==
              V1NativeLdacVolumeEndpointIdentity::Ambiguous,
          "duplicate exact endpoints were not rejected");
    Check(EvaluateV1NativeLdacVolumeEndpointIdentity(
              {Candidate(true, true, true, true),
               Candidate(true, true, false, false)}) ==
              V1NativeLdacVolumeEndpointIdentity::Ambiguous,
          "exact plus untrusted Native endpoint was not ambiguous");
    Check(EvaluateV1NativeLdacVolumeEndpointIdentity(
              {Candidate(false, true, true, true),
               Candidate(true, false, true, true)}) ==
              V1NativeLdacVolumeEndpointIdentity::Absent,
          "inactive or non-Native endpoints affected selection");
    return failures == 0 ? 0 : 1;
}
