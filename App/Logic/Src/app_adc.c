/*
 * ADC App Layer
 *
 * Copyright (c) 2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "app_adc.h"

#include "intf_adc.h"
#include "intf_hrpwm.h"
#include "intf_trgm.h"
#include <stddef.h>

static const uint8_t channel_to_hw[ADC_CH_COUNT] = {
    [ADC_CH_V_IN]   = 6,    /* PB14 */
    [ADC_CH_I_IN]   = 11,   /* PB08 */
    [ADC_CH_I_L]    = 2,    /* PB10 */
    [ADC_CH_V_LINK] = 3,    /* PB11 */
    [ADC_CH_I_COIL] = 4,    /* PB12 */
    [ADC_CH_I_LF]  = 5,    /* PB13 */
};

/* ADC0 (PWM0): V_IN + I_IN + I_L + V_LINK
 * ADC1 (PWM1): I_COIL + I_RES */
static const uint8_t channel_to_inst[ADC_CH_COUNT] = {
    [ADC_CH_V_IN]   = 0,
    [ADC_CH_I_IN]   = 0,
    [ADC_CH_I_L]    = 0,
    [ADC_CH_V_LINK] = 0,
    [ADC_CH_I_COIL] = 1,
    [ADC_CH_I_LF]  = 1,
};

extern void hpm_adc_driver_register(void);

/* ========================================================================
 * 基本操作 (Oneshot)
 * ======================================================================== */

void app_adc_init(void)
{
    hpm_adc_driver_register();

    intf_adc_cfg_t cfg = {
        .resolution   = INTF_ADC_RES_DEFAULT,
        .mode         = INTF_ADC_MODE_ONESHOT,
        .sample_cycle = INTF_ADC_DEFAULT_SAMPLE_CYCLE,
        .clock_div    = INTF_ADC_DEFAULT_CLOCK_DIV,
        .vref_mv      = INTF_ADC_DEFAULT_VREF_MV,
    };

    for (adc_channel_t ch = ADC_CH_I_IN; ch < ADC_CH_COUNT; ch++) {
        intf_adc_init(INTF_ADC_CH(channel_to_inst[ch], channel_to_hw[ch]), &cfg);
    }

    /* flush stale BUS_RESULT after init (oneshot only) */
    for (adc_channel_t ch = ADC_CH_I_IN; ch < ADC_CH_COUNT; ch++) {
        uint16_t dummy;
        intf_adc_read(INTF_ADC_CH(channel_to_inst[ch], channel_to_hw[ch]), &dummy);
        intf_adc_read(INTF_ADC_CH(channel_to_inst[ch], channel_to_hw[ch]), &dummy);
    }
}

uint16_t app_adc_read_raw(adc_channel_t ch)
{
    if (ch >= ADC_CH_COUNT) return 0;

    uint16_t raw;
    intf_adc_read(INTF_ADC_CH(channel_to_inst[ch], channel_to_hw[ch]), &raw);
    return raw;
}

void app_adc_read_all(uint16_t values[ADC_CH_COUNT])
{
    if (values == NULL) return;
    for (adc_channel_t ch = ADC_CH_I_IN; ch < ADC_CH_COUNT; ch++) {
        values[ch] = app_adc_read_raw(ch);
    }
}

void app_adc_set_vref(float mv)
{
    intf_adc_set_vref(INTF_ADC_CH(0, 0), mv);
}

void app_adc_calibrate(void)
{
    intf_adc_calibrate(INTF_ADC_CH(0, 0));
    intf_adc_calibrate(INTF_ADC_CH(1, 0));
}

/* ========================================================================
 * PMT 硬件触发
 * ======================================================================== */

void app_adc_pmt_init(uint8_t pmt_trig,
                  const adc_channel_t *ch_list, uint8_t count,
                  intf_adc_pmt_cb_t cb, void *user_data)
{
    if (count == 0 || count > 4 || ch_list == NULL) return;

    uint8_t hw_list[4];
    for (uint8_t i = 0; i < count; i++) {
        hw_list[i] = channel_to_hw[ch_list[i]];
    }

    intf_adc_cfg_t cfg = {
        .resolution      = INTF_ADC_RES_DEFAULT,
        .mode            = INTF_ADC_MODE_PMT,
        .sample_cycle    = INTF_ADC_DEFAULT_SAMPLE_CYCLE,
        .clock_div       = INTF_ADC_DEFAULT_CLOCK_DIV,
        .vref_mv         = INTF_ADC_DEFAULT_VREF_MV,
        .pmt_trig_ch     = pmt_trig,
        .pmt_ch_count    = count,
        .pmt_cb          = cb,
        .pmt_cb_user_data = user_data,
    };
    for (uint8_t i = 0; i < count; i++) {
        cfg.pmt_ch_list[i] = hw_list[i];
    }

    intf_adc_init(INTF_ADC_CH(0, 0), &cfg);
}

void app_adc_pmt_start(void)
{
    intf_adc_start(INTF_ADC_CH(0, 0));
}

void app_adc_pmt_stop(void)
{
    intf_adc_stop(INTF_ADC_CH(0, 0));
}

/* ========================================================================
 * Watchdog
 * ======================================================================== */

void app_adc_wdog_init(adc_channel_t ch, uint16_t thshd_high, uint16_t thshd_low,
                   intf_adc_wdog_cb_t cb, void *user_data)
{
    if (ch >= ADC_CH_COUNT) return;

    intf_adc_cfg_t cfg = {
        .resolution      = INTF_ADC_RES_DEFAULT,
        .mode            = INTF_ADC_MODE_ONESHOT,
        .sample_cycle    = INTF_ADC_DEFAULT_SAMPLE_CYCLE,
        .clock_div       = INTF_ADC_DEFAULT_CLOCK_DIV,
        .vref_mv         = INTF_ADC_DEFAULT_VREF_MV,
        .wdog_en         = true,
        .wdog_thshd_high  = thshd_high,
        .wdog_thshd_low   = thshd_low,
        .wdog_cb          = cb,
        .wdog_cb_user_data = user_data,
    };

    intf_adc_init(INTF_ADC_CH(channel_to_inst[ch], channel_to_hw[ch]), &cfg);
}

void app_adc_wdog_reenable(adc_channel_t ch)
{
    if (ch >= ADC_CH_COUNT) return;
    intf_adc_wdog_reenable(INTF_ADC_CH(channel_to_inst[ch], channel_to_hw[ch]));
}

/* ========================================================================
 * PWM→TRGM→ADC PMT 联动初始化
 * ======================================================================== */

void app_adc_pwm_trig_init(
    intf_adc_pmt_cb_t cb0, void *user0,
    intf_adc_pmt_cb_t cb1, void *user1)
{
    hpm_adc_driver_register();

    /* PWM0/PWM1: CMP8 配置为 50% 占空比触发信号 */
    intf_hrpwm_config_trigger_cmp(0, 8, 0.5f);
    intf_hrpwm_config_trigger_cmp(1, 8, 0.5f);

    /* TRGM: PWM0 CH8REF → ADC TRG0A, PWM1 CH8REF → ADC TRG1A */
    intf_trgm_connect(INTF_TRGM_SRC_PWM0_CH8REF, INTF_TRGM_DST_ADC_PTRGI0A);
    intf_trgm_connect(INTF_TRGM_SRC_PWM1_CH8REF, INTF_TRGM_DST_ADC_PTRGI1A);

    /* ADC0 PMT (TRG0A): V_IN(ch6) + I_IN(ch11) + I_L(ch2) + V_LINK(ch3) */
    intf_adc_cfg_t cfg0 = {
        .resolution      = INTF_ADC_RES_DEFAULT,
        .mode            = INTF_ADC_MODE_PMT,
        .sample_cycle    = INTF_ADC_DEFAULT_SAMPLE_CYCLE,
        .clock_div       = INTF_ADC_DEFAULT_CLOCK_DIV,
        .pmt_trig_ch     = 0,   /* TRG0A  */
        .pmt_ch_count    = 4,
        .pmt_ch_list     = {6, 11, 2, 3},
        .pmt_cb          = cb0,
        .pmt_cb_user_data = user0,
    };
    intf_adc_init(INTF_ADC_CH(0, 0), &cfg0);

    /* ADC1 PMT (TRG1A): I_COIL(ch4) + I_RES(ch5) */
    intf_adc_cfg_t cfg1 = {
        .resolution      = INTF_ADC_RES_DEFAULT,
        .mode            = INTF_ADC_MODE_PMT,
        .sample_cycle    = INTF_ADC_DEFAULT_SAMPLE_CYCLE,
        .clock_div       = INTF_ADC_DEFAULT_CLOCK_DIV,
        .pmt_trig_ch     = 3,   /* TRG1A  */
        .pmt_ch_count    = 2,
        .pmt_ch_list     = {4, 5},
        .pmt_cb          = cb1,
        .pmt_cb_user_data = user1,
    };
    intf_adc_init(INTF_ADC_CH(1, 0), &cfg1);
}
