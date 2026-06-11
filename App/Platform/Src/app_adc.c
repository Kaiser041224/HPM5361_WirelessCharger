/*
 * ADC App Layer - Full PMT Initialization
 *
 * Copyright (c) 2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "app_adc.h"
#include "app_analog_signal.h"
#include "app_hrpwm.h"
#include "irq_profiler.h"

#include "intf_adc.h"
#include "intf_hrpwm.h"
#include "intf_trgm.h"

#include <stddef.h>
#include <string.h>

static irq_prof_id_t g_prof_adc0;
static irq_prof_id_t g_prof_adc1;

/* ============================================================================
 * Channel Mapping
 * ============================================================================ */

typedef struct {
    uint8_t inst;
    uint8_t hw_ch;
} app_adc_map_t;

/*
 * ADC0 (PWM0 148kHz): V_IN + I_COIL + I_LF
 * ADC1 (PWM1 200kHz): V_LINK + I_L + I_IN
 */
static const app_adc_map_t adc_map[ADC_CH_COUNT] = {
    [ADC_CH_V_IN]   = {.inst = APP_ADC_INST_0, .hw_ch = 6},
    [ADC_CH_I_IN]   = {.inst = APP_ADC_INST_1, .hw_ch = 11},
    [ADC_CH_I_L]    = {.inst = APP_ADC_INST_1, .hw_ch = 2},
    [ADC_CH_V_LINK] = {.inst = APP_ADC_INST_1, .hw_ch = 3},
    [ADC_CH_I_COIL] = {.inst = APP_ADC_INST_0, .hw_ch = 4},
    [ADC_CH_I_LF]   = {.inst = APP_ADC_INST_0, .hw_ch = 5},
};

/* ============================================================================
 * Calibration State
 * ============================================================================ */

static app_adc_calibration_t adc_calibration[ADC_CH_COUNT] = {
    [ADC_CH_V_IN]   = {.sense_gain = 1.0f, .sense_offset_mv = 0.0f, .physical_gain = 1.0f, .physical_offset = 0.0f},
    [ADC_CH_I_IN]   = {.sense_gain = 1.0f, .sense_offset_mv = 0.0f, .physical_gain = 1.0f, .physical_offset = 0.0f},
    [ADC_CH_I_L]    = {.sense_gain = 1.0f, .sense_offset_mv = 0.0f, .physical_gain = 1.0f, .physical_offset = 0.0f},
    [ADC_CH_V_LINK] = {.sense_gain = 1.0f, .sense_offset_mv = 0.0f, .physical_gain = 1.0f, .physical_offset = 0.0f},
    [ADC_CH_I_COIL] = {.sense_gain = 1.0f, .sense_offset_mv = 0.0f, .physical_gain = 1.0f, .physical_offset = 0.0f},
    [ADC_CH_I_LF]   = {.sense_gain = 1.0f, .sense_offset_mv = 0.0f, .physical_gain = 1.0f, .physical_offset = 0.0f},
};

/* ============================================================================
 * Helpers
 * ============================================================================ */

static bool app_adc_channel_is_valid(adc_channel_t ch)
{
    return ch < ADC_CH_COUNT;
}

static intf_adc_ch_t app_adc_encoded_channel(adc_channel_t ch)
{
    return INTF_ADC_CH(adc_map[ch].inst, adc_map[ch].hw_ch);
}

/* ============================================================================
 * PMT Raw Cache (written by ISR callbacks, read by debug / control)
 * ============================================================================ */

static volatile uint16_t pmt_raw_cache[ADC_CH_COUNT];

int app_adc_get_pmt_raw(adc_channel_t ch, uint16_t *raw)
{
    if (!app_adc_channel_is_valid(ch) || raw == NULL) return -1;
    *raw = pmt_raw_cache[ch];
    return 0;
}

/* ============================================================================
 * PMT DMA Buffers (non-cacheable, 4-byte aligned)
 * ============================================================================ */

static uint32_t pmt_dma0[APP_ADC_PMT_DMA_BUFF_LEN] __attribute__((section(".noncacheable"), aligned(4)));
static uint32_t pmt_dma1[APP_ADC_PMT_DMA_BUFF_LEN] __attribute__((section(".noncacheable"), aligned(4)));

/* ============================================================================
 * PMT Callbacks → write to pmt_raw_cache
 * ============================================================================ */

static void app_adc_pmt_cb_adc0(intf_adc_ch_t trig, const uint16_t *values, uint8_t count, void *user_data)
{
    IRQ_PROF_ENTER(g_prof_adc0);

    (void)trig;
    (void)user_data;

    static const adc_channel_t slot_to_logic[4] = {
        [0] = ADC_CH_COUNT,
        [1] = ADC_CH_V_IN,
        [2] = ADC_CH_I_COIL,
        [3] = ADC_CH_I_LF,
    };

    for (uint8_t i = 1; i < count && i < 4U; i++) {
        if (slot_to_logic[i] < ADC_CH_COUNT) {
            pmt_raw_cache[slot_to_logic[i]] = values[i];
        }
    }

    IRQ_PROF_EXIT(g_prof_adc0);
}

static void app_adc_pmt_cb_adc1(intf_adc_ch_t trig, const uint16_t *values, uint8_t count, void *user_data)
{
    IRQ_PROF_ENTER(g_prof_adc1);

    (void)trig;
    (void)user_data;

    static const adc_channel_t slot_to_logic[4] = {
        [0] = ADC_CH_COUNT,
        [1] = ADC_CH_V_LINK,
        [2] = ADC_CH_I_L,
        [3] = ADC_CH_I_IN,
    };

    for (uint8_t i = 1; i < count && i < 4U; i++) {
        if (slot_to_logic[i] < ADC_CH_COUNT) {
            pmt_raw_cache[slot_to_logic[i]] = values[i];
        }
    }

    IRQ_PROF_EXIT(g_prof_adc1);
}

/* ============================================================================
 * Public API: app_adc_init (Full PMT with TRGM + HRPWM trigger chain)
 * ============================================================================ */

extern void hpm_adc_driver_register(void);
extern void hpm_trgm_driver_register(void);

void app_adc_init(void)
{
    g_prof_adc0 = irq_prof_register("ADC0_PMT");
    g_prof_adc1 = irq_prof_register("ADC1_PMT");
    irq_prof_measure_overhead();

    hpm_adc_driver_register();
    hpm_trgm_driver_register();

    /* ================================================================
     * Steps 1-2: ADC0 + ADC1 full init (calibration, channels, PMT,
     *            DMA, interrupts) — must complete before any trigger
     *            signal reaches the ADC inputs.
     * ================================================================ */

    /* ADC0 PMT — 4 channels (dummy, V_IN, I_COIL, I_LF) on trig_ch=0 (PWM0 148kHz) */
    {
        memset(pmt_dma0, 0, sizeof(pmt_dma0));
        intf_adc_cfg_t cfg = {
            .resolution       = INTF_ADC_RES_DEFAULT,
            .mode             = INTF_ADC_MODE_PMT,
            .sample_cycle     = INTF_ADC_DEFAULT_SAMPLE_CYCLE,
            .clock_div        = INTF_ADC_DEFAULT_CLOCK_DIV,
            .vref_mv          = INTF_ADC_DEFAULT_VREF_MV,
            .dma_en           = true,
            .dma_buff         = pmt_dma0,
            .dma_buff_len     = APP_ADC_PMT_DMA_BUFF_LEN,
            .pmt_trig_ch      = APP_ADC_PMT_ADC0_TRIG_CH,
            .pmt_ch_count     = APP_ADC_PMT_ADC0_CH_COUNT,
            .pmt_cb           = app_adc_pmt_cb_adc0,
            .pmt_cb_user_data = NULL,
        };
        cfg.pmt_ch_list[0] = 15U;
        cfg.pmt_ch_list[1] = 6U;
        cfg.pmt_ch_list[2] = 4U;
        cfg.pmt_ch_list[3] = 5U;
        (void)intf_adc_init(INTF_ADC_CH(APP_ADC_INST_0, 0), &cfg);
    }

    /* ADC1 PMT — 4 channels (dummy, V_LINK, I_L, I_IN) on trig_ch=3 (PWM1 200kHz) */
    {
        memset(pmt_dma1, 0, sizeof(pmt_dma1));
        intf_adc_cfg_t cfg = {
            .resolution       = INTF_ADC_RES_DEFAULT,
            .mode             = INTF_ADC_MODE_PMT,
            .sample_cycle     = INTF_ADC_DEFAULT_SAMPLE_CYCLE,
            .clock_div        = INTF_ADC_DEFAULT_CLOCK_DIV,
            .vref_mv          = INTF_ADC_DEFAULT_VREF_MV,
            .dma_en           = true,
            .dma_buff         = pmt_dma1,
            .dma_buff_len     = APP_ADC_PMT_DMA_BUFF_LEN,
            .pmt_trig_ch      = APP_ADC_PMT_ADC1_TRIG_CH,
            .pmt_ch_count     = APP_ADC_PMT_ADC1_CH_COUNT,
            .pmt_cb           = app_adc_pmt_cb_adc1,
            .pmt_cb_user_data = NULL,
        };
        cfg.pmt_ch_list[0] = 15U;
        cfg.pmt_ch_list[1] = 3U;
        cfg.pmt_ch_list[2] = 2U;
        cfg.pmt_ch_list[3] = 11U;
        (void)intf_adc_init(INTF_ADC_CH(APP_ADC_INST_1, 0), &cfg);
    }

    /* ================================================================
     * Step 3: TRGM routing — connects PWM compare outputs to ADC
     *         preemption trigger inputs. PWM counters are NOT yet
     *         running, so no signal flows.
     * ================================================================ */
    (void)intf_trgm_connect(INTF_TRGM_SRC_PWM0_CH8REF, INTF_TRGM_DST_ADC_PTRGI0A);
    (void)intf_trgm_connect(INTF_TRGM_SRC_PWM1_CH8REF, INTF_TRGM_DST_ADC_PTRGI1A);

    /* ================================================================
     * Step 4: HRPWM trigger compare — configures CMP8 shadow registers.
     *         PWM counters are NOT running; CMP8 takes effect on first
     *         counter start (app_hrpwm_start_all).
     * ================================================================ */
    (void)intf_hrpwm_config_trigger_cmp(HRPWM_INST_0,
                                        APP_ADC_PMT_TRIGGER_CMP_INDEX,
                                        APP_ADC_PMT_POSITION_RATIO_ADC0);
    (void)intf_hrpwm_config_trigger_cmp(HRPWM_INST_1,
                                        APP_ADC_PMT_TRIGGER_CMP_INDEX,
                                        APP_ADC_PMT_POSITION_RATIO_ADC1);
}

/* ============================================================================
 * Oneshot Read API
 * ============================================================================ */

uint16_t app_adc_read_raw(adc_channel_t ch)
{
    if (!app_adc_channel_is_valid(ch)) return 0;

    uint16_t raw = 0;
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

/* ============================================================================
 * Calibration API
 * ============================================================================ */

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

/* ============================================================================
 * PMT Control API
 * ============================================================================ */

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

/* ============================================================================
 * Watchdog API
 * ============================================================================ */

void app_adc_wdog_init(adc_channel_t ch, uint16_t thshd_high, uint16_t thshd_low,
                       intf_adc_wdog_cb_t cb, void *user_data)
{
    if (!app_adc_channel_is_valid(ch)) return;

    intf_adc_cfg_t cfg = {
        .resolution     = INTF_ADC_RES_DEFAULT,
        .mode           = INTF_ADC_MODE_ONESHOT,
        .sample_cycle   = INTF_ADC_DEFAULT_SAMPLE_CYCLE,
        .clock_div      = INTF_ADC_DEFAULT_CLOCK_DIV,
        .vref_mv        = INTF_ADC_DEFAULT_VREF_MV,
        .wdog_en        = true,
        .wdog_thshd_high = thshd_high,
        .wdog_thshd_low  = thshd_low,
        .wdog_cb        = cb,
        .wdog_cb_user_data = user_data,
    };

    (void)intf_adc_init(app_adc_encoded_channel(ch), &cfg);
}

void app_adc_wdog_reenable(adc_channel_t ch)
{
    if (!app_adc_channel_is_valid(ch)) return;
    intf_adc_wdog_reenable(app_adc_encoded_channel(ch));
}
