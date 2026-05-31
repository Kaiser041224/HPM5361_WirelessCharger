/*
 * Debug RTT - SEGGER RTT wrapper for debug output
 *
 * Copyright (c) 2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef APP_DEBUG_RTT_H
#define APP_DEBUG_RTT_H

#include <stdint.h>

void app_debug_init(void);
void app_debug_write(const char *str);
int app_debug_printf(const char *fmt, ...);

/* Debug test: print HRPWM compare register snapshot */
void app_debug_dump_hrpwm_cmp(void);

/* PWM中断回调函数类型 */
typedef void (*pwm_irq_user_callback_t)(void);

/* PWM中断管理 */
void app_debug_pwm_irq_enable(uint8_t inst);
void app_debug_pwm_irq_disable(uint8_t inst);
uint32_t app_debug_pwm_irq_get_count(uint8_t inst);
void app_debug_pwm_irq_reset_count(uint8_t inst);
void app_debug_pwm_irq_dump_status(void);

/* 注册用户业务回调（在PWM中断中执行） */
int app_debug_pwm_irq_register_callback(uint8_t inst, pwm_irq_user_callback_t callback);
void app_debug_pwm_irq_unregister_callback(uint8_t inst);

#endif /* APP_DEBUG_RTT_H */
