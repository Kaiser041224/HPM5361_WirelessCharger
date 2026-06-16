#include "app_control.h"
#include "app_debug_rtt.h"
#include "app_entry.h"
#include "intf_adc.h"
#include "intf_clock.h"

extern volatile uint32_t g_isr_cycles_max;

int main(void) {
    app_init();

    app_debug_printf("\r\n[MAIN] HPM5361 WirelessCharger started\r\n");

    uint32_t last_print_ms = 0;
    uint32_t loop_cnt = 0;
    uint32_t last_loop_cnt = 0;

    while (1) {
        app_run_once();
        loop_cnt++;

        uint32_t now_ms = intf_clock_get_cycle() / (intf_clock_get_cpu_freq() / 1000U);
        if (now_ms - last_print_ms >= 250U) {
            last_print_ms = now_ms;

            intf_adc_diag_snapshot_t diag;
            intf_adc_get_diag_snapshot(&diag);

            uint32_t loops_per_250ms = loop_cnt - last_loop_cnt;
            last_loop_cnt = loop_cnt;

            app_debug_printf(
                "[ISR] cb=%lu full=%lu/%lu | main_loop=%lu/250ms | ADC1 miss: cycle=%lu trig=%lu ch=%lu invalid=%lu\r\n",
                (unsigned long)g_isr_cycles_max,
                (unsigned long)diag.isr_cycles_max[0],
                (unsigned long)diag.isr_cycles_max[1],
                (unsigned long)loops_per_250ms,
                (unsigned long)diag.pmt_invalid_cycle[1],
                (unsigned long)diag.pmt_invalid_trig[1],
                (unsigned long)diag.pmt_invalid_channel[1],
                (unsigned long)diag.pmt_invalid[1]);
            g_isr_cycles_max = 0;
            intf_adc_reset_diag_max();
        }

        intf_clock_delay_ms(1);
    }
}
