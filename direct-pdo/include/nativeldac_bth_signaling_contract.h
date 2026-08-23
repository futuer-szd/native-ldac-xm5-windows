// SPDX-License-Identifier: Apache-2.0
#ifndef NATIVE_LDAC_BTH_SIGNALING_CONTRACT_H
#define NATIVE_LDAC_BTH_SIGNALING_CONTRACT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum NLD_BTH_SIGNALING_STATE {
    NldBthSignalingOffline = 0,
    NldBthSignalingClosed = 1,
    NldBthSignalingOpening = 2,
    NldBthSignalingChannelOpen = 3,
    NldBthSignalingClosing = 4,
    NldBthSignalingFaulted = 5
} NLD_BTH_SIGNALING_STATE;

typedef enum NLD_BTH_SIGNALING_ACTION {
    NldBthSignalingActionNone = 0,
    NldBthSignalingActionSubmitOpen = 1,
    NldBthSignalingActionCancelOpen = 2,
    NldBthSignalingActionSubmitClose = 3
} NLD_BTH_SIGNALING_ACTION;

typedef struct NLD_BTH_SIGNALING_OWNER {
    NLD_BTH_SIGNALING_STATE State;
    unsigned long Generation;
    int PnpStarted;
    int OpenPending;
    int ChannelHeld;
    int CloseRequested;
    int TargetOffline;
} NLD_BTH_SIGNALING_OWNER;

void NldBthSignalingOwnerInitialize(NLD_BTH_SIGNALING_OWNER* owner);

NLD_BTH_SIGNALING_ACTION NldBthSignalingOwnerOnPnpStart(
    NLD_BTH_SIGNALING_OWNER* owner);

NLD_BTH_SIGNALING_ACTION NldBthSignalingOwnerRequestOpen(
    NLD_BTH_SIGNALING_OWNER* owner);

NLD_BTH_SIGNALING_ACTION NldBthSignalingOwnerRequestClose(
    NLD_BTH_SIGNALING_OWNER* owner,
    int target_offline);

int NldBthSignalingOwnerOnRemoteDisconnect(
    NLD_BTH_SIGNALING_OWNER* owner,
    unsigned long generation);

NLD_BTH_SIGNALING_ACTION NldBthSignalingOwnerOnOpenComplete(
    NLD_BTH_SIGNALING_OWNER* owner,
    unsigned long generation,
    int succeeded,
    int channel_acquired);

void NldBthSignalingOwnerOnCloseComplete(
    NLD_BTH_SIGNALING_OWNER* owner,
    int succeeded);

int NldBthSignalingOwnerIsConsistent(
    const NLD_BTH_SIGNALING_OWNER* owner);

#ifdef __cplusplus
}
#endif

#endif
