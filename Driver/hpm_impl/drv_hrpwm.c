#include "intf_hrpwm.h"

#include "hpm_clock_drv.h"
#include "hpm_pwm_drv.h"

#include <stddef.h>

#define HRPWM_BASE HPM_PWM0
#define HRPWM_CLOCK_NAME clock_mot0
#define HRPWM_CHANNEL_COUNT (4U)

typedef struct {
    bool configured;
    uint32_t frequency_hz;
    float duty;
    bool invert_output;
    uint32_t reload;
} hrpwm_state_t;

static hrpwm_state_t hrpwm_state[PWM_SOC_PWM_MAX_COUNT];
static uint32_t hrpwm_reload;
static uint32_t hrpwm_frequency_hz;

static bool hrpwm_is_valid_channel(intf_hrpwm_ch_t ch)
{
    return ch < HRPWM_CHANNEL_COUNT;
}

static bool hrpwm_is_valid_duty(float duty)
{
    return (duty == duty) && (duty >= 0.0f) && (duty <= 1.0f);
}

static int hrpwm_apply_duty(intf_hrpwm_ch_t ch)
{
    hpm_stat_t status;

    status = pwm_update_duty_edge_aligned(HRPWM_BASE, ch, hrpwm_state[ch].duty * 100.0f);
    return (status == status_success) ? 0 : -1;
}

static int hrpwm_apply_frequency(uint32_t frequency_hz)
{
    uint32_t clock_hz;

    if (frequency_hz == 0U) {
        return -1;
    }

    clock_add_to_group(HRPWM_CLOCK_NAME, 0);
    clock_hz = clock_get_frequency(HRPWM_CLOCK_NAME);
    if (clock_hz <= frequency_hz) {
        return -1;
    }

    hrpwm_frequency_hz = frequency_hz;
    hrpwm_reload = (clock_hz / frequency_hz) - 1U;

    pwm_set_reload(HRPWM_BASE, 0, hrpwm_reload);
    pwm_set_start_count(HRPWM_BASE, 0, 0);

    for (uint8_t i = 0; i < HRPWM_CHANNEL_COUNT; i++) {
        if (hrpwm_state[i].configured && (hrpwm_apply_duty(i) != 0)) {
            return -1;
        }
    }

    return 0;
}

static int hrpwm_init(intf_hrpwm_ch_t ch, const intf_hrpwm_cfg_t *cfg)
{
    pwm_config_t pwm_config = {0};
    pwm_cmp_config_t cmp_config = {0};

    if ((cfg == NULL) || !hrpwm_is_valid_channel(ch) || !hrpwm_is_valid_duty(cfg->duty)) {
        return -1;
    }

    if (hrpwm_apply_frequency(cfg->frequency_hz) != 0) {
        return -1;
    }

    pwm_get_default_pwm_config(HRPWM_BASE, &pwm_config);
    pwm_config.enable_output = false;
    pwm_config.invert_output = cfg->invert_output;
    pwm_config.dead_zone_in_half_cycle = 0;

    cmp_config.mode = pwm_cmp_mode_output_compare;
    cmp_config.cmp = hrpwm_reload + 1U;
    cmp_config.update_trigger = pwm_shadow_register_update_on_modify;

    if (pwm_setup_waveform(HRPWM_BASE, ch, &pwm_config, ch, &cmp_config, 1) != status_success) {
        return -1;
    }

    hrpwm_state[ch].configured = true;
    hrpwm_state[ch].frequency_hz = hrpwm_frequency_hz;
    hrpwm_state[ch].reload = hrpwm_reload;
    hrpwm_state[ch].duty = cfg->duty;
    hrpwm_state[ch].invert_output = cfg->invert_output;
    return hrpwm_apply_duty(ch);
}

static int hrpwm_set_duty(intf_hrpwm_ch_t ch, float duty)
{
    if (!hrpwm_is_valid_channel(ch) || !hrpwm_state[ch].configured) {
        return -1;
    }

    if (!hrpwm_is_valid_duty(duty)) {
        return -1;
    }

    hrpwm_state[ch].duty = duty;
    return hrpwm_apply_duty(ch);
}

static int hrpwm_set_frequency(intf_hrpwm_ch_t ch, uint32_t frequency_hz)
{
    if (!hrpwm_is_valid_channel(ch) || !hrpwm_state[ch].configured) {
        return -1;
    }

    if (hrpwm_apply_frequency(frequency_hz) != 0) {
        return -1;
    }
    return 0;
}

static int hrpwm_start(intf_hrpwm_ch_t ch)
{
    if (!hrpwm_is_valid_channel(ch) || !hrpwm_state[ch].configured) {
        return -1;
    }

    pwm_enable_output(HRPWM_BASE, ch);
    pwm_start_counter(HRPWM_BASE);
    pwm_issue_shadow_register_lock_event(HRPWM_BASE);
    return 0;
}

static int hrpwm_stop(intf_hrpwm_ch_t ch)
{
    if (!hrpwm_is_valid_channel(ch)) {
        return -1;
    }

    pwm_disable_output(HRPWM_BASE, ch);
    return 0;
}

static const intf_hrpwm_t hrpwm_ops = {
    .instance_id = 0,
    .init = hrpwm_init,
    .set_duty = hrpwm_set_duty,
    .set_frequency = hrpwm_set_frequency,
    .start = hrpwm_start,
    .stop = hrpwm_stop,
};

void hpm_hrpwm_driver_register(void)
{
    intf_hrpwm_register(&hrpwm_ops);
}
