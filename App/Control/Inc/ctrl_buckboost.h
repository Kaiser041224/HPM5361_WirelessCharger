/*
 * ctrl_buckboost.h — 四开关 Buck-Boost 控制器
 *
 * 面向 PWM0 pair 0/1 的四开关拓扑，封装：
 *   - 内外双环 (I_L 电流内环 + V_OUT 电压外环)
 *   - 软启动斜坡
 *   - 模式切换 (Buck / Boost / Buck-Boost)
 *   - 占空比限幅与安全关断
 *
 * Copyright (c) 2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef CTRL_BUCKBOOST_H
#define CTRL_BUCKBOOST_H

#include "ctrl_types.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 类型定义
 * ============================================================================ */

/**
 * @brief Buck-Boost 工作模式。
 */
typedef enum {
    BB_MODE_IDLE       = 0, /**< 未激活 */
    BB_MODE_BUCK       = 1, /**< Buck 模式 (Vin > Vout) */
    BB_MODE_BOOST      = 2, /**< Boost 模式 (Vin < Vout) */
    BB_MODE_BUCKBOOST  = 3, /**< BuckBoost 过渡模式 (Vin ≈ Vout) */
} ctrl_bb_mode_t;

/**
 * @brief Buck-Boost 控制目标。
 */
typedef enum {
    BB_TARGET_CV = 0, /**< 恒压模式 (电压外环有效) */
    BB_TARGET_CC = 1, /**< 恒流模式 (仅电流内环) */
} ctrl_bb_target_t;

/**
 * @brief Buck-Boost 控制参数 (运行时可调)。
 */
typedef struct {
    ctrl_pid_params_t current_pid;  /**< 电流内环 PID */
    ctrl_pid_params_t voltage_pid;  /**< 电压外环 PID */
    float soft_start_step;          /**< 软启动每步占空比增量 */
    float i_l_limit_a;              /**< I_L 硬件限流 (A) */
    float v_out_limit_v;            /**< V_OUT 硬件限压 (V) */
} ctrl_bb_params_t;

/**
 * @brief Buck-Boost 控制器运行时状态。
 */
typedef struct {
    bool    enabled;
    bool    soft_start_active;
    float   duty;              /**< 当前输出占空比 [0, 1] */
    float   duty_target;       /**< 目标占空比 (软启动终点) */
    ctrl_bb_mode_t  mode;
    ctrl_bb_target_t target_type;
    float   v_out_target_v;    /**< 电压目标 (V) */
    float   i_l_target_a;      /**< 电流目标 (A) */
    float   current_integral;
    float   voltage_integral;
    float   last_current_error;
    float   last_voltage_error;
} ctrl_bb_state_t;

/**
 * @brief Buck-Boost 控制器对象。
 *
 * 一个实例对应一组四开关 Buck-Boost 功率级。
 */
typedef struct {
    ctrl_bb_params_t   params;
    ctrl_bb_state_t    state;
} ctrl_buckboost_t;

/* ============================================================================
 * 公开接口
 * ============================================================================ */

int  ctrl_buckboost_init(ctrl_buckboost_t *ctrl);
int  ctrl_buckboost_enable(ctrl_buckboost_t *ctrl);
void ctrl_buckboost_disable(ctrl_buckboost_t *ctrl);
void ctrl_buckboost_emergency_stop(ctrl_buckboost_t *ctrl);
void ctrl_buckboost_step(ctrl_buckboost_t *ctrl, float v_in, float v_out, float i_l);
void ctrl_buckboost_set_vout_target(ctrl_buckboost_t *ctrl, float target_v);
void ctrl_buckboost_set_il_target(ctrl_buckboost_t *ctrl, float target_a);
void ctrl_buckboost_set_target_type(ctrl_buckboost_t *ctrl, ctrl_bb_target_t target);
void ctrl_buckboost_set_params(ctrl_buckboost_t *ctrl, const ctrl_bb_params_t *params);
void ctrl_buckboost_soft_start(ctrl_buckboost_t *ctrl);

float          ctrl_buckboost_get_duty(const ctrl_buckboost_t *ctrl);
ctrl_bb_mode_t ctrl_buckboost_get_mode(const ctrl_buckboost_t *ctrl);
bool           ctrl_buckboost_is_enabled(const ctrl_buckboost_t *ctrl);

#ifdef __cplusplus
}
#endif

#endif /* CTRL_BUCKBOOST_H */
