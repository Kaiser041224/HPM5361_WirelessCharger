#include "app_entry.h"

#include "app_comm.h"
#include "app_control.h"

#include "app_buzzer.h"
#include "app_can.h"
#include "app_gpio.h"
#include "app_hrpwm.h"
#include "app_ws2812.h"

#include "board.h"
#include "intf_clock.h"

void app_init(void)
{
    board_init();
    intf_clock_init();

    hrpwm_init();

    app_gpio_init();
    app_gpio_set(PIN_DRVPWR, false);
    app_buzzer_init();
    app_ws2812_init();
    app_can_register_driver();
    app_can_init();

    app_control_init();
    app_comm_init();
}

void app_run_once(void)
{
    app_control_tick();
    app_comm_tick();
}
