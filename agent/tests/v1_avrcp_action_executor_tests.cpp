// SPDX-License-Identifier: Apache-2.0
#include "../v1_avrcp_action_executor.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using namespace native_ldac::agent;

int failures = 0;

void Expect(bool condition, const char* message) {
    if (condition) return;
    ++failures;
    std::fprintf(stderr, "v1_avrcp_action_executor_tests: %s\n", message);
}

struct RecordingSink final : V1AvrcpActionSink {
    std::vector<V1AvrcpActionSet> calls;
    bool Handle(const V1AvrcpActionSet& actions) override {
        calls.push_back(actions);
        return true;
    }
};

struct FeedbackSink final : V1AvrcpActionSink {
    std::vector<V1AvrcpActionSet> calls;
    AvrcpWindowsVolume readback{50u, false};
    bool Handle(const V1AvrcpActionSet& actions) override {
        calls.push_back(actions);
        if (V1AvrcpHasAction(actions, V1AvrcpActionSetWindowsVolume)) {
            readback = actions.windows_volume;
        }
        return true;
    }
    bool QueryWindowsVolume(AvrcpWindowsVolume* volume) override {
        if (volume == nullptr) return false;
        *volume = readback;
        return true;
    }
};

const char* const kRealisticLog =
    "event sequence=1 dt=0ms generation=1 type=transport-generation-started "
    "flags=0x00000000 value=0x00000000 status=0x00000000 stage=0 pkt=0\n"
    "event sequence=2 dt=60ms generation=1 type=volume-capability "
    "flags=0x00000001 value=0x00000001 status=0x00000000 stage=0 pkt=0\n"
    "event sequence=3 dt=96ms generation=1 type=absolute-volume "
    "flags=0x0000000C value=0x00000011 status=0x00000000 stage=0 pkt=0\n"
    "event sequence=11 dt=7853ms generation=1 type=absolute-volume "
    "flags=0x00000014 value=0x00000015 status=0x00000000 stage=0 pkt=0\n"
    "event sequence=13 dt=8658ms generation=1 type=absolute-volume "
    "flags=0x00000014 value=0x00000019 status=0x00000000 stage=0 pkt=0\n"
    "event sequence=23 dt=13891ms generation=1 type=pass-through "
    "flags=0x00000000 value=0x00000044 status=0x00000000 stage=0 pkt=0\n"
    "event sequence=24 dt=13903ms generation=1 type=pass-through "
    "flags=0x00000002 value=0x00000044 status=0x00000000 stage=0 pkt=0\n"
    "event sequence=25 dt=15150ms generation=1 type=pass-through "
    "flags=0x00000000 value=0x0000004B status=0x00000000 stage=0 pkt=0\n"
    "event sequence=26 dt=15168ms generation=1 type=pass-through "
    "flags=0x00000002 value=0x0000004B status=0x00000000 stage=0 pkt=0\n"
    "event sequence=28 dt=16876ms generation=1 type=transport-generation-ended "
    "flags=0x00000000 value=0x00000000 status=0x00000000 stage=0 pkt=0\n";

void TestAuthorizedReplay() {
    RecordingSink sink;
    V1AvrcpReplayStats stats{};
    V1AvrcpReplayOptions options{};
    options.media_session = {
        1u, V1MediaSessionPlayback::Paused, true, false, true, true};
    Expect(V1AvrcpRunReplay(kRealisticLog, options, &sink, &stats),
           "authorized replay failed");
    if (!(stats.lines == 10u && stats.recognized_events == 10u &&
          stats.ignored_lines == 0u && stats.errors == 0u &&
          stats.generation_ended == 1u)) {
        std::fprintf(stderr,
                     "  stats: lines=%llu recognized=%llu ignored=%llu "
                     "action_sets=%llu sink=%llu ended=%llu errors=%llu\n",
                     static_cast<unsigned long long>(stats.lines),
                     static_cast<unsigned long long>(stats.recognized_events),
                     static_cast<unsigned long long>(stats.ignored_lines),
                     static_cast<unsigned long long>(stats.action_sets),
                     static_cast<unsigned long long>(stats.sink_accepted),
                     static_cast<unsigned long long>(stats.generation_ended),
                     static_cast<unsigned long long>(stats.errors));
    }
    Expect(stats.lines == 10u && stats.recognized_events == 10u &&
               stats.ignored_lines == 0u && stats.errors == 0u &&
               stats.generation_ended == 1u,
           "authorized replay statistics changed");
    Expect(stats.action_sets == 6u && stats.sink_accepted == 6u &&
               sink.calls.size() == 6u,
           "authorized replay produced the wrong number of actions");
    if (sink.calls.size() < 6u) return;
    Expect(V1AvrcpHasAction(sink.calls[0],
                            V1AvrcpActionNotifyPlaybackStatus) &&
               sink.calls[0].playback_after == V1AvrcpPlaybackState::Paused,
           "initial PC playback status was not announced");
    Expect(V1AvrcpHasAction(sink.calls[1], V1AvrcpActionSetWindowsVolume) &&
               sink.calls[1].windows_volume.percent == 13u,
           "headset-preferred did not adopt interim 0x11 as Windows 13%%");
    Expect(V1AvrcpHasAction(sink.calls[2], V1AvrcpActionSetWindowsVolume) &&
               sink.calls[2].windows_volume.percent == 17u,
           "0x15 did not map to Windows 17%%");
    Expect(V1AvrcpHasAction(sink.calls[3], V1AvrcpActionSetWindowsVolume) &&
               sink.calls[3].windows_volume.percent == 20u,
           "0x19 did not map to Windows 20%%");
    Expect(V1AvrcpHasAction(sink.calls[4], V1AvrcpActionMediaPlay),
           "PLAY press did not reach the sink");
    Expect(V1AvrcpHasAction(sink.calls[5], V1AvrcpActionMediaNextTrack),
           "FORWARD press did not reach the sink");
}

void TestUnauthorizedReplayIsInert() {
    RecordingSink sink;
    V1AvrcpReplayStats stats{};
    V1AvrcpReplayOptions options{};
    options.owner_lease = 0u;
    Expect(V1AvrcpRunReplay(kRealisticLog, options, &sink, &stats),
           "unauthorized replay failed");
    Expect(stats.action_sets == 0u && stats.sink_accepted == 0u &&
               sink.calls.empty(),
           "unauthorized replay reached the sink");
}

void TestBadSequenceFails() {
    RecordingSink sink;
    V1AvrcpReplayStats stats{};
    const std::string bad =
        "event sequence=1 dt=0ms generation=1 type=transport-generation-started "
        "flags=0x00000000 value=0x00000000 status=0x00000000 stage=0 pkt=0\n"
        "event sequence=2 dt=1ms generation=1 type=transport-generation-started "
        "flags=0x00000000 value=0x00000000 status=0x00000000 stage=0 pkt=0\n";
    Expect(!V1AvrcpRunReplay(bad, V1AvrcpReplayOptions{}, &sink, &stats),
           "double generation start was accepted");
    Expect(stats.errors == 1u, "double generation start error not counted");
}

void TestMalformedAndForeignLinesIgnored() {
    RecordingSink sink;
    V1AvrcpReplayStats stats{};
    const std::string text =
        "garbage line\n" +
        std::string(kRealisticLog) +
        "event sequence=99 dt=1ms generation=1 type=vendor-command "
        "flags=0x00000000 value=0x00000010 status=0x00000000 stage=0 pkt=0 "
        "pdu=0x10 params=0x00000003 0x00000000\n";
    V1AvrcpReplayOptions options{};
    options.media_session = {
        1u, V1MediaSessionPlayback::Paused, true, false, true, true};
    Expect(V1AvrcpRunReplay(text, options, &sink, &stats),
           "replay with foreign lines failed");
    Expect(stats.lines == 12u && stats.ignored_lines == 2u &&
               stats.errors == 0u && stats.action_sets == 6u,
           "foreign or malformed lines were not ignored cleanly");
}

void TestFeedEventDirectly() {
    V1AvrcpControlMapperState mapper{};
    V1AvrcpReplayStats stats{};
    RecordingSink sink;
    V1AvrcpReplayOptions options{};

    V1AvrcpObservedEvent early{};
    early.kind = V1AvrcpObservedEvent::Kind::AbsoluteVolume;
    early.generation = 1u;
    early.value = 0x15u;
    early.volume_event = AvrcpXm5VolumeEvent::RemoteNotification;
    Expect(V1AvrcpFeedEvent(&mapper, early, options, &sink, &stats),
           "pre-generation event should be tolerated");
    Expect(stats.ignored_lines == 1u && sink.calls.empty(),
           "pre-generation event was not ignored");

    V1AvrcpObservedEvent started{};
    started.kind = V1AvrcpObservedEvent::Kind::GenerationStarted;
    started.generation = 1u;
    Expect(V1AvrcpFeedEvent(&mapper, started, options, &sink, &stats),
           "generation start failed");

    V1AvrcpObservedEvent capability{};
    capability.kind = V1AvrcpObservedEvent::Kind::VolumeCapability;
    capability.generation = 1u;
    capability.value = 1u;
    (void)V1AvrcpFeedEvent(&mapper, capability, options, &sink, &stats);

    V1AvrcpObservedEvent set_volume{};
    set_volume.kind =
        V1AvrcpObservedEvent::Kind::SetAbsoluteVolumeCommand;
    set_volume.generation = 1u;
    set_volume.value = 0x15u;
    Expect(V1AvrcpFeedEvent(&mapper, set_volume, options, &sink, &stats),
           "set-absolute-volume command feed failed");
    Expect(!sink.calls.empty() &&
               V1AvrcpHasAction(sink.calls.back(),
                                V1AvrcpActionSetWindowsVolume) &&
               sink.calls.back().windows_volume.percent == 17u,
           "set-absolute-volume command did not map to Windows 17%");

    const auto errors_before = stats.errors;
    V1AvrcpObservedEvent wrong_gen{};
    wrong_gen.kind = V1AvrcpObservedEvent::Kind::PassThrough;
    wrong_gen.generation = 2u;
    wrong_gen.value = 0x44u;
    Expect(V1AvrcpFeedEvent(&mapper, wrong_gen, options, &sink, &stats),
           "generation mismatch should be non-fatal");
    Expect(stats.errors == errors_before + 1u,
           "generation mismatch was not counted");

    V1AvrcpObservedEvent second_start{};
    second_start.kind = V1AvrcpObservedEvent::Kind::GenerationStarted;
    second_start.generation = 3u;
    Expect(!V1AvrcpFeedEvent(
               &mapper, second_start, options, &sink, &stats),
           "double generation start was accepted");
}


void TestFeedbackEchoPushConverges() {
    FeedbackSink sink;
    V1AvrcpReplayStats stats{};
    V1AvrcpReplayOptions options{};
    options.media_session = {
        1u, V1MediaSessionPlayback::Paused, true, false, true, true};
    Expect(V1AvrcpRunReplay(kRealisticLog,
                            options,
                            &sink,
                            &stats),
           "feedback replay failed");
    bool saw_echo_push = false;
    for (const auto& call : sink.calls) {
        if (V1AvrcpHasAction(call, V1AvrcpActionSendXm5Volume)) {
            saw_echo_push = true;
        }
    }
    Expect(saw_echo_push,
           "endpoint feedback echo push never reached the sink");
    Expect(sink.calls.size() < 20u,
           "feedback loop did not converge");
    Expect(stats.errors == 0u, "feedback replay produced errors");
}
void TestVirtualKeyMapping() {
    Expect(V1AvrcpVirtualKeyForAction(V1AvrcpActionStepVolumeUp) == 0xAFu,
           "volume-up VK changed");
    Expect(V1AvrcpVirtualKeyForAction(V1AvrcpActionStepVolumeDown) == 0xAEu,
           "volume-down VK changed");
    Expect(V1AvrcpVirtualKeyForAction(V1AvrcpActionToggleMute) == 0xADu,
           "mute VK changed");
    Expect(V1AvrcpVirtualKeyForAction(V1AvrcpActionMediaPlay) == 0xB3u &&
               V1AvrcpVirtualKeyForAction(V1AvrcpActionMediaPause) == 0xB3u &&
               V1AvrcpVirtualKeyForAction(V1AvrcpActionMediaPlayPause) == 0xB3u,
           "play/pause VK changed");
    Expect(V1AvrcpVirtualKeyForAction(V1AvrcpActionMediaNextTrack) == 0xB0u,
           "next VK changed");
    Expect(V1AvrcpVirtualKeyForAction(V1AvrcpActionMediaPreviousTrack) == 0xB1u,
           "previous VK changed");
    Expect(V1AvrcpVirtualKeyForAction(V1AvrcpActionSendXm5Volume) == 0u,
           "send-xm5-volume must not map to a key");
    Expect(V1AvrcpVirtualKeyForAction(V1AvrcpActionNotifyPlaybackStatus) == 0u,
           "notify-playback-status must not map to a key");
}

// Exact decoded trace from the policy 6 real-machine gate
// (avrcp-filter-20260810-180716-369): 35 decoded lines covering capability,
// volume INTERIM/CHANGED notifications, PASS THROUGH press/release with
// response echoes, and Microsoft vendor commands.
const char* const kFilterRealTrace =
    "decoded sequence=11 kind=vendor-command pdu=0x10 response=0x00\n"
    "decoded sequence=15 kind=volume-capability supported=1\n"
    "decoded sequence=16 kind=vendor-command pdu=0x31 response=0x00\n"
    "decoded sequence=19 kind=volume-changed volume=42 response=0x0F\n"
    "decoded sequence=21 kind=vendor-command pdu=0x10 response=0x00\n"
    "decoded sequence=22 kind=volume-capability supported=0\n"
    "decoded sequence=25 kind=vendor-command pdu=0x31 response=0x00\n"
    "decoded sequence=26 kind=vendor-command pdu=0x31 response=0x0F\n"
    "decoded sequence=29 kind=vendor-command pdu=0x31 response=0x00\n"
    "decoded sequence=30 kind=vendor-command pdu=0x31 response=0x0F\n"
    "decoded sequence=33 kind=vendor-command pdu=0x20 response=0x00\n"
    "decoded sequence=34 kind=vendor-command pdu=0x20 response=0x0C\n"
    "decoded sequence=37 kind=vendor-command pdu=0x20 response=0x00\n"
    "decoded sequence=38 kind=vendor-command pdu=0x20 response=0x0C\n"
    "decoded sequence=41 kind=vendor-command pdu=0x20 response=0x00\n"
    "decoded sequence=42 kind=vendor-command pdu=0x20 response=0x0C\n"
    "decoded sequence=45 kind=vendor-command pdu=0x20 response=0x00\n"
    "decoded sequence=46 kind=vendor-command pdu=0x20 response=0x0C\n"
    "decoded sequence=49 kind=volume-changed volume=47 response=0x0D\n"
    "decoded sequence=50 kind=vendor-command pdu=0x31 response=0x00\n"
    "decoded sequence=53 kind=volume-changed volume=47 response=0x0F\n"
    "decoded sequence=55 kind=volume-changed volume=51 response=0x0D\n"
    "decoded sequence=56 kind=vendor-command pdu=0x31 response=0x00\n"
    "decoded sequence=59 kind=volume-changed volume=51 response=0x0F\n"
    "decoded sequence=61 kind=volume-changed volume=47 response=0x0D\n"
    "decoded sequence=62 kind=vendor-command pdu=0x31 response=0x00\n"
    "decoded sequence=65 kind=volume-changed volume=47 response=0x0F\n"
    "decoded sequence=67 kind=volume-changed volume=42 response=0x0D\n"
    "decoded sequence=68 kind=vendor-command pdu=0x31 response=0x00\n"
    "decoded sequence=71 kind=volume-changed volume=42 response=0x0F\n"
    "decoded sequence=73 kind=pass-through operation=0x44 released=0 "
    "response=0x00\n"
    "decoded sequence=74 kind=pass-through operation=0x44 released=0 "
    "response=0x09\n"
    "decoded sequence=77 kind=pass-through operation=0x44 released=1 "
    "response=0x00\n"
    "decoded sequence=78 kind=pass-through operation=0x44 released=1 "
    "response=0x09\n"
    "decoded status: capability=2; volume-changed=9; pass-through=4; "
    "vendor-command=19; protocol-error=0\n";

void TestFilterReplayRealTrace() {
    RecordingSink sink;
    V1AvrcpReplayStats stats{};
    V1AvrcpReplayOptions options{};
    options.media_session = {
        1u, V1MediaSessionPlayback::Paused, true, false, true, true};
    Expect(V1AvrcpRunFilterReplay(kFilterRealTrace,
                                  options,
                                  &sink,
                                  &stats),
           "real filter trace replay failed");
    Expect(stats.lines == 35u && stats.recognized_events == 15u &&
               stats.ignored_lines == 22u && stats.errors == 0u &&
               stats.generation_ended == 1u,
           "real filter trace replay statistics changed");
    Expect(stats.action_sets == 3u && stats.sink_accepted == 3u &&
               sink.calls.size() == 3u,
           "real filter trace produced the wrong number of actions");
    if (sink.calls.size() < 3u) return;
    Expect(V1AvrcpHasAction(sink.calls[0],
                            V1AvrcpActionNotifyPlaybackStatus) &&
               sink.calls[0].playback_after == V1AvrcpPlaybackState::Paused,
           "filter replay did not announce the initial PC playback status");
    Expect(V1AvrcpHasAction(sink.calls[1], V1AvrcpActionSetWindowsVolume) &&
               sink.calls[1].windows_volume.percent == 33u,
           "headset-preferred adoption did not map 42 to Windows 33%%");
    // The mid-window capability re-query reported supported=0, so the
    // absolute-volume gate must fail closed: 47/51 CHANGED notifications
    // after that point produce no further actions.
    Expect(V1AvrcpHasAction(sink.calls[2], V1AvrcpActionMediaPlay),
           "PASS THROUGH PLAY press did not reach the sink");
}

void TestFilterReplayVolumeSync() {
    const char* const trace =
        "decoded sequence=1 kind=volume-capability supported=1\n"
        "decoded sequence=2 kind=volume-changed volume=42 response=0x0F\n"
        "decoded sequence=3 kind=volume-changed volume=47 response=0x0D\n"
        "decoded sequence=4 kind=volume-changed volume=47 response=0x0F\n"
        "decoded sequence=5 kind=volume-changed volume=51 response=0x0D\n"
        "decoded sequence=6 kind=volume-changed volume=47 response=0x0D\n"
        "decoded sequence=7 kind=pass-through operation=0x44 released=0 "
        "response=0x00\n"
        "decoded sequence=8 kind=pass-through operation=0x44 released=0 "
        "response=0x09\n"
        "decoded sequence=9 kind=pass-through operation=0x44 released=1 "
        "response=0x00\n";
    RecordingSink sink;
    V1AvrcpReplayStats stats{};
    V1AvrcpReplayOptions options{};
    options.media_session = {
        1u, V1MediaSessionPlayback::Paused, true, false, true, true};
    Expect(V1AvrcpRunFilterReplay(trace,
                                  options,
                                  &sink,
                                  &stats),
           "clean filter trace replay failed");
    Expect(stats.lines == 9u && stats.recognized_events == 10u &&
               stats.ignored_lines == 1u && stats.errors == 0u &&
               stats.generation_ended == 1u,
           "clean filter trace replay statistics changed");
    Expect(stats.action_sets == 6u && stats.sink_accepted == 6u &&
               sink.calls.size() == 6u,
           "clean filter trace produced the wrong number of actions");
    if (sink.calls.size() < 6u) return;
    Expect(V1AvrcpHasAction(sink.calls[0],
                            V1AvrcpActionNotifyPlaybackStatus) &&
               sink.calls[0].playback_after == V1AvrcpPlaybackState::Paused,
           "clean filter replay did not announce the initial PC playback status");
    const std::uint8_t expected[4] = {33u, 37u, 40u, 37u};
    for (int index = 0; index < 4; ++index) {
        const std::size_t call_index = static_cast<std::size_t>(index + 1);
        if (!V1AvrcpHasAction(sink.calls[call_index],
                              V1AvrcpActionSetWindowsVolume) ||
            sink.calls[call_index].windows_volume.percent != expected[index]) {
            std::fprintf(stderr,
                         "  volume action %d: percent=%u expected=%u\n",
                         index,
                         sink.calls[call_index].windows_volume.percent,
                         expected[index]);
        }
        Expect(V1AvrcpHasAction(
                   sink.calls[call_index],
                   V1AvrcpActionSetWindowsVolume) &&
                   sink.calls[call_index].windows_volume.percent ==
                       expected[index],
                "filter volume change did not map to the Windows percent");
    }
    Expect(V1AvrcpHasAction(sink.calls[5], V1AvrcpActionMediaPlay),
           "filter PLAY press did not reach the sink");
    // The INTERIM echo (sequence 4) and the PASS THROUGH response echo
    // (sequence 8) must not produce actions of their own.
    Expect(stats.action_sets == 6u,
           "INTERIM or response echoes produced duplicate actions");
}

void TestFilterReplayMalformedAndForeignLines() {
    const char* const trace =
        "decoded status: capability=2; volume-changed=9; pass-through=4; "
        "vendor-command=19; protocol-error=0\n"
        "event sequence=1 generation=1 type=volume-capability "
        "flags=0x00000001 value=0x00000001 status=0x00000000 stage=0 "
        "pkt=0\n"
        "decoded sequence=99 kind=volume-changed volume=200 response=0x0D\n"
        "decoded sequence=100 kind=vendor-command pdu=0x50 response=0x00\n"
        "decoded sequence=101 kind=write-response pdu=0x50 response=0x09\n"
        "decoded sequence=102 kind=protocol-error raw=16/01 00 00 00\n"
        "decoded sequence=103 kind=pass-through operation=0x44 released=0 "
        "response=0x09\n"
        "not a trace line at all\n";
    RecordingSink sink;
    V1AvrcpReplayStats stats{};
    Expect(V1AvrcpRunFilterReplay(trace,
                                  V1AvrcpReplayOptions{},
                                  &sink,
                                  &stats),
           "malformed filter trace replay failed");
    Expect(stats.lines == 8u && stats.recognized_events == 2u &&
               stats.ignored_lines == 8u && stats.errors == 0u &&
               stats.action_sets == 1u && stats.sink_accepted == 1u &&
               sink.calls.size() == 1u &&
               V1AvrcpHasAction(
                   sink.calls[0], V1AvrcpActionNotifyPlaybackStatus) &&
               sink.calls[0].playback_after == V1AvrcpPlaybackState::Stopped &&
               stats.generation_ended == 1u,
            "malformed or foreign lines were not ignored");
}

void TestFilterReplayEmptyAndZeroGeneration() {
    RecordingSink sink;
    V1AvrcpReplayStats stats{};
    Expect(V1AvrcpRunFilterReplay("",
                                  V1AvrcpReplayOptions{},
                                  &sink,
                                  &stats),
           "empty filter trace replay failed");
    Expect(stats.lines == 0u && stats.recognized_events == 2u &&
               stats.ignored_lines == 0u && stats.errors == 0u &&
               stats.action_sets == 1u && stats.sink_accepted == 1u &&
               stats.generation_ended == 1u && sink.calls.size() == 1u &&
               V1AvrcpHasAction(
                   sink.calls[0], V1AvrcpActionNotifyPlaybackStatus) &&
               sink.calls[0].playback_after == V1AvrcpPlaybackState::Stopped,
            "empty filter trace did not start and end a clean generation");
    V1AvrcpReplayOptions zero_generation;
    zero_generation.acl_generation = 0u;
    Expect(!V1AvrcpRunFilterReplay(kFilterRealTrace,
                                   zero_generation,
                                   &sink,
                                   &stats) &&
               stats.errors >= 1u,
           "zero filter replay generation was not rejected");
}

}  // namespace

int main() {
    TestAuthorizedReplay();
    TestUnauthorizedReplayIsInert();
    TestBadSequenceFails();
    TestMalformedAndForeignLinesIgnored();
    TestFeedEventDirectly();
    TestFeedbackEchoPushConverges();
    TestVirtualKeyMapping();
    TestFilterReplayRealTrace();
    TestFilterReplayVolumeSync();
    TestFilterReplayMalformedAndForeignLines();
    TestFilterReplayEmptyAndZeroGeneration();
    if (failures == 0) {
        std::puts("V1 AVRCP action executor offline tests passed.");
    }
    return failures == 0 ? 0 : 1;
}
