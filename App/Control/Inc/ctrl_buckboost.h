/*
 * ctrl_buckboost.h — 四开关 Buck-Boost 控制器
 *
 * 控制链:
 *   PI 输出 + 前馈 → V_cmd (V，有物理意义的目标电压)
 *   调制器: V_cmd + VIN + VLINK → DA, DB
 *
 * 调制器公式:
 *   DA = Dmax × V_cmd / (VIN + V_cmd)
 *   DB = Dmax × VIN / (VIN + V_cmd)
 *   稳态: VLINK = VIN × DA / DB = V_cmd
 *
 * 双向功率: V_cmd 符号由 PI + 前馈自然决定。
 * A/B 半桥独立控制，无移相。
 *
 * Copyright (c) 2026 Alliance HardWare Team
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef CTRL_BUCKBOOST_H
#define CTRL_BUCKBOOST_H

#include "algo_pid.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float kp;
    float ki;
    float kd;
    float integral_max;
    float output_max;
    float output_min;
} ctrl_buckboost_pid_params_t;

typedef enum {
    BUCKBOOST_TARGET_CV = 0,
    BUCKBOOST_TARGET_CC = 1,
} ctrl_buckboost_target_t;

typedef struct {
    ctrl_buckboost_pid_params_t current_pid;
    ctrl_buckboost_pid_params_t voltage_pid;
    float i_l_limit_a;
    float v_out_limit_v;
    float duty_min;
    float duty_max;
} ctrl_buckboost_params_t;

typedef struct {
    bool                    enabled;
    ctrl_buckboost_target_t target_type;

    float v_out_target_v;
    float i_l_target_a;

    algo_pid_t current_pid;
    algo_pid_t voltage_pid;

    float current_ref;

    float v_cmd;
    float duty_a;
    float duty_b;
} ctrl_buckboost_state_t;

typedef struct {
    ctrl_buckboost_params_t params;
    ctrl_buckboost_state_t  state;
} ctrl_buckboost_t;

int  ctrl_buckboost_init(ctrl_buckboost_t *ctrl);
int  ctrl_buckboost_enable(ctrl_buckboost_t *ctrl);
void ctrl_buckboost_disable(ctrl_buckboost_t *ctrl);
void ctrl_buckboost_emergency_stop(ctrl_buckboost_t *ctrl);

/*
 * 调制器: V_cmd (V) + VIN + VLINK → DA, DB
 *
 *   DA = Dmax × V_cmd / (VIN + V_cmd)
 *   DB = Dmax × VIN / (VIN + V_cmd)
 *
 * 稳态: VLINK = V_cmd
 * 输入 v_in, vlink 用于诊断区域判定。
 */
void ctrl_buckboost_modulate(ctrl_buckboost_t *ctrl,
                             float v_cmd, float v_in, float vlink);

/*
 * 电流内环 update (200kHz)
 *   PI(current_ref, i_l) → u_pi (V)
 *   u_pi + u_model_ff → v_cmd
 *   modulate(v_cmd, VIN, VLINK) → DA, DB
 */
void ctrl_buckboost_update_current(ctrl_buckboost_t *ctrl,
                                   float i_l, float v_in, float vlink);

/*
 * 电压外环 update (50kHz)
 *   PI(v_out_target, vlink) → current_ref
 */
void ctrl_buckboost_update_voltage(ctrl_buckboost_t *ctrl, float vlink);

/*
 * 功率外环 update (20kHz)
 *   PI(p_target, p_in) → v_out_target_v
 */
void ctrl_buckboost_update_power(ctrl_buckboost_t *ctrl, float p_in);

void ctrl_buckboost_set_vout_target(ctrl_buckboost_t *ctrl, float target_v);
void ctrl_buckboost_set_il_target(ctrl_buckboost_t *ctrl, float target_a);
void ctrl_buckboost_set_target_type(ctrl_buckboost_t *ctrl, ctrl_buckboost_target_t target);
void ctrl_buckboost_set_params(ctrl_buckboost_t *ctrl, const ctrl_buckboost_params_t *params);

float ctrl_buckboost_get_duty_a(const ctrl_buckboost_t *ctrl);
float ctrl_buckboost_get_duty_b(const ctrl_buckboost_t *ctrl);
float ctrl_buckboost_get_v_cmd(const ctrl_buckboost_t *ctrl);
float ctrl_buckboost_get_duty_max(const ctrl_buckboost_t *ctrl);
float ctrl_buckboost_get_current_ref(const ctrl_buckboost_t *ctrl);
bool  ctrl_buckboost_is_enabled(const ctrl_buckboost_t *ctrl);

#ifdef __cplusplus
}
#endif

#endif /* CTRL_BUCKBOOST_H */
