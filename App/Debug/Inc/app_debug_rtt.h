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

/* 变频测试 */
void app_debug_pwm_test_frequency_sweep(uint8_t inst, uint32_t freq_start, uint32_t freq_end, uint32_t freq_step, uint32_t delay_ms);

/* 移相测试 */
void app_debug_pwm_test_phase_sweep(uint8_t inst, uint8_t ref_pair, uint8_t target_pair, float phase_start, float phase_end, float phase_step, uint32_t delay_ms);

/* 占空比分辨率测试 */
void app_debug_pwm_test_duty_resolution(uint8_t inst, uint8_t pair, float duty_start, float duty_end, float duty_step, uint32_t delay_ms);

/* HRPWM综合验证测试 */
void app_debug_hrpwm_run_tests(void);

/* ADC测试 */
void app_debug_adc_dump_channels(void);
void app_debug_adc_run_tests(void);

/* ADC PMT 联动测试 */
void app_debug_adc_pmt_run_tests(void);

/* CAN 收发测试 */
void app_debug_can_run_tests(void);
void app_debug_can_loopback_test(void);

#endif /* APP_DEBUG_RTT_H */
