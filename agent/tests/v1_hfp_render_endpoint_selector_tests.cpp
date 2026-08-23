// SPDX-License-Identifier: Apache-2.0
#include "../v1_hfp_render_endpoint_selector.h"

#include <cstdio>

namespace {
using namespace native_ldac::agent;

int failures = 0;

void Expect(bool condition, const char* message) {
    if (condition) return;
    ++failures;
    std::fprintf(stderr,
                 "v1_hfp_render_endpoint_selector_tests: %s\n",
                 message);
}

V1HfpRenderEndpointCandidate Candidate(bool active,
                                       bool xm5,
                                       bool native,
                                       bool readable,
                                       bool matches) {
    return {active, xm5, native, readable, matches};
}

void TestExactIdentity() {
    Expect(EvaluateV1HfpRenderEndpointIdentity({}) ==
               V1HfpRenderEndpointIdentity::Absent,
           "empty render endpoint set was not absent");
    Expect(EvaluateV1HfpRenderEndpointIdentity(
               {Candidate(true, true, false, false, false)}) ==
               V1HfpRenderEndpointIdentity::Untrusted,
           "name-only HFP render endpoint was accepted");
    Expect(EvaluateV1HfpRenderEndpointIdentity(
               {Candidate(true, true, false, true, true)}) ==
               V1HfpRenderEndpointIdentity::Matched,
           "verified HFP render endpoint was not matched");
}

void TestNativeAndDuplicateEndpointsFailClosed() {
    Expect(EvaluateV1HfpRenderEndpointIdentity(
               {Candidate(true, true, true, true, true)}) ==
               V1HfpRenderEndpointIdentity::Absent,
           "Native LDAC endpoint was treated as HFP render");
    Expect(EvaluateV1HfpRenderEndpointIdentity(
               {Candidate(true, true, false, true, true),
                Candidate(true, true, false, true, true)}) ==
               V1HfpRenderEndpointIdentity::Ambiguous,
           "duplicate HFP render endpoints were selected");
    Expect(EvaluateV1HfpRenderEndpointIdentity(
               {Candidate(true, true, false, true, true),
                Candidate(true, true, false, false, false)}) ==
               V1HfpRenderEndpointIdentity::Ambiguous,
           "verified and untrusted HFP render endpoints were mixed");
}

void TestSnapshotReadiness() {
    const auto matched = BuildV1HfpRenderEndpointSnapshot(
        V1HfpRenderEndpointIdentity::Matched, 7u);
    Expect(matched.endpoint_present && matched.endpoint_matched &&
               matched.output_bridge_ready && matched.sequence == 7u,
           "matched render snapshot was not bridge-ready");
    const auto untrusted = BuildV1HfpRenderEndpointSnapshot(
        V1HfpRenderEndpointIdentity::Untrusted, 8u);
    Expect(untrusted.endpoint_present && !untrusted.endpoint_matched &&
               !untrusted.output_bridge_ready,
           "untrusted render snapshot was bridge-ready");
}

}  // namespace

int main() {
    TestExactIdentity();
    TestNativeAndDuplicateEndpointsFailClosed();
    TestSnapshotReadiness();
    if (failures == 0) {
        std::puts("V1 HFP render endpoint selector tests passed.");
    }
    return failures == 0 ? 0 : 1;
}
