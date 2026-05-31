/*
 * HRPWM Example API
 *
 * Copyright (c) 2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef APP_HRPWM_H
#define APP_HRPWM_H

#include <stdint.h>

typedef enum {
    PWM_PAIR_0 = 0,
    PWM_PAIR_1,
    PWM_PAIR_2,
    PWM_PAIR_3,
    PWM_PAIR_COUNT,
} pwm_pair_t;

void pwm_init(void);
void pwm_set_duty(pwm_pair_t pair, float duty);
void pwm_set_frequency(pwm_pair_t pair, uint32_t freq_hz);
void pwm_set_jitter(pwm_pair_t pair, uint8_t jitter_cmp);
void pwm_start(pwm_pair_t pair);
void pwm_stop(pwm_pair_t pair);
void pwm_stop_all(void);
void pwm_force_low(pwm_pair_t pair);
void pwm_force_release(pwm_pair_t pair);
void pwm_emergency_stop(void);
void pwm_resume(void);
void app_pwm_config_fault(void);
void app_pwm_clear_fault(void);

#endif /* APP_HRPWM_H */
