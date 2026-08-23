// SPDX-License-Identifier: Apache-2.0
#include <assert.h>
#include <stddef.h>

#include "ldac_native_ioctl.h"

int main(void) {
    assert(LDAC_NATIVE_ABI_MAJOR == 0u);
    assert(LDAC_NATIVE_ABI_MINOR == 5u);
    assert(LDAC_NATIVE_DEVICE_INFO_INBOUND_SIGNALING_READY == 0x00000008u);
    assert(LDAC_NATIVE_OPEN_DIAGNOSTIC_INBOUND_CHANNEL == 0x00000010u);
    assert(sizeof(LDAC_NATIVE_OPEN_SIGNALING_REQUEST) == 12u);
    assert(sizeof(LDAC_NATIVE_CHANNEL_INFO) == 16u);
    assert(sizeof(LDAC_NATIVE_SIGNALING_TRANSFER_REQUEST) == 12u);
    assert(sizeof(LDAC_NATIVE_TRANSFER_RESULT) == 36u);
    assert(sizeof(LDAC_NATIVE_TRANSFER_DIAGNOSTICS) == 112u);
    assert(sizeof(LDAC_NATIVE_OPEN_DIAGNOSTICS) == 48u);
    assert(offsetof(LDAC_NATIVE_CHANNEL_INFO, IncomingMtu) == 10u);
    assert((IOCTL_LDAC_NATIVE_OPEN_SIGNALING & 3u) == METHOD_BUFFERED);
    assert((IOCTL_LDAC_NATIVE_WRITE_SIGNALING & 3u) == METHOD_IN_DIRECT);
    assert((IOCTL_LDAC_NATIVE_READ_SIGNALING & 3u) == METHOD_OUT_DIRECT);
    assert((IOCTL_LDAC_NATIVE_OPEN_MEDIA & 3u) == METHOD_BUFFERED);
    assert((IOCTL_LDAC_NATIVE_WRITE_MEDIA & 3u) == METHOD_IN_DIRECT);
    assert((IOCTL_LDAC_NATIVE_GET_TRANSFER_DIAGNOSTICS & 3u) ==
           METHOD_BUFFERED);
    assert((IOCTL_LDAC_NATIVE_GET_OPEN_DIAGNOSTICS & 3u) ==
           METHOD_BUFFERED);
    assert(LDAC_NATIVE_MAX_SIGNALING_TRANSFER >= 1000u);
    assert(LDAC_NATIVE_MAX_MEDIA_TRANSFER >= 1000u);
    return 0;
}
