/*
 * HRPWM Platform Implementation
 *
 * PWM0: ch0/ch1 (pair 0), ch2/ch3 (pair 1)
 * PWM1: ch4/ch5 (pair 2), ch6/ch7 (pair 3)
 *
 * Copyright (c) 2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "app_hrpwm.h"

#include "intf_hrpwm.h"

#include <stdbool.h>

static const intf_hrpwm_ch_t pair_to_ch[HRPWM_PAIR_COUNT] = {0, 2, 4, 6};

extern void hpm_hrpwm_driver_register(void);

static bool hrpwm_pair_is_valid(hrpwm_pair_t pair) { return pair < HRPWM_PAIR_COUNT; }

static bool hrpwm_inst_is_valid(hrpwm_inst_t inst) { return inst < HRPWM_INST_COUNT; }

static intf_hrpwm_ch_t hrpwm_pair_channel(hrpwm_pair_t pair) { return pair_to_ch[pair]; }

void hrpwm_init(void) {
    hpm_hrpwm_driver_register();

    intf_hrpwm_pair_cfg_t cfg[HRPWM_PAIR_COUNT] = {
        [HRPWM_PAIR_0] =
            {.frequency_hz = 200000,
                          .duty = 0.0f,
                          .deadtime_ns = 10,
                          .jitter_cmp = 4,
                          .align = INTF_HRPWM_ALIGN_CENTER,
                          .invert_high_side = false,
                          .invert_low_side = false},
        [HRPWM_PAIR_1] =
            {.frequency_hz = 200000,
                          .duty = 0.0f,
                          .deadtime_ns = 10,
                          .jitter_cmp = 4,
                          .align = INTF_HRPWM_ALIGN_CENTER,
                          .invert_high_side = false,
                          .invert_low_side = false},
        [HRPWM_PAIR_2] =
            {.frequency_hz = 148000,
                          .duty = 0.0f,
                          .deadtime_ns = 25,
                          .jitter_cmp = 4,
                          .align = INTF_HRPWM_ALIGN_CENTER,
                          .invert_high_side = false,
                          .invert_low_side = false},
        [HRPWM_PAIR_3] =
            {.frequency_hz = 148000,
                          .duty = 0.0f,
                          .deadtime_ns = 25,
                          .jitter_cmp = 4,
                          .align = INTF_HRPWM_ALIGN_CENTER,
                          .invert_high_side = false,
                          .invert_low_side = false},
    };

    for (hrpwm_pair_t pair = HRPWM_PAIR_0; pair < HRPWM_PAIR_COUNT; pair++) {
        (void)intf_hrpwm_init_pair(hrpwm_pair_channel(pair), &cfg[pair]);
        /* PWM configured but NOT started — ADC calibrates in quiet environment first */
    }
}

void hrpwm_set_duty(hrpwm_pair_t pair, float duty) {
    if (!hrpwm_pair_is_valid(pair))
        return;

    (void)intf_hrpwm_set_duty(hrpwm_pair_channel(pair), duty);
}

void hrpwm_set_frequency(hrpwm_inst_t inst, uint32_t freq_hz) {
    if (!hrpwm_inst_is_valid(inst))
        return;

    (void)intf_hrpwm_set_frequency(inst, freq_hz);
}

void hrpwm_set_jitter(hrpwm_pair_t pair, uint8_t jitter_cmp) {
    if (!hrpwm_pair_is_valid(pair))
        return;

    (void)intf_hrpwm_set_jitter(hrpwm_pair_channel(pair), jitter_cmp);
}

void hrpwm_start(hrpwm_pair_t pair) {
    if (!hrpwm_pair_is_valid(pair))
        return;

    (void)intf_hrpwm_start(hrpwm_pair_channel(pair));
}

void hrpwm_stop(hrpwm_pair_t pair) {
    if (!hrpwm_pair_is_valid(pair))
        return;

    (void)intf_hrpwm_stop(hrpwm_pair_channel(pair));
    (void)intf_hrpwm_stop((intf_hrpwm_ch_t)(hrpwm_pair_channel(pair) + 1U));
}

void hrpwm_stop_all(void) {
    for (hrpwm_pair_t pair = HRPWM_PAIR_0; pair < HRPWM_PAIR_COUNT; pair++) {
        hrpwm_stop(pair);
    }
}

void hrpwm_start_all(void) {
    for (hrpwm_pair_t pair = HRPWM_PAIR_0; pair < HRPWM_PAIR_COUNT; pair++) {
        hrpwm_start(pair);
    }
}

void hrpwm_force_low(hrpwm_pair_t pair) {
    if (!hrpwm_pair_is_valid(pair))
        return;

    (void)intf_hrpwm_force_low(hrpwm_pair_channel(pair));
    (void)intf_hrpwm_force_low((intf_hrpwm_ch_t)(hrpwm_pair_channel(pair) + 1U));
}

void hrpwm_force_release(hrpwm_pair_t pair) {
    if (!hrpwm_pair_is_valid(pair))
        return;

    (void)intf_hrpwm_force_release(hrpwm_pair_channel(pair));
    (void)intf_hrpwm_force_release((intf_hrpwm_ch_t)(hrpwm_pair_channel(pair) + 1U));
}

void hrpwm_emergency_stop(void) {
    for (hrpwm_pair_t pair = HRPWM_PAIR_0; pair < HRPWM_PAIR_COUNT; pair++) {
        hrpwm_force_low(pair);
    }
}

void hrpwm_resume(void) {
    for (hrpwm_pair_t pair = HRPWM_PAIR_0; pair < HRPWM_PAIR_COUNT; pair++) {
        hrpwm_force_release(pair);
    }
}

void app_hrpwm_config_fault(void) {
    intf_hrpwm_fault_cfg_t fault_cfg = {
        .source = INTF_HRPWM_FAULT_SRC_EXTERNAL_0,
        .mode = INTF_HRPWM_FAULT_MODE_FORCE_LOW,
        .recovery = INTF_HRPWM_FAULT_RECOVERY_ON_FAULT_CLEAR,
        .active_low = true,
    };

    (void)intf_hrpwm_config_fault((intf_hrpwm_inst_t)HRPWM_INST_0, &fault_cfg);
    (void)intf_hrpwm_config_fault((intf_hrpwm_inst_t)HRPWM_INST_1, &fault_cfg);
}

void app_hrpwm_clear_fault(void) {
    (void)intf_hrpwm_clear_fault((intf_hrpwm_inst_t)HRPWM_INST_0);
    (void)intf_hrpwm_clear_fault((intf_hrpwm_inst_t)HRPWM_INST_1);
}

void hrpwm_set_phase(hrpwm_inst_t inst, uint8_t ref_pair, uint8_t target_pair, float phase_deg) {
    intf_hrpwm_phase_cfg_t cfg = {
        .inst = inst,
        .ref_pair = ref_pair,
        .target_pair = target_pair,
        .phase_deg = phase_deg,
    };
    (void)intf_hrpwm_set_phase(&cfg);
}

void hrpwm_config_phase_limit(hrpwm_inst_t inst,
                              float max_phase_deg,
                              float max_duty_ref,
                              float max_duty_target) {
    (void)inst; (void)max_phase_deg; (void)max_duty_ref; (void)max_duty_target;
}
