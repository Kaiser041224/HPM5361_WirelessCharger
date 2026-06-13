/*
 * app_control.c — 系统控制编排实现
 *
 * 分层设计：
 *   - ISR 回调（电流内环 200kHz / 电压外环 50kHz）→ 调用 Control 层 step()
 *   - 主循环 tick（~1kHz）→ 状态机 + 故障检查，不包含控制计算
 *   - 双缓冲 setpoint 传递：外环写 staging → 原子 swap → 内环读 active
 *
 * Copyright (c) 2026 Alliance HardWare Team
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "app_control.h"

#include "app_adc.h"
#include "app_gptmr.h"
#include "app_hrpwm.h"
#include "ctrl_buckboost.h"
#include "ctrl_fault.h"
#include "ctrl_lcc.h"
#include "intf_hrpwm.h"

#include <stddef.h>
#include <string.h>

/* ============================================================================
 * 双缓冲 setpoint
 * ============================================================================ */

typedef struct {
    float current_ref;
    float voltage_ref;
    float power_limit;
} ctrl_setpoints_t;

static volatile ctrl_setpoints_t s_active;
static ctrl_setpoints_t s_staging;

/* ============================================================================
 * 控制器实例
 * ============================================================================ */

static ctrl_buckboost_t g_buckboost;
static ctrl_lcc_t g_lcc;

/* ============================================================================
 * 滤波器实例 (占位 — 实际部署时使用 algo_lpf_t)
 * ============================================================================ */

/* static algo_lpf_t lpf_i_l; */
/* static algo_lpf_t lpf_v_link; */

/* ============================================================================
 * ISR 回调 — 电流内环 (200kHz, PWM1 Reload ISR)
 * ============================================================================ */

static void buckboost_current_loop_isr(void) {
    /* 读取 ADC cache */
    uint16_t raw_i_l;
    app_adc_get_pmt_raw(ADC_CH_I_L, &raw_i_l);

    /* TODO: float 换算 + LPF 滤波 */
    /* float i_l = app_adc_raw_to_physical(raw_i_l); */
    /* float filtered = algo_lpf_step(&lpf_i_l, i_l); */

    /* TODO: 调用控制器 step */
    /* ctrl_buckboost_step(&g_buckboost, s_active.voltage_ref, filtered); */

    (void)raw_i_l;
}

/* ============================================================================
 * ISR 回调 — LCC 电流内环 (148kHz, PWM0 Reload ISR)
 * ============================================================================ */

static void lcc_current_loop_isr(void) {
    /* 读取 ADC cache */
    uint16_t raw_i_coil, raw_i_lf;
    app_adc_get_pmt_raw(ADC_CH_I_COIL, &raw_i_coil);
    app_adc_get_pmt_raw(ADC_CH_I_LF, &raw_i_lf);

    /* TODO: float 换算 + LPF 滤波 */
    /* float i_coil = app_adc_raw_to_physical(raw_i_coil); */
    /* float i_lf   = app_adc_raw_to_physical(raw_i_lf); */

    /* TODO: 调用控制器 step */
    /* ctrl_lcc_step(&g_lcc, i_coil, i_lf); */

    (void)raw_i_coil;
    (void)raw_i_lf;
}

/* ============================================================================
 * ISR 回调 — 电压外环 (50kHz, GPTMR1 CH0)
 * ============================================================================ */

static void voltage_loop_isr(void) {
    /* 读取 ADC cache */
    uint16_t raw_v_link;
    app_adc_get_pmt_raw(ADC_CH_V_LINK, &raw_v_link);

    /* TODO: float 换算 + LPF 滤波 */
    /* float v_link = app_adc_raw_to_physical(raw_v_link); */
    /* float filtered = algo_lpf_step(&lpf_v_link, v_link); */

    /* TODO: 电压环 PI 计算，更新 staging */
    /* float error = s_active.voltage_ref - filtered; */
    /* s_staging.current_ref += kp * (error - prev_error) + ki * error; */

    /* 原子 swap staging → active */
    s_active = s_staging;

    (void)raw_v_link;
}

/* ============================================================================
 * ISR 回调 — 功率外环 (20kHz, GPTMR1 CH1)
 * ============================================================================ */

static void power_loop_isr(void) {
    /* 读取 ADC cache */
    uint16_t raw_v_in, raw_i_in;
    app_adc_get_pmt_raw(ADC_CH_V_IN, &raw_v_in);
    app_adc_get_pmt_raw(ADC_CH_I_IN, &raw_i_in);

    /* TODO: float 换算 + 功率计算 */
    /* float v_in = app_adc_raw_to_physical(raw_v_in); */
    /* float i_in = app_adc_raw_to_physical(raw_i_in); */
    /* float p_in = v_in * i_in; */

    /* TODO: 功率环 PI 计算，更新 staging */
    /* float error = s_active.power_limit - p_in; */
    /* s_staging.voltage_ref += kp * (error - prev_error) + ki * error; */

    (void)raw_v_in;
    (void)raw_i_in;
}

/* ============================================================================
 * 自检
 * ============================================================================ */

static bool self_test(void) { return true; }

/* ============================================================================
 * 内部状态
 * ============================================================================ */

static sys_state_t s_state = SYS_INIT;
static op_mode_t s_mode = MODE_IDLE;
static bool s_self_test_ok;

/* ============================================================================
 * 模式配置
 * ============================================================================ */

static void configure_controllers_for_mode(void) {
    switch (s_mode) {
    case MODE_BUCK_CV: ctrl_buckboost_set_target_type(&g_buckboost, BB_TARGET_CV); break;
    case MODE_BUCK_CC: ctrl_buckboost_set_target_type(&g_buckboost, BB_TARGET_CC); break;
    case MODE_LCC_OPEN: ctrl_lcc_set_mode(&g_lcc, LCC_MODE_OPEN_LOOP); break;
    case MODE_LCC_CLOSED: ctrl_lcc_set_mode(&g_lcc, LCC_MODE_CLOSED_LOOP); break;
    default: break;
    }
}

/* ============================================================================
 * 公开接口 — 初始化
 * ============================================================================ */

void app_control_init(void) {
    /* 1. 控制器初始化 (不涉及硬件) */
    ctrl_fault_init(NULL);
    ctrl_buckboost_init(&g_buckboost);
    ctrl_lcc_init(&g_lcc);

    /* 2. 双缓冲清零 */
    memset((void*)&s_active, 0, sizeof(s_active));
    memset(&s_staging, 0, sizeof(s_staging));

    /* 3. GPTMR1 外环定时器 (通过 Platform 层) */
    app_gptmr_init();
    app_gptmr_register_callback(APP_GPTMR_CH_VOLTAGE, voltage_loop_isr);
    app_gptmr_register_callback(APP_GPTMR_CH_POWER, power_loop_isr);

    /* 4. PWM1 Reload ISR: 电流内环 200kHz */
    intf_hrpwm_config_reload_irq(1, buckboost_current_loop_isr);
    intf_hrpwm_enable_reload_irq(1);

    /* 5. PWM0 Reload ISR: LCC 电流内环 148kHz */
    intf_hrpwm_config_reload_irq(0, lcc_current_loop_isr);
    intf_hrpwm_enable_reload_irq(0);

    /* 6. 状态初始化 */
    s_state = SYS_INIT;
    s_mode = MODE_IDLE;
    s_self_test_ok = false;
}

/* ============================================================================
 * 公开接口 — 主循环 tick (状态机 + 故障检查，不含控制计算)
 * ============================================================================ */

void app_control_tick(void) {
    uint32_t faults = ctrl_fault_check();
    if (faults != 0U) {
        s_state = SYS_FAULT;
        app_hrpwm_emergency_stop();
        ctrl_buckboost_emergency_stop(&g_buckboost);
        ctrl_lcc_emergency_stop(&g_lcc);
        return;
    }

    switch (s_state) {
    case SYS_INIT:
        s_self_test_ok = self_test();
        if (s_self_test_ok) {
            s_state = SYS_IDLE;
        }
        break;

    case SYS_IDLE: break;

    case SYS_RUN:
        /* 控制计算已全部在 ISR 中完成，主循环不参与 */
        break;

    case SYS_FAULT: break;

    default: s_state = SYS_INIT; break;
    }
}

/* ============================================================================
 * 公开接口 — 状态查询
 * ============================================================================ */

sys_state_t app_control_get_state(void) { return s_state; }
op_mode_t app_control_get_mode(void) { return s_mode; }

/* ============================================================================
 * 公开接口 — 模式切换
 * ============================================================================ */

int app_control_set_mode(op_mode_t mode) {
    switch (mode) {
    case MODE_IDLE:
    case MODE_STANDBY:
        if (s_state == SYS_FAULT)
            return -1;
        break;
    case MODE_BUCK_CV:
    case MODE_BUCK_CC:
    case MODE_LCC_OPEN:
    case MODE_LCC_CLOSED:
        if (s_state != SYS_IDLE && s_state != SYS_RUN)
            return -1;
        break;
    default: return -1;
    }

    s_mode = mode;
    configure_controllers_for_mode();
    return 0;
}

/* ============================================================================
 * 公开接口 — 功率控制
 * ============================================================================ */

int app_control_power_enable(void) {
    if (s_state != SYS_IDLE)
        return -1;
    if (!s_self_test_ok)
        return -1;

    ctrl_buckboost_enable(&g_buckboost);
    ctrl_lcc_enable(&g_lcc);

    app_gptmr_start_all();

    s_state = SYS_RUN;
    return 0;
}

void app_control_power_disable(void) {
    app_gptmr_stop_all();

    ctrl_buckboost_disable(&g_buckboost);
    ctrl_lcc_disable(&g_lcc);

    s_state = SYS_IDLE;
}

void app_control_emergency(void) {
    app_gptmr_stop_all();

    app_hrpwm_emergency_stop();
    ctrl_buckboost_emergency_stop(&g_buckboost);
    ctrl_lcc_emergency_stop(&g_lcc);

    s_state = SYS_FAULT;
}

/* ============================================================================
 * 公开接口 — 故障管理
 * ============================================================================ */

uint32_t app_control_get_faults(void) { return ctrl_fault_get_active(); }

int app_control_clear_faults(void) {
    int ret = ctrl_fault_clear_all();
    if (ret == 0) {
        s_state = SYS_INIT;
    }
    return ret;
}
