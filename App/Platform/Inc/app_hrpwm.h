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

typedef enum {
    PWM_INST_0 = 0,
    PWM_INST_1,
    PWM_INST_COUNT,
} pwm_inst_t;

void pwm_init(void);
void pwm_set_duty(pwm_pair_t pair, float duty);
void pwm_set_frequency(pwm_inst_t inst, uint32_t freq_hz);
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

void pwm_set_phase(pwm_inst_t inst, uint8_t ref_pair, uint8_t target_pair, float phase_deg);
void pwm_config_phase_limit(pwm_inst_t inst,
                            float max_phase_deg,
                            float max_duty_ref,
                            float max_duty_target);

#endif /* APP_HRPWM_H */
