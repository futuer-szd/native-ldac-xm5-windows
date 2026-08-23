// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>
#include <stddef.h>
#include <string.h>

#include "nativeldac_direct_pdo_media_abi.h"

static int fail(const char* message) {
    fprintf(stderr, "direct_pdo_media_abi_tests: %s\n", message);
    return 1;
}

int main(void) {
    unsigned char storage[NLD_DIRECT_PDO_MEDIA_PACKET_HEADER_SIZE + 128u];
    NLD_DIRECT_PDO_MEDIA_PACKET_V1* packet =
        (NLD_DIRECT_PDO_MEDIA_PACKET_V1*)storage;
    NLD_DIRECT_PDO_MEDIA_STATUS_V1 status;
    NLD_DIRECT_PDO_RECOVERY_REQUEST_V1 recovery;
    NLD_DIRECT_PDO_MEDIA_VALIDATION validation;
    volatile size_t status_size = sizeof(NLD_DIRECT_PDO_MEDIA_STATUS_V1);
    volatile size_t payload_offset =
        offsetof(NLD_DIRECT_PDO_MEDIA_PACKET_V1, Payload);
    volatile size_t backend_action_offset =
        offsetof(NLD_DIRECT_PDO_MEDIA_STATUS_V1, LastBackendAction);
    volatile size_t backend_status_offset =
        offsetof(NLD_DIRECT_PDO_MEDIA_STATUS_V1, LastBackendStatus);
    volatile size_t signaling_status_offset =
        offsetof(NLD_DIRECT_PDO_MEDIA_STATUS_V1, LastSignalingOpenStatus);
    volatile unsigned long open_attempt_mask =
        NLD_DIRECT_PDO_MEDIA_STATUS_OPEN_ATTEMPTS_MASK;
    volatile unsigned long stopping_flag =
        NLD_DIRECT_PDO_MEDIA_STATUS_STOPPING;
    volatile unsigned long backend_active_flag =
        NLD_DIRECT_PDO_MEDIA_STATUS_BACKEND_ACTIVE;
    volatile unsigned long fifth_attempt =
        5u << NLD_DIRECT_PDO_MEDIA_STATUS_OPEN_ATTEMPTS_SHIFT;
    volatile unsigned long read_discover =
        (NldDirectPdoProtocolPhaseRead <<
         NLD_DIRECT_PDO_MEDIA_STATUS_PROTOCOL_PHASE_SHIFT) |
        (0x01u <<
         NLD_DIRECT_PDO_MEDIA_STATUS_SIGNAL_ID_SHIFT);
    volatile unsigned long three_commands =
        3u << NLD_DIRECT_PDO_MEDIA_STATUS_COMMANDS_COMPLETED_SHIFT;

    if (status_size != 64u || sizeof(recovery) != 32u ||
        payload_offset != NLD_DIRECT_PDO_MEDIA_PACKET_HEADER_SIZE) {
        return fail("ABI layout changed");
    }
    if (backend_action_offset != 52u || backend_status_offset != 56u ||
        signaling_status_offset != 60u) {
        return fail("ABI v3 diagnostics layout changed");
    }
    if ((open_attempt_mask & stopping_flag) != 0u ||
        (open_attempt_mask & backend_active_flag) != 0u ||
        (stopping_flag & backend_active_flag) != 0u ||
        (NLD_DIRECT_PDO_MEDIA_STATUS_PROTOCOL_PHASE_MASK &
         NLD_DIRECT_PDO_MEDIA_STATUS_COMMANDS_COMPLETED_MASK) != 0u ||
        (NLD_DIRECT_PDO_MEDIA_STATUS_COMMANDS_COMPLETED_MASK &
         NLD_DIRECT_PDO_MEDIA_STATUS_SIGNAL_ID_MASK) != 0u ||
        fifth_attempt != 0x00000500u ||
        read_discover != 0x01020000u ||
        three_commands != 0x00300000u) {
        return fail("status diagnostic flags overlap or encode incorrectly");
    }

    memset(storage, 0, sizeof(storage));
    memset(&status, 0, sizeof(status));
    packet->Size = sizeof(storage);
    packet->Version = NLD_DIRECT_PDO_MEDIA_ABI_VERSION;
    packet->MediaGeneration = 7u;
    packet->PayloadLength = 128u;
    status.Size = sizeof(status);
    status.Version = NLD_DIRECT_PDO_MEDIA_ABI_VERSION;
    status.State = NldDirectPdoMediaStreaming;
    status.Flags = NLD_DIRECT_PDO_MEDIA_STATUS_PNP_STARTED |
                   NLD_DIRECT_PDO_MEDIA_STATUS_WAVERT_RUN |
                   NLD_DIRECT_PDO_MEDIA_STATUS_CHANNEL_OPEN |
                   NLD_DIRECT_PDO_MEDIA_STATUS_STREAMING;
    status.MediaGeneration = 7u;
    status.OutgoingMtu = 895u;

    validation = NldDirectPdoMediaValidatePacket(
        packet, sizeof(storage), &status);
    if (validation != NldDirectPdoMediaValidationOk) {
        return fail("valid packet was rejected");
    }
    packet->MediaGeneration = 8u;
    if (NldDirectPdoMediaValidatePacket(packet, sizeof(storage), &status) !=
        NldDirectPdoMediaValidationGeneration) {
        return fail("stale media generation was accepted");
    }
    packet->MediaGeneration = 7u;
    status.State = NldDirectPdoMediaOpen;
    status.Flags &= ~NLD_DIRECT_PDO_MEDIA_STATUS_STREAMING;
    if (NldDirectPdoMediaValidatePacket(packet, sizeof(storage), &status) !=
        NldDirectPdoMediaValidationOffline) {
        return fail("packet was accepted before START");
    }
    status.State = NldDirectPdoMediaStreaming;
    status.Flags |= NLD_DIRECT_PDO_MEDIA_STATUS_STREAMING;
    status.OutgoingMtu = 64u;
    if (NldDirectPdoMediaValidatePacket(packet, sizeof(storage), &status) !=
        NldDirectPdoMediaValidationMtu) {
        return fail("oversized packet was accepted");
    }
    status.OutgoingMtu = 895u;
    packet->Reserved[1] = 1u;
    if (NldDirectPdoMediaValidatePacket(packet, sizeof(storage), &status) !=
        NldDirectPdoMediaValidationReserved) {
        return fail("nonzero reserved field was accepted");
    }
    packet->Reserved[1] = 0u;
    packet->Size--;
    if (NldDirectPdoMediaValidatePacket(packet, sizeof(storage), &status) !=
        NldDirectPdoMediaValidationSize) {
        return fail("mismatched packet size was accepted");
    }

    memset(&recovery, 0, sizeof(recovery));
    recovery.Size = sizeof(recovery);
    recovery.Version = NLD_DIRECT_PDO_MEDIA_ABI_VERSION;
    recovery.ExpectedSessionGeneration = 12u;
    recovery.ExpectedFailureReason = NldDirectPdoFailureMediaTimeout;
    if (NldDirectPdoValidateRecoveryRequest(
            &recovery,
            sizeof(recovery),
            12u,
            NldDirectPdoFailureMediaTimeout) !=
        NldDirectPdoRecoveryValidationOk) {
        return fail("valid recovery request was rejected");
    }
    if (NldDirectPdoValidateRecoveryRequest(
            &recovery,
            sizeof(recovery),
            13u,
            NldDirectPdoFailureMediaTimeout) !=
        NldDirectPdoRecoveryValidationGeneration) {
        return fail("stale recovery generation was accepted");
    }
    if (NldDirectPdoValidateRecoveryRequest(
            &recovery,
            sizeof(recovery),
            12u,
            NldDirectPdoFailureRemoteDisconnect) !=
        NldDirectPdoRecoveryValidationReason) {
        return fail("mismatched recovery reason was accepted");
    }
    recovery.Reserved[2] = 1u;
    if (NldDirectPdoValidateRecoveryRequest(
            &recovery,
            sizeof(recovery),
            12u,
            NldDirectPdoFailureMediaTimeout) !=
        NldDirectPdoRecoveryValidationReserved) {
        return fail("nonzero recovery reserved field was accepted");
    }
    return 0;
}
