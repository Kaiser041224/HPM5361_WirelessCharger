#include "board.h"

#include "intf_clock.h"

#include "app_buzzer.h"
#include "app_debug_rtt.h"
#include "app_gpio.h"
#include "app_hrpwm.h"
#include "app_ws2812.h"

int main(void) {
    board_init();
    intf_clock_init();
    app_gpio_init();
    app_gpio_set(PIN_DRVPWR, false);
    app_buzzer_init();
    app_ws2812_init();

    app_debug_init();
    app_debug_write("\r\n[RTT] HPM5361 WirelessCharger started\r\n");
    app_debug_write("[RTT] Initializing HRPWM...\r\n");
    pwm_init();
    app_debug_write("[RTT] PWM center-aligned output active on PA24-PA31\r\n");

    app_debug_hrpwm_run_tests();
    app_debug_write("[RTT] Entering idle loop...\r\n");

    /* 空闲循环 */
    while (1) {
        intf_clock_delay_ms(1000);
        app_debug_pwm_irq_dump_status();
    }

    return 0;
}
