// SPDX-License-Identifier: Apache-2.0
#include "ldac_native/abr_controller.h"

#define LDAC_ABR_WARMUP_WINDOWS 2u
#define LDAC_ABR_DOWNGRADE_SLOW_PERCENT 20u
#define LDAC_ABR_SEVERE_SLOW_PERCENT 60u
#define LDAC_ABR_CLEAN_PACKET_PERCENT 3u
#define LDAC_ABR_SWITCH_COOLDOWN_WINDOWS 5u
#define LDAC_ABR_UPGRADE_CLEAN_WINDOWS 20u

static int valid_quality(ldac_encoder_quality quality) {
    return quality == LDAC_ENCODER_QUALITY_HQ ||
           quality == LDAC_ENCODER_QUALITY_SQ ||
           quality == LDAC_ENCODER_QUALITY_MQ;
}

static int percentage_at_least(unsigned count,
                               unsigned total,
                               unsigned percentage) {
    if (total == 0u) return 0;
    return (uint64_t)count * 100u >= (uint64_t)total * percentage;
}

void ldac_abr_init(ldac_abr_controller *controller,
                   ldac_encoder_quality initial_quality) {
    if (controller == NULL) return;
    controller->quality = valid_quality(initial_quality)
        ? initial_quality
        : LDAC_ENCODER_QUALITY_HQ;
    controller->warmup_windows = LDAC_ABR_WARMUP_WINDOWS;
    controller->clean_windows = 0u;
    controller->cooldown_windows = 0u;
}

ldac_abr_decision ldac_abr_update(ldac_abr_controller *controller,
                                  const ldac_abr_window *window) {
    ldac_abr_decision decision;
    int congested;
    int severe;
    int clean;
    int wasCoolingDown;

    decision.quality = controller != NULL
        ? controller->quality
        : LDAC_ENCODER_QUALITY_HQ;
    decision.reason = LDAC_ABR_REASON_NONE;
    decision.changed = 0;
    if (controller == NULL || window == NULL ||
        window->total_packets == 0u) {
        return decision;
    }

    if (controller->warmup_windows > 0u) {
        controller->warmup_windows--;
        controller->clean_windows = 0u;
        return decision;
    }

    congested = percentage_at_least(window->slow_write_packets,
                            window->total_packets,
                            LDAC_ABR_DOWNGRADE_SLOW_PERCENT);
    severe = percentage_at_least(window->slow_write_packets,
                            window->total_packets,
                            LDAC_ABR_SEVERE_SLOW_PERCENT);
    clean = !percentage_at_least(window->slow_write_packets,
                                 window->total_packets,
                                 LDAC_ABR_CLEAN_PACKET_PERCENT);

    wasCoolingDown = controller->cooldown_windows > 0u;
    if (wasCoolingDown) {
        controller->cooldown_windows--;
    }
    if (congested && controller->quality < LDAC_ENCODER_QUALITY_MQ &&
        (!wasCoolingDown || severe)) {
        controller->quality =
            (ldac_encoder_quality)((int)controller->quality + 1);
        controller->clean_windows = 0u;
        controller->cooldown_windows = LDAC_ABR_SWITCH_COOLDOWN_WINDOWS;
        decision.quality = controller->quality;
        decision.reason = LDAC_ABR_REASON_CONGESTION;
        decision.changed = 1;
        return decision;
    }
    if (wasCoolingDown) {
        controller->clean_windows = 0u;
        return decision;
    }
    if (!clean || controller->quality == LDAC_ENCODER_QUALITY_HQ) {
        controller->clean_windows = 0u;
        return decision;
    }
    controller->clean_windows++;
    if (controller->clean_windows >= LDAC_ABR_UPGRADE_CLEAN_WINDOWS) {
        controller->quality =
            (ldac_encoder_quality)((int)controller->quality - 1);
        controller->clean_windows = 0u;
        controller->cooldown_windows = LDAC_ABR_SWITCH_COOLDOWN_WINDOWS;
        decision.quality = controller->quality;
        decision.reason = LDAC_ABR_REASON_CLEAN_LINK;
        decision.changed = 1;
    }
    return decision;
}
