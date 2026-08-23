// SPDX-License-Identifier: Apache-2.0
#include "nativeldac_portcls_contract.h"

extern "C" NLD_DIRECT_PDO_ACTION NldDirectPdoApplyKsState(
    NLD_DIRECT_PDO_SESSION* session,
    KSSTATE state) {
    switch (state) {
        case KSSTATE_STOP:
            return NldDirectPdoSetKsIntent(session,
                                           NldDirectPdoKsStopped);
        case KSSTATE_ACQUIRE:
        case KSSTATE_PAUSE:
            return NldDirectPdoSetKsIntent(session,
                                           NldDirectPdoKsAcquired);
        case KSSTATE_RUN:
            return NldDirectPdoSetKsIntent(session,
                                           NldDirectPdoKsRunning);
        default:
            return NldDirectPdoActionNone;
    }
}
