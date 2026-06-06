/*
 * Debug RTT - SEGGER RTT wrapper for debug output
 *
 * Copyright (c) 2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "app_debug_rtt.h"

#include "SEGGER_RTT.h"
#include "app_adc.h"
#include "app_can.h"
#include "app_hrpwm.h"
#include "app_sampling_sync.h"
#include "board.h"
#include "drv_hrpwm.h"
#include "drv_mcan.h"
#include "hpm_pwm_drv.h"
#include "intf_clock.h"
#include "intf_can.h"
#include "intf_hrpwm.h"

#include <stdarg.h>
#include <stdio.h>

ATTR_PLACE_AT_NONCACHEABLE_WITH_ALIGNMENT(4) static uint32_t pmt_dma0[48];
ATTR_PLACE_AT_NONCACHEABLE_WITH_ALIGNMENT(4) static uint32_t pmt_dma1[48];

static void app_debug_adc_pmt_init(void);

void app_debug_init(void)
{
    app_debug_can_loopback_test();
    app_debug_adc_pmt_init();
}

void app_debug_write(const char* str) { SEGGER_RTT_WriteString(0, str); }

int app_debug_printf(const char* fmt, ...) {
    char buf[256];
    va_list ap;
    int len;

    va_start(ap, fmt);
    len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    SEGGER_RTT_WriteString(0, buf);
    return len;
}

#define APP_DBG_CMP_START_INDEX(pwm_index) ((uint8_t)((pwm_index) * 2U))

typedef struct {
    PWM_Type* base;
    const char* name;
    uint8_t pwm_index;
} app_dbg_hrpwm_probe_t;

static const app_dbg_hrpwm_probe_t app_dbg_hrpwm_probes[] = {
    {.base = BOARD_APP_HRPWM0, .name = "PWM0_PAIR0", .pwm_index = BOARD_APP_HRPWM_PWM0_PAIR0_OUT},
    {.base = BOARD_APP_HRPWM0, .name = "PWM0_PAIR1", .pwm_index = BOARD_APP_HRPWM_PWM0_PAIR1_OUT},
    {.base = BOARD_APP_HRPWM1, .name = "PWM1_PAIR0", .pwm_index = BOARD_APP_HRPWM_PWM1_PAIR0_OUT},
    {.base = BOARD_APP_HRPWM1, .name = "PWM1_PAIR1", .pwm_index = BOARD_APP_HRPWM_PWM1_PAIR1_OUT},
};

static const char* app_dbg_detect_alignment(uint32_t reload, uint32_t cmp_begin, uint32_t cmp_end) {
    if (cmp_end == reload) {
        return "EDGE-like";
    }

    if ((cmp_begin + cmp_end == reload) || (cmp_begin + cmp_end + 1U == reload)) {
        return "CENTER-like";
    }

    return "UNKNOWN";
}

void app_debug_dump_hrpwm_cmp(void) {
    app_debug_write("[RTT] HRPWM compare register snapshot\r\n");

    for (size_t i = 0; i < sizeof(app_dbg_hrpwm_probes) / sizeof(app_dbg_hrpwm_probes[0]); i++) {
        const app_dbg_hrpwm_probe_t* probe = &app_dbg_hrpwm_probes[i];
        uint8_t cmp_start = APP_DBG_CMP_START_INDEX(probe->pwm_index);
        uint32_t reload = pwm_get_reload_val(probe->base);
        uint32_t cmp_begin = pwm_cmp_get_cmp_value(probe->base, cmp_start);
        uint32_t cmp_end = pwm_cmp_get_cmp_value(probe->base, cmp_start + 1U);
        const char* align = app_dbg_detect_alignment(reload, cmp_begin, cmp_end);

        app_debug_printf(
            "[RTT] %s: reload=%lu cmp[%u]=%lu cmp[%u]=%lu => %s\r\n", probe->name,
            (unsigned long)reload, (unsigned int)cmp_start, (unsigned long)cmp_begin,
            (unsigned int)(cmp_start + 1U), (unsigned long)cmp_end, align);
    }
}

/* ============================================================================
 * PWM中断测试 + 用户回调链
 * ============================================================================ */

#define PWM_IRQ_INSTANCE_COUNT (2U)

/* 中断测试变量 */
static volatile uint32_t pwm_irq_count[PWM_IRQ_INSTANCE_COUNT] = {0};
static volatile bool pwm_irq_enabled[PWM_IRQ_INSTANCE_COUNT] = {0};

/* 用户回调函数指针 */
static pwm_irq_user_callback_t pwm_user_callback[PWM_IRQ_INSTANCE_COUNT] = {NULL};

/* PWM0中心点中断回调（内部，包含计数 + 用户回调） */
static void pwm0_center_irq_callback(void) {
    pwm_irq_count[0]++;

    /* 调用用户注册的业务回调 */
    if (pwm_user_callback[0] != NULL) {
        pwm_user_callback[0]();
    }
}

/* PWM1中心点中断回调（内部，包含计数 + 用户回调） */
static void pwm1_center_irq_callback(void) {
    pwm_irq_count[1]++;

    /* 调用用户注册的业务回调 */
    if (pwm_user_callback[1] != NULL) {
        pwm_user_callback[1]();
    }
}

int app_debug_pwm_irq_register_callback(uint8_t inst, pwm_irq_user_callback_t callback) {
    if (inst >= PWM_IRQ_INSTANCE_COUNT) {
        return -1;
    }
    pwm_user_callback[inst] = callback;
    app_debug_printf("[IRQ] PWM%d user callback registered\r\n", inst);
    return 0;
}

void app_debug_pwm_irq_unregister_callback(uint8_t inst) {
    if (inst >= PWM_IRQ_INSTANCE_COUNT) {
        return;
    }
    pwm_user_callback[inst] = NULL;
    app_debug_printf("[IRQ] PWM%d user callback unregistered\r\n", inst);
}

void app_debug_pwm_irq_enable(uint8_t inst) {
    if (inst >= PWM_IRQ_INSTANCE_COUNT) {
        return;
    }

    int ret;
    if (inst == 0) {
        ret = intf_hrpwm_config_reload_irq(0, pwm0_center_irq_callback);
        if (ret == 0) {
            ret = intf_hrpwm_enable_reload_irq(0);
        }
    } else {
        ret = intf_hrpwm_config_reload_irq(1, pwm1_center_irq_callback);
        if (ret == 0) {
            ret = intf_hrpwm_enable_reload_irq(1);
        }
    }

    if (ret == 0) {
        pwm_irq_enabled[inst] = true;
        pwm_irq_count[inst] = 0;
        app_debug_printf("[IRQ] PWM%d center IRQ enabled\r\n", inst);
    } else {
        app_debug_printf("[IRQ] PWM%d center IRQ enable FAILED\r\n", inst);
    }
}

void app_debug_pwm_irq_disable(uint8_t inst) {
    if (inst >= PWM_IRQ_INSTANCE_COUNT) {
        return;
    }

    if (inst == 0) {
        intf_hrpwm_disable_reload_irq(0);
    } else {
        intf_hrpwm_disable_reload_irq(1);
    }

    pwm_irq_enabled[inst] = false;
    app_debug_printf(
        "[IRQ] PWM%d center IRQ disabled, count=%lu\r\n", inst, (unsigned long)pwm_irq_count[inst]);
}

uint32_t app_debug_pwm_irq_get_count(uint8_t inst) {
    if (inst >= PWM_IRQ_INSTANCE_COUNT) {
        return 0;
    }
    return pwm_irq_count[inst];
}

void app_debug_pwm_irq_reset_count(uint8_t inst) {
    if (inst < PWM_IRQ_INSTANCE_COUNT) {
        pwm_irq_count[inst] = 0;
    }
}

void app_debug_pwm_irq_dump_status(void) {
    app_debug_write("[IRQ] PWM IRQ Status:\r\n");
    for (uint8_t i = 0; i < PWM_IRQ_INSTANCE_COUNT; i++) {
        app_debug_printf(
            "[IRQ]   PWM%d: %s, count=%lu\r\n", i, pwm_irq_enabled[i] ? "enabled" : "disabled",
            (unsigned long)pwm_irq_count[i]);
    }
}

/* ============================================================================
 * 变频测试
 * ============================================================================ */

void app_debug_pwm_test_frequency_sweep(
    uint8_t inst, uint32_t freq_start, uint32_t freq_end, uint32_t freq_step, uint32_t delay_ms) {
    app_debug_printf(
        "[TEST] PWM%d frequency sweep: %lu -> %lu Hz, step=%lu Hz\r\n", inst,
        (unsigned long)freq_start, (unsigned long)freq_end, (unsigned long)freq_step);

    int32_t direction = (freq_end > freq_start) ? 1 : -1;
    uint32_t freq = freq_start;

    while (1) {
        intf_hrpwm_set_frequency(inst, freq);
        app_debug_printf("[TEST]   freq = %lu Hz\r\n", (unsigned long)freq);

        intf_clock_delay_ms(delay_ms);

        if (direction > 0) {
            if (freq >= freq_end)
                break;
            freq += freq_step;
            if (freq > freq_end)
                freq = freq_end;
        } else {
            if (freq <= freq_end)
                break;
            if (freq < freq_step)
                break;
            freq -= freq_step;
            if (freq < freq_end)
                freq = freq_end;
        }
    }

    app_debug_printf("[TEST] PWM%d frequency sweep done\r\n", inst);
}

/* ============================================================================
 * 移相测试
 * ============================================================================ */

void app_debug_pwm_test_phase_sweep(
    uint8_t inst, uint8_t ref_pair, uint8_t target_pair, float phase_start, float phase_end,
    float phase_step, uint32_t delay_ms) {
    app_debug_printf(
        "[TEST] PWM%d phase sweep: %.1f -> %.1f deg, step=%.1f deg\r\n", inst, phase_start,
        phase_end, phase_step);

    intf_hrpwm_phase_cfg_t cfg = {
        .inst = inst,
        .ref_pair = ref_pair,
        .target_pair = target_pair,
    };

    float phase = phase_start;
    int32_t direction = (phase_end > phase_start) ? 1 : -1;

    while (1) {
        cfg.phase_deg = phase;
        intf_hrpwm_set_phase(&cfg);
        app_debug_printf("[TEST]   phase = %.1f deg\r\n", phase);

        intf_clock_delay_ms(delay_ms);

        if (direction > 0) {
            if (phase >= phase_end)
                break;
            phase += phase_step;
            if (phase > phase_end)
                phase = phase_end;
        } else {
            if (phase <= phase_end)
                break;
            phase -= phase_step;
            if (phase < phase_end)
                phase = phase_end;
        }
    }

    app_debug_printf("[TEST] PWM%d phase sweep done\r\n", inst);
}

/* ============================================================================
 * 占空比分辨率测试
 * ============================================================================ */

void app_debug_pwm_test_duty_resolution(
    uint8_t inst, uint8_t pair, float duty_start, float duty_end, float duty_step,
    uint32_t delay_ms) {
    app_debug_printf("[TEST] PWM%d pair%d duty resolution test\r\n", inst, pair);
    app_debug_printf(
        "[TEST]   range: %.4f -> %.4f, step=%.4f\r\n", duty_start, duty_end, duty_step);

#if defined(HRPWM_USE_EXTENDED_COUNTER) && (HRPWM_USE_EXTENDED_COUNTER == 1)
    app_debug_printf("[TEST]   mode: 28-bit counter (higher resolution)\r\n");
#else
    app_debug_printf("[TEST]   mode: 24-bit counter (standard)\r\n");
#endif

    uint8_t ch = pair * 2U + inst * 4U;
    float duty = duty_start;
    int32_t direction = (duty_end > duty_start) ? 1 : -1;

    while (1) {
        intf_hrpwm_set_duty(ch, duty);
        app_debug_printf("[TEST]   duty = %.4f (%.2f%%)\r\n", duty, duty * 100.0f);

        intf_clock_delay_ms(delay_ms);

        if (direction > 0) {
            if (duty >= duty_end)
                break;
            duty += duty_step;
            if (duty > duty_end)
                duty = duty_end;
        } else {
            if (duty <= duty_end)
                break;
            duty -= duty_step;
            if (duty < duty_end)
                duty = duty_end;
        }
    }

    app_debug_printf("[TEST] PWM%d pair%d duty resolution test done\r\n", inst, pair);
}

/* ============================================================================
 * HRPWM综合验证测试
 * ============================================================================ */

void app_debug_hrpwm_run_tests(void) {
    app_debug_write("\r\n[RTT] === HRPWM Validation Tests ===\r\n");
    app_debug_dump_hrpwm_cmp();

    for (pwm_pair_t pair = PWM_PAIR_0; pair < PWM_PAIR_COUNT; pair++) {
        pwm_set_duty(pair, 0.5f);
    }

    app_debug_write("[RTT] Enabling PWM0 & PWM1 reload IRQ...\r\n");
    app_debug_pwm_irq_enable(0);
    app_debug_pwm_irq_enable(1);

    app_debug_write("[RTT] Waiting 1s before HRPWM tests...\r\n");
    intf_clock_delay_ms(1000);

    app_debug_write("\r\n[RTT] --- Test 1: PWM0 static initial phase = 180 deg ---\r\n");
    pwm_stop(PWM_PAIR_0);
    pwm_stop(PWM_PAIR_1);
    intf_clock_delay_ms(20);
    pwm_set_phase(0, 0, 1, 180.0f);
    pwm_start(PWM_PAIR_0);
    pwm_start(PWM_PAIR_1);
    app_debug_dump_hrpwm_cmp();
    intf_clock_delay_ms(4000);

    app_debug_write("\r\n[RTT] --- Test 2: PWM0 runtime phase sweep 0 -> 89 deg ---\r\n");
    pwm_set_phase(0, 0, 1, 0.0f);
    intf_clock_delay_ms(100);
    app_debug_pwm_test_phase_sweep(0, 0, 1, 0.0f, 89.0f, 1.0f, 80);
    pwm_set_phase(0, 0, 1, 0.0f);
    intf_clock_delay_ms(200);

    app_debug_write("\r\n[RTT] --- Test 3: PWM0 frequency sweep with phase replay ---\r\n");
    pwm_set_phase(0, 0, 1, 60.0f);
    app_debug_pwm_test_frequency_sweep(0, 160000, 240000, 20000, 500);
    pwm_set_frequency(PWM_INST_0, 200000);
    pwm_set_phase(0, 0, 1, 0.0f);
    intf_clock_delay_ms(200);

    app_debug_write("\r\n[RTT] --- Test 4: PWM1 static initial phase = 150 deg ---\r\n");
    pwm_stop(PWM_PAIR_2);
    pwm_stop(PWM_PAIR_3);
    intf_clock_delay_ms(20);
    pwm_set_phase(1, 0, 1, 150.0f);
    pwm_start(PWM_PAIR_2);
    pwm_start(PWM_PAIR_3);
    app_debug_dump_hrpwm_cmp();
    intf_clock_delay_ms(2000);

    app_debug_write("\r\n[RTT] --- Test 5: PWM1 runtime phase sweep 0 -> 89 deg ---\r\n");
    pwm_set_phase(1, 0, 1, 0.0f);
    intf_clock_delay_ms(100);
    app_debug_pwm_test_phase_sweep(1, 0, 1, 0.0f, 89.0f, 1.0f, 80);
    pwm_set_phase(1, 0, 1, 0.0f);
    intf_clock_delay_ms(200);

    app_debug_write("\r\n[RTT] --- Test 6: PWM0 duty resolution around 50% ---\r\n");
    app_debug_pwm_test_duty_resolution(0, 0, 0.45f, 0.55f, 0.0005f, 80);
    pwm_set_duty(PWM_PAIR_0, 0.5f);
    pwm_set_phase(0, 0, 1, 0.0f);

    app_debug_write("\r\n[RTT] === HRPWM Validation Tests Completed ===\r\n");
}

/* ============================================================================
 * ADC 测试
 * ============================================================================ */

static const char* adc_ch_names[ADC_CH_COUNT] = {
    [ADC_CH_V_IN] = "V_IN  ",   [ADC_CH_I_IN] = "I_IN  ",   [ADC_CH_I_L] = "I_L   ",
    [ADC_CH_V_LINK] = "V_LINK", [ADC_CH_I_COIL] = "I_COIL", [ADC_CH_I_LF] = "I_LF  ",
};

void app_debug_adc_dump_channels(void) {
    app_debug_write("[ADC]  Channel  Inst  Raw(hex)  Raw(dec)   ADC(mV)  Sense(mV)  Physical\r\n");

    for (adc_channel_t ch = ADC_CH_V_IN; ch < ADC_CH_COUNT; ch++) {
        uint16_t raw = app_adc_read_raw(ch);
        float adc_mv = 0.0f;
        float sense_mv = 0.0f;
        float physical = 0.0f;

        (void)app_adc_read_adc_voltage_mv(ch, &adc_mv);
        (void)app_adc_read_sense_voltage_mv(ch, &sense_mv);
        (void)app_adc_read_physical(ch, &physical);

        app_debug_printf(
            "[ADC]  %s   ADC%d  0x%04X   %5u   %8.1f   %9.1f   %8.3f\r\n", adc_ch_names[ch],
            (ch <= ADC_CH_V_LINK) ? 0 : 1, raw, raw, adc_mv, sense_mv, physical);
    }
}

void app_debug_adc_run_tests(void) {
    app_debug_write("\r\n[RTT] === ADC Oneshot Channel Scan ===\r\n");

    uint16_t val[ADC_CH_COUNT];

    /* oneshot bus mode: x3 reads per channel to flush crosstalk */
    for (adc_channel_t ch = ADC_CH_V_IN; ch < ADC_CH_COUNT; ch++) {
        app_adc_read_raw(ch);
        app_adc_read_raw(ch);
        val[ch] = app_adc_read_raw(ch);
    }

    app_debug_write("[ADC]  Channel  Inst  Raw(hex)  Raw(dec)   ADC(mV)\r\n");
    for (adc_channel_t ch = ADC_CH_V_IN; ch < ADC_CH_COUNT; ch++) {
        float mv = 0.0f;
        (void)app_adc_read_adc_voltage_mv(ch, &mv);
        app_debug_printf(
            "[ADC]  %s   ADC%d  0x%04X   %5u   %8.1f\r\n", adc_ch_names[ch],
            (ch <= ADC_CH_V_LINK) ? 0 : 1, val[ch], val[ch], mv);
    }
}

/* ============================================================================
 * ADC PMT 联动测试
 * ============================================================================ */

static volatile uint16_t pmt_val_adc0[4];
static volatile uint16_t pmt_val_adc1[2];
static volatile bool pmt_ready_adc0;
static volatile bool pmt_ready_adc1;

static void pmt_cb_adc0(intf_adc_ch_t trig, const uint16_t* v, uint8_t n, void* u) {
    (void)trig;
    (void)u;
    if (n >= 4) {
        pmt_val_adc0[0] = v[0]; /* ch6=V_IN */
        pmt_val_adc0[1] = v[1]; /* ch11=I_IN */
        pmt_val_adc0[2] = v[2]; /* ch2=I_L */
        pmt_val_adc0[3] = v[3]; /* ch3=V_LINK */
        pmt_ready_adc0 = true;
    }
}

static void pmt_cb_adc1(intf_adc_ch_t trig, const uint16_t* v, uint8_t n, void* u) {
    (void)trig;
    (void)u;
    if (n >= 2) {
        pmt_val_adc1[0] = v[0]; /* ch4=I_COIL */
        pmt_val_adc1[1] = v[1]; /* ch5=I_LF */
        pmt_ready_adc1 = true;
    }
}

static bool adc_pmt_initialized;

static void app_debug_adc_pmt_init(void)
{
    if (adc_pmt_initialized) {
        return;
    }
    adc_pmt_initialized = true;

    app_debug_write("\r\n[RTT] === ADC PMT + PWM + TRGM Test ===\r\n");

    pwm_init();

    app_sampling_sync_cfg_t sync_cfg;
    app_sampling_sync_get_default_config(&sync_cfg);
    sync_cfg.adc0_dma_en = true;
    sync_cfg.adc0_dma_buff = pmt_dma0;
    sync_cfg.adc0_dma_buff_len = 48U;
    sync_cfg.adc1_dma_en = true;
    sync_cfg.adc1_dma_buff = pmt_dma1;
    sync_cfg.adc1_dma_buff_len = 48U;
    sync_cfg.adc0_cb = pmt_cb_adc0;
    sync_cfg.adc1_cb = pmt_cb_adc1;

    (void)app_sampling_sync_init(&sync_cfg);
    app_sampling_sync_start();

    app_debug_write("[ADC-PMT] initialized\r\n");
}

void app_debug_adc_pmt_run_tests(void) {
    if (!adc_pmt_initialized) {
        return;
    }

    if (pmt_ready_adc0 || pmt_ready_adc1) {
        if (pmt_ready_adc0 && pmt_ready_adc1) {
            pmt_ready_adc0 = false;
            pmt_ready_adc1 = false;

            uint16_t val[ADC_CH_COUNT];
            val[ADC_CH_V_IN] = pmt_val_adc0[0];
            val[ADC_CH_I_IN] = pmt_val_adc0[1];
            val[ADC_CH_I_L] = pmt_val_adc0[2];
            val[ADC_CH_V_LINK] = pmt_val_adc0[3];
            val[ADC_CH_I_COIL] = pmt_val_adc1[0];
            val[ADC_CH_I_LF] = pmt_val_adc1[1];
            app_debug_write("[ADC]==========================================\r\n");

            app_debug_write("[ADC]  Channel  Inst  Raw(hex)  Raw(dec)     mV\r\n");
            /* ADC0 */
            for (adc_channel_t ch = ADC_CH_V_IN; ch < ADC_CH_COUNT; ch++) {
                int inst = (ch >= ADC_CH_I_COIL) ? 1 : 0;
                float mv = (float)val[ch] * 3300.0f / 65535.0f;
                app_debug_printf(
                    "[ADC]  %s   ADC%d  0x%04X   %5u   %7.1f\r\n", adc_ch_names[ch], inst, val[ch],
                    val[ch], mv);
            }
        }
    }
}

/* ============================================================================
 * CAN 收发测试
 * ============================================================================ */

static void can_test_rx_callback(const app_can_msg_t *msg)
{
    app_debug_printf("[CAN-RX] ID=0x%03lX DLC=%u data=",
                     (unsigned long)msg->id, msg->dlc);
    for (uint8_t i = 0U; i < msg->dlc; i++) {
        app_debug_printf("%02X ", msg->data[i]);
    }
    app_debug_write("\r\n");
}

static void app_debug_can_dump_status_and_stats(void)
{
    intf_can_status_t status;
    app_can_stats_t stats;

    if (app_can_get_status(&status) == 0) {
        app_debug_printf("[CAN-STS] tx_err=%u rx_err=%u bus_off=%d warn=%d passive=%d lec=%u\r\n",
                         status.tx_error_count, status.rx_error_count, status.bus_off,
                         status.error_warning, status.error_passive, status.last_error_code);
    } else {
        app_debug_write("[CAN-STS] status read FAILED\r\n");
    }

    if (app_can_get_stats(&stats) == 0) {
        app_debug_printf("[CAN-STAT] rx=%lu irq=%lu drop=%lu ovf=%lu fifo_full=%lu fifo_lost=%lu tx_enq=%lu tx_ok=%lu tx_fail=%lu pending=%u evt=0x%08lX\r\n",
                         (unsigned long)stats.rx_count,
                         (unsigned long)stats.rx_irq_count,
                         (unsigned long)stats.rx_drop_count,
                         (unsigned long)stats.rx_overflow_count,
                         (unsigned long)stats.rx_fifo_full_count,
                         (unsigned long)stats.rx_fifo_lost_count,
                         (unsigned long)stats.tx_enqueue_ok_count,
                         (unsigned long)stats.tx_ok_count,
                         (unsigned long)stats.tx_fail_count,
                         stats.rx_pending_count,
                         (unsigned long)stats.last_event_flags);
        app_debug_printf("[CAN-ERR] bus_off=%lu warn=%lu passive=%lu proto=%lu ram=%lu last_tx_ret=%d last_rx_id=0x%03lX last_rx_dlc=%u\r\n",
                         (unsigned long)stats.bus_off_count,
                         (unsigned long)stats.error_warning_count,
                         (unsigned long)stats.error_passive_count,
                         (unsigned long)stats.protocol_error_count,
                         (unsigned long)stats.ram_access_fail_count,
                         stats.last_tx_ret,
                         (unsigned long)stats.last_rx_id,
                         stats.last_rx_dlc);
    } else {
        app_debug_write("[CAN-STAT] stats read FAILED\r\n");
    }
}

void app_debug_can_run_tests(void)
{
    static bool initialized = false;
    static uint32_t tx_count = 0U;
    int ret;
    int filter_ret;

    if (!initialized) {
        intf_can_status_t tmp;
        app_debug_write("\r\n[RTT] === CAN Test ===\r\n");

        if (app_can_get_status(&tmp) == 0) {
            app_debug_write("[CAN] init OK\r\n");
        } else {
            app_debug_write("[CAN] init FAILED\r\n");
            return;
        }

        app_can_set_rx_callback(can_test_rx_callback);
        app_can_clear_stats();

        filter_ret = app_can_add_std_filter(0x114U, 0x7FFU);
        if (filter_ret != 0) {
            app_debug_write("[CAN] add std filter FAILED\r\n");
            return;
        }
        filter_ret = app_can_add_std_filter(0x000U, 0x000U);
        if (filter_ret != 0) {
            app_debug_write("[CAN] add catch-all filter FAILED\r\n");
            return;
        }

        initialized = true;
        app_debug_can_dump_status_and_stats();
    }

    uint8_t tx_data[8];
    for (uint8_t i = 0U; i < 8U; i++) {
        tx_data[i] = (uint8_t)((tx_count >> (i * 4U)) & 0x0FU)
                    | (uint8_t)(i << 4U);
    }

    ret = app_can_send_std(0x114U, tx_data, 8U);
    app_debug_printf("[CAN-TX] seq=%lu ret=%d\r\n", (unsigned long)tx_count, ret);
    tx_count++;

    intf_clock_delay_ms(100);
    app_can_poll();
    app_debug_can_dump_status_and_stats();
}

static volatile bool lb_rx_done;
static volatile app_can_msg_t lb_rx_msg;

static void lb_irq_handler(intf_can_inst_t inst, uint32_t events, void *user_data)
{
    (void)inst;
    (void)events;
    (void)user_data;
    if (events & INTF_CAN_EVENT_RX_FIFO0_NEW_MSG) {
        intf_can_frame_t f;
        memset(&f, 0, sizeof(f));
        if (intf_can_receive_nonblocking(0, &f) == 0) {
            lb_rx_msg.id = f.id;
            lb_rx_msg.is_ext_id = f.is_ext_id;
            lb_rx_msg.dlc = f.dlc;
            if (f.dlc != 0U) {
                memcpy((void *)lb_rx_msg.data, f.data, f.dlc);
            }
            lb_rx_done = true;
        }
    }
}

void app_debug_can_loopback_test(void)
{
    int ret;
    uint32_t wait_ms;

    app_debug_write("\r\n[RTT] === CAN Internal Loopback Test ===\r\n");

    app_can_deinit();
    drv_can_register();
    memset((void *)&lb_rx_msg, 0, sizeof(lb_rx_msg));

    intf_can_cfg_t cfg = {
        .baudrate       = 1000000U,
        .mode           = INTF_CAN_MODE_LOOPBACK_INTERNAL,
        .enable_canfd   = false,
        .interrupt_mask = INTF_CAN_EVENT_RX_FIFO0_NEW_MSG
                        | INTF_CAN_EVENT_RX_FIFO0_FULL
                        | INTF_CAN_EVENT_RX_FIFO0_MSG_LOST,
        .ram = {
            .std_filter_count = APP_CAN_FILTER_COUNT,
            .ext_filter_count = APP_CAN_FILTER_COUNT,
        },
    };

    ret = intf_can_init(0, &cfg);
    if (ret != 0) {
        app_debug_write("[CAN-LB] Init FAILED\r\n");
        return;
    }
    app_debug_write("[CAN-LB] Init OK (internal loopback mode)\r\n");

    ret = intf_can_config_irq_callback(0, lb_irq_handler, NULL);
    if (ret != 0) {
        app_debug_write("[CAN-LB] IRQ callback config FAILED\r\n");
        intf_can_deinit(0);
        return;
    }

    intf_can_filter_elem_t filter = {
        .type = INTF_CAN_FILTER_CLASSIC,
        .target_fifo = INTF_CAN_FILTER_FIFO0,
        .id = 0U, .mask = 0U,
    };
    ret = intf_can_config_filter(0, 0, &filter);
    if (ret != 0) {
        app_debug_write("[CAN-LB] Filter config FAILED\r\n");
        intf_can_config_irq_callback(0, NULL, NULL);
        intf_can_deinit(0);
        return;
    }

    uint8_t data[8] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11};
    intf_can_frame_t tx_frame = {
        .id = 0x114U, .dlc = 8U, .frame_type = INTF_CAN_FRAME_CLASSIC,
    };
    memcpy(tx_frame.data, data, 8);

    lb_rx_done = false;
    ret = intf_can_send_nonblocking(0, &tx_frame, NULL);
    app_debug_printf("[CAN-LB] TX nonblocking ret=%d\r\n", ret);
    if (ret != 0) {
        intf_can_config_irq_callback(0, NULL, NULL);
        intf_can_deinit(0);
        return;
    }

    for (wait_ms = 0U; wait_ms < 100U && !lb_rx_done; wait_ms++) {
        intf_clock_delay_ms(1U);
    }

    if (lb_rx_done) {
        app_debug_printf("[CAN-LB] RX OK: ID=0x%03lX DLC=%u data=",
                         (unsigned long)lb_rx_msg.id, lb_rx_msg.dlc);
        for (uint8_t i = 0U; i < lb_rx_msg.dlc; i++) {
            app_debug_printf("%02X ", lb_rx_msg.data[i]);
        }
        app_debug_write("\r\n");

        bool match = true;
        for (uint8_t i = 0U; i < 8U; i++) {
            if (lb_rx_msg.data[i] != data[i]) { match = false; break; }
        }
        app_debug_printf("[CAN-LB] Data match: %s\r\n", match ? "YES" : "NO");
    } else {
        app_debug_printf("[CAN-LB] RX TIMEOUT after %lu ms — MCAN internal loopback FAILED\r\n",
                         (unsigned long)wait_ms);
        intf_can_status_t st;
        intf_can_get_status(0, &st);
        app_debug_printf("[CAN-LB] Status: tx_err=%u rx_err=%u bus_off=%d\r\n",
                         st.tx_error_count, st.rx_error_count, st.bus_off);
    }

    intf_can_config_irq_callback(0, NULL, NULL);
    intf_can_deinit(0);
}
