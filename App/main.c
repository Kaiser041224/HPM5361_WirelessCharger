#include "app_debug.h"
#include "app_entry.h"
#include "intf_clock.h"

int main(void) {
    app_init();

    /* ---- 测试阶段 ---- */
    app_debug_init();
    app_debug_printf("\r\n[MAIN] HPM5361 WirelessCharger started\r\n");
    /* -------------------------- */

    while (1) {
        app_debug_run_once();
        intf_clock_delay_ms(500);
    }
}
