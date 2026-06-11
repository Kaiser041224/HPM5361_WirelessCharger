/*
 * HRPWM Platform API
 *
 * Copyright (c) 2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef APP_HRPWM_H
#define APP_HRPWM_H

#include <stdint.h>

typedef enum {
    HRPWM_PAIR_0 = 0,
    HRPWM_PAIR_1,
    HRPWM_PAIR_2,
    HRPWM_PAIR_3,
    HRPWM_PAIR_COUNT,
} hrpwm_pair_t;

typedef enum {
    HRPWM_INST_0 = 0,
    HRPWM_INST_1,
    HRPWM_INST_COUNT,
} hrpwm_inst_t;

void app_hrpwm_init(void);
void app_hrpwm_set_duty(hrpwm_pair_t pair, float duty);
void app_hrpwm_set_frequency(hrpwm_inst_t inst, uint32_t freq_hz);
void app_hrpwm_set_jitter(hrpwm_pair_t pair, uint8_t jitter_cmp);
void app_hrpwm_start(hrpwm_pair_t pair);
void app_hrpwm_stop(hrpwm_pair_t pair);
void app_hrpwm_stop_all(void);
void app_hrpwm_start_all(void);
void app_hrpwm_force_low(hrpwm_pair_t pair);
void app_hrpwm_force_release(hrpwm_pair_t pair);
void app_hrpwm_emergency_stop(void);
void app_hrpwm_resume(void);
void app_hrpwm_config_fault(void);
void app_hrpwm_clear_fault(void);

void app_hrpwm_set_phase(hrpwm_inst_t inst, uint8_t ref_pair, uint8_t target_pair, float phase_deg);
void app_hrpwm_config_phase_limit(hrpwm_inst_t inst,
                                  float max_phase_deg,
                                  float max_duty_ref,
                                  float max_duty_target);

#endif /* APP_HRPWM_H */
