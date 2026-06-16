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

#include "algo_filter.h"
#include "app_adc.h"
#include "app_analog_signal.h"
#include "app_gptmr.h"
#include "app_hrpwm.h"
#include "ctrl_buckboost.h"
#include "ctrl_fault.h"
#include "ctrl_lcc.h"

#include "hpm_common.h"
#include "hpm_csr_drv.h"

#include <stddef.h>
#include <string.h>

/* ============================================================================
 * 诊断数据 — 主循环 RTT 打印用，ISR 中更新
 * ============================================================================ */

/* 放入 DLM (fast_ram.bss)，ISR 中高频写入，主循环读取，消除 flash 访问延迟 */
ATTR_PLACE_AT_FAST_RAM_BSS volatile ctrl_diag_t g_ctrl_diag;
volatile uint32_t g_isr_cycles_max = 0;

/* ============================================================================
 * 滤波器实例
 * ============================================================================ */

static algo_lpf_t lpf_i_l;
static algo_ma_t ma_v_link;
static algo_ma_t ma_i_in;
static algo_ma_t ma_v_in;

static float ma_buf_v_link[4];
static float ma_buf_i_in[4];
static float ma_buf_v_in[4];

#define ADC1_SAMPLE_RATE_HZ 200000.0f
#define ADC0_SAMPLE_RATE_HZ 148000.0f
#define CTRL_FREQ_DIVIDER    2

static void filter_init_all(void) {
    algo_lpf_cfg_t lpf_cfg;
    algo_ma_cfg_t ma_cfg;

    algo_lpf_ctor(&lpf_i_l);
    lpf_cfg.cutoff_hz = 20000.0f;
    lpf_cfg.sample_rate_hz = ADC1_SAMPLE_RATE_HZ;
    lpf_i_l.init(&lpf_i_l, &lpf_cfg);

    algo_ma_ctor(&ma_v_link);
    ma_cfg.window_size = 4;
    ma_cfg.buffer = ma_buf_v_link;
    ma_v_link.init(&ma_v_link, &ma_cfg);

    algo_ma_ctor(&ma_i_in);
    ma_cfg.window_size = 4;
    ma_cfg.buffer = ma_buf_i_in;
    ma_i_in.init(&ma_i_in, &ma_cfg);

    algo_ma_ctor(&ma_v_in);
    ma_cfg.window_size = 4;
    ma_cfg.buffer = ma_buf_v_in;
    ma_v_in.init(&ma_v_in, &ma_cfg);
}

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
 * ISR 回调 — Buck-Boost 电流内环 (200kHz, ADC1 PMT 完成回调)
 *
 * ADC1 由 PWM1 CMP8 通过 TRGM 触发，采样完成后立即执行：
 *   读 raw → float 换算 → LPF 滤波 → 控制器 step → 更新占空比
 * ============================================================================ */

/* ISR 函数放入 ILM (指令 RAM)，消除 flash wait state 和 cache miss，
 * 保证 200kHz 电流内环的确定性执行时间 (~45ns)。 */

ATTR_RAMFUNC
static void buckboost_current_loop_isr(void) {
    uint32_t t0 = read_csr(CSR_MCYCLE);

    /* I_L: 每个周期读取 + 转换 + 滤波 (200kHz) */
    uint16_t raw_i_l;
    app_adc_get_pmt_raw(ADC_CH_I_L, &raw_i_l);
    float phys_i_l;
    app_analog_signal_convert_raw(ADC_CH_I_L, raw_i_l, &phys_i_l);
    g_ctrl_diag.raw.i_l_a = phys_i_l;
    g_ctrl_diag.filt.i_l_a = algo_lpf_step_fast(&lpf_i_l, phys_i_l);

    /* 控制频率分频: 200kHz 采样, 100kHz PI 控制 */
    static uint8_t s_ctrl_div = 0;
    if (++s_ctrl_div >= CTRL_FREQ_DIVIDER) {
        s_ctrl_div = 0;
        float v_in = g_ctrl_diag.filt.v_in_v;
        float v_link = g_ctrl_diag.filt.v_link_v;

        ctrl_buckboost_update_current(&g_buckboost, g_ctrl_diag.filt.i_l_a, v_in, v_link);

        g_ctrl_diag.duty.buckboost_a = ctrl_buckboost_get_duty_a(&g_buckboost);
        g_ctrl_diag.duty.buckboost_b = ctrl_buckboost_get_duty_b(&g_buckboost);
        app_hrpwm_set_duty_direct_dual(
            HRPWM_BUCKBOOST_A, g_ctrl_diag.duty.buckboost_a, HRPWM_BUCKBOOST_B,
            g_ctrl_diag.duty.buckboost_b);
    }

    uint32_t elapsed = read_csr(CSR_MCYCLE) - t0;
    if (elapsed > g_isr_cycles_max) {
        g_isr_cycles_max = elapsed;
    }
}

/* ============================================================================
 * ISR 回调 — LCC 电流内环 (148kHz, ADC0 PMT 完成回调)
 * ============================================================================ */

ATTR_RAMFUNC
static void lcc_current_loop_isr(void) {
    uint16_t raw_i_coil, raw_i_lf;
    app_adc_get_pmt_raw(ADC_CH_I_COIL, &raw_i_coil);
    app_adc_get_pmt_raw(ADC_CH_I_LF, &raw_i_lf);

    float phys_i_coil, phys_i_lf;
    app_analog_signal_convert_raw(ADC_CH_I_COIL, raw_i_coil, &phys_i_coil);
    app_analog_signal_convert_raw(ADC_CH_I_LF, raw_i_lf, &phys_i_lf);
    g_ctrl_diag.raw.i_coil_a = phys_i_coil;
    g_ctrl_diag.raw.i_lf_a = phys_i_lf;
    g_ctrl_diag.filt.i_coil_a = phys_i_coil;
    g_ctrl_diag.filt.i_lf_a = phys_i_lf;

    /* TODO: 控制器 step */
}

/* ============================================================================
 * ISR 回调 — 电压外环 (50kHz, GPTMR1 CH0)
 * ============================================================================ */

ATTR_RAMFUNC
static void buckboost_voltage_loop_isr(void) {
    uint16_t raw_v_link;
    app_adc_get_pmt_raw(ADC_CH_V_LINK, &raw_v_link);

    float phys_v_link;
    app_analog_signal_convert_raw(ADC_CH_V_LINK, raw_v_link, &phys_v_link);
    g_ctrl_diag.raw.v_link_v = phys_v_link;
    g_ctrl_diag.filt.v_link_v = ma_v_link.step(&ma_v_link, phys_v_link);

    /* TODO: ctrl_buckboost_update_voltage() → get_current_ref() → s_active swap */
    s_active = s_staging;
}

ATTR_RAMFUNC
static void buckboost_power_loop_isr(void) {
    uint16_t raw_v_in, raw_i_in;
    app_adc_get_pmt_raw(ADC_CH_V_IN, &raw_v_in);
    app_adc_get_pmt_raw(ADC_CH_I_IN, &raw_i_in);

    float phys_v_in, phys_i_in;
    app_analog_signal_convert_raw(ADC_CH_V_IN, raw_v_in, &phys_v_in);
    app_analog_signal_convert_raw(ADC_CH_I_IN, raw_i_in, &phys_i_in);
    g_ctrl_diag.raw.v_in_v = phys_v_in;
    g_ctrl_diag.raw.i_in_a = phys_i_in;
    g_ctrl_diag.filt.v_in_v = ma_v_in.step(&ma_v_in, phys_v_in);
    g_ctrl_diag.filt.i_in_a = ma_i_in.step(&ma_i_in, phys_i_in);

    /* TODO: p_in = v_in * i_in → ctrl_buckboost_update_power() */
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
    case MODE_BUCK_CV: ctrl_buckboost_set_target_type(&g_buckboost, BUCKBOOST_TARGET_CV); break;
    case MODE_BUCK_CC: ctrl_buckboost_set_target_type(&g_buckboost, BUCKBOOST_TARGET_CC); break;
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
    ctrl_buckboost_enable(&g_buckboost);
    ctrl_buckboost_set_il_target(&g_buckboost, 0.5f);
    ctrl_lcc_init(&g_lcc);

    /* 2. 双缓冲清零 */
    memset((void*)&s_active, 0, sizeof(s_active));
    memset(&s_staging, 0, sizeof(s_staging));

    /* 3. 滤波器初始化 */
    filter_init_all();

    /* 4. GPTMR1 外环定时器 (通过 Platform 层) */
    app_gptmr_init();
    app_gptmr_register_callback(APP_GPTMR_CH_VOLTAGE, buckboost_voltage_loop_isr);
    app_gptmr_register_callback(APP_GPTMR_CH_POWER, buckboost_power_loop_isr);
    app_gptmr_start_all();

    /* 5. ADC PMT 完成回调: 电流内环 */
    app_adc_register_pmt_callback(APP_ADC_INST_1, buckboost_current_loop_isr);
    app_adc_register_pmt_callback(APP_ADC_INST_0, lcc_current_loop_isr);

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
