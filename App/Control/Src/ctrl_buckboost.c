/*
 * ctrl_buckboost.c — 四开关 Buck-Boost 控制器实现
 *
 * 调制器: V_cmd → DA, DB
 *   DA = Dmax × V_cmd / (VIN + V_cmd)
 *   DB = Dmax × VIN / (VIN + V_cmd)
 *   稳态: VLINK = VIN × DA/DB = V_cmd
 *
 * Copyright (c) 2026 Alliance HardWare Team
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ctrl_buckboost.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define BUCKBOOST_DUTY_MIN_DEFAULT      0.0f
#define BUCKBOOST_DUTY_MAX_DEFAULT      0.95f
#define BUCKBOOST_I_L_LIMIT_DEFAULT     16.0f
#define BUCKBOOST_V_OUT_LIMIT_DEFAULT   48.0f
#define BUCKBOOST_CROSSOVER_BAND_DEFAULT 0.05f
#define BUCKBOOST_CURRENT_PID_OUT_MIN   (-60.0f)
#define BUCKBOOST_CURRENT_PID_OUT_MAX   ( 60.0f)
#define BUCKBOOST_V_CMD_MIN             0.0f

static inline float clampf(float x, float lo, float hi)
{
    if (x < lo) return lo;
    if (x > hi) return hi;
    return x;
}

static inline bool isfinite_f(float x)
{
    union { float f; uint32_t u; } v;
    v.f = x;
    return (v.u & 0x7F800000u) != 0x7F800000u;
}

/* ============================================================================
 * 调制器: V_cmd → DA, DB
 *
 * DA = Dmax × V_cmd / (VIN + V_cmd)
 * DB = Dmax × VIN / (VIN + V_cmd)
 *
 * 稳态: VLINK = VIN × DA / DB = V_cmd
 * ============================================================================ */

static void buckboost_modulate(
    float v_cmd, float v_in, float d_min, float d_max,
    float *out_da, float *out_db)
{
    float v_sum = v_in + v_cmd;
    float da = d_max * v_cmd / v_sum;
    float db = d_max * v_in / v_sum;

    *out_da = clampf(da, d_min, d_max);
    *out_db = clampf(db, d_min, d_max);
}

/* ============================================================================
 * 诊断区域判定
 * ============================================================================ */

static ctrl_buckboost_region_t buckboost_detect_region(float v_cmd, float v_in, float band)
{
    float v_sum = v_in + v_cmd;
    if (v_sum <= 0.0f) return BUCKBOOST_REGION_CROSSOVER;
    float ratio = v_cmd / v_sum;
    if (ratio < 0.5f - band) return BUCKBOOST_REGION_BUCK;
    if (ratio > 0.5f + band) return BUCKBOOST_REGION_BOOST;
    return BUCKBOOST_REGION_CROSSOVER;
}

/* ============================================================================
 * 初始化 / 使能 / 禁能
 * ============================================================================ */

int ctrl_buckboost_init(ctrl_buckboost_t *ctrl)
{
    if (ctrl == NULL) return -1;
    memset(ctrl, 0, sizeof(*ctrl));

    ctrl->params.duty_min = BUCKBOOST_DUTY_MIN_DEFAULT;
    ctrl->params.duty_max = BUCKBOOST_DUTY_MAX_DEFAULT;
    ctrl->params.i_l_limit_a = BUCKBOOST_I_L_LIMIT_DEFAULT;
    ctrl->params.v_out_limit_v = BUCKBOOST_V_OUT_LIMIT_DEFAULT;
    ctrl->params.crossover_band = BUCKBOOST_CROSSOVER_BAND_DEFAULT;

    algo_pid_ctor(&ctrl->state.current_pid);

    algo_pid_cfg_t pid_cfg = {
        .mode                 = ALGO_PID_MODE_INCREMENTAL,
        .kp                   = 0.0f,
        .ki                   = 0.0f,
        .kd                   = 0.0f,
        .sample_time_s        = 1.0f / 200000.0f,
        .out_min              = BUCKBOOST_CURRENT_PID_OUT_MIN,
        .out_max              = BUCKBOOST_CURRENT_PID_OUT_MAX,
        .integral_min         = BUCKBOOST_CURRENT_PID_OUT_MIN,
        .integral_max         = BUCKBOOST_CURRENT_PID_OUT_MAX,
        .antiwindup           = ALGO_PID_ANTIWINDUP_CLAMP,
        .backcalc_coeff       = 1.0f,
        .deriv_filter_coeff   = 0.0f,
        .deriv_on_measurement = true,
        .rate_limit           = 0.0f,
        .setpoint_weight_p    = 1.0f,
        .setpoint_weight_d    = 0.0f,
    };
    ctrl->state.current_pid.init(&ctrl->state.current_pid, &pid_cfg);

    return 0;
}

int ctrl_buckboost_enable(ctrl_buckboost_t *ctrl)
{
    if (ctrl == NULL) return -1;
    ctrl->state.enabled = true;
    ctrl->state.v_cmd = 0.0f;
    ctrl->state.duty_a = 0.0f;
    ctrl->state.duty_b = 0.0f;
    ctrl->state.current_ref = 0.0f;
    ctrl->state.region = BUCKBOOST_REGION_CROSSOVER;
    if (ctrl->state.current_pid.reset) {
        ctrl->state.current_pid.reset(&ctrl->state.current_pid);
    }
    return 0;
}

void ctrl_buckboost_disable(ctrl_buckboost_t *ctrl)
{
    if (ctrl == NULL) return;
    ctrl->state.enabled = false;
    ctrl->state.v_cmd = 0.0f;
    ctrl->state.duty_a = 0.0f;
    ctrl->state.duty_b = 0.0f;
}

void ctrl_buckboost_emergency_stop(ctrl_buckboost_t *ctrl)
{
    if (ctrl == NULL) return;
    ctrl->state.enabled = false;
    ctrl->state.v_cmd = 0.0f;
    ctrl->state.duty_a = 0.0f;
    ctrl->state.duty_b = 0.0f;
    ctrl->state.current_ref = 0.0f;
    if (ctrl->state.current_pid.reset) {
        ctrl->state.current_pid.reset(&ctrl->state.current_pid);
    }
    if (ctrl->state.voltage_pid.reset) {
        ctrl->state.voltage_pid.reset(&ctrl->state.voltage_pid);
    }
}

/* ============================================================================
 * 调制器接口
 * ============================================================================ */

void ctrl_buckboost_modulate(ctrl_buckboost_t *ctrl,
                             float v_cmd, float v_in, float vlink)
{
    if (ctrl == NULL || !ctrl->state.enabled) {
        return;
    }
    if (!isfinite_f(v_cmd) || !isfinite_f(v_in) || !isfinite_f(vlink)
        || v_in <= 0.0f || v_cmd <= BUCKBOOST_V_CMD_MIN) {
        return;
    }

    float d_min = ctrl->params.duty_min;
    float d_max = ctrl->params.duty_max;

    float da, db;
    buckboost_modulate(v_cmd, v_in, d_min, d_max, &da, &db);

    ctrl->state.v_cmd = v_cmd;
    ctrl->state.duty_a = da;
    ctrl->state.duty_b = db;
    ctrl->state.region = buckboost_detect_region(v_cmd, v_in,
                                                 ctrl->params.crossover_band);
}

/* ============================================================================
 * 电流内环 update (200kHz)
 *
 * PI(current_ref, i_l) → u_pi (V)
 * u_pi + u_model_ff → v_cmd
 * modulate(v_cmd, VIN, VLINK) → DA, DB
 * ============================================================================ */

void ctrl_buckboost_update_current(ctrl_buckboost_t *ctrl,
                                   float i_l, float v_in, float vlink)
{
    if (ctrl == NULL || !ctrl->state.enabled) {
        return;
    }

    float u_pi = ctrl->state.current_pid.step(
        &ctrl->state.current_pid, ctrl->state.current_ref, i_l);

    float u_model_ff = 0.0f;
    float v_cmd = u_pi + u_model_ff;

    ctrl_buckboost_modulate(ctrl, v_cmd, v_in, vlink);
}

/* ============================================================================
 * 电压外环 update (50kHz)
 * ============================================================================ */

void ctrl_buckboost_update_voltage(ctrl_buckboost_t *ctrl, float vlink)
{
    if (ctrl == NULL || !ctrl->state.enabled) {
        return;
    }
    (void)vlink;
}

/* ============================================================================
 * 功率外环 update (20kHz)
 * ============================================================================ */

void ctrl_buckboost_update_power(ctrl_buckboost_t *ctrl, float p_in)
{
    if (ctrl == NULL || !ctrl->state.enabled) {
        return;
    }
    (void)p_in;
}

/* ============================================================================
 * 参数配置
 * ============================================================================ */

void ctrl_buckboost_set_vout_target(ctrl_buckboost_t *ctrl, float target_v)
{
    if (ctrl != NULL) ctrl->state.v_out_target_v = target_v;
}

void ctrl_buckboost_set_il_target(ctrl_buckboost_t *ctrl, float target_a)
{
    if (ctrl == NULL) return;
    ctrl->state.i_l_target_a = target_a;
    ctrl->state.current_ref = clampf(target_a,
                                     -ctrl->params.i_l_limit_a,
                                      ctrl->params.i_l_limit_a);
}

void ctrl_buckboost_set_target_type(ctrl_buckboost_t *ctrl, ctrl_buckboost_target_t target)
{
    if (ctrl != NULL) ctrl->state.target_type = target;
}

void ctrl_buckboost_set_params(ctrl_buckboost_t *ctrl, const ctrl_buckboost_params_t *params)
{
    if (ctrl == NULL || params == NULL) return;
    ctrl->params = *params;

    if (ctrl->state.current_pid.set_gains) {
        ctrl->state.current_pid.set_gains(
            &ctrl->state.current_pid,
            params->current_pid.kp,
            params->current_pid.ki,
            params->current_pid.kd);
    }
}

/* ============================================================================
 * 输出读取
 * ============================================================================ */

float ctrl_buckboost_get_duty_a(const ctrl_buckboost_t *ctrl)
{
    return (ctrl != NULL) ? ctrl->state.duty_a : 0.0f;
}

float ctrl_buckboost_get_duty_b(const ctrl_buckboost_t *ctrl)
{
    return (ctrl != NULL) ? ctrl->state.duty_b : 0.0f;
}

float ctrl_buckboost_get_v_cmd(const ctrl_buckboost_t *ctrl)
{
    return (ctrl != NULL) ? ctrl->state.v_cmd : 0.0f;
}

float ctrl_buckboost_get_duty_max(const ctrl_buckboost_t *ctrl)
{
    return (ctrl != NULL) ? ctrl->params.duty_max : 0.0f;
}

float ctrl_buckboost_get_current_ref(const ctrl_buckboost_t *ctrl)
{
    return (ctrl != NULL) ? ctrl->state.current_ref : 0.0f;
}

ctrl_buckboost_region_t ctrl_buckboost_get_region(const ctrl_buckboost_t *ctrl)
{
    return (ctrl != NULL) ? ctrl->state.region : BUCKBOOST_REGION_CROSSOVER;
}

bool ctrl_buckboost_is_enabled(const ctrl_buckboost_t *ctrl)
{
    return (ctrl != NULL) ? ctrl->state.enabled : false;
}
