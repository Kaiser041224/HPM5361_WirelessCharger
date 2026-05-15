#include "app_buzzer.h"
#include "intf_gpwm.h"

#define APP_BUZZER_GPWM_CH (3U)
#define APP_BUZZER_DUTY    (0.5f)

extern void hpm_gpwm_driver_register(void);

static bool buzzer_initialized;

void app_buzzer_init(void)
{
    intf_gpwm_cfg_t cfg = {
        .frequency_hz = APP_BUZZER_DEFAULT_FREQ_HZ,
        .duty = APP_BUZZER_DUTY,
        .invert_output = false,
    };

    hpm_gpwm_driver_register();

    if (intf_gpwm_init(APP_BUZZER_GPWM_CH, &cfg) == 0) {
        intf_gpwm_force_low(APP_BUZZER_GPWM_CH);
        buzzer_initialized = true;
    }
}

int app_buzzer_set(bool enabled, uint32_t frequency_hz)
{
    if (!buzzer_initialized) {
        return -1;
    }

    if (!enabled) {
        return intf_gpwm_force_low(APP_BUZZER_GPWM_CH);
    }

    if (frequency_hz == 0U) {
        frequency_hz = APP_BUZZER_DEFAULT_FREQ_HZ;
    }

    if (intf_gpwm_set_frequency(APP_BUZZER_GPWM_CH, frequency_hz) != 0) {
        return -1;
    }

    return intf_gpwm_force_release(APP_BUZZER_GPWM_CH);
}
