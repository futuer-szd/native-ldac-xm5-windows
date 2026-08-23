#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ks.h>

#include <stddef.h>
#include <stdio.h>

#include "nativeldac_pcm_abi.h"
#include "nativeldac_link_state_logic.h"
#include "nativeldac_presence_state_logic.h"
#include "nativeldac_remote_container.h"

static int expect_true(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 1;
    }
    return 0;
}

static int guid_is_nonzero(const GUID *value)
{
    const unsigned char *bytes;
    size_t index;

    bytes = (const unsigned char *)value;
    for (index = 0; index < sizeof(*value); index++) {
        if (bytes[index] != 0u) {
            return 1;
        }
    }
    return 0;
}

int main(void)
{
    int failed;
    NATIVE_LDAC_LINK_STATE link_state;
    NATIVE_LDAC_PRESENCE_STATE presence_state;

    failed = 0;
    failed += expect_true(
        sizeof(NATIVE_LDAC_PCM_INFO) == 64,
        "NATIVE_LDAC_PCM_INFO must remain 64 bytes");
    failed += expect_true(
        sizeof(NATIVE_LDAC_PCM_READ_HEADER) == 72,
        "NATIVE_LDAC_PCM_READ_HEADER must remain 72 bytes");
    failed += expect_true(
        offsetof(NATIVE_LDAC_PCM_READ_HEADER, BytesReturned) == 64,
        "BytesReturned ABI offset changed");
    failed += expect_true(
        NativeLdacPcmPropertyLinkState == 2,
        "link-state property ID changed");
    failed += expect_true(
        NativeLdacPcmPropertyPreferredFormat == 3,
        "preferred-format property ID changed");
    failed += expect_true(
        NativeLdacPcmPropertyPhysicalPresence == 4,
        "physical-presence property ID changed");
    failed += expect_true(
        NativeLdacPcmPropertyConsumerLease == 5,
        "consumer-lease property ID changed");
    failed += expect_true(
        sizeof(NATIVE_LDAC_PCM_CONSUMER_LEASE) == 24,
        "NATIVE_LDAC_PCM_CONSUMER_LEASE must remain 24 bytes");
    failed += expect_true(
        offsetof(NATIVE_LDAC_PCM_CONSUMER_LEASE,
                 ConsumerGeneration) == 16,
        "consumer generation ABI offset changed");
    failed += expect_true(
        NativeLdacPcmConsumerReleased == 0 &&
            NativeLdacPcmConsumerAcquired == 1,
        "consumer-lease values changed");
    failed += expect_true(
        sizeof(NATIVE_LDAC_PREFERRED_FORMAT) == 32,
        "NATIVE_LDAC_PREFERRED_FORMAT must remain 32 bytes");
    failed += expect_true(
        NATIVE_LDAC_FORMAT_RATE_44100 == 0x1u &&
            NATIVE_LDAC_FORMAT_RATE_48000 == 0x2u &&
            NATIVE_LDAC_FORMAT_RATE_88200 == 0x4u &&
            NATIVE_LDAC_FORMAT_RATE_96000 == 0x8u,
        "preferred-format sample-rate mask changed");
    failed += expect_true(
        NATIVE_LDAC_FORMAT_BITS_16 == 0x1u &&
            NATIVE_LDAC_FORMAT_BITS_24 == 0x2u,
        "preferred-format bit-depth mask changed");
    failed += expect_true(
        sizeof(NATIVE_LDAC_LINK_STATE) == 40,
        "NATIVE_LDAC_LINK_STATE must remain 40 bytes");
    failed += expect_true(
        offsetof(NATIVE_LDAC_LINK_STATE, SessionId) == 16,
        "link-state SessionId ABI offset changed");
    failed += expect_true(
        offsetof(NATIVE_LDAC_LINK_STATE, UpdatedInterruptTime100ns) == 32,
        "link-state update-time ABI offset changed");
    failed += expect_true(
        NativeLdacLinkStateDisconnected == 0 &&
            NativeLdacLinkStateConnecting == 1 &&
            NativeLdacLinkStateConnected == 2 &&
            NativeLdacLinkStateStopping == 3,
        "link-state values changed");
    failed += expect_true(
        sizeof(NATIVE_LDAC_PRESENCE_STATE) == 40,
        "NATIVE_LDAC_PRESENCE_STATE must remain 40 bytes");
    failed += expect_true(
        offsetof(NATIVE_LDAC_PRESENCE_STATE, PresenceGeneration) == 16,
        "presence generation ABI offset changed");
    failed += expect_true(
        offsetof(NATIVE_LDAC_PRESENCE_STATE,
                 UpdatedInterruptTime100ns) == 32,
        "presence update-time ABI offset changed");
    failed += expect_true(
        NativeLdacPresenceAbsent == 0 &&
            NativeLdacPresencePresent == 1,
        "presence-state values changed");
    failed += expect_true(
        guid_is_nonzero(&NativeLdacRemoteContainerId),
        "generated remote Container ID must not be GUID_NULL");

    ZeroMemory(&link_state, sizeof(link_state));
    link_state.Size = sizeof(link_state);
    link_state.AbiVersion = NATIVE_LDAC_LINK_STATE_ABI_VERSION;
    link_state.State = NativeLdacLinkStateConnected;
    link_state.UpdatedInterruptTime100ns = 10000000ull;
    failed += expect_true(
        NativeLdacLinkStateIsFreshConnected(&link_state, 59999999ull),
        "connected heartbeat should remain fresh before five seconds");
    failed += expect_true(
        !NativeLdacLinkStateIsFreshConnected(&link_state, 60000000ull),
        "connected heartbeat should expire at five seconds");
    failed += expect_true(
        NativeLdacLinkStateRemainingFreshTime100ns(
            &link_state,
            35000000ull) == 25000000ull,
        "heartbeat remaining-time calculation changed");
    link_state.SessionId = 42ull;
    link_state.UpdateSequence = 7ull;
    failed += expect_true(
        !NativeLdacExpireConnectedLinkState(&link_state, 59999999ull),
        "fresh heartbeat must not expire");
    failed += expect_true(
        NativeLdacExpireConnectedLinkState(&link_state, 60000000ull),
        "stale connected heartbeat must expire");
    failed += expect_true(
        link_state.State == NativeLdacLinkStateDisconnected &&
            link_state.SessionId == 42ull &&
            link_state.UpdateSequence == 8ull &&
            link_state.UpdatedInterruptTime100ns == 60000000ull,
        "heartbeat expiry must publish a versioned disconnected state");
    link_state.State = NativeLdacLinkStateStopping;
    failed += expect_true(
        !NativeLdacLinkStateIsFreshConnected(&link_state, 35000000ull),
        "stopping must report the jack as disconnected");

    ZeroMemory(&presence_state, sizeof(presence_state));
    presence_state.Size = sizeof(presence_state);
    presence_state.AbiVersion = NATIVE_LDAC_PRESENCE_STATE_ABI_VERSION;
    presence_state.State = NativeLdacPresencePresent;
    presence_state.PresenceGeneration = 9ull;
    presence_state.UpdateSequence = 3ull;
    presence_state.UpdatedInterruptTime100ns = 10000000ull;
    failed += expect_true(
        NativeLdacPresenceStateIsFreshPresent(
            &presence_state,
            159999999ull),
        "presence lease should remain fresh before fifteen seconds");
    failed += expect_true(
        !NativeLdacPresenceStateIsFreshPresent(
            &presence_state,
            160000000ull),
        "presence lease should expire at fifteen seconds");
    failed += expect_true(
        NativeLdacPresenceStateRemainingTime100ns(
            &presence_state,
            85000000ull) == 75000000ull,
        "presence remaining-time calculation changed");
    failed += expect_true(
        NativeLdacExpirePresenceState(
            &presence_state,
            160000000ull),
        "stale presence lease must expire");
    failed += expect_true(
        presence_state.State == NativeLdacPresenceAbsent &&
            presence_state.PresenceGeneration == 9ull &&
            presence_state.UpdateSequence == 4ull &&
            presence_state.UpdatedInterruptTime100ns == 160000000ull,
        "presence expiry must publish an absent state");
    failed += expect_true(
        NATIVE_LDAC_PCM_ABI_VERSION == 2u,
        "multi-format PCM ABI version changed");
    failed += expect_true(
        NATIVE_LDAC_PCM_RING_CAPACITY_BYTES % 4u == 0u &&
            NATIVE_LDAC_PCM_RING_CAPACITY_BYTES % 8u == 0u,
        "PCM ring capacity must contain whole 16/24-valid-bit stereo frames");
    failed += expect_true(
        NATIVE_LDAC_PCM_DEFAULT_SAMPLE_RATE *
                NATIVE_LDAC_PCM_DEFAULT_BLOCK_ALIGN / 4 ==
            NATIVE_LDAC_PCM_RING_CAPACITY_BYTES,
        "default PCM ring capacity must remain 250 ms");

    if (failed == 0) {
        printf("Native LDAC PCM ABI tests passed.\n");
        return 0;
    }
    return 1;
}
