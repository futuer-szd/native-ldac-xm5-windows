// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_direct_pdo_media_abi.h"

NLD_DIRECT_PDO_MEDIA_VALIDATION
NldDirectPdoMediaValidatePacket(
    const NLD_DIRECT_PDO_MEDIA_PACKET_V1* packet,
    NLD_DIRECT_PDO_MEDIA_U32 buffer_size,
    const NLD_DIRECT_PDO_MEDIA_STATUS_V1* status) {
    NLD_DIRECT_PDO_MEDIA_U32 expected_size;
    unsigned int index;

    if (packet == 0 || status == 0) {
        return NldDirectPdoMediaValidationNull;
    }
    if (buffer_size < NLD_DIRECT_PDO_MEDIA_PACKET_HEADER_SIZE) {
        return NldDirectPdoMediaValidationBufferTooSmall;
    }
    if (packet->Version != NLD_DIRECT_PDO_MEDIA_ABI_VERSION ||
        status->Version != NLD_DIRECT_PDO_MEDIA_ABI_VERSION) {
        return NldDirectPdoMediaValidationVersion;
    }
    if (packet->Flags != 0u) {
        return NldDirectPdoMediaValidationFlags;
    }
    for (index = 0u; index < 3u; ++index) {
        if (packet->Reserved[index] != 0u) {
            return NldDirectPdoMediaValidationReserved;
        }
    }
    if (packet->PayloadLength == 0u ||
        packet->PayloadLength > NLD_DIRECT_PDO_MEDIA_MAX_PACKET_SIZE) {
        return NldDirectPdoMediaValidationPayload;
    }
    expected_size = NLD_DIRECT_PDO_MEDIA_PACKET_HEADER_SIZE +
                    packet->PayloadLength;
    if (packet->Size != expected_size || buffer_size != expected_size) {
        return NldDirectPdoMediaValidationSize;
    }
    if (status->Size != sizeof(*status) ||
        status->State != NldDirectPdoMediaStreaming ||
        (status->Flags & NLD_DIRECT_PDO_MEDIA_STATUS_STREAMING) == 0u) {
        return NldDirectPdoMediaValidationOffline;
    }
    if (packet->MediaGeneration == 0u ||
        packet->MediaGeneration != status->MediaGeneration) {
        return NldDirectPdoMediaValidationGeneration;
    }
    if (status->OutgoingMtu == 0u ||
        packet->PayloadLength > status->OutgoingMtu) {
        return NldDirectPdoMediaValidationMtu;
    }
    return NldDirectPdoMediaValidationOk;
}

NLD_DIRECT_PDO_RECOVERY_VALIDATION
NldDirectPdoValidateRecoveryRequest(
    const NLD_DIRECT_PDO_RECOVERY_REQUEST_V1* request,
    NLD_DIRECT_PDO_MEDIA_U32 buffer_size,
    NLD_DIRECT_PDO_MEDIA_U32 observed_session_generation,
    NLD_DIRECT_PDO_MEDIA_U32 observed_failure_reason) {
    unsigned int index;

    if (request == 0) {
        return NldDirectPdoRecoveryValidationNull;
    }
    if (buffer_size < NLD_DIRECT_PDO_RECOVERY_REQUEST_SIZE) {
        return NldDirectPdoRecoveryValidationBufferTooSmall;
    }
    if (request->Size != NLD_DIRECT_PDO_RECOVERY_REQUEST_SIZE ||
        buffer_size != NLD_DIRECT_PDO_RECOVERY_REQUEST_SIZE) {
        return NldDirectPdoRecoveryValidationSize;
    }
    if (request->Version != NLD_DIRECT_PDO_MEDIA_ABI_VERSION) {
        return NldDirectPdoRecoveryValidationVersion;
    }
    if (request->Flags != 0u) {
        return NldDirectPdoRecoveryValidationFlags;
    }
    for (index = 0u; index < 3u; ++index) {
        if (request->Reserved[index] != 0u) {
            return NldDirectPdoRecoveryValidationReserved;
        }
    }
    if (request->ExpectedSessionGeneration == 0u ||
        request->ExpectedSessionGeneration != observed_session_generation) {
        return NldDirectPdoRecoveryValidationGeneration;
    }
    if (request->ExpectedFailureReason == NldDirectPdoFailureNone ||
        request->ExpectedFailureReason > NldDirectPdoFailureBackend ||
        request->ExpectedFailureReason != observed_failure_reason) {
        return NldDirectPdoRecoveryValidationReason;
    }
    return NldDirectPdoRecoveryValidationOk;
}
