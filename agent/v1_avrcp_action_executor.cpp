// SPDX-License-Identifier: Apache-2.0
#include "v1_avrcp_action_executor.h"

#include <cstdlib>
#include <regex>
#include <sstream>
#include <string>

namespace native_ldac::agent {
namespace {

constexpr std::uint32_t kFlagChanged = 0x10u;
constexpr std::uint32_t kFlagReleased = 0x02u;
constexpr std::uint32_t kSetAbsoluteVolumePdu = 0x50u;
constexpr std::uint32_t kAvrcpResponseChanged = 0x0Du;

bool ParseHexWord(const std::string& text, std::uint32_t* value) {
    if (value == nullptr) return false;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(text.c_str(), &end, 16);
    if (end == nullptr || *end != '\0' || parsed > 0xFFFFFFFFul) {
        return false;
    }
    *value = static_cast<std::uint32_t>(parsed);
    return true;
}

bool ParseEventLine(const std::string& line, V1AvrcpObservedEvent* event) {
    if (event == nullptr) return false;
    static const std::regex type_pattern("type=([A-Za-z0-9-]+)");
    static const std::regex generation_pattern("generation=([0-9]+)");
    static const std::regex flags_pattern("flags=0x([0-9A-Fa-f]+)");
    static const std::regex value_pattern("value=0x([0-9A-Fa-f]+)");
    static const std::regex vendor_pattern(
        "pdu=0x([0-9A-Fa-f]+) params=0x([0-9A-Fa-f]+)");
    std::smatch match;
    if (!std::regex_search(line, match, type_pattern) ||
        match.size() != 2u) {
        return false;
    }
    const std::string type = match[1].str();
    if (!std::regex_search(line, match, generation_pattern) ||
        match.size() != 2u) {
        return false;
    }
    const std::uint64_t generation =
        std::strtoull(match[1].str().c_str(), nullptr, 10);
    std::uint32_t flags = 0u;
    std::uint32_t value = 0u;
    if (!std::regex_search(line, match, flags_pattern) ||
        match.size() != 2u ||
        !ParseHexWord(match[1].str(), &flags)) {
        return false;
    }
    if (!std::regex_search(line, match, value_pattern) ||
        match.size() != 2u ||
        !ParseHexWord(match[1].str(), &value)) {
        return false;
    }

    *event = V1AvrcpObservedEvent{};
    event->generation = generation;
    event->flags = flags;
    event->value = value;
    if (type == "transport-generation-started") {
        event->kind = V1AvrcpObservedEvent::Kind::GenerationStarted;
    } else if (type == "transport-generation-ended") {
        event->kind = V1AvrcpObservedEvent::Kind::GenerationEnded;
    } else if (type == "volume-capability") {
        event->kind = V1AvrcpObservedEvent::Kind::VolumeCapability;
    } else if (type == "absolute-volume") {
        event->kind = V1AvrcpObservedEvent::Kind::AbsoluteVolume;
        event->volume_event =
            (flags & kFlagChanged) != 0u
                ? AvrcpXm5VolumeEvent::RemoteNotification
                : AvrcpXm5VolumeEvent::CommandResponse;
    } else if (type == "pass-through") {
        event->kind = V1AvrcpObservedEvent::Kind::PassThrough;
    } else if (type == "vendor-command") {
        std::smatch vendor_match;
        std::uint32_t pdu = 0u;
        std::uint32_t params0 = 0u;
        if (!std::regex_search(line, vendor_match, vendor_pattern) ||
            vendor_match.size() != 3u ||
            !ParseHexWord(vendor_match[1].str(), &pdu) ||
            !ParseHexWord(vendor_match[2].str(), &params0)) {
            return false;
        }
        if (pdu != kSetAbsoluteVolumePdu) return false;
        event->kind = V1AvrcpObservedEvent::Kind::SetAbsoluteVolumeCommand;
        event->value = params0 & 0x7Fu;
    } else {
        return false;
    }
    return true;
}

// Parses one v1_avrcp_filter_probe "decoded sequence=... kind=..." line.
// The filter surface preserves Microsoft AVRCP, so this parser only lifts
// the events the volume-sync reducer consumes from the headset side:
// volume capability, absolute volume notifications (CHANGED is a
// headset-initiated remote notification, INTERIM is a command response),
// and PASS THROUGH commands. Response echoes of PASS THROUGH commands
// (response != 0x00) and all vendor-command/write-response/protocol-error
// lines are rejected so they count as ignored rather than double-firing
// media actions. Returns false for anything that is not one of those lines.
bool ParseFilterDecodedLine(const std::string& line,
                            V1AvrcpObservedEvent* event,
                            std::uint64_t generation) {
    if (event == nullptr) return false;
    static const std::regex kind_pattern(
        "decoded sequence=[0-9]+ kind=([a-z-]+)");
    static const std::regex volume_pattern(
        "volume=([0-9]+) response=0x([0-9A-Fa-f]+)");
    static const std::regex pass_through_pattern(
        "operation=0x([0-9A-Fa-f]+) released=([01]) response=0x"
        "([0-9A-Fa-f]+)");
    static const std::regex capability_pattern("supported=([01])");
    std::smatch match;
    if (!std::regex_search(line, match, kind_pattern) ||
        match.size() != 2u) {
        return false;
    }
    const std::string kind = match[1].str();
    *event = V1AvrcpObservedEvent{};
    event->generation = generation;
    if (kind == "volume-capability") {
        if (!std::regex_search(line, match, capability_pattern) ||
            match.size() != 2u) {
            return false;
        }
        event->kind = V1AvrcpObservedEvent::Kind::VolumeCapability;
        event->value = match[1].str() == "1" ? 1u : 0u;
        return true;
    }
    if (kind == "volume-changed") {
        std::uint32_t response = 0u;
        if (!std::regex_search(line, match, volume_pattern) ||
            match.size() != 3u ||
            !ParseHexWord(match[2].str(), &response)) {
            return false;
        }
        const std::uint32_t volume = static_cast<std::uint32_t>(
            std::strtoul(match[1].str().c_str(), nullptr, 10));
        if (volume > 127u) return false;
        event->kind = V1AvrcpObservedEvent::Kind::AbsoluteVolume;
        event->value = volume;
        event->volume_event =
            response == kAvrcpResponseChanged
                ? AvrcpXm5VolumeEvent::RemoteNotification
                : AvrcpXm5VolumeEvent::CommandResponse;
        return true;
    }
    if (kind == "pass-through") {
        std::uint32_t operation = 0u;
        std::uint32_t response = 0u;
        std::uint32_t released = 0u;
        if (!std::regex_search(line, match, pass_through_pattern) ||
            match.size() != 4u ||
            !ParseHexWord(match[1].str(), &operation) ||
            !ParseHexWord(match[3].str(), &response)) {
            return false;
        }
        if (operation > 0x7Fu || response != 0u) return false;
        released = match[2].str() == "1" ? 1u : 0u;
        event->kind = V1AvrcpObservedEvent::Kind::PassThrough;
        event->value = operation;
        if (released != 0u) event->flags = kFlagReleased;
        return true;
    }
    return false;
}

}  // namespace

std::uint16_t V1AvrcpVirtualKeyForAction(V1AvrcpAction action) {
    switch (action) {
        case V1AvrcpActionStepVolumeUp:
            return 0xAFu;
        case V1AvrcpActionStepVolumeDown:
            return 0xAEu;
        case V1AvrcpActionToggleMute:
            return 0xADu;
        case V1AvrcpActionMediaPlay:
        case V1AvrcpActionMediaPause:
        case V1AvrcpActionMediaPlayPause:
            return 0xB3u;
        case V1AvrcpActionMediaStop:
            return 0xB2u;
        case V1AvrcpActionMediaNextTrack:
            return 0xB0u;
        case V1AvrcpActionMediaPreviousTrack:
            return 0xB1u;
        default:
            return 0u;
    }
}

bool V1AvrcpFeedEvent(V1AvrcpControlMapperState* mapper,
                      const V1AvrcpObservedEvent& event,
                      const V1AvrcpReplayOptions& options,
                      V1AvrcpActionSink* sink,
                      V1AvrcpReplayStats* stats) {
    if (mapper == nullptr || event.generation == 0u) {
        if (stats != nullptr) ++stats->errors;
        return false;
    }

    if (event.kind == V1AvrcpObservedEvent::Kind::GenerationStarted) {
        if (mapper->acl_generation_current) {
            if (stats != nullptr) ++stats->errors;
            return false;
        }
        const V1AvrcpActionSet actions =
            V1AvrcpBeginAclGeneration(mapper, event.generation);
        if (actions.acl_generation == 0u) {
            if (stats != nullptr) ++stats->errors;
            return false;
        }
        (void)V1AvrcpSetControlMode(
            mapper, event.generation, options.volume_sync,
            options.media_routing);
        (void)V1AvrcpAcquireOwnerLease(
            mapper, event.generation, options.owner_lease);
        mapper->headset_preferred = options.headset_preferred;
        mapper->xm5_volume_seen = false;
        (void)V1AvrcpObserveWindowsVolume(
            mapper, event.generation, options.initial_windows_volume);
        V1MediaSessionSnapshot media_session = options.media_session;
        media_session.acl_generation = event.generation;
        const V1AvrcpActionSet media_actions =
            V1AvrcpSetMediaSessionSnapshot(mapper, media_session);
        V1AvrcpDispatchAuthorizedActions(
            mapper, media_actions, sink, stats);
        if (stats != nullptr) ++stats->recognized_events;
        return true;
    }

    if (event.kind == V1AvrcpObservedEvent::Kind::GenerationEnded) {
        if (!mapper->acl_generation_current) {
            if (stats != nullptr) ++stats->errors;
            return false;
        }
        (void)V1AvrcpEndAclGeneration(mapper, event.generation);
        if (stats != nullptr) {
            ++stats->recognized_events;
            ++stats->generation_ended;
        }
        return true;
    }

    if (!mapper->acl_generation_current) {
        if (stats != nullptr) ++stats->ignored_lines;
        return true;
    }
    if (event.generation != mapper->acl_generation) {
        if (stats != nullptr) ++stats->errors;
        return true;
    }

    V1AvrcpActionSet actions;
    switch (event.kind) {
        case V1AvrcpObservedEvent::Kind::VolumeCapability:
            actions = V1AvrcpObserveVolumeCapability(
                mapper, event.generation, event.value != 0u);
            break;
        case V1AvrcpObservedEvent::Kind::AbsoluteVolume:
            actions = V1AvrcpObserveXm5AbsoluteVolume(
                mapper,
                event.generation,
                event.value & 0x7Fu,
                event.volume_event);
            break;
        case V1AvrcpObservedEvent::Kind::PassThrough:
            actions = V1AvrcpObservePassThrough(
                mapper,
                event.generation,
                event.value & 0x7Fu,
                (event.flags & kFlagReleased) != 0u);
            break;
        case V1AvrcpObservedEvent::Kind::SetAbsoluteVolumeCommand:
            actions = V1AvrcpObserveXm5AbsoluteVolume(
                mapper,
                event.generation,
                event.value & 0x7Fu,
                AvrcpXm5VolumeEvent::RemoteNotification);
            break;
        default:
            if (stats != nullptr) ++stats->ignored_lines;
            return true;
    }
    if (stats != nullptr) ++stats->recognized_events;
    V1AvrcpDispatchAuthorizedActions(mapper, actions, sink, stats);
    return true;
}

void V1AvrcpDispatchAuthorizedActions(
    V1AvrcpControlMapperState* mapper,
    const V1AvrcpActionSet& actions,
    V1AvrcpActionSink* sink,
    V1AvrcpReplayStats* stats) {
    if (mapper == nullptr || actions.actions == V1AvrcpActionNone ||
        !actions.authorized_current) {
        return;
    }
    if (stats != nullptr) ++stats->action_sets;
    if (sink == nullptr || !sink->Handle(actions)) return;
    if (stats != nullptr) ++stats->sink_accepted;
    if (V1AvrcpHasAction(actions, V1AvrcpActionSetWindowsVolume)) {
        // Keep the reducer's authoritative Windows endpoint state in sync
        // after an applied volume write. The resulting echo push (if the
        // endpoint did not accept the exact requested value) is dispatched
        // back through the sink so the PC-to-XM5 direction works.
        AvrcpWindowsVolume readback{};
        if (sink->QueryWindowsVolume(&readback)) {
            const V1AvrcpActionSet feedback =
                V1AvrcpObserveWindowsVolume(
                    mapper,
                    mapper->acl_generation,
                    readback);
            V1AvrcpDispatchAuthorizedActions(
                mapper, feedback, sink, stats);
        }
    }
}

bool V1AvrcpRunReplay(const std::string& log_text,
                      const V1AvrcpReplayOptions& options,
                      V1AvrcpActionSink* sink,
                      V1AvrcpReplayStats* stats) {
    if (stats != nullptr) *stats = V1AvrcpReplayStats{};
    V1AvrcpControlMapperState mapper{};
    std::istringstream stream(log_text);
    std::string line;
    while (std::getline(stream, line)) {
        if (stats != nullptr) ++stats->lines;
        V1AvrcpObservedEvent event;
        if (!ParseEventLine(line, &event)) {
            if (stats != nullptr) ++stats->ignored_lines;
            continue;
        }
        if (!V1AvrcpFeedEvent(&mapper, event, options, sink, stats)) {
            return false;
        }
    }
    if (mapper.acl_generation_current) {
        // The observer log legitimately ends while the ACL generation is
        // still active (the gate tears the channel down after the probe
        // exits). End the generation cleanly instead of failing the replay.
        V1AvrcpObservedEvent ended;
        ended.kind = V1AvrcpObservedEvent::Kind::GenerationEnded;
        ended.generation = mapper.acl_generation;
        (void)V1AvrcpFeedEvent(&mapper, ended, options, sink, stats);
    }
    return true;
}

bool V1AvrcpRunFilterReplay(const std::string& log_text,
                            const V1AvrcpReplayOptions& options,
                            V1AvrcpActionSink* sink,
                            V1AvrcpReplayStats* stats) {
    if (stats != nullptr) *stats = V1AvrcpReplayStats{};
    if (options.acl_generation == 0u) {
        if (stats != nullptr) ++stats->errors;
        return false;
    }
    V1AvrcpControlMapperState mapper{};
    // The filter trace is scoped to one bounded observation window and has
    // no generation markers; synthesize the session start so the mapper
    // contract (control mode, owner lease, headset-preferred adoption) holds
    // exactly as it does for the observer replay path.
    V1AvrcpObservedEvent started;
    started.kind = V1AvrcpObservedEvent::Kind::GenerationStarted;
    started.generation = options.acl_generation;
    if (!V1AvrcpFeedEvent(&mapper, started, options, sink, stats)) {
        return false;
    }
    std::istringstream stream(log_text);
    std::string line;
    while (std::getline(stream, line)) {
        if (stats != nullptr) ++stats->lines;
        V1AvrcpObservedEvent event;
        if (!ParseFilterDecodedLine(line, &event, options.acl_generation)) {
            if (stats != nullptr) ++stats->ignored_lines;
            continue;
        }
        if (!V1AvrcpFeedEvent(&mapper, event, options, sink, stats)) {
            return false;
        }
    }
    if (mapper.acl_generation_current) {
        V1AvrcpObservedEvent ended;
        ended.kind = V1AvrcpObservedEvent::Kind::GenerationEnded;
        ended.generation = mapper.acl_generation;
        (void)V1AvrcpFeedEvent(&mapper, ended, options, sink, stats);
    }
    return true;
}

}  // namespace native_ldac::agent
