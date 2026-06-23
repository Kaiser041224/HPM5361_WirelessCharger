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

    while (1) {
        app_run_once();

        uint32_t now_ms = intf_clock_get_cycle() / (intf_clock_get_cpu_freq() / 1000U);
        if (now_ms - last_print_ms >= 250U) {
            last_print_ms = now_ms;

            intf_adc_diag_snapshot_t diag;
            intf_adc_get_diag_snapshot(&diag);
            ctrl_diag_t d = g_ctrl_diag;

            app_debug_printf(
                "[PWR] V_IN=%.1fV I_L=%.2fA V_LINK=%.1fV | "
                "I_IN=%.2fA(calc) P_in=%.1fW target=%.1fW PID=%.2f\r\n",
                d.raw.v_in_v, d.raw.i_l_a, d.raw.v_link_v,
                d.raw.i_in_a, d.ff.p_in_w, d.ff.p_target_w, d.ff.power_pid_out);
            app_debug_printf(
                "[ISR] cyc=%-6lu dc0=%-6lu dc1=%-6lu\r\n",
                (unsigned long)g_isr_cycles_max,
                (unsigned long)diag.isr_cycles_max[0],
                (unsigned long)diag.isr_cycles_max[1]);

            g_isr_cycles_max = 0;
            intf_adc_reset_diag_max();
        }

        intf_clock_delay_ms(1);
    }
}
