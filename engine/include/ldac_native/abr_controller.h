// SPDX-License-Identifier: Apache-2.0
#ifndef LDAC_NATIVE_ABR_CONTROLLER_H
#define LDAC_NATIVE_ABR_CONTROLLER_H

#include <stdint.h>

#include "ldac_native/ldac_encoder.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ldac_abr_window {
    unsigned total_packets;
    unsigned late_packets;
    unsigned slow_write_packets;
    uint64_t max_schedule_lag_us;
    uint64_t max_write_us;
} ldac_abr_window;

typedef enum ldac_abr_reason {
    LDAC_ABR_REASON_NONE = 0,
    LDAC_ABR_REASON_CONGESTION,
    LDAC_ABR_REASON_CLEAN_LINK
} ldac_abr_reason;

typedef struct ldac_abr_decision {
    ldac_encoder_quality quality;
    ldac_abr_reason reason;
    int changed;
} ldac_abr_decision;

typedef struct ldac_abr_controller {
    ldac_encoder_quality quality;
    unsigned warmup_windows;
    unsigned clean_windows;
    unsigned cooldown_windows;
} ldac_abr_controller;

void ldac_abr_init(ldac_abr_controller *controller,
                   ldac_encoder_quality initial_quality);

ldac_abr_decision ldac_abr_update(ldac_abr_controller *controller,
                                  const ldac_abr_window *window);

#ifdef __cplusplus
}
#endif

#endif
