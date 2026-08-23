// SPDX-License-Identifier: Apache-2.0
#ifndef NATIVE_LDAC_PORTCLS_CONTRACT_H
#define NATIVE_LDAC_PORTCLS_CONTRACT_H

#include <portcls.h>

#include "nativeldac_direct_pdo_contract.h"

#ifdef __cplusplus
extern "C" {
#endif

NLD_DIRECT_PDO_ACTION NldDirectPdoApplyKsState(
    NLD_DIRECT_PDO_SESSION* session,
    KSSTATE state);

#ifdef __cplusplus
}
#endif

#endif
