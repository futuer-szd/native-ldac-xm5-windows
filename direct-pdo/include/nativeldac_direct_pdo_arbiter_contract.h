// SPDX-License-Identifier: Apache-2.0
#ifndef NATIVE_LDAC_DIRECT_PDO_ARBITER_CONTRACT_H
#define NATIVE_LDAC_DIRECT_PDO_ARBITER_CONTRACT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum NLD_DIRECT_PDO_ARBITER_STATE {
    NldDirectPdoArbiterOffline = 0,
    NldDirectPdoArbiterIdle = 1,
    NldDirectPdoArbiterRenderOwned = 2,
    NldDirectPdoArbiterDiagnosticOwned = 3,
    NldDirectPdoArbiterStopping = 4
} NLD_DIRECT_PDO_ARBITER_STATE;

typedef enum NLD_DIRECT_PDO_ARBITER_CLIENT {
    NldDirectPdoArbiterClientNone = 0,
    NldDirectPdoArbiterClientRender = 1,
    NldDirectPdoArbiterClientDiagnostic = 2
} NLD_DIRECT_PDO_ARBITER_CLIENT;

typedef enum NLD_DIRECT_PDO_ARBITER_ACQUIRE_RESULT {
    NldDirectPdoArbiterAcquireRejected = 0,
    NldDirectPdoArbiterAcquireGranted = 1,
    NldDirectPdoArbiterAcquireAlreadyOwned = 2
} NLD_DIRECT_PDO_ARBITER_ACQUIRE_RESULT;

typedef enum NLD_DIRECT_PDO_ARBITER_DEMAND_RESULT {
    NldDirectPdoArbiterDemandRejected = 0,
    NldDirectPdoArbiterDemandAccepted = 1,
    NldDirectPdoArbiterDemandPreemptDiagnostic = 2
} NLD_DIRECT_PDO_ARBITER_DEMAND_RESULT;

typedef struct NLD_DIRECT_PDO_ARBITER_OWNER {
    NLD_DIRECT_PDO_ARBITER_STATE State;
    NLD_DIRECT_PDO_ARBITER_CLIENT Client;
    unsigned long Generation;
    unsigned long ActiveGeneration;
    int PnpStarted;
    int StopRequested;
    int RenderDemand;
} NLD_DIRECT_PDO_ARBITER_OWNER;

void NldDirectPdoArbiterInitialize(
    NLD_DIRECT_PDO_ARBITER_OWNER* owner);

int NldDirectPdoArbiterOnPnpStart(
    NLD_DIRECT_PDO_ARBITER_OWNER* owner);

int NldDirectPdoArbiterOnPnpStop(
    NLD_DIRECT_PDO_ARBITER_OWNER* owner);

NLD_DIRECT_PDO_ARBITER_DEMAND_RESULT
NldDirectPdoArbiterSetRenderDemand(
    NLD_DIRECT_PDO_ARBITER_OWNER* owner,
    int active);

NLD_DIRECT_PDO_ARBITER_ACQUIRE_RESULT NldDirectPdoArbiterTryAcquire(
    NLD_DIRECT_PDO_ARBITER_OWNER* owner,
    NLD_DIRECT_PDO_ARBITER_CLIENT client,
    unsigned long* generation);

int NldDirectPdoArbiterRelease(
    NLD_DIRECT_PDO_ARBITER_OWNER* owner,
    NLD_DIRECT_PDO_ARBITER_CLIENT client,
    unsigned long generation);

void NldDirectPdoArbiterForceOffline(
    NLD_DIRECT_PDO_ARBITER_OWNER* owner);

int NldDirectPdoArbiterIsConsistent(
    const NLD_DIRECT_PDO_ARBITER_OWNER* owner);

#ifdef __cplusplus
}
#endif

#endif
