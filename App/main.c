#include "board.h"

#include "intf_clock.h"

#include "app_buzzer.h"
#include "app_can.h"
#include "app_debug_rtt.h"
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

    app_debug_write("\r\n[RTT] HPM5361 WirelessCharger started\r\n");

    while (1) {
        app_debug_adc_pmt_run_tests();
        app_debug_can_run_tests();
        intf_clock_delay_ms(1000);
    }

    return 0;
}
