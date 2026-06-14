/*
 * ctrl_lcc.h — 全桥 LCC 谐振控制器
 *
 * 面向 PWM0 pair 2/3 的全桥 LCC 拓扑，封装：
 *   - 频率控制 (调频调压)
 *   - 移相控制 (调功)
 *   - PLL 谐振频率跟踪
 *   - 线圈电流闭环
 *   - 频率/相位限幅与安全关断
 *
 * Copyright (c) 2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef CTRL_LCC_H
#define CTRL_LCC_H

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
} ctrl_lcc_pid_params_t;

typedef enum {
    LCC_MODE_IDLE        = 0, /**< 未激活 */
    LCC_MODE_OPEN_LOOP   = 1, /**< 开环 (固定频率/相位) */
    LCC_MODE_CLOSED_LOOP = 2, /**< 闭环电流控制 */
    LCC_MODE_PLL_TRACK   = 3, /**< PLL 谐振频率跟踪 */
} ctrl_lcc_mode_t;

/**
 * @brief LCC PLL 参数。
 */
typedef struct {
    float kp;
    float ki;
    float integral_max;
    float freq_min_hz;
    float freq_max_hz;
} ctrl_lcc_pll_params_t;

/**
 * @brief LCC 控制参数 (运行时可调)。
 */
typedef struct {
    ctrl_lcc_pid_params_t  current_pid;  /**< 线圈电流 PID */
    ctrl_lcc_pll_params_t  pll;          /**< PLL 参数 */
    float phase_max_deg;                 /**< 移相上限 (degree) */
    float freq_min_hz;                   /**< 频率下限 (Hz) */
    float freq_max_hz;                   /**< 频率上限 (Hz) */
    float i_coil_limit_a;                /**< 线圈电流限值 (A) */
} ctrl_lcc_params_t;

/**
 * @brief LCC 控制器运行时状态。
 */
typedef struct {
    bool    enabled;
    bool    pll_locked;
    float   frequency_hz;
    float   phase_deg;
    ctrl_lcc_mode_t mode;
    float   i_coil_target_a;
    float   current_integral;
    float   pll_integral;
    float   last_current_error;
    float   last_phase_error;
} ctrl_lcc_state_t;

/**
 * @brief LCC 控制器对象。
 *
 * 一个实例对应一组全桥 LCC 功率级。
 */
typedef struct {
    ctrl_lcc_params_t   params;
    ctrl_lcc_state_t    state;
} ctrl_lcc_t;

/* ============================================================================
 * 公开接口
 * ============================================================================ */

/**
 * @brief 初始化 LCC 控制器。
 *
 * @param ctrl  控制器实例指针。
 * @param hw    硬件映射配置。
 * @return 0 成功。
 */
int ctrl_lcc_init(ctrl_lcc_t *ctrl);

/**
 * @brief 使能功率输出。
 *
 * 先设置默认频率/相位，再启动 PWM。
 *
 * @param ctrl 控制器实例指针。
 * @return 0 成功，-1 故障条件未清除。
 */
int ctrl_lcc_enable(ctrl_lcc_t *ctrl);

/**
 * @brief 禁能功率输出。
 *
 * 将相位归零后关闭 PWM。
 *
 * @param ctrl 控制器实例指针。
 */
void ctrl_lcc_disable(ctrl_lcc_t *ctrl);

/**
 * @brief 紧急停止。
 *
 * 立即拉低 PWM。
 *
 * @param ctrl 控制器实例指针。
 */
void ctrl_lcc_emergency_stop(ctrl_lcc_t *ctrl);

/**
 * @brief 执行一次控制迭代。
 *
 * 应由 fast/medium 任务调用。
 * 根据当前模式执行：开环保持 / PID 闭环 / PLL 频率跟踪。
 *
 * @param ctrl    控制器实例指针。
 * @param i_coil  线圈电流测量值 (A)。
 * @param i_lf    LCC 谐振电流测量值 (A)，用于 PLL 鉴相。
 */
void ctrl_lcc_step(ctrl_lcc_t *ctrl, float i_coil, float i_lf);

/**
 * @brief 设置 LCC 工作模式。
 *
 * @param ctrl  控制器实例指针。
 * @param mode  目标模式。
 */
void ctrl_lcc_set_mode(ctrl_lcc_t *ctrl, ctrl_lcc_mode_t mode);

/**
 * @brief 设置输出频率 (开环模式直接写入；闭环模式作为初始值)。
 *
 * @param ctrl     控制器实例指针。
 * @param freq_hz  频率 (Hz)。
 */
void ctrl_lcc_set_frequency(ctrl_lcc_t *ctrl, float freq_hz);

/**
 * @brief 设置移相角。
 *
 * @param ctrl       控制器实例指针。
 * @param phase_deg  移相角 (degree)。
 */
void ctrl_lcc_set_phase(ctrl_lcc_t *ctrl, float phase_deg);

/**
 * @brief 设置线圈电流目标 (闭环模式)。
 *
 * @param ctrl      控制器实例指针。
 * @param target_a  目标电流 (A)。
 */
void ctrl_lcc_set_i_coil_target(ctrl_lcc_t *ctrl, float target_a);

/**
 * @brief 更新控制器参数 (运行时可调)。
 *
 * @param ctrl    控制器实例指针。
 * @param params  新参数配置。
 */
void ctrl_lcc_set_params(ctrl_lcc_t *ctrl, const ctrl_lcc_params_t *params);

/**
 * @brief 获取当前频率。
 *
 * @param ctrl 控制器实例指针 (只读)。
 * @return 频率 (Hz)。
 */
float ctrl_lcc_get_frequency(const ctrl_lcc_t *ctrl);

/**
 * @brief 获取当前移相角。
 *
 * @param ctrl 控制器实例指针 (只读)。
 * @return 移相角 (degree)。
 */
float ctrl_lcc_get_phase(const ctrl_lcc_t *ctrl);

/**
 * @brief 查询 PLL 是否锁定。
 *
 * @param ctrl 控制器实例指针 (只读)。
 * @return true 已锁定。
 */
bool ctrl_lcc_is_pll_locked(const ctrl_lcc_t *ctrl);

/**
 * @brief 查询控制器是否已使能。
 *
 * @param ctrl 控制器实例指针 (只读)。
 * @return true 已使能。
 */
bool ctrl_lcc_is_enabled(const ctrl_lcc_t *ctrl);

#ifdef __cplusplus
}
#endif

#endif /* CTRL_LCC_H */
