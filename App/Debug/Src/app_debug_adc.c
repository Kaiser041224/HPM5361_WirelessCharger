#include "app_debug_adc.h"

#include "app_adc.h"
#include "app_debug_rtt.h"
#include "app_hrpwm.h"
#include "app_sampling_sync.h"
#include "hpm_common.h"

#include <stdbool.h>
#include <stdint.h>

static const char *adc_ch_names[ADC_CH_COUNT] = {
    [ADC_CH_V_IN] = "V_IN  ",   [ADC_CH_I_IN] = "I_IN  ",   [ADC_CH_I_L] = "I_L   ",
    [ADC_CH_V_LINK] = "V_LINK", [ADC_CH_I_COIL] = "I_COIL", [ADC_CH_I_LF] = "I_LF  ",
};

void app_debug_adc_dump_channels(void)
{
    app_debug_printf("[ADC]  Channel  Inst  Raw(hex)  Raw(dec)   ADC(mV)  Sense(mV)  Physical\r\n");

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

void app_debug_adc_run_tests(void)
{
    app_debug_printf("\r\n[ADC] === ADC Oneshot Channel Scan ===\r\n");

    uint16_t val[ADC_CH_COUNT];

    for (adc_channel_t ch = ADC_CH_V_IN; ch < ADC_CH_COUNT; ch++) {
        app_adc_read_raw(ch);
        app_adc_read_raw(ch);
        val[ch] = app_adc_read_raw(ch);
    }

    app_debug_printf("[ADC]  Channel  Inst  Raw(hex)  Raw(dec)   ADC(mV)\r\n");
    for (adc_channel_t ch = ADC_CH_V_IN; ch < ADC_CH_COUNT; ch++) {
        float mv = 0.0f;
        (void)app_adc_read_adc_voltage_mv(ch, &mv);
        app_debug_printf(
            "[ADC]  %s   ADC%d  0x%04X   %5u   %8.1f\r\n", adc_ch_names[ch],
            (ch <= ADC_CH_V_LINK) ? 0 : 1, val[ch], val[ch], mv);
    }
}

ATTR_PLACE_AT_NONCACHEABLE_WITH_ALIGNMENT(4) static uint32_t pmt_dma0[48];
ATTR_PLACE_AT_NONCACHEABLE_WITH_ALIGNMENT(4) static uint32_t pmt_dma1[48];

static volatile uint16_t pmt_val_adc0[4];
static volatile uint16_t pmt_val_adc1[2];
static volatile bool pmt_ready_adc0;
static volatile bool pmt_ready_adc1;
static bool adc_pmt_initialized;

static void pmt_cb_adc0(intf_adc_ch_t trig, const uint16_t *v, uint8_t n, void *u)
{
    (void)trig;
    (void)u;
    if (n >= 4U) {
        pmt_val_adc0[0] = v[0];
        pmt_val_adc0[1] = v[1];
        pmt_val_adc0[2] = v[2];
        pmt_val_adc0[3] = v[3];
        pmt_ready_adc0 = true;
    }
}

static void pmt_cb_adc1(intf_adc_ch_t trig, const uint16_t *v, uint8_t n, void *u)
{
    (void)trig;
    (void)u;
    if (n >= 2U) {
        pmt_val_adc1[0] = v[0];
        pmt_val_adc1[1] = v[1];
        pmt_ready_adc1 = true;
    }
}

void app_debug_adc_pmt_init(void)
{
    if (adc_pmt_initialized) {
        return;
    }
    adc_pmt_initialized = true;

    app_debug_printf("\r\n[ADC] === ADC PMT + PWM + TRGM Test ===\r\n");

    hrpwm_init();

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

    app_debug_printf("[ADC] PMT initialized\r\n");
}

void app_debug_adc_pmt_run_tests(void)
{
    if (!adc_pmt_initialized) {
        return;
    }

    if (!(pmt_ready_adc0 && pmt_ready_adc1)) {
        return;
    }

    pmt_ready_adc0 = false;
    pmt_ready_adc1 = false;

    uint16_t val[ADC_CH_COUNT];
    val[ADC_CH_V_IN] = pmt_val_adc0[0];
    val[ADC_CH_I_IN] = pmt_val_adc0[1];
    val[ADC_CH_I_L] = pmt_val_adc0[2];
    val[ADC_CH_V_LINK] = pmt_val_adc0[3];
    val[ADC_CH_I_COIL] = pmt_val_adc1[0];
    val[ADC_CH_I_LF] = pmt_val_adc1[1];
    app_debug_printf("[ADC]==========================================\r\n");
    app_debug_printf("[ADC]  Channel  Inst  Raw(hex)  Raw(dec)     mV\r\n");

    for (adc_channel_t ch = ADC_CH_V_IN; ch < ADC_CH_COUNT; ch++) {
        int inst = (ch >= ADC_CH_I_COIL) ? 1 : 0;
        float mv = (float) val[ch] * 3300.0f / 65535.0f;
        app_debug_printf("[ADC]  %s   ADC%d  0x%04X   %5u   %7.1f\r\n", adc_ch_names[ch],
                         inst, val[ch], val[ch], mv);
    }
}
