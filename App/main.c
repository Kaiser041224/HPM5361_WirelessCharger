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
    app_debug_dump_hrpwm_cmp();
    app_debug_write("[RTT] PWM center-aligned output active on PA24-PA31\r\n");
    app_debug_write("[RTT] Duty sweep: 0.0 -> 1.0 -> 0.0 (step=0.001, delay=4ms)\r\n");

    /* 使能PWM0和PWM1中心点中断 */
    app_debug_write("[RTT] Enabling PWM0 & PWM1 center IRQ...\r\n");
    app_debug_pwm_irq_enable(0);
    app_debug_pwm_irq_enable(1);

    const float duty_step = 0.001f;
    const uint32_t sweep_delay_ms = 4;
    const uint32_t irq_dump_interval_ms = 1000;
    uint32_t irq_dump_timer = 0;
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

        /* 定期打印中断计数 */
        irq_dump_timer += sweep_delay_ms;
        if (irq_dump_timer >= irq_dump_interval_ms) {
            irq_dump_timer = 0;
            app_debug_pwm_irq_dump_status();
        }
    }

    return 0;
}
