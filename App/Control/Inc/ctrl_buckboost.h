/*
 * ctrl_buckboost.h — 四开关 Buck-Boost 控制器
 *
 * 控制链:
 *   PI 输出 → V_L_cmd (V，有符号平均电感电压命令)
 *   VIN/VLINK 前馈: V_L_cmd → generalized_duty (0.0-1.0)
 *   单输入调制器: generalized_duty → DA, DB
 *
 * 调制器公式:
 *   DA = Dmax × generalized_duty
 *   DB = Dmax × (1 - generalized_duty)
 *
 * 理想平均电感电压:
 *   V_L = DA × VIN - DB × VLINK
 *       = Dmax × ((VIN + VLINK) × generalized_duty - VLINK)
 *
 * 双向功率: V_L_cmd 符号由 PI 自然决定，前馈负责转换为 generalized_duty。
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
    ctrl_buckboost_pid_params_t current_cc_pid;
    float i_l_limit_a;
    float v_out_limit_v;
    float voltage_ff_gain;
    float duty_min;
    float duty_max;
} ctrl_buckboost_params_t;

typedef struct {
    bool                    enabled;
    ctrl_buckboost_target_t target_type;

    float v_out_target_v;
    float i_l_target_a;
    float i_link_target_a;

    algo_pid_t current_pid;
    algo_pid_t voltage_pid;
    algo_pid_t current_cc_pid;

    volatile float current_ref;
    float voltage_pid_out;
    float cc_pid_out;

    float v_cmd;
    float generalized_duty;
    bool  vlink_limit_active;
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
 * 单输入调制器: generalized_duty (0.0-1.0) → DA, DB
 *
 *   DA = Dmax × generalized_duty
 *   DB = Dmax × (1 - generalized_duty)
 *
 * VIN/VLINK 前馈不在调制器内完成，而是在 update_current() 中完成。
 */
void ctrl_buckboost_modulate(ctrl_buckboost_t *ctrl,
                             float generalized_duty);

/*
 * 电流内环 update (200kHz)
 *   PI(current_ref, i_l) → V_L_cmd (V)
 *   feedforward(V_L_cmd, VIN, VLINK) → generalized_duty
 *   modulate(generalized_duty) → DA, DB
 */
void ctrl_buckboost_update_current(ctrl_buckboost_t *ctrl,
                                   float i_l, float v_in, float vlink);

/*
 * 电压外环 update (50kHz)
 *   PI(v_out_target, vlink) + i_load_ff × ff_gain → current_ref
 */
void ctrl_buckboost_update_voltage(ctrl_buckboost_t *ctrl, float vlink,
                                   float i_load_ff, float i_link);

/*
 * 进入恒压 (CV) 模式，配置软起动
 *   - 设定 v_out_target 并 reset 电压环 PID，从零输出开始
 *   - target_type 自动设为 BUCKBOOST_TARGET_CV
 */
void ctrl_buckboost_enter_cv_mode(ctrl_buckboost_t *ctrl, float target_v);

/*
 * 功率外环 update (20kHz)
 *   PI(p_target, p_in) → v_out_target_v
 */
void ctrl_buckboost_update_power(ctrl_buckboost_t *ctrl, float p_in);

void ctrl_buckboost_set_vout_target(ctrl_buckboost_t *ctrl, float target_v);
void ctrl_buckboost_set_il_target(ctrl_buckboost_t *ctrl, float target_a);
void ctrl_buckboost_set_ilink_target(ctrl_buckboost_t *ctrl, float target_a);
void ctrl_buckboost_set_target_type(ctrl_buckboost_t *ctrl, ctrl_buckboost_target_t target);
void ctrl_buckboost_set_params(ctrl_buckboost_t *ctrl, const ctrl_buckboost_params_t *params);

float ctrl_buckboost_get_duty_a(const ctrl_buckboost_t *ctrl);
float ctrl_buckboost_get_duty_b(const ctrl_buckboost_t *ctrl);
float ctrl_buckboost_get_v_cmd(const ctrl_buckboost_t *ctrl);
float ctrl_buckboost_get_generalized_duty(const ctrl_buckboost_t *ctrl);
float ctrl_buckboost_get_duty_max(const ctrl_buckboost_t *ctrl);
float ctrl_buckboost_get_current_ref(const ctrl_buckboost_t *ctrl);
bool  ctrl_buckboost_is_enabled(const ctrl_buckboost_t *ctrl);

#ifdef __cplusplus
}
#endif

#endif /* CTRL_BUCKBOOST_H */
