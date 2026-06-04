/*
 * HRPWM Usage Example - Object-Oriented API
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

static const intf_hrpwm_ch_t pair_to_ch[PWM_PAIR_COUNT] = {0, 2, 4, 6};

extern void hpm_hrpwm_driver_register(void);

static bool pwm_pair_is_valid(pwm_pair_t pair)
{
    return pair < PWM_PAIR_COUNT;
}

static bool pwm_inst_is_valid(pwm_inst_t inst)
{
    return inst < PWM_INST_COUNT;
}

static intf_hrpwm_ch_t pwm_pair_channel(pwm_pair_t pair)
{
    return pair_to_ch[pair];
}

void pwm_init(void) {
    hpm_hrpwm_driver_register();

    intf_hrpwm_pair_cfg_t cfg[PWM_PAIR_COUNT] = {
        [PWM_PAIR_0] =
            {.frequency_hz = 200000,
                          .duty = 0.5f,
                          .deadtime_ns = 10,
                          .jitter_cmp = 4,
                          .align = INTF_HRPWM_ALIGN_CENTER,
                          .invert_high_side = false,
                          .invert_low_side = false},
        [PWM_PAIR_1] =
            {.frequency_hz = 200000,
                          .duty = 0.3f,
                          .deadtime_ns = 10,
                          .jitter_cmp = 4,
                          .align = INTF_HRPWM_ALIGN_CENTER,
                          .invert_high_side = false,
                          .invert_low_side = false},
        [PWM_PAIR_2] =
            {.frequency_hz = 148000,
                          .duty = 0.5f,
                          .deadtime_ns = 25,
                          .jitter_cmp = 4,
                          .align = INTF_HRPWM_ALIGN_CENTER,
                          .invert_high_side = false,
                          .invert_low_side = false},
        [PWM_PAIR_3] =
            {.frequency_hz = 148000,
                          .duty = 0.4f,
                          .deadtime_ns = 25,
                          .jitter_cmp = 4,
                          .align = INTF_HRPWM_ALIGN_CENTER,
                          .invert_high_side = false,
                          .invert_low_side = false},
    };

    for (pwm_pair_t pair = PWM_PAIR_0; pair < PWM_PAIR_COUNT; pair++) {
        (void)intf_hrpwm_init_pair(pwm_pair_channel(pair), &cfg[pair]);
        (void)intf_hrpwm_start(pwm_pair_channel(pair));
    }
}

void pwm_set_duty(pwm_pair_t pair, float duty) {
    if (!pwm_pair_is_valid(pair)) return;

    (void)intf_hrpwm_set_duty(pwm_pair_channel(pair), duty);
}

void pwm_set_frequency(pwm_inst_t inst, uint32_t freq_hz) {
    if (!pwm_inst_is_valid(inst)) return;

    (void)intf_hrpwm_set_frequency(inst, freq_hz);
}

void pwm_set_jitter(pwm_pair_t pair, uint8_t jitter_cmp) {
    if (!pwm_pair_is_valid(pair)) return;

    (void)intf_hrpwm_set_jitter(pwm_pair_channel(pair), jitter_cmp);
}

void pwm_start(pwm_pair_t pair) {
    if (!pwm_pair_is_valid(pair)) return;

    (void)intf_hrpwm_start(pwm_pair_channel(pair));
}

void pwm_stop(pwm_pair_t pair) {
    if (!pwm_pair_is_valid(pair)) return;

    (void)intf_hrpwm_stop(pwm_pair_channel(pair));
    (void)intf_hrpwm_stop((intf_hrpwm_ch_t)(pwm_pair_channel(pair) + 1U));
}

void pwm_stop_all(void) {
    for (pwm_pair_t pair = PWM_PAIR_0; pair < PWM_PAIR_COUNT; pair++) {
        pwm_stop(pair);
    }
}

void pwm_force_low(pwm_pair_t pair) {
    if (!pwm_pair_is_valid(pair)) return;

    (void)intf_hrpwm_force_low(pwm_pair_channel(pair));
    (void)intf_hrpwm_force_low((intf_hrpwm_ch_t)(pwm_pair_channel(pair) + 1U));
}

void pwm_force_release(pwm_pair_t pair) {
    if (!pwm_pair_is_valid(pair)) return;

    (void)intf_hrpwm_force_release(pwm_pair_channel(pair));
    (void)intf_hrpwm_force_release((intf_hrpwm_ch_t)(pwm_pair_channel(pair) + 1U));
}

void pwm_emergency_stop(void) {
    for (pwm_pair_t pair = PWM_PAIR_0; pair < PWM_PAIR_COUNT; pair++) {
        pwm_force_low(pair);
    }
}

void pwm_resume(void) {
    for (pwm_pair_t pair = PWM_PAIR_0; pair < PWM_PAIR_COUNT; pair++) {
        pwm_force_release(pair);
    }
}

void app_pwm_config_fault(void) {
    intf_hrpwm_fault_cfg_t fault_cfg = {
        .source = INTF_HRPWM_FAULT_SRC_EXTERNAL_0,
        .mode = INTF_HRPWM_FAULT_MODE_FORCE_LOW,
        .recovery = INTF_HRPWM_FAULT_RECOVERY_ON_FAULT_CLEAR,
        .active_low = true,
    };

    (void)intf_hrpwm_config_fault((intf_hrpwm_inst_t)PWM_INST_0, &fault_cfg);
    (void)intf_hrpwm_config_fault((intf_hrpwm_inst_t)PWM_INST_1, &fault_cfg);
}

void app_pwm_clear_fault(void) {
    (void)intf_hrpwm_clear_fault((intf_hrpwm_inst_t)PWM_INST_0);
    (void)intf_hrpwm_clear_fault((intf_hrpwm_inst_t)PWM_INST_1);
}

void pwm_set_phase(pwm_inst_t inst, uint8_t ref_pair, uint8_t target_pair, float phase_deg) {
    intf_hrpwm_phase_cfg_t cfg = {
        .inst = inst,
        .ref_pair = ref_pair,
        .target_pair = target_pair,
        .phase_deg = phase_deg,
    };
    (void)intf_hrpwm_set_phase(&cfg);
}

void pwm_config_phase_limit(pwm_inst_t inst,
                            float max_phase_deg,
                            float max_duty_ref,
                            float max_duty_target) {
    if (!pwm_inst_is_valid(inst)) return;

    intf_hrpwm_phase_limit_t limit = {
        .max_phase_deg = max_phase_deg,
        .max_duty_ref = max_duty_ref,
        .max_duty_target = max_duty_target,
    };
    (void)intf_hrpwm_config_phase_limit(inst, &limit);
}
