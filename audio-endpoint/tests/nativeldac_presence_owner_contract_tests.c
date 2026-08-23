#include <stdio.h>
#include <stdlib.h>

#include "nativeldac_presence_owner_contract.h"

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            fprintf(stderr, "CHECK failed at line %d: %s\n", \
                    __LINE__, #condition); \
            return EXIT_FAILURE; \
        } \
    } while (0)

int main(void)
{
    NATIVE_LDAC_PRESENCE_OWNER owner;

    NativeLdacPresenceOwnerInitialize(&owner);
    CHECK(NativeLdacPresenceOwnerSetState(
        &owner, 11u, 101u, 1u, 0u, 100u) ==
        NativeLdacPresenceOwnerAccepted);
    CHECK(owner.OwnerId == 11u && owner.PresenceGeneration == 101u);

    CHECK(NativeLdacPresenceOwnerSetState(
        &owner, 11u, 101u, 1u, 1u, 200u) ==
        NativeLdacPresenceOwnerAccepted);
    CHECK(owner.LastUpdateTime100ns == 200u);

    CHECK(NativeLdacPresenceOwnerSetState(
        &owner, 12u, 102u, 1u, 1u, 300u) ==
        NativeLdacPresenceOwnerBusy);
    CHECK(NativeLdacPresenceOwnerSetState(
        &owner, 12u, 102u, 1u, 0u, 350u) ==
        NativeLdacPresenceOwnerBusy);
    CHECK(NativeLdacPresenceOwnerSetState(
        &owner, 11u, 102u, 0u, 1u, 400u) ==
        NativeLdacPresenceOwnerBusy);

    CHECK(NativeLdacPresenceOwnerSetState(
        &owner, 11u, 101u, 0u, 1u, 500u) ==
        NativeLdacPresenceOwnerReleased);
    CHECK(owner.OwnerId == 0u);

    CHECK(NativeLdacPresenceOwnerSetState(
        &owner, 12u, 102u, 0u, 0u, 600u) ==
        NativeLdacPresenceOwnerReleased);

    CHECK(NativeLdacPresenceOwnerSetState(
        &owner, 11u, 201u, 1u, 0u, 1000u) ==
        NativeLdacPresenceOwnerAccepted);
    CHECK(NativeLdacPresenceOwnerSetState(
        &owner,
        12u,
        202u,
        1u,
        1u,
        1000u + NATIVE_LDAC_PRESENCE_OWNER_LEASE_TIMEOUT_100NS) ==
        NativeLdacPresenceOwnerAccepted);
    CHECK(owner.OwnerId == 12u && owner.PresenceGeneration == 202u);

    CHECK(NativeLdacPresenceOwnerSetState(
        0, 1u, 1u, 1u, 0u, 1u) ==
        NativeLdacPresenceOwnerInvalid);
    CHECK(NativeLdacPresenceOwnerSetState(
        &owner, 0u, 1u, 1u, 0u, 1u) ==
        NativeLdacPresenceOwnerInvalid);
    CHECK(NativeLdacPresenceOwnerSetState(
        &owner, 1u, 0u, 1u, 0u, 1u) ==
        NativeLdacPresenceOwnerInvalid);

    puts("nativeldac_presence_owner_contract_tests: ok");
    return EXIT_SUCCESS;
}
