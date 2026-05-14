#include "app_gpio.h"
#include "board.h"
#include "intf_clock.h"

#include "hpm_clock_drv.h"

int main(void) {
    board_init();
    intf_clock_init();
    app_gpio_init();

    while (1) {
        app_gpio_toggle(PIN_DRVPWR);
        clock_cpu_delay_us(500000);
    }

    return 0;
}
