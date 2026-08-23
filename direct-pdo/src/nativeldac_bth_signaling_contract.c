// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_bth_signaling_contract.h"

static void NldBthSignalingOwnerResetRequestFlags(
    NLD_BTH_SIGNALING_OWNER* owner) {
    owner->OpenPending = 0;
    owner->ChannelHeld = 0;
    owner->CloseRequested = 0;
    owner->TargetOffline = 0;
}

void NldBthSignalingOwnerInitialize(NLD_BTH_SIGNALING_OWNER* owner) {
    if (owner == 0) return;
    owner->State = NldBthSignalingOffline;
    owner->Generation = 0ul;
    owner->PnpStarted = 0;
    NldBthSignalingOwnerResetRequestFlags(owner);
}

NLD_BTH_SIGNALING_ACTION NldBthSignalingOwnerOnPnpStart(
    NLD_BTH_SIGNALING_OWNER* owner) {
    if (owner == 0 || owner->PnpStarted ||
        owner->State != NldBthSignalingOffline) {
        return NldBthSignalingActionNone;
    }
    owner->PnpStarted = 1;
    owner->State = NldBthSignalingClosed;
    NldBthSignalingOwnerResetRequestFlags(owner);
    ++owner->Generation;
    return NldBthSignalingActionNone;
}

NLD_BTH_SIGNALING_ACTION NldBthSignalingOwnerRequestOpen(
    NLD_BTH_SIGNALING_OWNER* owner) {
    if (owner == 0 || !owner->PnpStarted ||
        owner->State != NldBthSignalingClosed || owner->OpenPending ||
        owner->ChannelHeld) {
        return NldBthSignalingActionNone;
    }
    owner->State = NldBthSignalingOpening;
    owner->OpenPending = 1;
    owner->CloseRequested = 0;
    owner->TargetOffline = 0;
    return NldBthSignalingActionSubmitOpen;
}

NLD_BTH_SIGNALING_ACTION NldBthSignalingOwnerRequestClose(
    NLD_BTH_SIGNALING_OWNER* owner,
    int target_offline) {
    if (owner == 0) return NldBthSignalingActionNone;
    owner->CloseRequested = 1;
    if (target_offline) {
        owner->TargetOffline = 1;
        owner->PnpStarted = 0;
    }

    if (owner->State == NldBthSignalingOpening && owner->OpenPending) {
        return NldBthSignalingActionCancelOpen;
    }
    if (owner->State == NldBthSignalingChannelOpen && owner->ChannelHeld) {
        owner->State = NldBthSignalingClosing;
        return NldBthSignalingActionSubmitClose;
    }
    if (owner->State == NldBthSignalingClosed ||
        owner->State == NldBthSignalingFaulted ||
        owner->State == NldBthSignalingOffline) {
        owner->State = owner->TargetOffline
            ? NldBthSignalingOffline
            : NldBthSignalingClosed;
        owner->OpenPending = 0;
        owner->ChannelHeld = 0;
        owner->CloseRequested = 0;
        owner->TargetOffline = 0;
    }
    return NldBthSignalingActionNone;
}

int NldBthSignalingOwnerOnRemoteDisconnect(
    NLD_BTH_SIGNALING_OWNER* owner,
    unsigned long generation) {
    if (owner == 0 || generation != owner->Generation ||
        !owner->PnpStarted ||
        owner->State != NldBthSignalingChannelOpen ||
        !owner->ChannelHeld) {
        return 0;
    }

    owner->State = NldBthSignalingFaulted;
    owner->OpenPending = 0;
    owner->ChannelHeld = 0;
    owner->CloseRequested = 0;
    owner->TargetOffline = 0;
    ++owner->Generation;
    if (owner->Generation == 0ul) ++owner->Generation;
    return 1;
}

NLD_BTH_SIGNALING_ACTION NldBthSignalingOwnerOnOpenComplete(
    NLD_BTH_SIGNALING_OWNER* owner,
    unsigned long generation,
    int succeeded,
    int channel_acquired) {
    if (owner == 0) {
        return channel_acquired
            ? NldBthSignalingActionSubmitClose
            : NldBthSignalingActionNone;
    }
    if (generation != owner->Generation || !owner->OpenPending ||
        owner->State != NldBthSignalingOpening) {
        return channel_acquired
            ? NldBthSignalingActionSubmitClose
            : NldBthSignalingActionNone;
    }

    owner->OpenPending = 0;
    if (succeeded && channel_acquired) {
        owner->ChannelHeld = 1;
        if (owner->CloseRequested || owner->TargetOffline ||
            !owner->PnpStarted) {
            owner->State = NldBthSignalingClosing;
            return NldBthSignalingActionSubmitClose;
        }
        owner->State = NldBthSignalingChannelOpen;
        return NldBthSignalingActionNone;
    }

    owner->ChannelHeld = 0;
    if (owner->TargetOffline || !owner->PnpStarted) {
        owner->State = NldBthSignalingOffline;
    } else if (owner->CloseRequested) {
        owner->State = NldBthSignalingClosed;
    } else {
        owner->State = NldBthSignalingFaulted;
    }
    owner->CloseRequested = 0;
    owner->TargetOffline = 0;
    return NldBthSignalingActionNone;
}

void NldBthSignalingOwnerOnCloseComplete(
    NLD_BTH_SIGNALING_OWNER* owner,
    int succeeded) {
    int target_offline;

    if (owner == 0 || owner->State != NldBthSignalingClosing) return;
    target_offline = owner->TargetOffline || !owner->PnpStarted;
    owner->OpenPending = 0;
    owner->ChannelHeld = 0;
    owner->CloseRequested = 0;
    owner->TargetOffline = 0;
    if (target_offline) {
        owner->State = NldBthSignalingOffline;
    } else {
        owner->State = succeeded
            ? NldBthSignalingClosed
            : NldBthSignalingFaulted;
    }
}

int NldBthSignalingOwnerIsConsistent(
    const NLD_BTH_SIGNALING_OWNER* owner) {
    if (owner == 0) return 0;
    if (owner->State < NldBthSignalingOffline ||
        owner->State > NldBthSignalingFaulted) {
        return 0;
    }
    if (owner->OpenPending !=
        (owner->State == NldBthSignalingOpening)) {
        return 0;
    }
    if (owner->ChannelHeld !=
        (owner->State == NldBthSignalingChannelOpen ||
         owner->State == NldBthSignalingClosing)) {
        return 0;
    }
    if (!owner->PnpStarted &&
        owner->State != NldBthSignalingOffline &&
        !owner->TargetOffline) {
        return 0;
    }
    if (owner->State == NldBthSignalingOffline && owner->PnpStarted) {
        return 0;
    }
    return 1;
}
