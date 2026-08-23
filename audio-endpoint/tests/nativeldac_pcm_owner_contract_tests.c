#include <stdio.h>
#include <stdlib.h>

#include "nativeldac_pcm_owner_contract.h"

#define CHECK(expression) do {                                           \
    if (!(expression)) {                                                 \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n",                 \
                __FILE__, __LINE__, #expression);                        \
        exit(1);                                                         \
    }                                                                    \
} while (0)

static void test_owner_and_reader_are_exclusive(void)
{
    NATIVE_LDAC_PCM_OWNER owner;

    NativeLdacPcmOwnerInitialize(&owner);
    CHECK(NativeLdacPcmOwnerSetLinkState(
              &owner, 10u, 100u, 1u, 0u, 1u) ==
          NativeLdacPcmOwnerAccepted);
    CHECK(NativeLdacPcmOwnerBeginRead(&owner, 10u, 1u, 1u) ==
          NativeLdacPcmOwnerNotReady);
    CHECK(NativeLdacPcmOwnerSetLinkState(
              &owner, 11u, 101u, 1u, 1u, 2u) ==
          NativeLdacPcmOwnerBusy);
    CHECK(NativeLdacPcmOwnerSetLinkState(
              &owner, 10u, 100u, 2u, 1u, 3u) ==
          NativeLdacPcmOwnerAccepted);
    CHECK(NativeLdacPcmOwnerBeginRead(&owner, 11u, 2u, 4u) ==
          NativeLdacPcmOwnerBusy);
    CHECK(NativeLdacPcmOwnerBeginRead(&owner, 10u, 2u, 5u) ==
          NativeLdacPcmOwnerAccepted);
    CHECK(NativeLdacPcmOwnerBeginRead(&owner, 10u, 2u, 6u) ==
          NativeLdacPcmOwnerBusy);
    CHECK(NativeLdacPcmOwnerSetLinkState(
              &owner, 10u, 100u, 0u, 2u, 7u) ==
          NativeLdacPcmOwnerBusy);
    CHECK(NativeLdacPcmOwnerEndRead(&owner, 10u) ==
          NativeLdacPcmOwnerAccepted);
    CHECK(NativeLdacPcmOwnerSetLinkState(
              &owner, 10u, 100u, 0u, 2u, 8u) ==
          NativeLdacPcmOwnerReleased);
}

static void test_consumer_lease_reads_without_media_link(void)
{
    NATIVE_LDAC_PCM_OWNER owner;

    NativeLdacPcmOwnerInitialize(&owner);
    CHECK(NativeLdacPcmOwnerBeginRead(&owner, 40u, 0u, 1u) ==
          NativeLdacPcmOwnerNotReady);
    CHECK(NativeLdacPcmOwnerAcquireConsumer(
              &owner, 40u, 400u, 2u) == NativeLdacPcmOwnerAccepted);
    CHECK(owner.Sources == NATIVE_LDAC_PCM_OWNER_SOURCE_CONSUMER);
    CHECK(NativeLdacPcmOwnerBeginRead(&owner, 40u, 0u, 3u) ==
          NativeLdacPcmOwnerAccepted);
    CHECK(NativeLdacPcmOwnerEndRead(&owner, 40u) ==
          NativeLdacPcmOwnerAccepted);
    CHECK(NativeLdacPcmOwnerReleaseConsumer(&owner, 40u, 400u) ==
          NativeLdacPcmOwnerReleased);
    CHECK(owner.OwnerId == 0u && owner.Sources == 0u);
}

static void test_link_and_consumer_sources_can_share_one_owner(void)
{
    NATIVE_LDAC_PCM_OWNER owner;

    NativeLdacPcmOwnerInitialize(&owner);
    CHECK(NativeLdacPcmOwnerAcquireConsumer(
              &owner, 50u, 500u, 1u) == NativeLdacPcmOwnerAccepted);
    CHECK(NativeLdacPcmOwnerSetLinkState(
              &owner, 50u, 500u, 1u, 0u, 2u) ==
          NativeLdacPcmOwnerAccepted);
    CHECK(owner.Sources == (NATIVE_LDAC_PCM_OWNER_SOURCE_LINK |
                            NATIVE_LDAC_PCM_OWNER_SOURCE_CONSUMER));
    CHECK(NativeLdacPcmOwnerSetLinkState(
              &owner, 50u, 500u, 0u, 2u, 3u) ==
          NativeLdacPcmOwnerReleased);
    CHECK(owner.OwnerId == 50u &&
          owner.Sources == NATIVE_LDAC_PCM_OWNER_SOURCE_CONSUMER);
    CHECK(NativeLdacPcmOwnerBeginRead(&owner, 50u, 0u, 4u) ==
          NativeLdacPcmOwnerAccepted);
    CHECK(NativeLdacPcmOwnerEndRead(&owner, 50u) ==
          NativeLdacPcmOwnerAccepted);
    CHECK(NativeLdacPcmOwnerReleaseConsumer(&owner, 50u, 500u) ==
          NativeLdacPcmOwnerReleased);
}

static void test_consumer_lease_is_exclusive_and_expires(void)
{
    NATIVE_LDAC_PCM_OWNER owner;
    const unsigned long long timeout =
        NATIVE_LDAC_PCM_OWNER_LEASE_TIMEOUT_100NS;

    NativeLdacPcmOwnerInitialize(&owner);
    CHECK(NativeLdacPcmOwnerAcquireConsumer(
              &owner, 60u, 600u, 100u) == NativeLdacPcmOwnerAccepted);
    CHECK(NativeLdacPcmOwnerAcquireConsumer(
              &owner, 61u, 601u, 100u + timeout - 1u) ==
          NativeLdacPcmOwnerBusy);
    CHECK(NativeLdacPcmOwnerAcquireConsumer(
              &owner, 61u, 601u, 100u + timeout) ==
          NativeLdacPcmOwnerAccepted);
    CHECK(NativeLdacPcmOwnerReleaseConsumer(&owner, 60u, 600u) ==
          NativeLdacPcmOwnerBusy);
    CHECK(NativeLdacPcmOwnerReleaseConsumer(&owner, 61u, 601u) ==
          NativeLdacPcmOwnerReleased);
}

static void test_stale_or_disconnected_owner_can_be_replaced(void)
{
    NATIVE_LDAC_PCM_OWNER owner;
    const unsigned long long timeout =
        NATIVE_LDAC_PCM_OWNER_LEASE_TIMEOUT_100NS;

    NativeLdacPcmOwnerInitialize(&owner);
    CHECK(NativeLdacPcmOwnerSetLinkState(
              &owner, 20u, 200u, 1u, 0u, 100u) ==
          NativeLdacPcmOwnerAccepted);
    CHECK(NativeLdacPcmOwnerSetLinkState(
              &owner, 21u, 201u, 1u, 1u, 100u + timeout - 1u) ==
          NativeLdacPcmOwnerBusy);
    CHECK(NativeLdacPcmOwnerSetLinkState(
              &owner, 21u, 201u, 1u, 1u, 100u + timeout) ==
          NativeLdacPcmOwnerAccepted);
    CHECK(owner.OwnerId == 21u && owner.SessionId == 201u);
    CHECK(NativeLdacPcmOwnerSetLinkState(
              &owner, 22u, 202u, 1u, 0u, 100u + timeout + 1u) ==
          NativeLdacPcmOwnerAccepted);
    CHECK(owner.OwnerId == 22u && owner.SessionId == 202u);
}

static void test_session_token_is_required(void)
{
    NATIVE_LDAC_PCM_OWNER owner;

    NativeLdacPcmOwnerInitialize(&owner);
    CHECK(NativeLdacPcmOwnerSetLinkState(
              &owner, 30u, 0u, 1u, 0u, 1u) ==
          NativeLdacPcmOwnerInvalid);
    CHECK(NativeLdacPcmOwnerSetLinkState(
              &owner, 30u, 300u, 1u, 0u, 2u) ==
          NativeLdacPcmOwnerAccepted);
    CHECK(NativeLdacPcmOwnerSetLinkState(
              &owner, 30u, 301u, 2u, 1u, 3u) ==
          NativeLdacPcmOwnerBusy);
}

int main(void)
{
    test_owner_and_reader_are_exclusive();
    test_stale_or_disconnected_owner_can_be_replaced();
    test_session_token_is_required();
    test_consumer_lease_reads_without_media_link();
    test_link_and_consumer_sources_can_share_one_owner();
    test_consumer_lease_is_exclusive_and_expires();
    puts("nativeldac_pcm_owner_contract_tests: ok");
    return 0;
}
