#include "app_debug_adc.h"

#include "app_adc.h"
#include "app_debug_rtt.h"
#include "intf_adc.h"

#include <stdbool.h>
#include <stdint.h>

static const char* adc_ch_names[ADC_CH_COUNT] = {
    [ADC_CH_V_IN] = "V_IN  ",   [ADC_CH_I_IN] = "I_IN  ",   [ADC_CH_I_L] = "I_L   ",
    [ADC_CH_V_LINK] = "V_LINK", [ADC_CH_I_COIL] = "I_COIL", [ADC_CH_I_LF] = "I_LF  ",
};

void app_debug_adc_dump_channels(void) {
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
            (ch == ADC_CH_I_IN || ch == ADC_CH_I_L || ch == ADC_CH_V_LINK) ? 0 : 1, raw, raw, adc_mv, sense_mv, physical);
    }
}

void app_debug_adc_dump_pmt(void) {
    uint16_t val[ADC_CH_COUNT];
    bool valid = false;

    for (adc_channel_t ch = ADC_CH_V_IN; ch < ADC_CH_COUNT; ch++) {
        if (app_adc_get_pmt_raw(ch, &val[ch]) != 0) {
            val[ch] = 0;
        } else {
            valid = true;
        }
    }

    if (!valid)
        return;
    app_debug_printf("--------------------------------------------------\r\n");
    app_debug_printf("[ADC]  Channel  Inst  Raw(hex)  Raw(dec)     mV\r\n");

    for (adc_channel_t ch = ADC_CH_V_IN; ch < ADC_CH_COUNT; ch++) {
        int inst = (ch == ADC_CH_I_IN || ch == ADC_CH_I_L || ch == ADC_CH_V_LINK) ? 0 : 1;
        float mv = (float)val[ch] * 3300.0f / 65535.0f;
        app_debug_printf(
            "[ADC]  %s   ADC%d  0x%04X   %5u   %7.1f\r\n", adc_ch_names[ch], inst, val[ch], val[ch],
            mv);
    }
}

void app_debug_adc_run_tests(void) {
    app_debug_printf("\r\n[ADC] === ADC Oneshot Channel Scan ===\r\n");

    uint16_t val[ADC_CH_COUNT];

    for (adc_channel_t ch = ADC_CH_V_IN; ch < ADC_CH_COUNT; ch++) {
        app_adc_read_raw(ch);
        app_adc_read_raw(ch);
        val[ch] = app_adc_read_raw(ch);
    }
    app_debug_printf("--------------------------------------------------\r\n");
    app_debug_printf("[ADC]  Channel  Inst  Raw(hex)  Raw(dec)   ADC(mV)\r\n");
    for (adc_channel_t ch = ADC_CH_V_IN; ch < ADC_CH_COUNT; ch++) {
        float mv = 0.0f;
        (void)app_adc_read_adc_voltage_mv(ch, &mv);
        app_debug_printf(
            "[ADC]  %s   ADC%d  0x%04X   %5u   %8.1f\r\n", adc_ch_names[ch],
            (ch == ADC_CH_I_IN || ch == ADC_CH_I_L || ch == ADC_CH_V_LINK) ? 0 : 1, val[ch], val[ch], mv);
    }
}
