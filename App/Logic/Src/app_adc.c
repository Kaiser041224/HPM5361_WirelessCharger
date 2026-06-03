/*
 * ADC App Layer
 *
 * Copyright (c) 2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "app_adc.h"

#include "intf_adc.h"
#include <stddef.h>

static const uint8_t channel_to_hw[ADC_CH_COUNT] = {
    [ADC_CH_I_IN]   = 11,   /* PB08 */
    [ADC_CH_I_L]    = 2,    /* PB10 */
    [ADC_CH_V_LINK] = 3,    /* PB11 */
    [ADC_CH_I_COIL] = 4,    /* PB12 */
    [ADC_CH_I_RES]  = 5,    /* PB13 */
    [ADC_CH_V_IN]   = 6,    /* PB14 */
};

/* ADC0: 电流内环 (I_IN + I_L, 最高优先级采样)
 * ADC1: 电压/Monitor (V_LINK + I_COIL + I_RES + V_IN) */
static const uint8_t channel_to_inst[ADC_CH_COUNT] = {
    [ADC_CH_I_IN]   = 0,
    [ADC_CH_I_L]    = 0,
    [ADC_CH_V_LINK] = 1,
    [ADC_CH_I_COIL] = 1,
    [ADC_CH_I_RES]  = 1,
    [ADC_CH_V_IN]   = 1,
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
