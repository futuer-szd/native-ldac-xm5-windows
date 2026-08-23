// SPDX-License-Identifier: Apache-2.0
#ifndef NATIVE_LDAC_AVDTP_TRANSACTION_H
#define NATIVE_LDAC_AVDTP_TRANSACTION_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NLD_AVDTP_SIGNAL_DISCOVER 0x01u
#define NLD_AVDTP_MESSAGE_COMMAND 0u
#define NLD_AVDTP_MESSAGE_GENERAL_REJECT 1u
#define NLD_AVDTP_MESSAGE_ACCEPT 2u
#define NLD_AVDTP_MESSAGE_REJECT 3u
#define NLD_AVDTP_PACKET_SINGLE 0u

typedef enum NLD_AVDTP_TRANSACTION_STATUS {
    NldAvdtpTransactionOk = 0,
    NldAvdtpTransactionInvalidArgument = -1,
    NldAvdtpTransactionBusy = -2,
    NldAvdtpTransactionTruncated = -3,
    NldAvdtpTransactionUnsupportedFragment = -4,
    NldAvdtpTransactionUnexpectedResponse = -5,
    NldAvdtpTransactionRejected = -6,
    NldAvdtpTransactionStale = -7,
    NldAvdtpTransactionFaulted = -8
} NLD_AVDTP_TRANSACTION_STATUS;

typedef struct NLD_AVDTP_TRANSACTION {
    unsigned long Generation;
    unsigned char NextLabel;
    unsigned char PendingLabel;
    unsigned char PendingSignalId;
    int Pending;
    int Faulted;
} NLD_AVDTP_TRANSACTION;

void NldAvdtpTransactionInitialize(
    NLD_AVDTP_TRANSACTION* transaction);

void NldAvdtpTransactionReset(
    NLD_AVDTP_TRANSACTION* transaction);

NLD_AVDTP_TRANSACTION_STATUS NldAvdtpTransactionBegin(
    NLD_AVDTP_TRANSACTION* transaction,
    unsigned char signal_id,
    unsigned char* packet,
    size_t packet_capacity,
    size_t* packet_size,
    unsigned long* generation);

NLD_AVDTP_TRANSACTION_STATUS NldAvdtpTransactionAcceptResponse(
    NLD_AVDTP_TRANSACTION* transaction,
    unsigned long generation,
    const unsigned char* packet,
    size_t packet_size,
    size_t* payload_offset);

int NldAvdtpTransactionIsConsistent(
    const NLD_AVDTP_TRANSACTION* transaction);

#ifdef __cplusplus
}
#endif

#endif
