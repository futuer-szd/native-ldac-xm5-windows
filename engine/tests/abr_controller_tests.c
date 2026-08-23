// SPDX-License-Identifier: Apache-2.0
#include <stdio.h>

#include "ldac_native/abr_controller.h"

static int failures = 0;

#define CHECK(condition) do {                                                \
    if (!(condition)) {                                                      \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n",                    \
                __FILE__, __LINE__, #condition);                             \
        failures++;                                                          \
    }                                                                        \
} while (0)

static ldac_abr_window clean_window(void) {
    ldac_abr_window window = {188u, 0u, 0u, 500u, 1200u};
    return window;
}

static void test_downgrade_and_recovery(void) {
    ldac_abr_controller controller;
    ldac_abr_window window = clean_window();
    ldac_abr_decision decision;
    unsigned index;

    ldac_abr_init(&controller, LDAC_ENCODER_QUALITY_HQ);
    for (index = 0u; index < 25u; ++index) {
        decision = ldac_abr_update(&controller, &window);
        CHECK(decision.changed == 0);
        CHECK(decision.quality == LDAC_ENCODER_QUALITY_HQ);
    }

    window.late_packets = 30u;
    window.slow_write_packets = 45u;
    window.max_schedule_lag_us = 12000u;
    decision = ldac_abr_update(&controller, &window);
    CHECK(decision.changed != 0);
    CHECK(decision.reason == LDAC_ABR_REASON_CONGESTION);
    CHECK(decision.quality == LDAC_ENCODER_QUALITY_SQ);

    window.late_packets = 20u;
    window.slow_write_packets = 40u;
    window.max_schedule_lag_us = 9000u;
    decision = ldac_abr_update(&controller, &window);
    CHECK(decision.changed == 0);
    CHECK(decision.quality == LDAC_ENCODER_QUALITY_SQ);

    window.late_packets = 100u;
    window.slow_write_packets = 120u;
    window.max_schedule_lag_us = 30000u;
    decision = ldac_abr_update(&controller, &window);
    CHECK(decision.changed != 0);
    CHECK(decision.quality == LDAC_ENCODER_QUALITY_MQ);

    window = clean_window();
    for (index = 0u; index < 24u; ++index) {
        decision = ldac_abr_update(&controller, &window);
        CHECK(decision.changed == 0);
    }
    decision = ldac_abr_update(&controller, &window);
    CHECK(decision.changed != 0);
    CHECK(decision.reason == LDAC_ABR_REASON_CLEAN_LINK);
    CHECK(decision.quality == LDAC_ENCODER_QUALITY_SQ);
}

static void test_invalid_initial_quality(void) {
    ldac_abr_controller controller;
    ldac_abr_init(&controller, (ldac_encoder_quality)99);
    CHECK(controller.quality == LDAC_ENCODER_QUALITY_HQ);
}

static void test_single_scheduler_spike_does_not_downgrade(void) {
    ldac_abr_controller controller;
    ldac_abr_window window = clean_window();
    ldac_abr_decision decision;

    ldac_abr_init(&controller, LDAC_ENCODER_QUALITY_HQ);
    (void)ldac_abr_update(&controller, &window);
    (void)ldac_abr_update(&controller, &window);
    window.late_packets = 1u;
    window.slow_write_packets = 1u;
    window.max_schedule_lag_us = 30000u;
    window.max_write_us = 12000u;
    decision = ldac_abr_update(&controller, &window);
    CHECK(decision.changed == 0);
    CHECK(decision.quality == LDAC_ENCODER_QUALITY_HQ);
}

static void test_capture_cadence_lag_does_not_downgrade(void) {
    ldac_abr_controller controller;
    ldac_abr_window window = clean_window();
    ldac_abr_decision decision;

    ldac_abr_init(&controller, LDAC_ENCODER_QUALITY_HQ);
    (void)ldac_abr_update(&controller, &window);
    (void)ldac_abr_update(&controller, &window);
    window.late_packets = 130u;
    window.slow_write_packets = 14u;
    window.max_schedule_lag_us = 17000u;
    window.max_write_us = 11000u;
    decision = ldac_abr_update(&controller, &window);
    CHECK(decision.changed == 0);
    CHECK(decision.quality == LDAC_ENCODER_QUALITY_HQ);
}

int main(void) {
    test_downgrade_and_recovery();
    test_invalid_initial_quality();
    test_single_scheduler_spike_does_not_downgrade();
    test_capture_cadence_lag_does_not_downgrade();
    if (failures != 0) {
        fprintf(stderr, "%d ABR controller test(s) failed\n", failures);
        return 1;
    }
    puts("All LDAC ABR controller tests passed.");
    return 0;
}
