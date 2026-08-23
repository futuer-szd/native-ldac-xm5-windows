// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_bth_transfer_contract.h"

static unsigned long NldBthTransferNextNonzero(
    unsigned long value) {
    ++value;
    return value == 0ul ? 1ul : value;
}

void NldBthTransferInitialize(
    NLD_BTH_TRANSFER_OWNER* owner) {
    if (owner == 0) return;
    owner->ChannelGeneration = 0ul;
    owner->NextRequestGeneration = 0ul;
    owner->ReadGeneration = 0ul;
    owner->WriteGeneration = 0ul;
    owner->ChannelOpen = 0;
    owner->Closing = 0;
    owner->ReadPending = 0;
    owner->WritePending = 0;
    owner->RemoteDisconnected = 0;
}

unsigned long NldBthTransferOpenChannel(
    NLD_BTH_TRANSFER_OWNER* owner) {
    if (owner == 0 || owner->ChannelOpen || owner->Closing ||
        owner->ReadPending || owner->WritePending) {
        return 0ul;
    }
    owner->ChannelGeneration = NldBthTransferNextNonzero(
        owner->ChannelGeneration);
    owner->ChannelOpen = 1;
    owner->RemoteDisconnected = 0;
    return owner->ChannelGeneration;
}

NLD_BTH_TRANSFER_RESULT NldBthTransferBegin(
    NLD_BTH_TRANSFER_OWNER* owner,
    NLD_BTH_TRANSFER_DIRECTION direction,
    unsigned long* channel_generation,
    unsigned long* request_generation) {
    unsigned long generation;

    if (channel_generation != 0) *channel_generation = 0ul;
    if (request_generation != 0) *request_generation = 0ul;
    if (owner == 0 || channel_generation == 0 ||
        request_generation == 0 ||
        (direction != NldBthTransferRead &&
         direction != NldBthTransferWrite)) {
        return NldBthTransferInvalidArgument;
    }
    if (!owner->ChannelOpen || owner->Closing) {
        return NldBthTransferDisconnected;
    }
    if ((direction == NldBthTransferRead && owner->ReadPending) ||
        (direction == NldBthTransferWrite && owner->WritePending)) {
        return NldBthTransferBusy;
    }

    generation = NldBthTransferNextNonzero(
        owner->NextRequestGeneration);
    owner->NextRequestGeneration = generation;
    if (direction == NldBthTransferRead) {
        owner->ReadPending = 1;
        owner->ReadGeneration = generation;
    } else {
        owner->WritePending = 1;
        owner->WriteGeneration = generation;
    }
    *channel_generation = owner->ChannelGeneration;
    *request_generation = generation;
    return NldBthTransferOk;
}

unsigned NldBthTransferRequestClose(
    NLD_BTH_TRANSFER_OWNER* owner,
    int remote_disconnect) {
    unsigned cancel_mask = NLD_BTH_TRANSFER_CANCEL_NONE;

    if (owner == 0) return cancel_mask;
    owner->ChannelOpen = 0;
    owner->Closing = 1;
    if (remote_disconnect) owner->RemoteDisconnected = 1;
    if (owner->ReadPending) cancel_mask |= NLD_BTH_TRANSFER_CANCEL_READ;
    if (owner->WritePending) cancel_mask |= NLD_BTH_TRANSFER_CANCEL_WRITE;
    return cancel_mask;
}

NLD_BTH_TRANSFER_RESULT NldBthTransferComplete(
    NLD_BTH_TRANSFER_OWNER* owner,
    NLD_BTH_TRANSFER_DIRECTION direction,
    unsigned long channel_generation,
    unsigned long request_generation) {
    int* pending;
    unsigned long* generation;

    if (owner == 0 ||
        (direction != NldBthTransferRead &&
         direction != NldBthTransferWrite)) {
        return NldBthTransferInvalidArgument;
    }
    if (channel_generation != owner->ChannelGeneration) {
        return NldBthTransferStale;
    }
    if (direction == NldBthTransferRead) {
        pending = &owner->ReadPending;
        generation = &owner->ReadGeneration;
    } else {
        pending = &owner->WritePending;
        generation = &owner->WriteGeneration;
    }
    if (!*pending || request_generation != *generation) {
        return NldBthTransferStale;
    }
    *pending = 0;
    *generation = 0ul;
    if (owner->Closing && !owner->ReadPending && !owner->WritePending) {
        return NldBthTransferDrained;
    }
    return NldBthTransferOk;
}

NLD_BTH_TRANSFER_RESULT NldBthTransferFinishClose(
    NLD_BTH_TRANSFER_OWNER* owner) {
    if (owner == 0) return NldBthTransferInvalidArgument;
    if (!owner->Closing || owner->ReadPending || owner->WritePending) {
        return NldBthTransferBusy;
    }
    owner->Closing = 0;
    return NldBthTransferOk;
}

int NldBthTransferIsConsistent(
    const NLD_BTH_TRANSFER_OWNER* owner) {
    if (owner == 0) return 0;
    if (owner->ChannelOpen && owner->Closing) return 0;
    if ((owner->ReadPending == 0) != (owner->ReadGeneration == 0ul)) {
        return 0;
    }
    if ((owner->WritePending == 0) != (owner->WriteGeneration == 0ul)) {
        return 0;
    }
    if ((owner->ReadPending || owner->WritePending) &&
        owner->ChannelGeneration == 0ul) {
        return 0;
    }
    if (owner->RemoteDisconnected && owner->ChannelOpen) return 0;
    return 1;
}
