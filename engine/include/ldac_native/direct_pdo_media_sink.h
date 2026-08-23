// SPDX-License-Identifier: Apache-2.0
#ifndef LDAC_NATIVE_DIRECT_PDO_MEDIA_SINK_H
#define LDAC_NATIVE_DIRECT_PDO_MEDIA_SINK_H

#include <stddef.h>
#include <wchar.h>

#include "nativeldac_direct_pdo_media_abi.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum direct_pdo_media_sink_status {
    DIRECT_PDO_MEDIA_SINK_OK = 0,
    DIRECT_PDO_MEDIA_SINK_INVALID_ARGUMENT = -1,
    DIRECT_PDO_MEDIA_SINK_NO_MEMORY = -2,
    DIRECT_PDO_MEDIA_SINK_OPEN_FAILED = -3,
    DIRECT_PDO_MEDIA_SINK_IO_ERROR = -4,
    DIRECT_PDO_MEDIA_SINK_UNSUPPORTED = -5,
    DIRECT_PDO_MEDIA_SINK_NOT_READY = -6,
    DIRECT_PDO_MEDIA_SINK_STALE_SESSION = -7
} direct_pdo_media_sink_status;

typedef struct direct_pdo_media_sink direct_pdo_media_sink;

direct_pdo_media_sink_status direct_pdo_media_sink_create(
    const wchar_t* interface_path,
    direct_pdo_media_sink** out);
direct_pdo_media_sink_status direct_pdo_media_sink_create_first(
    direct_pdo_media_sink** out,
    wchar_t* interface_path,
    size_t interface_path_capacity);
void direct_pdo_media_sink_destroy(direct_pdo_media_sink* sink);

direct_pdo_media_sink_status direct_pdo_media_sink_get_status(
    direct_pdo_media_sink* sink,
    NLD_DIRECT_PDO_MEDIA_STATUS_V1* status);

direct_pdo_media_sink_status direct_pdo_media_sink_write(
    direct_pdo_media_sink* sink,
    unsigned long media_generation,
    const void* packet,
    size_t packet_size);

unsigned long direct_pdo_media_sink_last_error(
    const direct_pdo_media_sink* sink);

#ifdef __cplusplus
}
#endif

#endif
