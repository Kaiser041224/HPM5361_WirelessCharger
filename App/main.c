#include "app_control.h"
#include "app_debug_rtt.h"
#include "app_entry.h"
#include "intf_clock.h"

#include <string.h>

int main(void) {
    app_init();

    app_debug_printf("\r\n[MAIN] HPM5361 WirelessCharger started\r\n");

    uint32_t last_print_ms = 0;

    while (1) {
        app_run_once();

        uint32_t now_ms = intf_clock_get_cycle() / (intf_clock_get_cpu_freq() / 1000U);
        if (now_ms - last_print_ms >= 250U) {
            last_print_ms = now_ms;

            ctrl_diag_t snap;
            memcpy(&snap, (const void*)&g_ctrl_diag, sizeof(snap));

            app_debug_printf("-------------------------------------------------------\r\n");
            app_debug_printf("  Signal      Raw(A/V)  Filt(A/V)  Unit\r\n");
            app_debug_printf("-------------------------------------------------------\r\n");
            app_debug_printf(
                "  I_L         %7.3f   %7.3f    A\r\n", snap.raw.i_l_a, snap.filt.i_l_a);
            app_debug_printf(
                "  V_LINK      %7.3f   %7.3f    V\r\n", snap.raw.v_link_v, snap.filt.v_link_v);
            app_debug_printf(
                "  I_IN        %7.3f   %7.3f    A\r\n", snap.raw.i_in_a, snap.filt.i_in_a);
            app_debug_printf(
                "  V_IN        %7.3f   %7.3f    V\r\n", snap.raw.v_in_v, snap.filt.v_in_v);
            app_debug_printf(
                "  I_COIL      %7.3f   %7.3f    A\r\n", snap.raw.i_coil_a, snap.filt.i_coil_a);
            app_debug_printf(
                "  I_LF        %7.3f   %7.3f    A\r\n", snap.raw.i_lf_a, snap.filt.i_lf_a);
            app_debug_printf("-------------------------------------------------------\r\n");
            app_debug_printf("  duty_bb=%8.5f  duty_lcc=%8.5f\r\n", snap.duty.bb, snap.duty.lcc);
            app_debug_printf("-------------------------------------------------------\r\n");
        }

        intf_clock_delay_ms(1);
    }
}
