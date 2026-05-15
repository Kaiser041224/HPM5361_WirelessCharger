#include "board.h"

#include "hpm_clock_drv.h"
#include "intf_clock.h"

#include "app_buzzer.h"
#include "app_gpio.h"

int main(void) {
    board_init();
    intf_clock_init();
    app_gpio_init();
    app_gpio_set(PIN_DRVPWR, true); /* Power on the driver */
    app_buzzer_init();
    while (1) {
        app_buzzer_set(true, 2000); /* Play buzzer at 2 kHz */
        intf_clock_delay_ms(500);
        app_buzzer_set(false, 0);   /* Stop buzzer */
        intf_clock_delay_ms(500);
    }

    return 0;
}
