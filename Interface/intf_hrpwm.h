/*
 * HRPWM Interface - hardware-independent contract
 *
 * Copyright (c) 2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef INTF_HRPWM_H
#define INTF_HRPWM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t intf_hrpwm_ch_t;

typedef struct {
    uint32_t frequency_hz;
    float duty;
    bool invert_output;
} intf_hrpwm_cfg_t;

typedef struct {
    uint8_t instance_id;
    struct {
        int (*init)(intf_hrpwm_ch_t ch, const intf_hrpwm_cfg_t *cfg);
        int (*set_duty)(intf_hrpwm_ch_t ch, float duty);
        int (*set_frequency)(intf_hrpwm_ch_t ch, uint32_t frequency_hz);
        int (*start)(intf_hrpwm_ch_t ch);
        int (*stop)(intf_hrpwm_ch_t ch);
    };
} intf_hrpwm_t;

int intf_hrpwm_register(const intf_hrpwm_t *ops);
int intf_hrpwm_init(intf_hrpwm_ch_t ch, const intf_hrpwm_cfg_t *cfg);
int intf_hrpwm_set_duty(intf_hrpwm_ch_t ch, float duty);
int intf_hrpwm_set_frequency(intf_hrpwm_ch_t ch, uint32_t frequency_hz);
int intf_hrpwm_start(intf_hrpwm_ch_t ch);
int intf_hrpwm_stop(intf_hrpwm_ch_t ch);

#ifdef __cplusplus
}
#endif

#endif /* INTF_HRPWM_H */
