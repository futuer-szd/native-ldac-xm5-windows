// SPDX-License-Identifier: Apache-2.0
#include "../v1_hfp_capture_monitor.h"

#include <cstdio>
#include <vector>

namespace {
using namespace native_ldac::agent;

int failures = 0;

void Expect(bool condition, const char* message) {
    if (condition) return;
    ++failures;
    std::fprintf(stderr, "v1_hfp_capture_monitor_tests: %s\n", message);
}

V1HfpCaptureEndpointCandidate Candidate(bool active,
                                        bool named,
                                        bool readable,
                                        bool matches) {
    return {active, named, readable, matches};
}

void TestExactIdentityRequired() {
    Expect(EvaluateV1HfpCaptureEndpointIdentity({}) ==
               V1HfpCaptureEndpointIdentity::Absent,
           "empty endpoint set must be absent");
    Expect(EvaluateV1HfpCaptureEndpointIdentity(
               {Candidate(true, true, false, false)}) ==
               V1HfpCaptureEndpointIdentity::Untrusted,
           "name-only XM5 endpoint must fail closed");
    Expect(EvaluateV1HfpCaptureEndpointIdentity(
               {Candidate(true, true, true, false)}) ==
               V1HfpCaptureEndpointIdentity::Untrusted,
           "wrong-container XM5 endpoint must fail closed");
    Expect(EvaluateV1HfpCaptureEndpointIdentity(
               {Candidate(true, true, true, true)}) ==
               V1HfpCaptureEndpointIdentity::Matched,
           "name and verified container should match");
}

void TestUnrelatedAndInactiveEndpointsIgnored() {
    const std::vector<V1HfpCaptureEndpointCandidate> candidates = {
        Candidate(true, false, true, true),
        Candidate(false, true, true, true),
        Candidate(true, true, true, true),
    };
    Expect(EvaluateV1HfpCaptureEndpointIdentity(candidates) ==
               V1HfpCaptureEndpointIdentity::Matched,
           "unrelated or inactive capture endpoints affected XM5 identity");
}

void TestAmbiguousMatchFailsClosed() {
    const std::vector<V1HfpCaptureEndpointCandidate> candidates = {
        Candidate(true, true, true, true),
        Candidate(true, true, true, true),
    };
    Expect(EvaluateV1HfpCaptureEndpointIdentity(candidates) ==
               V1HfpCaptureEndpointIdentity::Ambiguous,
           "duplicate verified capture endpoints must be ambiguous");

    const std::vector<V1HfpCaptureEndpointCandidate> mixed = {
        Candidate(true, true, true, true),
        Candidate(true, true, false, false),
    };
    Expect(EvaluateV1HfpCaptureEndpointIdentity(mixed) ==
               V1HfpCaptureEndpointIdentity::Ambiguous,
           "verified plus untrusted XM5 endpoints must be ambiguous");
}

}  // namespace

int main() {
    TestExactIdentityRequired();
    TestUnrelatedAndInactiveEndpointsIgnored();
    TestAmbiguousMatchFailsClosed();
    if (failures == 0) {
        std::puts("V1 HFP capture monitor identity tests passed.");
    }
    return failures == 0 ? 0 : 1;
}
