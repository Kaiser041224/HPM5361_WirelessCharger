#include "board.h"

#include "hpm_clock_drv.h"
#include "hpm_dma_mgr.h"
#include "intf_clock.h"

#include "SEGGER_RTT.h"

#include "app_buzzer.h"
#include "app_gpio.h"
#include "app_hrpwm_example.h"
#include "app_ws2812.h"

extern int drv_ws2812_register(void);

int main(void) {
    board_init();
    intf_clock_init();
    app_gpio_init();
    app_gpio_set(PIN_DRVPWR, false);
    app_buzzer_init();

    dma_mgr_init();
    drv_ws2812_register();
    app_ws2812_init();

    SEGGER_RTT_WriteString(0, "\r\n[RTT] HPM5361 WirelessCharger started\r\n");
    SEGGER_RTT_WriteString(0, "[RTT] Initializing HRPWM...\r\n");
    pwm_init();
    SEGGER_RTT_WriteString(0, "[RTT] PWM output active on PA24-PA31\r\n");
    SEGGER_RTT_WriteString(0, "[RTT] Duty sweep: 0.0 -> 1.0 -> 0.0 (step=0.0001, delay=10ms)\r\n");

    const float duty_step = 0.001f;
    const uint32_t sweep_delay_ms = 10;
    float duty = 0.0f;
    bool rising = true;

    while (1) {
        for (pwm_pair_t pair = PWM_PAIR_0; pair < PWM_PAIR_COUNT; pair++) {
            pwm_set_duty(pair, duty);
        }

        if (rising) {
            duty += duty_step;
            if (duty >= 1.0f) {
                duty = 1.0f;
                rising = false;
            }
        } else {
            duty -= duty_step;
            if (duty <= 0.0f) {
                duty = 0.0f;
                rising = true;
            }
        }

        intf_clock_delay_ms(sweep_delay_ms);
    }

    return 0;
}
