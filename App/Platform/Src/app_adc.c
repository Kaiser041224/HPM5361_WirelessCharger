/*
 * ADC App Layer
 *
 * Copyright (c) 2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "app_adc.h"

#include "intf_adc.h"
#include <stddef.h>

typedef struct {
    uint8_t inst;
    uint8_t hw_ch;
} app_adc_map_t;

/* ADC0 (PWM0): V_IN + I_IN + I_L + V_LINK
 * ADC1 (PWM1): I_COIL + I_LF */
static const app_adc_map_t adc_map[ADC_CH_COUNT] = {
    [ADC_CH_V_IN]   = {.inst = APP_ADC_INST_0, .hw_ch = 6},   /* PB14 */
    [ADC_CH_I_IN]   = {.inst = APP_ADC_INST_0, .hw_ch = 11},  /* PB08 */
    [ADC_CH_I_L]    = {.inst = APP_ADC_INST_0, .hw_ch = 2},   /* PB10 */
    [ADC_CH_V_LINK] = {.inst = APP_ADC_INST_0, .hw_ch = 3},   /* PB11 */
    [ADC_CH_I_COIL] = {.inst = APP_ADC_INST_1, .hw_ch = 4},   /* PB12 */
    [ADC_CH_I_LF]   = {.inst = APP_ADC_INST_1, .hw_ch = 5},   /* PB13 */
};

static app_adc_calibration_t adc_calibration[ADC_CH_COUNT] = {
    [ADC_CH_V_IN]   = {.sense_gain = 1.0f, .sense_offset_mv = 0.0f, .physical_gain = 1.0f, .physical_offset = 0.0f},
    [ADC_CH_I_IN]   = {.sense_gain = 1.0f, .sense_offset_mv = 0.0f, .physical_gain = 1.0f, .physical_offset = 0.0f},
    [ADC_CH_I_L]    = {.sense_gain = 1.0f, .sense_offset_mv = 0.0f, .physical_gain = 1.0f, .physical_offset = 0.0f},
    [ADC_CH_V_LINK] = {.sense_gain = 1.0f, .sense_offset_mv = 0.0f, .physical_gain = 1.0f, .physical_offset = 0.0f},
    [ADC_CH_I_COIL] = {.sense_gain = 1.0f, .sense_offset_mv = 0.0f, .physical_gain = 1.0f, .physical_offset = 0.0f},
    [ADC_CH_I_LF]   = {.sense_gain = 1.0f, .sense_offset_mv = 0.0f, .physical_gain = 1.0f, .physical_offset = 0.0f},
};

extern void hpm_adc_driver_register(void);

static bool app_adc_channel_is_valid(adc_channel_t ch)
{
    return ch < ADC_CH_COUNT;
}

static intf_adc_ch_t app_adc_encoded_channel(adc_channel_t ch)
{
    return INTF_ADC_CH(adc_map[ch].inst, adc_map[ch].hw_ch);
}

static void app_adc_flush_oneshot_result(adc_channel_t ch)
{
    uint16_t dummy;

    (void)intf_adc_read(app_adc_encoded_channel(ch), &dummy);
    (void)intf_adc_read(app_adc_encoded_channel(ch), &dummy);
}

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

    for (adc_channel_t ch = ADC_CH_V_IN; ch < ADC_CH_COUNT; ch++) {
        (void)intf_adc_init(app_adc_encoded_channel(ch), &cfg);
    }

    /* flush stale BUS_RESULT after init (oneshot only) */
    for (adc_channel_t ch = ADC_CH_V_IN; ch < ADC_CH_COUNT; ch++) {
        app_adc_flush_oneshot_result(ch);
    }
}

uint16_t app_adc_read_raw(adc_channel_t ch)
{
    if (!app_adc_channel_is_valid(ch)) return 0;

    uint16_t raw;
    raw = 0;
    (void)intf_adc_read(app_adc_encoded_channel(ch), &raw);
    return raw;
}

void app_adc_read_all(uint16_t values[ADC_CH_COUNT])
{
    if (values == NULL) return;
    for (adc_channel_t ch = ADC_CH_V_IN; ch < ADC_CH_COUNT; ch++) {
        values[ch] = app_adc_read_raw(ch);
    }
}

int app_adc_read_adc_voltage_mv(adc_channel_t ch, float *voltage_mv)
{
    if (!app_adc_channel_is_valid(ch) || voltage_mv == NULL) return -1;

    return intf_adc_read_voltage(app_adc_encoded_channel(ch), voltage_mv);
}

int app_adc_read_sense_voltage_mv(adc_channel_t ch, float *voltage_mv)
{
    float adc_voltage_mv;

    if (!app_adc_channel_is_valid(ch) || voltage_mv == NULL) return -1;
    if (app_adc_read_adc_voltage_mv(ch, &adc_voltage_mv) != 0) return -1;

    *voltage_mv = adc_voltage_mv * adc_calibration[ch].sense_gain + adc_calibration[ch].sense_offset_mv;
    return 0;
}

int app_adc_read_physical(adc_channel_t ch, float *value)
{
    float sense_voltage_mv;

    if (!app_adc_channel_is_valid(ch) || value == NULL) return -1;
    if (app_adc_read_sense_voltage_mv(ch, &sense_voltage_mv) != 0) return -1;

    *value = sense_voltage_mv * adc_calibration[ch].physical_gain + adc_calibration[ch].physical_offset;
    return 0;
}

void app_adc_set_calibration(adc_channel_t ch, const app_adc_calibration_t *cal)
{
    if (!app_adc_channel_is_valid(ch) || cal == NULL) return;

    adc_calibration[ch] = *cal;
}

int app_adc_get_calibration(adc_channel_t ch, app_adc_calibration_t *cal)
{
    if (!app_adc_channel_is_valid(ch) || cal == NULL) return -1;

    *cal = adc_calibration[ch];
    return 0;
}

void app_adc_set_vref_inst(app_adc_inst_t inst, float mv)
{
    if (inst >= APP_ADC_INST_COUNT) return;

    intf_adc_set_vref(INTF_ADC_CH(inst, 0), mv);
}

void app_adc_set_vref_all(float mv)
{
    for (app_adc_inst_t inst = APP_ADC_INST_0; inst < APP_ADC_INST_COUNT; inst++) {
        app_adc_set_vref_inst(inst, mv);
    }
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

    if (!app_adc_channel_is_valid(ch_list[0])) return;

    uint8_t hw_list[4];
    uint8_t inst = adc_map[ch_list[0]].inst;

    for (uint8_t i = 0; i < count; i++) {
        if (!app_adc_channel_is_valid(ch_list[i])) return;
        if (adc_map[ch_list[i]].inst != inst) return;
        hw_list[i] = adc_map[ch_list[i]].hw_ch;
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

    (void)intf_adc_init(INTF_ADC_CH(inst, 0), &cfg);
}

void app_adc_pmt_start_inst(app_adc_inst_t inst)
{
    if (inst >= APP_ADC_INST_COUNT) return;

    (void)intf_adc_start(INTF_ADC_CH(inst, 0));
}

void app_adc_pmt_stop_inst(app_adc_inst_t inst)
{
    if (inst >= APP_ADC_INST_COUNT) return;

    (void)intf_adc_stop(INTF_ADC_CH(inst, 0));
}

/* ========================================================================
 * Watchdog
 * ======================================================================== */

void app_adc_wdog_init(adc_channel_t ch, uint16_t thshd_high, uint16_t thshd_low,
                   intf_adc_wdog_cb_t cb, void *user_data)
{
    if (!app_adc_channel_is_valid(ch)) return;

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

    (void)intf_adc_init(app_adc_encoded_channel(ch), &cfg);
}

void app_adc_wdog_reenable(adc_channel_t ch)
{
    if (!app_adc_channel_is_valid(ch)) return;
    intf_adc_wdog_reenable(app_adc_encoded_channel(ch));
}
