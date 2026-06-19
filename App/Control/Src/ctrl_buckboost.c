/*
 * ctrl_buckboost.c — 四开关 Buck-Boost 控制器实现
 *
 * 控制链:
 *   PI(current_ref, i_l) → V_L_cmd (有符号平均电感电压命令)
 *   VIN/VLINK 前馈 → generalized_duty (0.0-1.0)
 *   单输入调制器 generalized_duty → DA, DB
 *
 * 调制器:
 *   DA = Dmax × generalized_duty
 *   DB = Dmax × (1 - generalized_duty)
 *
 * 理想平均电感电压:
 *   V_L = DA × VIN - DB × VLINK
 *
 * Copyright (c) 2026 Alliance HardWare Team
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "ctrl_buckboost.h"

#include <stddef.h>
#include <string.h>

#ifndef ATTR_RAMFUNC
# define ATTR_RAMFUNC __attribute__((section(".fast")))
#endif

/* 物理 PWM duty 默认范围: 最终写入 HRPWM 的 DA/DB 上限 */
#define BUCKBOOST_DUTY_MIN_DEFAULT 0.0f
#define BUCKBOOST_DUTY_MAX_DEFAULT 0.95f

/* 控制保护默认值 */
#define BUCKBOOST_I_L_LIMIT_DEFAULT       16.0f
#define BUCKBOOST_V_OUT_LIMIT_DEFAULT     48.0f
#define BUCKBOOST_BUS_SUM_MIN_V           1.0f
#define BUCKBOOST_VLINK_LIMIT_ENTER_RATIO 1.00f
#define BUCKBOOST_VLINK_LIMIT_EXIT_RATIO  0.96f

/* 电流环 PID 输出为有符号平均电感电压命令 V_L_cmd */
#define BUCKBOOST_CURRENT_PID_OUT_MIN (-48.0f)
#define BUCKBOOST_CURRENT_PID_OUT_MAX (48.0f)

/* 电压环 PID 输出为电流命令 current_ref (A)，范围 ±i_l_limit */
#define BUCKBOOST_VOLTAGE_PID_OUT_MIN (-BUCKBOOST_I_L_LIMIT_DEFAULT)
#define BUCKBOOST_VOLTAGE_PID_OUT_MAX (BUCKBOOST_I_L_LIMIT_DEFAULT)

/* generalized_duty 命令范围: 单输入调制器的最终命令钳位 */
#define BUCKBOOST_GENERALIZED_DUTY_MIN 0.0f
#define BUCKBOOST_GENERALIZED_DUTY_MAX 0.98f

static inline float clampf(float x, float lo, float hi) {
    if (x < lo)
        return lo;
    if (x > hi)
        return hi;
    return x;
}

static inline float absf_fast(float x) { return (x < 0.0f) ? -x : x; }

typedef struct {
    float d_min;
    float d_max;
    float g_min;
    float g_max;
    float inv_d_max;
} buckboost_mod_ctx_t;

typedef struct {
    buckboost_mod_ctx_t mod;
    float vin;
    float vlk;
    float inv_bus_sum;
} buckboost_current_ctx_t;

ATTR_RAMFUNC
static bool buckboost_modulation_params_valid(float d_min, float d_max) {
    return algo_pid_finite(d_min) && algo_pid_finite(d_max) && d_min >= 0.0f && d_max > 0.0f
        && d_max <= 1.0f && d_min <= (d_max * 0.5f);
}

ATTR_RAMFUNC
static bool buckboost_prepare_mod_ctx(float d_min, float d_max, buckboost_mod_ctx_t* ctx) {
    if (ctx == NULL || !buckboost_modulation_params_valid(d_min, d_max)) {
        return false;
    }

    float inv_d_max = 1.0f / d_max;
    if (!algo_pid_finite(inv_d_max)) {
        return false;
    }

    float g_min = d_min * inv_d_max;
    if (!algo_pid_finite(g_min)) {
        return false;
    }

    float g_max_by_duty_min = 1.0f - g_min;
    float g_max = clampf(BUCKBOOST_GENERALIZED_DUTY_MAX, g_min, g_max_by_duty_min);
    if (!algo_pid_finite(g_max)) {
        return false;
    }

    ctx->d_min = d_min;
    ctx->d_max = d_max;
    ctx->g_min = g_min;
    ctx->g_max = g_max;
    ctx->inv_d_max = inv_d_max;
    return true;
}

ATTR_RAMFUNC
static bool buckboost_prepare_current_ctx(
    const ctrl_buckboost_t* ctrl, float i_l, float v_in, float vlink,
    buckboost_current_ctx_t* ctx) {
    if (ctrl == NULL || ctx == NULL) {
        return false;
    }
    if (!algo_pid_finite(i_l) || !algo_pid_finite(ctrl->state.current_ref)) {
        return false;
    }
    if (!buckboost_prepare_mod_ctx(ctrl->params.duty_min, ctrl->params.duty_max, &ctx->mod)) {
        return false;
    }

    float vin = absf_fast(v_in);
    float vlk = absf_fast(vlink);
    if (!algo_pid_finite(vin) || !algo_pid_finite(vlk)) {
        return false;
    }

    float bus_sum = vin + vlk;
    if (!algo_pid_finite(bus_sum) || bus_sum <= BUCKBOOST_BUS_SUM_MIN_V) {
        return false;
    }

    float inv_bus_sum = 1.0f / bus_sum;
    if (!algo_pid_finite(inv_bus_sum)) {
        return false;
    }

    ctx->vin = vin;
    ctx->vlk = vlk;
    ctx->inv_bus_sum = inv_bus_sum;
    return true;
}

/* ============================================================================
 * 单输入调制器: generalized_duty → DA, DB
 *
 * DA = Dmax × generalized_duty
 * DB = Dmax × (1 - generalized_duty)
 * ============================================================================ */

ATTR_RAMFUNC
static bool buckboost_modulate_generalized(
    float generalized_duty, const buckboost_mod_ctx_t* ctx, float* out_da, float* out_db,
    float* out_effective_generalized_duty) {
    if (ctx == NULL || out_da == NULL || out_db == NULL) {
        return false;
    }
    if (!algo_pid_finite(generalized_duty)) {
        *out_da = 0.0f;
        *out_db = 0.0f;
        if (out_effective_generalized_duty != NULL) {
            *out_effective_generalized_duty = 0.0f;
        }
        return false;
    }

    float g = clampf(generalized_duty, ctx->g_min, ctx->g_max);
    float da = ctx->d_max * g;
    float db = ctx->d_max * (1.0f - g);

    *out_da = da;
    *out_db = db;
    if (out_effective_generalized_duty != NULL) {
        *out_effective_generalized_duty = g;
    }
    return true;
}

/* ============================================================================
 * VIN/VLINK 前馈: V_L_cmd → generalized_duty
 *
 * V_L = Dmax × ((VIN + VLINK) × g - VLINK)
 * g   = (V_L / Dmax + VLINK) / (VIN + VLINK)
 * ============================================================================ */

ATTR_RAMFUNC
static bool buckboost_vl_cmd_to_generalized_duty(
    float v_l_cmd, const buckboost_current_ctx_t* ctx, float* out_generalized_duty) {
    if (out_generalized_duty == NULL) {
        return false;
    }

    if (ctx == NULL || !algo_pid_finite(v_l_cmd)) {
        *out_generalized_duty = 0.0f;
        return false;
    }

    float v_l_per_duty = v_l_cmd * ctx->mod.inv_d_max;
    if (!algo_pid_finite(v_l_per_duty)) {
        *out_generalized_duty = 0.0f;
        return false;
    }

    float numerator = v_l_per_duty + ctx->vlk;
    if (!algo_pid_finite(numerator)) {
        *out_generalized_duty = 0.0f;
        return false;
    }

    float generalized_duty = numerator * ctx->inv_bus_sum;
    if (!algo_pid_finite(generalized_duty)) {
        *out_generalized_duty = 0.0f;
        return false;
    }

    *out_generalized_duty = clampf(generalized_duty, ctx->mod.g_min, ctx->mod.g_max);
    return true;
}

/* ============================================================================
 * VLINK 动态限幅: 仅在过压区启用，低压区释放以保持最大调制性能
 *
 * 进入: |VLINK| >= limit
 * 退出: |VLINK| <= limit * BUCKBOOST_VLINK_LIMIT_EXIT_RATIO
 *
 * 正向过压时，最大允许 g 按目标最高输出电压反推:
 *   g_limit = V_LIMIT / (VIN + V_LIMIT)
 * 这样 VLINK > V_LIMIT 时会产生抑制继续升压的调制边界。
 * ============================================================================ */

ATTR_RAMFUNC
static bool buckboost_update_vlink_limit_state(
    ctrl_buckboost_t* ctrl, float vlink_abs, float voltage_limit) {
    if (ctrl == NULL || !algo_pid_finite(vlink_abs) || !algo_pid_finite(voltage_limit)
        || voltage_limit <= 0.0f) {
        return false;
    }

    float enter = voltage_limit * BUCKBOOST_VLINK_LIMIT_ENTER_RATIO;
    float exit = voltage_limit * BUCKBOOST_VLINK_LIMIT_EXIT_RATIO;
    if (!algo_pid_finite(enter) || !algo_pid_finite(exit) || exit < 0.0f || exit > enter) {
        return false;
    }

    if (ctrl->state.vlink_limit_active) {
        if (vlink_abs <= exit) {
            ctrl->state.vlink_limit_active = false;
        }
    } else if (vlink_abs >= enter) {
        ctrl->state.vlink_limit_active = true;
    }

    return true;
}

ATTR_RAMFUNC
static bool buckboost_limit_generalized_duty_by_vlink(
    ctrl_buckboost_t* ctrl, float vlink, float voltage_limit, const buckboost_current_ctx_t* ctx,
    float* io_generalized_duty) {
    if (ctrl == NULL || ctx == NULL || io_generalized_duty == NULL
        || !algo_pid_finite(*io_generalized_duty)) {
        return false;
    }

    float limit = absf_fast(voltage_limit);
    float vlink_abs = absf_fast(vlink);
    if (!algo_pid_finite(limit) || limit <= 0.0f || !algo_pid_finite(vlink_abs)) {
        return false;
    }
    if (!buckboost_update_vlink_limit_state(ctrl, vlink_abs, limit)) {
        return false;
    }
    if (!ctrl->state.vlink_limit_active) {
        return true;
    }

    float limit_sum = ctx->vin + limit;
    if (!algo_pid_finite(limit_sum) || limit_sum <= BUCKBOOST_BUS_SUM_MIN_V) {
        return false;
    }

    float dynamic_g = limit / limit_sum;
    if (!algo_pid_finite(dynamic_g)) {
        return false;
    }
    dynamic_g = clampf(dynamic_g, ctx->mod.g_min, ctx->mod.g_max);

    if (vlink >= 0.0f) {
        if (*io_generalized_duty > dynamic_g) {
            *io_generalized_duty = dynamic_g;
        }
    } else if (*io_generalized_duty < dynamic_g) {
        *io_generalized_duty = dynamic_g;
    }

    return algo_pid_finite(*io_generalized_duty);
}

ATTR_RAMFUNC
static void buckboost_set_safe_output(ctrl_buckboost_t* ctrl) {
    if (ctrl == NULL) {
        return;
    }
    ctrl->state.v_cmd = 0.0f;
    ctrl->state.generalized_duty = 0.0f;
    ctrl->state.vlink_limit_active = false;
    ctrl->state.duty_a = 0.0f;
    ctrl->state.duty_b = 0.0f;
}

/* ============================================================================
 * 初始化 / 使能 / 禁能
 * ============================================================================ */

int ctrl_buckboost_init(ctrl_buckboost_t* ctrl) {
    if (ctrl == NULL)
        return -1;
    memset(ctrl, 0, sizeof(*ctrl));

    ctrl->params.duty_min = BUCKBOOST_DUTY_MIN_DEFAULT;
    ctrl->params.duty_max = BUCKBOOST_DUTY_MAX_DEFAULT;
    ctrl->params.i_l_limit_a = BUCKBOOST_I_L_LIMIT_DEFAULT;
    ctrl->params.v_out_limit_v = BUCKBOOST_V_OUT_LIMIT_DEFAULT;

    algo_pid_ctor(&ctrl->state.current_pid);

    algo_pid_cfg_t inductor_current_pid_cfg = {
        .mode = ALGO_PID_MODE_INCREMENTAL,
        .kp = 0.125f,
        .ki = 750.0f,
        .kd = 0.0f,
        .sample_time_s = 1.0f / 100000.0f,
        .out_min = BUCKBOOST_CURRENT_PID_OUT_MIN,
        .out_max = BUCKBOOST_CURRENT_PID_OUT_MAX,
        .integral_min = BUCKBOOST_CURRENT_PID_OUT_MIN,
        .integral_max = BUCKBOOST_CURRENT_PID_OUT_MAX,
        .antiwindup = ALGO_PID_ANTIWINDUP_CLAMP,
        .backcalc_coeff = 1.0f,
        .deriv_filter_coeff = 0.0f,
        .deriv_on_measurement = true,
        .rate_limit = 0.0f,
        .setpoint_weight_p = 1.0f,
        .setpoint_weight_d = 0.0f,
    };
    ctrl->state.current_pid.init(&ctrl->state.current_pid, &inductor_current_pid_cfg);

    algo_pid_ctor(&ctrl->state.voltage_pid);
    algo_pid_cfg_t voltage_pid_cfg = {
        .mode = ALGO_PID_MODE_POSITIONAL,
        .kp = 0.05f,
        .ki = 200.0f,
        .kd = 0.0f,
        .sample_time_s = 1.0f / 50000.0f,
        .out_min = BUCKBOOST_VOLTAGE_PID_OUT_MIN,
        .out_max = BUCKBOOST_VOLTAGE_PID_OUT_MAX,
        .integral_min = BUCKBOOST_VOLTAGE_PID_OUT_MIN,
        .integral_max = BUCKBOOST_VOLTAGE_PID_OUT_MAX,
        .antiwindup = ALGO_PID_ANTIWINDUP_CLAMP,
        .backcalc_coeff = 1.0f,
        .deriv_filter_coeff = 0.0f,
        .deriv_on_measurement = true,
        .rate_limit = 0.0f,
        .setpoint_weight_p = 1.0f,
        .setpoint_weight_d = 0.0f,
    };
    ctrl->state.voltage_pid.init(&ctrl->state.voltage_pid, &voltage_pid_cfg);

    return 0;
}

int ctrl_buckboost_enable(ctrl_buckboost_t* ctrl) {
    if (ctrl == NULL)
        return -1;
    ctrl->state.enabled = true;
    ctrl->state.v_cmd = 0.0f;
    ctrl->state.generalized_duty = 0.0f;
    ctrl->state.vlink_limit_active = false;
    ctrl->state.duty_a = 0.0f;
    ctrl->state.duty_b = 0.0f;
    ctrl->state.current_ref = 0.0f;
    if (ctrl->state.current_pid.reset) {
        ctrl->state.current_pid.reset(&ctrl->state.current_pid);
    }
    ctrl->state.voltage_pid_out = 0.0f;
    if (ctrl->state.voltage_pid.reset) {
        ctrl->state.voltage_pid.reset(&ctrl->state.voltage_pid);
    }
    return 0;
}

void ctrl_buckboost_disable(ctrl_buckboost_t* ctrl) {
    if (ctrl == NULL)
        return;
    ctrl->state.enabled = false;
    ctrl->state.v_cmd = 0.0f;
    ctrl->state.generalized_duty = 0.0f;
    ctrl->state.vlink_limit_active = false;
    ctrl->state.duty_a = 0.0f;
    ctrl->state.duty_b = 0.0f;
    ctrl->state.voltage_pid_out = 0.0f;
}

void ctrl_buckboost_emergency_stop(ctrl_buckboost_t* ctrl) {
    if (ctrl == NULL)
        return;
    ctrl->state.enabled = false;
    ctrl->state.v_cmd = 0.0f;
    ctrl->state.generalized_duty = 0.0f;
    ctrl->state.vlink_limit_active = false;
    ctrl->state.duty_a = 0.0f;
    ctrl->state.duty_b = 0.0f;
    ctrl->state.current_ref = 0.0f;
    ctrl->state.voltage_pid_out = 0.0f;
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

ATTR_RAMFUNC
void ctrl_buckboost_modulate(ctrl_buckboost_t* ctrl, float generalized_duty) {
    if (ctrl == NULL || !ctrl->state.enabled) {
        return;
    }

    float d_min = ctrl->params.duty_min;
    float d_max = ctrl->params.duty_max;
    buckboost_mod_ctx_t mod_ctx;
    if (!buckboost_prepare_mod_ctx(d_min, d_max, &mod_ctx)) {
        buckboost_set_safe_output(ctrl);
        return;
    }

    float da = 0.0f;
    float db = 0.0f;
    float g = 0.0f;
    if (!buckboost_modulate_generalized(generalized_duty, &mod_ctx, &da, &db, &g)) {
        buckboost_set_safe_output(ctrl);
        return;
    }

    ctrl->state.generalized_duty = g;
    ctrl->state.duty_a = da;
    ctrl->state.duty_b = db;
}

/* ============================================================================
 * 电流内环 update (200kHz)
 *
 * PI(current_ref, i_l) → V_L_cmd (V)
 * VIN/VLINK feedforward → generalized_duty
 * modulate(generalized_duty) → DA, DB
 * ============================================================================ */

ATTR_RAMFUNC
void ctrl_buckboost_update_current(ctrl_buckboost_t* ctrl, float i_l, float v_in, float vlink) {
    if (ctrl == NULL || !ctrl->state.enabled) {
        return;
    }

    buckboost_current_ctx_t ctx;
    if (!buckboost_prepare_current_ctx(ctrl, i_l, v_in, vlink, &ctx)) {
        buckboost_set_safe_output(ctrl);
        return;
    }

    /* 1. PID: 电流误差 → 有符号平均电感电压命令 */
    float v_l_cmd =
        ctrl->state.current_pid.step(&ctrl->state.current_pid, ctrl->state.current_ref, i_l);

    if (!algo_pid_finite(v_l_cmd)) {
        buckboost_set_safe_output(ctrl);
        return;
    }

    float voltage_limit = absf_fast(ctrl->params.v_out_limit_v);
    if (!algo_pid_finite(voltage_limit) || voltage_limit <= 0.0f) {
        voltage_limit = BUCKBOOST_V_OUT_LIMIT_DEFAULT;
    }
    v_l_cmd = clampf(v_l_cmd, -voltage_limit, voltage_limit);

    /* 2. 前馈: V_L_cmd + VIN/VLINK → generalized_duty，并按 VLINK 双向限幅 */
    float generalized_duty = 0.0f;
    if (!buckboost_vl_cmd_to_generalized_duty(v_l_cmd, &ctx, &generalized_duty)) {
        buckboost_set_safe_output(ctrl);
        return;
    }
    if (!buckboost_limit_generalized_duty_by_vlink(
            ctrl, vlink, voltage_limit, &ctx, &generalized_duty)) {
        buckboost_set_safe_output(ctrl);
        return;
    }

    /* 3. 调制器: generalized_duty → DA/DB */
    float da = 0.0f;
    float db = 0.0f;
    float effective_generalized_duty = 0.0f;
    if (!buckboost_modulate_generalized(
            generalized_duty, &ctx.mod, &da, &db, &effective_generalized_duty)) {
        buckboost_set_safe_output(ctrl);
        return;
    }

    ctrl->state.v_cmd = v_l_cmd;
    ctrl->state.generalized_duty = effective_generalized_duty;
    ctrl->state.duty_a = da;
    ctrl->state.duty_b = db;
}

/* ============================================================================
 * 电压外环 update (50kHz)
 * ============================================================================ */

void ctrl_buckboost_update_voltage(ctrl_buckboost_t* ctrl, float vlink) {
    if (ctrl == NULL || !ctrl->state.enabled) {
        return;
    }
    if (!algo_pid_finite(vlink) || !algo_pid_finite(ctrl->state.v_out_target_v)) {
        return;
    }
    /* 电压外环 PI: v_out_target → current_ref 命令。
     * 结果暂存 voltage_pid_out，不传递给电流内环 current_ref (级联传递待下一阶段)。*/
    float i_ref_cmd =
        ctrl->state.voltage_pid.step(&ctrl->state.voltage_pid, ctrl->state.v_out_target_v, vlink);
    ctrl->state.voltage_pid_out = algo_pid_finite(i_ref_cmd) ? i_ref_cmd : 0.0f;
}

/* ============================================================================
 * 功率外环 update (20kHz)
 * ============================================================================ */

void ctrl_buckboost_update_power(ctrl_buckboost_t* ctrl, float p_in) {
    if (ctrl == NULL || !ctrl->state.enabled) {
        return;
    }
    (void)p_in;
}

/* ============================================================================
 * 参数配置
 * ============================================================================ */

void ctrl_buckboost_set_vout_target(ctrl_buckboost_t* ctrl, float target_v) {
    if (ctrl != NULL)
        ctrl->state.v_out_target_v = target_v;
}

void ctrl_buckboost_set_il_target(ctrl_buckboost_t* ctrl, float target_a) {
    if (ctrl == NULL)
        return;
    if (!algo_pid_finite(target_a)) {
        ctrl->state.current_ref = 0.0f;
        ctrl->state.i_l_target_a = 0.0f;
        if (ctrl->state.current_pid.reset) {
            ctrl->state.current_pid.reset(&ctrl->state.current_pid);
        }
        buckboost_set_safe_output(ctrl);
        return;
    }
    float i_l_limit = absf_fast(ctrl->params.i_l_limit_a);
    if (!algo_pid_finite(i_l_limit) || i_l_limit <= 0.0f) {
        i_l_limit = BUCKBOOST_I_L_LIMIT_DEFAULT;
    }
    /* 不 reset 内环 PID：级联时外环周期更新 ref，reset 会破坏积分连续性 */
    ctrl->state.current_ref = clampf(target_a, -i_l_limit, i_l_limit);
    ctrl->state.i_l_target_a = target_a;
}

void ctrl_buckboost_set_target_type(ctrl_buckboost_t* ctrl, ctrl_buckboost_target_t target) {
    if (ctrl != NULL)
        ctrl->state.target_type = target;
}

void ctrl_buckboost_set_params(ctrl_buckboost_t* ctrl, const ctrl_buckboost_params_t* params) {
    if (ctrl == NULL || params == NULL)
        return;
    ctrl->params = *params;

    if (ctrl->state.current_pid.set_gains) {
        ctrl->state.current_pid.set_gains(
            &ctrl->state.current_pid, params->current_pid.kp, params->current_pid.ki,
            params->current_pid.kd);
    }
    if (ctrl->state.voltage_pid.set_gains) {
        ctrl->state.voltage_pid.set_gains(
            &ctrl->state.voltage_pid, params->voltage_pid.kp, params->voltage_pid.ki,
            params->voltage_pid.kd);
        float i_limit = (algo_pid_finite(params->i_l_limit_a) && params->i_l_limit_a > 0.0f)
                            ? params->i_l_limit_a : BUCKBOOST_I_L_LIMIT_DEFAULT;
        ctrl->state.voltage_pid._out_min = -i_limit;
        ctrl->state.voltage_pid._out_max =  i_limit;
        ctrl->state.voltage_pid._integral_min = -i_limit;
        ctrl->state.voltage_pid._integral_max =  i_limit;
    }
}

/* ============================================================================
 * 输出读取
 * ============================================================================ */

float ctrl_buckboost_get_duty_a(const ctrl_buckboost_t* ctrl) {
    return (ctrl != NULL) ? ctrl->state.duty_a : 0.0f;
}

float ctrl_buckboost_get_duty_b(const ctrl_buckboost_t* ctrl) {
    return (ctrl != NULL) ? ctrl->state.duty_b : 0.0f;
}

float ctrl_buckboost_get_v_cmd(const ctrl_buckboost_t* ctrl) {
    return (ctrl != NULL) ? ctrl->state.v_cmd : 0.0f;
}

float ctrl_buckboost_get_generalized_duty(const ctrl_buckboost_t* ctrl) {
    return (ctrl != NULL) ? ctrl->state.generalized_duty : 0.0f;
}

float ctrl_buckboost_get_duty_max(const ctrl_buckboost_t* ctrl) {
    return (ctrl != NULL) ? ctrl->params.duty_max : 0.0f;
}

float ctrl_buckboost_get_current_ref(const ctrl_buckboost_t* ctrl) {
    return (ctrl != NULL) ? ctrl->state.current_ref : 0.0f;
}

bool ctrl_buckboost_is_enabled(const ctrl_buckboost_t* ctrl) {
    return (ctrl != NULL) ? ctrl->state.enabled : false;
}
