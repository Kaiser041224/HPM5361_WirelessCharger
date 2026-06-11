#include "app_entry.h"

#include "app_adc.h"
#include "app_comm.h"
#include "app_control.h"

#include "app_analog_signal.h"
#include "app_buzzer.h"
#include "app_can.h"
#include "app_gpio.h"
#include "app_hrpwm.h"
#include "app_ws2812.h"

#include "board.h"
#include "intf_clock.h"

void app_init(void) {
    board_init();
    intf_clock_init();

    app_gpio_init();
    app_gpio_set(PIN_DRVPWR, false);

    hrpwm_init();                /* PWM configured, NOT started (quiet for ADC cal) */
    app_adc_init();              /* ADC calibration + PMT setup in quiet environment */

    app_analog_signal_init();    /* Filters ready BEFORE PWM triggers ADC interrupts */

    hrpwm_start_all();           /* PWM starts — PMT callbacks now safe to update filters */
    app_buzzer_init();
    app_ws2812_init();

    app_can_register_driver();
    app_can_init();

    app_control_init();
    app_comm_init();
}

void app_run_once(void) {
    app_analog_signal_process();
    app_control_tick();
    app_comm_tick();
}
