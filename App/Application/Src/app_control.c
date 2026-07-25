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

#include <stddef.h>

/* ============================================================================
 * 诊断数据 — 主循环 RTT 打印用，ISR 中更新
 * ============================================================================ */

/* 放入 DLM (fast_ram.bss)，ISR 中高频写入，主循环读取，消除 flash 访问延迟 */
ATTR_PLACE_AT_FAST_RAM_BSS volatile ctrl_diag_t g_ctrl_diag;

#define CTRL_FREQ_DIVIDER 2
/* ============================================================================
 * 控制器实例
 * ============================================================================ */

ATTR_PLACE_AT_FAST_RAM_BSS static ctrl_buckboost_t g_buckboost;
ATTR_PLACE_AT_FAST_RAM_BSS static ctrl_lcc_t g_lcc;
ATTR_PLACE_AT_FAST_RAM_BSS static volatile uint32_t s_buckboost_vlink_sample_seq;
ATTR_PLACE_AT_FAST_RAM_BSS static uint32_t s_buckboost_vlink_consumed_seq;
ATTR_PLACE_AT_FAST_RAM_BSS static uint32_t s_lcc_last_freq_hz;

static inline void buckboost_vlink_sample_reset(void) {
    s_buckboost_vlink_sample_seq = 0U;
    s_buckboost_vlink_consumed_seq = 0U;
}

/* ============================================================================
 * ISR 回调 — Buck-Boost 采样/电流内环 (200kHz ADC1 PMT, 100kHz 电流 PI)
 *
 * ADC1 由 PWM1 CMP10 通过 TRGM 触发，采样完成后立即执行：
 *   读 raw → float 换算 → LPF 滤波；每 CTRL_FREQ_DIVIDER 次执行控制器 step
 * ============================================================================ */

/* ISR 函数放入 ILM (指令 RAM)，消除 flash wait state 和 cache miss，
 * 保证 ADC1 PMT 快路径的确定性。 */

ATTR_RAMFUNC
static void buckboost_current_loop_isr(void) {
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
    g_ctrl_diag.raw.v_link_v = phys_v_link;
    g_ctrl_diag.filt.v_link_fast_v = app_analog_signal_lpf_step_fast(ADC_CH_V_LINK, phys_v_link);
    s_buckboost_vlink_sample_seq++;

    /* 控制频率分频: 200kHz 采样, 100kHz PI 控制 */
    ATTR_PLACE_AT_FAST_RAM_BSS static uint8_t s_ctrl_div = 0;
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
}

/* ============================================================================
 * ISR 回调 — LCC 电流采集 (ADC0 PMT 完成回调，只采样滤波，不做控制)
 *
 * 控制计算已解耦到 10kHz 的 lcc_control_loop_isr (GPTMR1 CH2)，本回调只负责
 * 高频采集：读 raw → float 换算 → LPF 滤波，结果存入 g_ctrl_diag 供控制环读取。
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
    g_ctrl_diag.filt.i_coil_a = app_analog_signal_lpf_step_fast(ADC_CH_I_COIL, phys_i_coil);
    g_ctrl_diag.filt.i_lf_a = app_analog_signal_lpf_step_fast(ADC_CH_I_LF, phys_i_lf);

    /* 4 点相位采样跟随 ADC0 采集节奏推进 (控制决策在 10kHz lcc_control_loop_isr) */
    ctrl_lcc_push_sample(&g_lcc, phys_i_coil, phys_i_lf);
}

/* ============================================================================
 * ISR 回调 — LCC 控制环 (10kHz, GPTMR1 CH2)
 *
 * 与 ADC0 高频采集解耦：读取采集环滤波好的电流值 → ctrl_lcc_step → 下发 PWM0
 * 命令。低频控制使 ADC0 快路径只做采集，大幅降低单次 ISR 耗时与 CPU 占用。
 * ============================================================================ */

ATTR_RAMFUNC
static void lcc_control_loop_isr(void) {
    ctrl_lcc_step(&g_lcc);

    ctrl_lcc_cmd_t lcc_cmd;
    ctrl_lcc_get_cmd(&g_lcc, &lcc_cmd);

    /* 频率变化时才下发：hrpwm_apply_frequency 内部会重算 reload 并用驱动侧
     * 已保存的占空比/相位重新应用，开销比单纯改占空比/相位大很多。 */
    uint32_t freq_hz = (uint32_t)lcc_cmd.frequency_hz;
    if (freq_hz != s_lcc_last_freq_hz) {
        s_lcc_last_freq_hz = freq_hz;
        app_hrpwm_set_frequency(HRPWM_INST_LCC, freq_hz);
        /* reload 随频率重算后，之前设置的绝对 CMP 值已失效，需按当前四点
         * 相位循环所在的位置重新校准，不能固定成某个相位，否则会打断
         * ctrl_lcc_push_sample 里跨周期推进的相位序列 (0°→90°→180°→270°)。 */
        app_adc_set_pmt_trigger_position(
            APP_ADC_INST_0, ctrl_lcc_get_current_trigger_position(&g_lcc));
    }

    /* 占空比/相位每次都下发：手动赋值的可控开环，命令值随时可能被上层改变，
     * 寄存器写开销很轻，与 buckboost 侧每次 update_current 后立即下发的模式一致。 */
    app_hrpwm_set_duty(HRPWM_LCC_A, lcc_cmd.duty);
    app_hrpwm_set_duty(HRPWM_LCC_B, lcc_cmd.duty);
    app_hrpwm_set_phase(HRPWM_INST_LCC, HRPWM_LCC_A, HRPWM_LCC_B, lcc_cmd.phase_deg);
}

/* ============================================================================
 * ISR 回调 — 电压外环 (50kHz, GPTMR1 CH0)
 * ============================================================================ */

ATTR_RAMFUNC
static void buckboost_voltage_loop_isr(void) {
    /* V_LINK 采样已在 200kHz 电流环中完成；电压环只对 fast 值做 MA 平滑。
     * sample_seq 同时保留启动阶段 PMT cache 未就绪时跳过外环的行为，并避免
     * ADC/PMT 停滞后继续消费同一份旧样本。 */
    uint32_t vlink_sample_seq = s_buckboost_vlink_sample_seq;
    if (vlink_sample_seq == 0U || vlink_sample_seq == s_buckboost_vlink_consumed_seq) {
        goto exit;
    }
    s_buckboost_vlink_consumed_seq = vlink_sample_seq;

    float v_link_fb = app_analog_signal_ma_step(ADC_CH_V_LINK, g_ctrl_diag.filt.v_link_fast_v);
    if (!algo_flt_finite(v_link_fb)) {
        goto exit;
    }
    g_ctrl_diag.filt.v_link_v = v_link_fb;

    /* 负载电流前馈: I_load = I_L × Dmax × (1 - g)
     * 基于四开关 Buck-Boost 拓扑平均电流关系，适用于 Buck/Boost/Buck-Boost 全工况。
     * g ∈ [0, 1]: Dmax×(1-g) 为 VLINK 侧导通时间占比。 */
    float i_l = g_ctrl_diag.filt.i_l_a;
    ctrl_buckboost_t* buckboost = &g_buckboost;
    float g = buckboost->state.generalized_duty;
    float d_max = buckboost->params.duty_max;
    float i_load_ff = 0.0f;
    if (algo_flt_finite(i_l) && algo_flt_finite(g) && algo_flt_finite(d_max)) {
        float gc = (g < 0.0f) ? 0.0f : (g > 1.0f) ? 1.0f : g;
        i_load_ff = i_l * d_max * (1.0f - gc);
    }
    g_ctrl_diag.ff.i_load_est_a = i_load_ff;

    /* 电压外环 + 输出电流环 (50kHz): CV/CC 竞争 → current_ref */
    ctrl_buckboost_update_voltage(buckboost, v_link_fb, i_load_ff, i_load_ff);

exit:;
}

ATTR_RAMFUNC
static void buckboost_power_loop_isr(void) {
    uint16_t raw_v_in, raw_i_in;
    if (app_adc_get_pmt_raw(ADC_CH_V_IN, &raw_v_in) != 0
        || app_adc_get_pmt_raw(ADC_CH_I_IN, &raw_i_in) != 0) {
        goto exit;
    }
    g_ctrl_diag.raw_adc.v_in = raw_v_in;
    g_ctrl_diag.raw_adc.i_in = raw_i_in;

    float phys_v_in, phys_i_in;
    app_analog_signal_convert_raw(ADC_CH_V_IN, raw_v_in, &phys_v_in);
    app_analog_signal_convert_raw(ADC_CH_I_IN, raw_i_in, &phys_i_in);
    g_ctrl_diag.raw.v_in_v = phys_v_in;
    g_ctrl_diag.raw.i_in_a = phys_i_in;

    g_ctrl_diag.filt.v_in_v = app_analog_signal_ma_step(ADC_CH_V_IN, phys_v_in);
    float i_in_lpf = app_analog_signal_lpf_step_fast(ADC_CH_I_IN, phys_i_in);
    g_ctrl_diag.filt.i_in_a = app_analog_signal_ma_step(ADC_CH_I_IN, i_in_lpf);

    float i_l = g_ctrl_diag.filt.i_l_a;
    float g = g_buckboost.state.generalized_duty;
    float d_max = g_buckboost.params.duty_max;
    float i_in_calc = 0.0f;
    if (algo_flt_finite(i_l) && algo_flt_finite(g) && algo_flt_finite(d_max)) {
        i_in_calc = i_l * d_max * g;
    }
    g_ctrl_diag.ff.i_in_calc_a = i_in_calc;

    float p_in = g_ctrl_diag.filt.v_in_v * i_in_calc;
    if (!algo_flt_finite(p_in)) {
        p_in = 0.0f;
    }

    ctrl_buckboost_update_power(&g_buckboost, p_in);

    g_ctrl_diag.ff.p_in_w = p_in;
    g_ctrl_diag.ff.p_target_w = g_buckboost.state.p_target_w;
    g_ctrl_diag.ff.power_pid_out = g_buckboost.state.power_pid_out;

exit:;
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
        ctrl_buckboost_enter_cw_mode(&g_buckboost, BUCKBOOST_P_TARGET_DEFAULT);
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
    buckboost_vlink_sample_reset();

    /* 1. 控制器初始化 (不涉及硬件) */
    ctrl_fault_init(NULL);
    ctrl_buckboost_init(&g_buckboost);
    ctrl_buckboost_enable(&g_buckboost);
    ctrl_buckboost_enter_cw_mode(&g_buckboost, BUCKBOOST_P_TARGET_DEFAULT);

    ctrl_lcc_init(&g_lcc);
    ctrl_lcc_set_frequency(&g_lcc, CTRL_LCC_FREQ_DEFAULT_HZ);
    ctrl_lcc_set_phase(&g_lcc, CTRL_LCC_PHASE_DEFAULT_DEG);
    ctrl_lcc_set_duty(&g_lcc, CTRL_LCC_DUTY_DEFAULT);
    s_lcc_last_freq_hz = 0U;

    /* 2. GPTMR1 外环定时器 (通过 Platform 层)，先注册定时器回调但暂不启动 */
    app_gptmr_init();
    app_gptmr_register_callback(APP_GPTMR_CH_VOLTAGE, buckboost_voltage_loop_isr);
    app_gptmr_register_callback(APP_GPTMR_CH_POWER, buckboost_power_loop_isr);
    app_gptmr_register_callback(APP_GPTMR_CH_LCC, lcc_control_loop_isr);

    /* 3. ADC PMT 完成回调: 电流内环，必须早于 GPTMR 外环启动 */
    app_adc_register_pmt_callback(APP_ADC_INST_1, buckboost_current_loop_isr);
    app_adc_register_pmt_callback(APP_ADC_INST_0, lcc_current_loop_isr);

    /* 4. 外环定时器启动: PMT cache 尚未有效时 ISR 会跳过本轮 */
    app_gptmr_start_all();

    /* 5. 状态初始化 */
    s_state = SYS_INIT;
    s_mode = MODE_LCC_OPEN;
    configure_controllers_for_mode();
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
            (void)app_control_power_enable();
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

    ctrl_lcc_cmd_t lcc_cmd;
    ctrl_lcc_get_cmd(&g_lcc, &lcc_cmd);
    app_hrpwm_set_duty(HRPWM_LCC_A, lcc_cmd.duty);
    app_hrpwm_set_duty(HRPWM_LCC_B, lcc_cmd.duty);
    app_hrpwm_set_phase(HRPWM_INST_LCC, HRPWM_LCC_A, HRPWM_LCC_B, lcc_cmd.phase_deg);

    app_gptmr_start_all();

    s_state = SYS_RUN;
    return 0;
}

void app_control_power_disable(void) {
    app_gptmr_stop_all();
    buckboost_vlink_sample_reset();

    ctrl_buckboost_disable(&g_buckboost);
    ctrl_lcc_disable(&g_lcc);

    app_hrpwm_set_duty(HRPWM_LCC_A, 0.0f);
    app_hrpwm_set_duty(HRPWM_LCC_B, 0.0f);

    s_state = SYS_IDLE;
}

void app_control_emergency(void) {
    app_gptmr_stop_all();
    buckboost_vlink_sample_reset();

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
        buckboost_vlink_sample_reset();
        s_state = SYS_INIT;
    }
    return ret;
}
