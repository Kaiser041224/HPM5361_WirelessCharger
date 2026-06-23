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
#include "app_analog_signal.h"
#include "app_gptmr.h"
#include "app_hrpwm.h"
#include "ctrl_buckboost.h"
#include "ctrl_fault.h"
#include "ctrl_lcc.h"

#include "hpm_common.h"
#include "irq_profiler.h"

#include <stddef.h>

/* ============================================================================
 * 诊断数据 — 主循环 RTT 打印用，ISR 中更新
 * ============================================================================ */

/* 放入 DLM (fast_ram.bss)，ISR 中高频写入，主循环读取，消除 flash 访问延迟 */
ATTR_PLACE_AT_FAST_RAM_BSS volatile ctrl_diag_t g_ctrl_diag;
volatile uint32_t g_isr_cycles_max = 0;
volatile uint32_t g_isr_cycles_max_voltage = 0;
volatile uint32_t g_isr_cycles_max_power = 0;

#define CTRL_FREQ_DIVIDER               2
#define APP_CONTROL_CW_TARGET_DEFAULT_W 15.0f

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
    uint32_t t0 = irq_prof_read_cycle();
    uint32_t elapsed;

    /* I_L: 每个周期读取 + 转换 + 滤波 (200kHz) */
    uint16_t raw_i_l;
    if (app_adc_get_pmt_raw(ADC_CH_I_L, &raw_i_l) != 0) {
        goto exit;
    }
    g_ctrl_diag.raw_adc.i_l = raw_i_l;
    float phys_i_l;
    app_analog_signal_convert_raw(ADC_CH_I_L, raw_i_l, &phys_i_l);
    g_ctrl_diag.raw.i_l_a = phys_i_l;
    g_ctrl_diag.filt.i_l_a = app_analog_signal_lpf_step_fast(ADC_CH_I_L, phys_i_l);

    /* V_LINK: 同周期新鲜采样 + 轻 LPF，供前馈使用 (减小前馈相位滞后) */
    uint16_t raw_v_link;
    if (app_adc_get_pmt_raw(ADC_CH_V_LINK, &raw_v_link) != 0) {
        goto exit;
    }
    g_ctrl_diag.raw_adc.v_link = raw_v_link;
    float phys_v_link;
    app_analog_signal_convert_raw(ADC_CH_V_LINK, raw_v_link, &phys_v_link);
    g_ctrl_diag.filt.v_link_fast_v = app_analog_signal_lpf_step_fast(ADC_CH_V_LINK, phys_v_link);

    /* 控制频率分频: 200kHz 采样, 100kHz PI 控制 */
    static uint8_t s_ctrl_div = 0;
    if (++s_ctrl_div >= CTRL_FREQ_DIVIDER) {
        s_ctrl_div = 0;
        float v_in = g_ctrl_diag.filt.v_in_v;
        float v_link = g_ctrl_diag.filt.v_link_fast_v;

        ctrl_buckboost_update_current(&g_buckboost, g_ctrl_diag.filt.i_l_a, v_in, v_link);

        g_ctrl_diag.duty.buckboost_a = ctrl_buckboost_get_duty_a(&g_buckboost);
        g_ctrl_diag.duty.buckboost_b = ctrl_buckboost_get_duty_b(&g_buckboost);

        app_hrpwm_set_duty_direct_dual(
            HRPWM_BUCKBOOST_A, g_ctrl_diag.duty.buckboost_a, HRPWM_BUCKBOOST_B,
            g_ctrl_diag.duty.buckboost_b);
    }

exit:;
    elapsed = irq_prof_read_cycle() - t0;
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
    if (app_adc_get_pmt_raw(ADC_CH_I_COIL, &raw_i_coil) != 0
        || app_adc_get_pmt_raw(ADC_CH_I_LF, &raw_i_lf) != 0) {
        return;
    }

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
    uint32_t t0 = irq_prof_read_cycle();
    uint32_t elapsed;

    uint16_t raw_v_link;
    if (app_adc_get_pmt_raw(ADC_CH_V_LINK, &raw_v_link) != 0) {
        goto exit;
    }
    g_ctrl_diag.raw_adc.v_link = raw_v_link;

    float phys_v_link;
    app_analog_signal_convert_raw(ADC_CH_V_LINK, raw_v_link, &phys_v_link);
    g_ctrl_diag.raw.v_link_v = phys_v_link;
    g_ctrl_diag.filt.v_link_v =
        app_analog_signal_ma_step(ADC_CH_V_LINK, g_ctrl_diag.filt.v_link_fast_v);

    /* 负载电流前馈: I_load = I_L × Dmax × (1 - g)
     * 基于四开关 Buck-Boost 拓扑平均电流关系，适用于 Buck/Boost/Buck-Boost 全工况。
     * g ∈ [0, 1]: Dmax×(1-g) 为 VLINK 侧导通时间占比。 */
    float i_l = g_ctrl_diag.filt.i_l_a;
    float g = ctrl_buckboost_get_generalized_duty(&g_buckboost);
    float d_max = ctrl_buckboost_get_duty_max(&g_buckboost);
    float i_load_ff = 0.0f;
    if (algo_flt_finite(i_l) && algo_flt_finite(g) && algo_flt_finite(d_max)) {
        float gc = (g < 0.0f) ? 0.0f : (g > 1.0f) ? 1.0f : g;
        i_load_ff = i_l * d_max * (1.0f - gc);
    }
    g_ctrl_diag.ff.i_load_est_a = i_load_ff;

    /* 电压外环 + 输出电流环 (50kHz): CV/CC 竞争 → current_ref */
    ctrl_buckboost_update_voltage(&g_buckboost, g_ctrl_diag.filt.v_link_v, i_load_ff, i_load_ff);

exit:;
    elapsed = irq_prof_read_cycle() - t0;
    if (elapsed > g_isr_cycles_max_voltage) {
        g_isr_cycles_max_voltage = elapsed;
    }
}

ATTR_RAMFUNC
static void buckboost_power_loop_isr(void) {
    uint32_t t0 = irq_prof_read_cycle();
    uint32_t elapsed;

    uint16_t raw_v_in;
    if (app_adc_get_pmt_raw(ADC_CH_V_IN, &raw_v_in) != 0) {
        goto exit;
    }
    g_ctrl_diag.raw_adc.v_in = raw_v_in;

    float phys_v_in;
    app_analog_signal_convert_raw(ADC_CH_V_IN, raw_v_in, &phys_v_in);
    g_ctrl_diag.raw.v_in_v = phys_v_in;
    g_ctrl_diag.filt.v_in_v = app_analog_signal_ma_step(ADC_CH_V_IN, phys_v_in);

    /* I_IN 均值 = I_L_filt × generalized_duty
     * 输入电流为脉冲型，单点采样无法得均值。功率外环只需稳态平均值。 */
    float i_l_filt = g_ctrl_diag.filt.i_l_a;
    float d_buck = ctrl_buckboost_get_generalized_duty(&g_buckboost);
    float i_in_est = 0.0f;
    if (algo_flt_finite(i_l_filt) && algo_flt_finite(d_buck)) {
        float d = (d_buck < 0.0f) ? 0.0f : (d_buck > 1.0f) ? 1.0f : d_buck;
        i_in_est = i_l_filt * d;
    }
    g_ctrl_diag.raw.i_in_a = i_in_est;
    g_ctrl_diag.filt.i_in_a = i_in_est;

    float p_in = g_ctrl_diag.filt.v_in_v * i_in_est;
    if (!algo_flt_finite(p_in)) {
        p_in = 0.0f;
    }

    ctrl_buckboost_update_power(&g_buckboost, p_in);

    g_ctrl_diag.ff.p_in_w = p_in;
    g_ctrl_diag.ff.p_target_w = ctrl_buckboost_get_ptarget(&g_buckboost);
    g_ctrl_diag.ff.power_pid_out = ctrl_buckboost_get_power_pid_out(&g_buckboost);

exit:;
    elapsed = irq_prof_read_cycle() - t0;
    if (elapsed > g_isr_cycles_max_power) {
        g_isr_cycles_max_power = elapsed;
    }
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
    case MODE_BUCK_CW:
        ctrl_buckboost_enter_cw_mode(&g_buckboost, APP_CONTROL_CW_TARGET_DEFAULT_W);
        break;
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
    ctrl_buckboost_enter_cw_mode(&g_buckboost, APP_CONTROL_CW_TARGET_DEFAULT_W);
    ctrl_lcc_init(&g_lcc);

    /* 2. GPTMR1 外环定时器 (通过 Platform 层)，先注册定时器回调但暂不启动 */
    app_gptmr_init();
    app_gptmr_register_callback(APP_GPTMR_CH_VOLTAGE, buckboost_voltage_loop_isr);
    app_gptmr_register_callback(APP_GPTMR_CH_POWER, buckboost_power_loop_isr);

    /* 3. ADC PMT 完成回调: 电流内环，必须早于 GPTMR 外环启动 */
    app_adc_register_pmt_callback(APP_ADC_INST_1, buckboost_current_loop_isr);
    app_adc_register_pmt_callback(APP_ADC_INST_0, lcc_current_loop_isr);

    /* 4. 外环定时器启动: PMT cache 尚未有效时 ISR 会跳过本轮 */
    app_gptmr_start_all();

    /* 5. 状态初始化 */
    s_state = SYS_INIT;
    s_mode = MODE_BUCK_CW;
    s_self_test_ok = false;
}

/* ============================================================================
 * 公开接口 — 主循环 tick (状态机 + 故障检查，不含控制计算)
 * ============================================================================ */

void app_control_tick(void) {
    uint32_t faults = ctrl_fault_check();
    if (faults != 0U) {
        app_control_emergency();
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
    case MODE_BUCK_CW:
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
