#include "board.h"

#include "hpm_clock_drv.h"
#include "hpm_dma_mgr.h"
#include "hpm_pwm_drv.h"
#include "intf_clock.h"

#include "SEGGER_RTT.h"

#include "app_buzzer.h"
#include "app_gpio.h"
#include "app_hrpwm.h"
#include "app_ws2812.h"

#include <stdio.h>

extern int drv_ws2812_register(void);
extern void hpm_hrpwm_driver_register(void);

#define APP_HRPWM_CMP_START_INDEX(pwm_index) ((uint8_t)((pwm_index) * 2U))

typedef struct {
    PWM_Type *base;
    const char *name;
    uint8_t pwm_index;
} app_hrpwm_probe_t;

static const app_hrpwm_probe_t app_hrpwm_probes[] = {
    {.base = BOARD_APP_HRPWM0, .name = "PWM0_PAIR0", .pwm_index = BOARD_APP_HRPWM_PWM0_PAIR0_OUT},
    {.base = BOARD_APP_HRPWM0, .name = "PWM0_PAIR1", .pwm_index = BOARD_APP_HRPWM_PWM0_PAIR1_OUT},
    {.base = BOARD_APP_HRPWM1, .name = "PWM1_PAIR0", .pwm_index = BOARD_APP_HRPWM_PWM1_PAIR0_OUT},
    {.base = BOARD_APP_HRPWM1, .name = "PWM1_PAIR1", .pwm_index = BOARD_APP_HRPWM_PWM1_PAIR1_OUT},
};

static const char *app_hrpwm_detect_alignment(uint32_t reload, uint32_t cmp_begin, uint32_t cmp_end)
{
    if (cmp_end == reload) {
        return "EDGE-like";
    }

    if ((cmp_begin + cmp_end == reload) || (cmp_begin + cmp_end + 1U == reload)) {
        return "CENTER-like";
    }

    return "UNKNOWN";
}

static void app_hrpwm_dump_cmp_state(void)
{
    char log_buf[160];

    SEGGER_RTT_WriteString(0, "[RTT] HRPWM compare register snapshot\r\n");

    for (size_t i = 0; i < sizeof(app_hrpwm_probes) / sizeof(app_hrpwm_probes[0]); i++) {
        const app_hrpwm_probe_t *probe = &app_hrpwm_probes[i];
        uint8_t cmp_start = APP_HRPWM_CMP_START_INDEX(probe->pwm_index);
        uint32_t reload = pwm_get_reload_val(probe->base);
        uint32_t cmp_begin = pwm_cmp_get_cmp_value(probe->base, cmp_start);
        uint32_t cmp_end = pwm_cmp_get_cmp_value(probe->base, cmp_start + 1U);
        const char *align = app_hrpwm_detect_alignment(reload, cmp_begin, cmp_end);

        (void)snprintf(log_buf,
                       sizeof(log_buf),
                       "[RTT] %s: reload=%lu cmp[%u]=%lu cmp[%u]=%lu => %s\r\n",
                       probe->name,
                       (unsigned long)reload,
                       (unsigned int)cmp_start,
                       (unsigned long)cmp_begin,
                       (unsigned int)(cmp_start + 1U),
                       (unsigned long)cmp_end,
                       align);
        SEGGER_RTT_WriteString(0, log_buf);
    }
}

int main(void) {
    board_init();
    intf_clock_init();
    app_gpio_init();
    app_gpio_set(PIN_DRVPWR, false);
    app_buzzer_init();

    dma_mgr_init();
    drv_ws2812_register();
    hpm_hrpwm_driver_register();
    app_ws2812_init();

    SEGGER_RTT_WriteString(0, "\r\n[RTT] HPM5361 WirelessCharger started\r\n");
    SEGGER_RTT_WriteString(0, "[RTT] Initializing HRPWM...\r\n");
    pwm_init();
    app_hrpwm_dump_cmp_state();
    SEGGER_RTT_WriteString(0, "[RTT] PWM center-aligned output active on PA24-PA31\r\n");
    SEGGER_RTT_WriteString(0, "[RTT] Duty sweep: 0.0 -> 1.0 -> 0.0 (step=0.001, delay=10ms)\r\n");

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
