// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_avdtp_transaction.h"

void NldAvdtpTransactionInitialize(
    NLD_AVDTP_TRANSACTION* transaction) {
    if (transaction == 0) return;
    transaction->Generation = 0ul;
    transaction->NextLabel = 0u;
    transaction->PendingLabel = 0u;
    transaction->PendingSignalId = 0u;
    transaction->Pending = 0;
    transaction->Faulted = 0;
}

void NldAvdtpTransactionReset(
    NLD_AVDTP_TRANSACTION* transaction) {
    if (transaction == 0) return;
    ++transaction->Generation;
    transaction->PendingLabel = 0u;
    transaction->PendingSignalId = 0u;
    transaction->Pending = 0;
    transaction->Faulted = 0;
}

NLD_AVDTP_TRANSACTION_STATUS NldAvdtpTransactionBegin(
    NLD_AVDTP_TRANSACTION* transaction,
    unsigned char signal_id,
    unsigned char* packet,
    size_t packet_capacity,
    size_t* packet_size,
    unsigned long* generation) {
    unsigned char label;

    if (packet_size != 0) *packet_size = 0u;
    if (generation != 0) *generation = 0ul;
    if (transaction == 0 || packet == 0 || packet_size == 0 ||
        generation == 0 || signal_id == 0u || signal_id > 0x3fu ||
        packet_capacity < 2u) {
        return NldAvdtpTransactionInvalidArgument;
    }
    if (transaction->Faulted) return NldAvdtpTransactionFaulted;
    if (transaction->Pending) return NldAvdtpTransactionBusy;

    label = (unsigned char)(transaction->NextLabel & 0x0fu);
    transaction->NextLabel = (unsigned char)((label + 1u) & 0x0fu);
    transaction->PendingLabel = label;
    transaction->PendingSignalId = signal_id;
    transaction->Pending = 1;
    packet[0] = (unsigned char)(label << 4u);
    packet[1] = signal_id;
    *packet_size = 2u;
    *generation = transaction->Generation;
    return NldAvdtpTransactionOk;
}

NLD_AVDTP_TRANSACTION_STATUS NldAvdtpTransactionAcceptResponse(
    NLD_AVDTP_TRANSACTION* transaction,
    unsigned long generation,
    const unsigned char* packet,
    size_t packet_size,
    size_t* payload_offset) {
    unsigned char label;
    unsigned char packet_type;
    unsigned char message_type;
    unsigned char signal_id;

    if (payload_offset != 0) *payload_offset = 0u;
    if (transaction == 0 || packet == 0 || payload_offset == 0) {
        return NldAvdtpTransactionInvalidArgument;
    }
    if (generation != transaction->Generation) {
        return NldAvdtpTransactionStale;
    }
    if (transaction->Faulted) return NldAvdtpTransactionFaulted;
    if (!transaction->Pending) {
        return NldAvdtpTransactionUnexpectedResponse;
    }
    if (packet_size < 2u) {
        transaction->Pending = 0;
        transaction->Faulted = 1;
        return NldAvdtpTransactionTruncated;
    }

    label = (unsigned char)(packet[0] >> 4u);
    packet_type = (unsigned char)((packet[0] >> 2u) & 0x03u);
    message_type = (unsigned char)(packet[0] & 0x03u);
    signal_id = (unsigned char)(packet[1] & 0x3fu);
    if (label != transaction->PendingLabel ||
        signal_id != transaction->PendingSignalId) {
        return NldAvdtpTransactionUnexpectedResponse;
    }
    transaction->Pending = 0;
    if (packet_type != NLD_AVDTP_PACKET_SINGLE) {
        transaction->Faulted = 1;
        return NldAvdtpTransactionUnsupportedFragment;
    }
    if (message_type == NLD_AVDTP_MESSAGE_REJECT ||
        message_type == NLD_AVDTP_MESSAGE_GENERAL_REJECT) {
        return NldAvdtpTransactionRejected;
    }
    if (message_type != NLD_AVDTP_MESSAGE_ACCEPT) {
        transaction->Faulted = 1;
        return NldAvdtpTransactionUnexpectedResponse;
    }
    *payload_offset = 2u;
    return NldAvdtpTransactionOk;
}

int NldAvdtpTransactionIsConsistent(
    const NLD_AVDTP_TRANSACTION* transaction) {
    if (transaction == 0 || transaction->NextLabel > 0x0fu ||
        transaction->PendingLabel > 0x0fu ||
        transaction->PendingSignalId > 0x3fu) {
        return 0;
    }
    if (transaction->Pending &&
        transaction->PendingSignalId == 0u) {
        return 0;
    }
    if (transaction->Pending && transaction->Faulted) return 0;
    return 1;
}
