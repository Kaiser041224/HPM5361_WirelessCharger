#include "app_control.h"
#include "app_debug_rtt.h"
#include "app_entry.h"
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

            app_debug_printf("[ISR] max=%lu cycles (%lu ns)\r\n",
                (unsigned long)g_isr_cycles_max,
                (unsigned long)(g_isr_cycles_max * 1000U / 480U));
            g_isr_cycles_max = 0;
        }

        intf_clock_delay_ms(1);
    }
}
