#include "board.h"

#include "hpm_clock_drv.h"
#include "hpm_dma_mgr.h"
#include "intf_clock.h"

#include "app_buzzer.h"
#include "app_gpio.h"
#include "app_ws2812.h"

extern int drv_ws2812_register(void);

int main(void) {
    board_init();
    intf_clock_init();
    app_gpio_init();
    app_gpio_set(PIN_DRVPWR, true);
    app_buzzer_init();

    dma_mgr_init();
    drv_ws2812_register();
    app_ws2812_init();

    while (1) {
        app_ws2812_rainbow(16);
        intf_clock_delay_ms(20);
    }

    return 0;
}
