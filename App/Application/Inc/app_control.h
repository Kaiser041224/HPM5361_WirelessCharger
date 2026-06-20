/*
 * app_control.h — 系统控制编排
 *
 * 职责：状态机、运行模式、功率使能/禁能、故障检查、ISR 注册。
 * 不包含控制算法（算法在 Control/ 层），只做"何时做什么、谁来调用"的决策。
 *
 * Copyright (c) 2026 Alliance HardWare Team
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef APP_CONTROL_H
#define APP_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SYS_INIT   = 0,
    SYS_IDLE   = 1,
    SYS_RUN    = 2,
    SYS_FAULT  = 3,
} sys_state_t;

typedef enum {
    MODE_IDLE       = 0,
    MODE_BUCK_CV    = 1,
    MODE_BUCK_CC    = 2,
    MODE_LCC_OPEN   = 3,
    MODE_LCC_CLOSED = 4,
    MODE_STANDBY    = 5,
} op_mode_t;

typedef struct {
    struct {
        float i_l_a;
        float v_link_v;
        float i_in_a;
        float v_in_v;
        float i_coil_a;
        float i_lf_a;
    } raw;
    struct {
        float i_l_a;
        float v_link_v;      /* MA4 @50kHz: 电压外环反馈用 (稳态平滑) */
        float v_link_fast_v; /* 1阶LPF @40kHz: 电流内环前馈用 (动态快, 勿用于反馈) */
        float i_in_a;
        float v_in_v;
        float i_coil_a;
        float i_lf_a;
    } filt;
    struct {
        float buckboost_a;
        float buckboost_b;
        float lcc_a;
        float lcc_b;
    } duty;
    struct {
        float i_load_est_a;
    } ff;
} ctrl_diag_t;

extern volatile ctrl_diag_t g_ctrl_diag;

void app_control_init(void);

void app_control_tick(void);

sys_state_t app_control_get_state(void);
op_mode_t   app_control_get_mode(void);
int         app_control_set_mode(op_mode_t mode);

int  app_control_power_enable(void);
void app_control_power_disable(void);
void app_control_emergency(void);

uint32_t app_control_get_faults(void);
int      app_control_clear_faults(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_CONTROL_H */
