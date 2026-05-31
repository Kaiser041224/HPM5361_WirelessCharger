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

static const intf_hrpwm_ch_t pair_to_ch[PWM_PAIR_COUNT] = {0, 2, 4, 6};
static const intf_hrpwm_inst_t pair_to_inst[PWM_PAIR_COUNT] = {0, 0, 1, 1};

extern void hpm_hrpwm_driver_register(void);

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
        intf_hrpwm_init_pair(pair_to_ch[pair], &cfg[pair]);
        intf_hrpwm_start(pair_to_ch[pair]);
    }
}

void pwm_set_duty(pwm_pair_t pair, float duty) {
    if (pair < PWM_PAIR_COUNT) {
        intf_hrpwm_set_duty(pair_to_ch[pair], duty);
    }
}

void pwm_set_frequency(pwm_pair_t pair, uint32_t freq_hz) {
    if (pair < PWM_PAIR_COUNT) {
        intf_hrpwm_set_frequency(pair_to_inst[pair], freq_hz);
    }
}

void pwm_set_jitter(pwm_pair_t pair, uint8_t jitter_cmp) {
    if (pair < PWM_PAIR_COUNT) {
        intf_hrpwm_set_jitter(pair_to_ch[pair], jitter_cmp);
    }
}

void pwm_start(pwm_pair_t pair) {
    if (pair < PWM_PAIR_COUNT) {
        intf_hrpwm_start(pair_to_ch[pair]);
    }
}

void pwm_stop(pwm_pair_t pair) {
    if (pair < PWM_PAIR_COUNT) {
        intf_hrpwm_stop(pair_to_ch[pair]);
        intf_hrpwm_stop(pair_to_ch[pair] + 1);
    }
}

void pwm_stop_all(void) {
    for (pwm_pair_t pair = PWM_PAIR_0; pair < PWM_PAIR_COUNT; pair++) {
        pwm_stop(pair);
    }
}

void pwm_force_low(pwm_pair_t pair) {
    if (pair < PWM_PAIR_COUNT) {
        intf_hrpwm_force_low(pair_to_ch[pair]);
        intf_hrpwm_force_low(pair_to_ch[pair] + 1);
    }
}

void pwm_force_release(pwm_pair_t pair) {
    if (pair < PWM_PAIR_COUNT) {
        intf_hrpwm_force_release(pair_to_ch[pair]);
        intf_hrpwm_force_release(pair_to_ch[pair] + 1);
    }
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

    intf_hrpwm_config_fault(0, &fault_cfg);
    intf_hrpwm_config_fault(1, &fault_cfg);
}

void app_pwm_clear_fault(void) {
    intf_hrpwm_clear_fault(0);
    intf_hrpwm_clear_fault(1);
}
