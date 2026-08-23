// SPDX-License-Identifier: Apache-2.0
#ifndef NATIVE_LDAC_DIRECT_PDO_PUBLIC_H
#define NATIVE_LDAC_DIRECT_PDO_PUBLIC_H

#include "nativeldac_direct_pdo_control_abi.h"
#include "nativeldac_direct_pdo_media_abi.h"

// {969D6310-302C-4E32-BAAC-3FB5D807F5DC}
#define STATIC_KSPROPSETID_NativeLdacDirectPdo                         \
    0x969d6310, 0x302c, 0x4e32, 0xba, 0xac, 0x3f, 0xb5, 0xd8, 0x07, 0xf5, 0xdc
DEFINE_GUIDSTRUCT(
    "969D6310-302C-4E32-BAAC-3FB5D807F5DC",
    KSPROPSETID_NativeLdacDirectPdo);
#define KSPROPSETID_NativeLdacDirectPdo \
    DEFINE_GUIDNAMED(KSPROPSETID_NativeLdacDirectPdo)

typedef enum NLD_DIRECT_PDO_PROPERTY {
    // Read-only diagnostic data. This property cannot initiate traffic.
    NldDirectPdoPropertySnapshot = 0,
    // Read-only render/media generation and MTU snapshot.
    NldDirectPdoPropertyMediaStatus = 1,
    // Write-only, generation-bound RTP/LDAC packet submission.
    NldDirectPdoPropertyMediaPacket = 2,
    // Write-only, fault-generation-bound idle recovery request.
    NldDirectPdoPropertyRecovery = 3
} NLD_DIRECT_PDO_PROPERTY;

#endif
