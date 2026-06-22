#include "app_adc.h"
#include "app_control.h"
#include "app_debug_rtt.h"
#include "app_entry.h"
#include "intf_adc.h"
#include "intf_clock.h"

extern volatile uint32_t g_isr_cycles_max;
extern volatile uint32_t g_isr_cycles_max_voltage;
extern volatile uint32_t g_isr_cycles_max_power;

static const char *adc_names[ADC_CH_COUNT] = {
    "V_IN  ", "I_IN  ", "I_L   ", "V_LINK", "I_COIL", "I_LF  ",
};
static const char *adc_units[ADC_CH_COUNT] = {
    "V", "A", "A", "V", "A", "A",
};

int main(void) {
    app_init();
    app_debug_printf("\r\n[MAIN] HPM5361 WirelessCharger started\r\n");

    uint32_t last_print_ms = 0;
    uint32_t loop_cnt = 0;

    while (1) {
        app_run_once();
        loop_cnt++;

        uint32_t now_ms = intf_clock_get_cycle() / (intf_clock_get_cpu_freq() / 1000U);
        if (now_ms - last_print_ms >= 250U) {
            last_print_ms = now_ms;

            intf_adc_diag_snapshot_t diag;
            intf_adc_get_diag_snapshot(&diag);
            ctrl_diag_t d = g_ctrl_diag;

            app_debug_printf("\r\n====== ADC 250ms ======\r\n");
            app_debug_printf(" Ch  Name     Raw      Phys       Unit\r\n");

            for (adc_channel_t ch = ADC_CH_V_IN; ch < ADC_CH_COUNT; ch++) {
                uint16_t raw = 0;
                app_adc_get_pmt_raw(ch, &raw);

                float phys = 0.0f;
                switch (ch) {
                case ADC_CH_V_IN:   phys = d.raw.v_in_v;   break;
                case ADC_CH_I_IN:   phys = d.raw.i_in_a;   break;
                case ADC_CH_I_L:    phys = d.raw.i_l_a;    break;
                case ADC_CH_V_LINK: phys = d.raw.v_link_v;  break;
                case ADC_CH_I_COIL: phys = d.raw.i_coil_a; break;
                case ADC_CH_I_LF:   phys = d.raw.i_lf_a;   break;
                default: break;
                }

                app_debug_printf("  %u  %s %5u   %9.3f   %s\r\n",
                                 (unsigned)ch, adc_names[ch], (unsigned)raw, phys, adc_units[ch]);
            }

            app_debug_printf("--- power -----------------\r\n");
            app_debug_printf(" I_IN cal:  gain=%.6f  offset=%.3f\r\n",
                             d.cal.i_in_cal_gain, d.cal.i_in_cal_offset);
            uint16_t slots[4];
            app_adc_get_pmt_adc1_slots(slots);
            app_debug_printf(" ADC1 DMA: s0(dmy)=%5u  s1(VLK)=%5u  s2(IL)=%5u  s3(IIN)=%5u\r\n",
                             (unsigned)slots[0], (unsigned)slots[1],
                             (unsigned)slots[2], (unsigned)slots[3]);
            app_debug_printf(" P_in=%.3fW  P_target=%.3fW  PID=%.3f\r\n",
                             d.ff.p_in_w, d.ff.p_target_w, d.ff.power_pid_out);

            app_debug_printf("--- ISR -------\r\n");
            app_debug_printf(" ADC0 Irq=%-6lu ADC1 Irq=%-6lu  viaADC0=%-6lu\r\n",
                             (unsigned long)diag.irq_entry[0],
                             (unsigned long)diag.irq_entry[1],
                             (unsigned long)diag.adc1_handled_in_adc0_irq);
            app_debug_printf(" ADC0 cb=%-6lu  inv=%lu(%lu/%lu/%lu)\r\n",
                             (unsigned long)diag.pmt_callback[0],
                             (unsigned long)diag.pmt_invalid[0],
                             (unsigned long)diag.pmt_invalid_cycle[0],
                             (unsigned long)diag.pmt_invalid_trig[0],
                             (unsigned long)diag.pmt_invalid_channel[0]);
            app_debug_printf(" ADC1 cb=%-6lu  inv=%lu(%lu/%lu/%lu)\r\n",
                             (unsigned long)diag.pmt_callback[1],
                             (unsigned long)diag.pmt_invalid[1],
                             (unsigned long)diag.pmt_invalid_cycle[1],
                             (unsigned long)diag.pmt_invalid_trig[1],
                             (unsigned long)diag.pmt_invalid_channel[1]);
            app_debug_printf(" ISR max: cur=%-7lu dc0=%-7lu dc1=%-7lu\r\n",
                             (unsigned long)g_isr_cycles_max,
                             (unsigned long)diag.isr_cycles_max[0],
                             (unsigned long)diag.isr_cycles_max[1]);
            app_debug_printf("          volt=%-7lu pwr=%-7lu\r\n",
                             (unsigned long)g_isr_cycles_max_voltage,
                             (unsigned long)g_isr_cycles_max_power);

            g_isr_cycles_max = 0;
            g_isr_cycles_max_voltage = 0;
            g_isr_cycles_max_power = 0;
            intf_adc_reset_diag_max();
        }

        intf_clock_delay_ms(1);
    }
}
