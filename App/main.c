#include "board.h"

#include "intf_clock.h"

#include "app_buzzer.h"
#include "app_can.h"
#include "app_debug.h"
#include "app_gpio.h"
#include "app_ws2812.h"

int main(void) {
    board_init();
    intf_clock_init();
    app_gpio_init();
    app_gpio_set(PIN_DRVPWR, false);
    app_buzzer_init();
    app_ws2812_init();
    app_debug_init();
    app_can_init();

    app_debug_printf("\r\n[MAIN] HPM5361 WirelessCharger started\r\n");

    while (1) {
        app_debug_run_once();
        intf_clock_delay_ms(1000);
    }

    return 0;
}
