/*
 * ctrl_lcc.h — 全桥 LCC 谐振控制器
 *
 * 面向 PWM0 pair 0/1 的全桥 LCC 拓扑，封装：
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

#define CTRL_LCC_PHASE_SAMPLE_COUNT (4U)

/* LCC 开环默认工作点：频率默认值作为跨层单一来源，消除多处写死不一致 */
#define CTRL_LCC_FREQ_DEFAULT_HZ   (114514.0f)
#define CTRL_LCC_FREQ_MIN_HZ       (100000.0f)
#define CTRL_LCC_FREQ_MAX_HZ       (220000.0f)
#define CTRL_LCC_DUTY_DEFAULT      (0.5f)
#define CTRL_LCC_PHASE_DEFAULT_DEG (180.0f)
#define CTRL_LCC_PHASE_MAX_DEG     (180.0f)
#define CTRL_LCC_I_COIL_LIMIT_A    (10.0f)

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CTRL_LCC_PHASE_SAMPLE_0_DEG = 0,
    CTRL_LCC_PHASE_SAMPLE_90_DEG = 1,
    CTRL_LCC_PHASE_SAMPLE_180_DEG = 2,
    CTRL_LCC_PHASE_SAMPLE_270_DEG = 3,
} ctrl_lcc_phase_sample_t;

typedef struct {
    float i_coil_a[CTRL_LCC_PHASE_SAMPLE_COUNT];
    float i_lf_a[CTRL_LCC_PHASE_SAMPLE_COUNT];
    uint8_t valid_mask;
    bool frame_ready;
    uint32_t frame_id;
} ctrl_lcc_phase_samples_t;

typedef struct {
    float kp;
    float ki;
    float kd;
    float integral_max;
    float output_max;
    float output_min;
} ctrl_lcc_pid_params_t;

typedef enum {
    LCC_MODE_IDLE = 0,        /**< 未激活 */
    LCC_MODE_OPEN_LOOP = 1,   /**< 开环 (固定频率/相位) */
    LCC_MODE_CLOSED_LOOP = 2, /**< 闭环电流控制 */
    LCC_MODE_PLL_TRACK = 3,   /**< PLL 谐振频率跟踪 */
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
    ctrl_lcc_pid_params_t current_pid; /**< 线圈电流 PID */
    ctrl_lcc_pll_params_t pll;         /**< PLL 参数 */
    float phase_max_deg;               /**< 移相上限 (degree) */
    float freq_min_hz;                 /**< 频率下限 (Hz) */
    float freq_max_hz;                 /**< 频率上限 (Hz) */
    float i_coil_limit_a;              /**< 线圈电流限值 (A) */
} ctrl_lcc_params_t;

/**
 * @brief LCC 控制器运行时状态。
 */
typedef struct {
    bool enabled;
    bool output_enabled;
    bool pll_locked;
    float frequency_hz;
    float phase_deg;
    float duty;
    ctrl_lcc_mode_t mode;
    float i_coil_target_a;
    float current_integral;
    float pll_integral;
    float last_current_error;
    float last_phase_error;
    uint8_t sample_phase_index;
    ctrl_lcc_phase_samples_t phase_samples;
} ctrl_lcc_state_t;

/**
 * @brief LCC 控制器对象。
 *
 * 一个实例对应一组全桥 LCC 功率级。
 */
typedef struct {
    ctrl_lcc_params_t params;
    ctrl_lcc_state_t state;
} ctrl_lcc_t;

/**
 * @brief LCC 控制器下发命令。
 *
 * step() 依据当前模式产生本结构，由 Application 层翻译成 HRPWM 硬件操作，
 * 使 Control 层不直接依赖 Driver 层接口。
 */
typedef struct {
    float frequency_hz;
    float phase_deg;
    float duty;
    bool output_enabled;
} ctrl_lcc_cmd_t;

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
int ctrl_lcc_init(ctrl_lcc_t* ctrl);

/**
 * @brief 使能功率输出。
 *
 * 先设置默认频率/相位，再启动 PWM。
 *
 * @param ctrl 控制器实例指针。
 * @return 0 成功，-1 故障条件未清除。
 */
int ctrl_lcc_enable(ctrl_lcc_t* ctrl);

/**
 * @brief 禁能功率输出。
 *
 * 将相位归零后关闭 PWM。
 *
 * @param ctrl 控制器实例指针。
 */
void ctrl_lcc_disable(ctrl_lcc_t* ctrl);

/**
 * @brief 紧急停止。
 *
 * 立即拉低 PWM。
 *
 * @param ctrl 控制器实例指针。
 */
void ctrl_lcc_emergency_stop(ctrl_lcc_t* ctrl);

/**
 * @brief 推进一次 4 点相位采样 (高频，随 ADC 采集节奏调用)。
 *
 * 将本次线圈/谐振电流样本写入当前相位槽并前进；集满 4 点标记 frame_ready。
 * 与控制解耦：应在 ADC0 采集回调中调用，使相位采样跟随采集频率。
 *
 * @param ctrl    控制器实例指针。
 * @param i_coil  线圈电流测量值 (A)。
 * @param i_lf    LCC 谐振电流测量值 (A)，用于 PLL 鉴相。
 */
void ctrl_lcc_push_sample(ctrl_lcc_t* ctrl, float i_coil, float i_lf);

/**
 * @brief 执行一次控制迭代 (低频，随控制环节奏调用)。
 *
 * 根据当前模式执行：开环保持 / PID 闭环 / PLL 频率跟踪。
 * 相位采样由 ctrl_lcc_push_sample 独立推进，本函数只消费/决策。
 *
 * @param ctrl    控制器实例指针。
 */
void ctrl_lcc_step(ctrl_lcc_t* ctrl);

/**
 * @brief 设置 LCC 工作模式。
 *
 * @param ctrl  控制器实例指针。
 * @param mode  目标模式。
 */
void ctrl_lcc_set_mode(ctrl_lcc_t* ctrl, ctrl_lcc_mode_t mode);

/**
 * @brief 设置输出频率 (开环模式直接写入；闭环模式作为初始值)。
 *
 * @param ctrl     控制器实例指针。
 * @param freq_hz  频率 (Hz)。
 */
void ctrl_lcc_set_frequency(ctrl_lcc_t* ctrl, float freq_hz);

/**
 * @brief 设置移相角。
 *
 * @param ctrl       控制器实例指针。
 * @param phase_deg  移相角 (degree)。
 */
void ctrl_lcc_set_phase(ctrl_lcc_t* ctrl, float phase_deg);

/**
 * @brief 设置线圈电流目标 (闭环模式)。
 *
 * @param ctrl      控制器实例指针。
 * @param target_a  目标电流 (A)。
 */
void ctrl_lcc_set_i_coil_target(ctrl_lcc_t* ctrl, float target_a);

/**
 * @brief 设置占空比 (受控开环阶段固定，预留变占空比能力)。
 *
 * @param ctrl  控制器实例指针。
 * @param duty  占空比 [0,1]。
 */
void ctrl_lcc_set_duty(ctrl_lcc_t* ctrl, float duty);

/**
 * @brief 读取本帧下发命令 (由 step 更新)。
 *
 * @param ctrl 控制器实例指针。
 * @param cmd  输出命令，供 Application 层翻译成硬件操作。
 */
void ctrl_lcc_get_cmd(const ctrl_lcc_t* ctrl, ctrl_lcc_cmd_t* cmd);

/**
 * @brief 更新控制器参数 (运行时可调)。
 *
 * @param ctrl    控制器实例指针。
 * @param params  新参数配置。
 */
void ctrl_lcc_set_params(ctrl_lcc_t* ctrl, const ctrl_lcc_params_t* params);

/**
 * @brief 获取当前频率。
 *
 * @param ctrl 控制器实例指针 (只读)。
 * @return 频率 (Hz)。
 */
float ctrl_lcc_get_frequency(const ctrl_lcc_t* ctrl);

/**
 * @brief 获取当前移相角。
 *
 * @param ctrl 控制器实例指针 (只读)。
 * @return 移相角 (degree)。
 */
float ctrl_lcc_get_phase(const ctrl_lcc_t* ctrl);

/**
 * @brief 查询 PLL 是否锁定。
 *
 * @param ctrl 控制器实例指针 (只读)。
 * @return true 已锁定。
 */
bool ctrl_lcc_is_pll_locked(const ctrl_lcc_t* ctrl);

/**
 * @brief 查询控制器是否已使能。
 *
 * @param ctrl 控制器实例指针 (只读)。
 * @return true 已使能。
 */
bool ctrl_lcc_is_enabled(const ctrl_lcc_t* ctrl);

int ctrl_lcc_get_phase_samples(const ctrl_lcc_t* ctrl, ctrl_lcc_phase_samples_t* samples);

/**
 * @brief 获取当前四点相位采样索引对应的 ADC0 触发位置比例。
 *
 * 供 Application 层在 PWM0 频率变化(reload 重算)后重新校准触发位置时使用，
 * 避免误设成固定值打断跨周期四点采样的相位循环。
 *
 * @param ctrl 控制器实例指针 (只读)。
 * @return position_ratio [0.0, 1.0]，ctrl 为 NULL 时返回 0.0。
 */
float ctrl_lcc_get_current_trigger_position(const ctrl_lcc_t* ctrl);

#ifdef __cplusplus
}
#endif

#endif /* CTRL_LCC_H */
