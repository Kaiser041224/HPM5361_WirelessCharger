/*
 * GPWM Interface - GPTMR based PWM contract
 *
 * Copyright (c) 2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef INTF_GPWM_H
#define INTF_GPWM_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t intf_gpwm_ch_t;

typedef struct {
    uint32_t frequency_hz;
    float duty;
    bool invert_output;
} intf_gpwm_cfg_t;

typedef enum {
    INTF_GPWM_CAPTURE_EDGE_RISING = 0,
    INTF_GPWM_CAPTURE_EDGE_FALLING,
    INTF_GPWM_CAPTURE_EDGE_BOTH,
} intf_gpwm_capture_edge_t;

typedef struct {
    intf_gpwm_capture_edge_t edge;
} intf_gpwm_capture_cfg_t;

typedef struct {
    bool captured;
    uint32_t count;
    uint32_t period_ticks;
} intf_gpwm_capture_t;

typedef struct {
    uint8_t instance_id;
    struct {
        int (*init)(intf_gpwm_ch_t ch, const intf_gpwm_cfg_t *cfg);
        int (*set_duty)(intf_gpwm_ch_t ch, float duty);
        int (*set_frequency)(intf_gpwm_ch_t ch, uint32_t frequency_hz);
        int (*start)(intf_gpwm_ch_t ch);
        int (*stop)(intf_gpwm_ch_t ch);
        int (*force_low)(intf_gpwm_ch_t ch);
        int (*force_release)(intf_gpwm_ch_t ch);
        int (*capture_init)(intf_gpwm_ch_t ch, const intf_gpwm_capture_cfg_t *cfg);
        int (*capture_start)(intf_gpwm_ch_t ch);
        int (*capture_stop)(intf_gpwm_ch_t ch);
        int (*capture_poll)(intf_gpwm_ch_t ch, intf_gpwm_capture_t *capture);
    };
} intf_gpwm_t;

int intf_gpwm_register(const intf_gpwm_t *ops);
int intf_gpwm_init(intf_gpwm_ch_t ch, const intf_gpwm_cfg_t *cfg);
int intf_gpwm_set_duty(intf_gpwm_ch_t ch, float duty);
int intf_gpwm_set_frequency(intf_gpwm_ch_t ch, uint32_t frequency_hz);
int intf_gpwm_start(intf_gpwm_ch_t ch);
int intf_gpwm_stop(intf_gpwm_ch_t ch);
int intf_gpwm_force_low(intf_gpwm_ch_t ch);
int intf_gpwm_force_release(intf_gpwm_ch_t ch);
int intf_gpwm_capture_init(intf_gpwm_ch_t ch, const intf_gpwm_capture_cfg_t *cfg);
int intf_gpwm_capture_start(intf_gpwm_ch_t ch);
int intf_gpwm_capture_stop(intf_gpwm_ch_t ch);
int intf_gpwm_capture_poll(intf_gpwm_ch_t ch, intf_gpwm_capture_t *capture);

#ifdef __cplusplus
}
#endif

#endif /* INTF_GPWM_H */
