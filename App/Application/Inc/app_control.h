/*
 * app_control.h — 系统控制编排
 *
 * 合并了状态机、运行模式、功率使能/禁能和故障检查的编排逻辑。
 * 不包含具体控制算法（算法在 Control/ 层），只做"何时做什么"的决策。
 *
 * Copyright (c) 2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef APP_CONTROL_H
#define APP_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 状态与模式
 * ============================================================================ */

typedef enum {
    SYS_INIT   = 0,
    SYS_IDLE   = 1,
    SYS_RUN    = 2,
    SYS_FAULT  = 3,
} sys_state_t;

typedef enum {
    MODE_IDLE      = 0,
    MODE_BUCK_CV   = 1,
    MODE_BUCK_CC   = 2,
    MODE_LCC_OPEN  = 3,
    MODE_LCC_CLOSED = 4,
    MODE_STANDBY   = 5,
} op_mode_t;

/* ============================================================================
 * 公开接口
 * ============================================================================ */

void app_control_init(void);

/** @brief 每次主循环调用，推进状态评估、故障检查和模式逻辑。 */
void app_control_tick(void);

sys_state_t app_control_get_state(void);
op_mode_t   app_control_get_mode(void);
int         app_control_set_mode(op_mode_t mode);

/** @brief 按安全顺序使能功率输出。 */
int  app_control_power_enable(void);
/** @brief 按安全顺序关闭功率输出。 */
void app_control_power_disable(void);
/** @brief 紧急停止，立即拉低 PWM。 */
void app_control_emergency(void);

uint32_t app_control_get_faults(void);
int      app_control_clear_faults(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_CONTROL_H */
