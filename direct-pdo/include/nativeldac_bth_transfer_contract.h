// SPDX-License-Identifier: Apache-2.0
#ifndef NATIVE_LDAC_BTH_TRANSFER_CONTRACT_H
#define NATIVE_LDAC_BTH_TRANSFER_CONTRACT_H

#ifdef __cplusplus
extern "C" {
#endif

#define NLD_BTH_TRANSFER_CANCEL_NONE 0u
#define NLD_BTH_TRANSFER_CANCEL_READ 0x01u
#define NLD_BTH_TRANSFER_CANCEL_WRITE 0x02u

typedef enum NLD_BTH_TRANSFER_DIRECTION {
    NldBthTransferRead = 1,
    NldBthTransferWrite = 2
} NLD_BTH_TRANSFER_DIRECTION;

typedef enum NLD_BTH_TRANSFER_RESULT {
    NldBthTransferOk = 0,
    NldBthTransferInvalidArgument = -1,
    NldBthTransferDisconnected = -2,
    NldBthTransferBusy = -3,
    NldBthTransferStale = -4,
    NldBthTransferDrained = 1
} NLD_BTH_TRANSFER_RESULT;

typedef struct NLD_BTH_TRANSFER_OWNER {
    unsigned long ChannelGeneration;
    unsigned long NextRequestGeneration;
    unsigned long ReadGeneration;
    unsigned long WriteGeneration;
    int ChannelOpen;
    int Closing;
    int ReadPending;
    int WritePending;
    int RemoteDisconnected;
} NLD_BTH_TRANSFER_OWNER;

void NldBthTransferInitialize(
    NLD_BTH_TRANSFER_OWNER* owner);

unsigned long NldBthTransferOpenChannel(
    NLD_BTH_TRANSFER_OWNER* owner);

NLD_BTH_TRANSFER_RESULT NldBthTransferBegin(
    NLD_BTH_TRANSFER_OWNER* owner,
    NLD_BTH_TRANSFER_DIRECTION direction,
    unsigned long* channel_generation,
    unsigned long* request_generation);

unsigned NldBthTransferRequestClose(
    NLD_BTH_TRANSFER_OWNER* owner,
    int remote_disconnect);

NLD_BTH_TRANSFER_RESULT NldBthTransferComplete(
    NLD_BTH_TRANSFER_OWNER* owner,
    NLD_BTH_TRANSFER_DIRECTION direction,
    unsigned long channel_generation,
    unsigned long request_generation);

NLD_BTH_TRANSFER_RESULT NldBthTransferFinishClose(
    NLD_BTH_TRANSFER_OWNER* owner);

int NldBthTransferIsConsistent(
    const NLD_BTH_TRANSFER_OWNER* owner);

#ifdef __cplusplus
}
#endif

#endif
