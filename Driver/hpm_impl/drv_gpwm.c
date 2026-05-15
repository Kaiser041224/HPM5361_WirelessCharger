#include "intf_gpwm.h"

#include "hpm_clock_drv.h"
#include "hpm_gptmr_drv.h"

#include <stddef.h>

#define GPWM_BASE HPM_GPTMR0
#define GPWM_CLOCK_NAME clock_gptmr0
#define GPWM_CHANNEL_COUNT (4U)
#define GPWM_FIRST_OUTPUT_CHANNEL (2U)
#define GPWM_CAPTURE_CHANNEL (1U)

typedef struct {
    bool configured;
    bool started;
    bool has_first_edge;
    intf_gpwm_capture_edge_t edge;
    uint32_t first_count;
    uint32_t period_ticks;
} gpwm_capture_state_t;

typedef struct {
    bool configured;
    uint32_t frequency_hz;
    float duty;
    bool invert_output;
    uint32_t reload;
} gpwm_state_t;

static gpwm_state_t gpwm_state[GPWM_CHANNEL_COUNT];
static gpwm_capture_state_t gpwm_capture_state[GPWM_CHANNEL_COUNT];

static bool gpwm_is_valid_channel(intf_gpwm_ch_t ch)
{
    return (ch >= GPWM_FIRST_OUTPUT_CHANNEL) && (ch < GPWM_CHANNEL_COUNT);
}

static bool gpwm_is_valid_capture_channel(intf_gpwm_ch_t ch)
{
    return ch == GPWM_CAPTURE_CHANNEL;
}

static bool gpwm_is_valid_duty(float duty)
{
    return (duty == duty) && (duty >= 0.0f) && (duty <= 1.0f);
}

static int gpwm_apply_duty(intf_gpwm_ch_t ch, float duty)
{
    uint32_t cmp;

    if (!gpwm_is_valid_channel(ch) || !gpwm_state[ch].configured) {
        return -1;
    }

    if (!gpwm_is_valid_duty(duty)) {
        return -1;
    }

    cmp = (uint32_t)((float)gpwm_state[ch].reload * duty);
    gpwm_state[ch].duty = duty;

    gptmr_update_cmp(GPWM_BASE, ch, 0, cmp);
    gptmr_update_cmp(GPWM_BASE, ch, 1, gpwm_state[ch].reload);
    return 0;
}

static int gpwm_init(intf_gpwm_ch_t ch, const intf_gpwm_cfg_t *cfg)
{
    gptmr_channel_config_t config;
    uint32_t clock_hz;

    if ((cfg == NULL) || !gpwm_is_valid_channel(ch) ||
        (cfg->frequency_hz == 0U) || !gpwm_is_valid_duty(cfg->duty)) {
        return -1;
    }

    clock_add_to_group(GPWM_CLOCK_NAME, 0);
    clock_hz = clock_get_frequency(GPWM_CLOCK_NAME);
    if (clock_hz < cfg->frequency_hz) {
        return -1;
    }

    gpwm_state[ch].reload = clock_hz / cfg->frequency_hz;
    gpwm_state[ch].frequency_hz = cfg->frequency_hz;
    gpwm_state[ch].invert_output = cfg->invert_output;

    gptmr_channel_get_default_config(GPWM_BASE, &config);
    config.mode = gptmr_work_mode_no_capture;
    config.reload = gpwm_state[ch].reload;
    config.cmp_initial_polarity_high = cfg->invert_output;
    config.enable_cmp_output = false;

    gptmr_stop_counter(GPWM_BASE, ch);
    if (gptmr_channel_config(GPWM_BASE, ch, &config, false) != status_success) {
        return -1;
    }
    gptmr_channel_reset_count(GPWM_BASE, ch);

    gpwm_state[ch].configured = true;
    return gpwm_apply_duty(ch, cfg->duty);
}

static gptmr_work_mode_t gpwm_get_capture_mode(intf_gpwm_capture_edge_t edge)
{
    switch (edge) {
    case INTF_GPWM_CAPTURE_EDGE_RISING:
        return gptmr_work_mode_capture_at_rising_edge;
    case INTF_GPWM_CAPTURE_EDGE_FALLING:
        return gptmr_work_mode_capture_at_falling_edge;
    case INTF_GPWM_CAPTURE_EDGE_BOTH:
        return gptmr_work_mode_capture_at_both_edge;
    default:
        return gptmr_work_mode_capture_at_rising_edge;
    }
}

static gptmr_counter_type_t gpwm_get_capture_counter_type(intf_gpwm_capture_edge_t edge)
{
    switch (edge) {
    case INTF_GPWM_CAPTURE_EDGE_FALLING:
        return gptmr_counter_type_falling_edge;
    case INTF_GPWM_CAPTURE_EDGE_RISING:
    case INTF_GPWM_CAPTURE_EDGE_BOTH:
    default:
        return gptmr_counter_type_rising_edge;
    }
}

static uint32_t gpwm_calc_delta(uint32_t first, uint32_t next)
{
    return (next >= first) ? (next - first) : ((UINT32_MAX - first) + next + 1U);
}

static int gpwm_capture_init(intf_gpwm_ch_t ch, const intf_gpwm_capture_cfg_t *cfg)
{
    gptmr_channel_config_t config;

    if ((cfg == NULL) || !gpwm_is_valid_capture_channel(ch)) {
        return -1;
    }

    if (cfg->edge > INTF_GPWM_CAPTURE_EDGE_BOTH) {
        return -1;
    }

    clock_add_to_group(GPWM_CLOCK_NAME, 0);
    gptmr_channel_get_default_config(GPWM_BASE, &config);
    config.mode = gpwm_get_capture_mode(cfg->edge);
    config.enable_cmp_output = false;

    gptmr_stop_counter(GPWM_BASE, ch);
    gptmr_disable_irq(GPWM_BASE, GPTMR_CH_CAP_IRQ_MASK(ch));
    gptmr_clear_status(GPWM_BASE, GPTMR_CH_CAP_STAT_MASK(ch));
    if (gptmr_channel_config(GPWM_BASE, ch, &config, false) != status_success) {
        return -1;
    }
    gptmr_channel_reset_count(GPWM_BASE, ch);

    gpwm_capture_state[ch].configured = true;
    gpwm_capture_state[ch].started = false;
    gpwm_capture_state[ch].has_first_edge = false;
    gpwm_capture_state[ch].edge = cfg->edge;
    gpwm_capture_state[ch].first_count = 0;
    gpwm_capture_state[ch].period_ticks = 0;
    return 0;
}

static int gpwm_set_duty(intf_gpwm_ch_t ch, float duty)
{
    return gpwm_apply_duty(ch, duty);
}

static int gpwm_set_frequency(intf_gpwm_ch_t ch, uint32_t frequency_hz)
{
    uint32_t clock_hz;

    if (!gpwm_is_valid_channel(ch) || !gpwm_state[ch].configured) {
        return -1;
    }

    if (frequency_hz == 0U) {
        return -1;
    }

    clock_add_to_group(GPWM_CLOCK_NAME, 0);
    clock_hz = clock_get_frequency(GPWM_CLOCK_NAME);
    if (clock_hz < frequency_hz) {
        return -1;
    }

    gpwm_state[ch].reload = clock_hz / frequency_hz;
    gpwm_state[ch].frequency_hz = frequency_hz;

    gptmr_stop_counter(GPWM_BASE, ch);
    gptmr_channel_config_update_reload(GPWM_BASE, ch, gpwm_state[ch].reload);
    gptmr_channel_reset_count(GPWM_BASE, ch);
    gpwm_apply_duty(ch, gpwm_state[ch].duty);

    gptmr_start_counter(GPWM_BASE, ch);
    return 0;
}

static int gpwm_start(intf_gpwm_ch_t ch)
{
    if (!gpwm_is_valid_channel(ch) || !gpwm_state[ch].configured) {
        return -1;
    }

    gptmr_stop_counter(GPWM_BASE, ch);
    gptmr_channel_reset_count(GPWM_BASE, ch);
    gptmr_enable_cmp_output(GPWM_BASE, ch);
    gptmr_start_counter(GPWM_BASE, ch);
    return 0;
}

static int gpwm_stop(intf_gpwm_ch_t ch)
{
    if (!gpwm_is_valid_channel(ch)) {
        return -1;
    }

    gptmr_disable_cmp_output(GPWM_BASE, ch);
    gptmr_stop_counter(GPWM_BASE, ch);
    return 0;
}

static int gpwm_force_low(intf_gpwm_ch_t ch)
{
    if (!gpwm_is_valid_channel(ch) || !gpwm_state[ch].configured) {
        return -1;
    }

    gptmr_stop_counter(GPWM_BASE, ch);
    gptmr_update_cmp(GPWM_BASE, ch, 0, 0xFFFFFFFFU);
    gptmr_update_cmp(GPWM_BASE, ch, 1, 0xFFFFFFFFU);
    gptmr_enable_cmp_output(GPWM_BASE, ch);
    gptmr_channel_reset_count(GPWM_BASE, ch);
    gptmr_start_counter(GPWM_BASE, ch);
    return 0;
}

static int gpwm_force_release(intf_gpwm_ch_t ch)
{
    if (!gpwm_is_valid_channel(ch) || !gpwm_state[ch].configured) {
        return -1;
    }

    gptmr_stop_counter(GPWM_BASE, ch);
    gptmr_update_cmp(GPWM_BASE, ch, 0, (uint32_t)((float)gpwm_state[ch].reload * gpwm_state[ch].duty));
    gptmr_update_cmp(GPWM_BASE, ch, 1, gpwm_state[ch].reload);
    gptmr_enable_cmp_output(GPWM_BASE, ch);
    gptmr_channel_reset_count(GPWM_BASE, ch);
    gptmr_start_counter(GPWM_BASE, ch);
    return 0;
}

static int gpwm_capture_start(intf_gpwm_ch_t ch)
{
    if (!gpwm_is_valid_capture_channel(ch) || !gpwm_capture_state[ch].configured) {
        return -1;
    }

    gpwm_capture_state[ch].has_first_edge = false;
    gpwm_capture_state[ch].period_ticks = 0;
    gptmr_clear_status(GPWM_BASE, GPTMR_CH_CAP_STAT_MASK(ch));
    gptmr_channel_reset_count(GPWM_BASE, ch);
    gptmr_start_counter(GPWM_BASE, ch);
    gpwm_capture_state[ch].started = true;
    return 0;
}

static int gpwm_capture_stop(intf_gpwm_ch_t ch)
{
    if (!gpwm_is_valid_capture_channel(ch)) {
        return -1;
    }

    gptmr_stop_counter(GPWM_BASE, ch);
    gpwm_capture_state[ch].started = false;
    return 0;
}

static int gpwm_capture_poll(intf_gpwm_ch_t ch, intf_gpwm_capture_t *capture)
{
    uint32_t count;

    if ((capture == NULL) || !gpwm_is_valid_capture_channel(ch) ||
        !gpwm_capture_state[ch].configured || !gpwm_capture_state[ch].started) {
        return -1;
    }

    capture->captured = false;
    capture->count = 0;
    capture->period_ticks = gpwm_capture_state[ch].period_ticks;

    if (!gptmr_check_status(GPWM_BASE, GPTMR_CH_CAP_STAT_MASK(ch))) {
        return 0;
    }

    gptmr_clear_status(GPWM_BASE, GPTMR_CH_CAP_STAT_MASK(ch));
    count = gptmr_channel_get_counter(GPWM_BASE, ch,
                                      gpwm_get_capture_counter_type(gpwm_capture_state[ch].edge));
    capture->count = count;

    if (!gpwm_capture_state[ch].has_first_edge) {
        gpwm_capture_state[ch].first_count = count;
        gpwm_capture_state[ch].has_first_edge = true;
        return 0;
    }

    gpwm_capture_state[ch].period_ticks = gpwm_calc_delta(gpwm_capture_state[ch].first_count, count);
    gpwm_capture_state[ch].first_count = count;
    capture->captured = true;
    capture->period_ticks = gpwm_capture_state[ch].period_ticks;
    return 0;
}

static const intf_gpwm_t gpwm_ops = {
    .instance_id = 0,
    .init = gpwm_init,
    .set_duty = gpwm_set_duty,
    .set_frequency = gpwm_set_frequency,
    .start = gpwm_start,
    .stop = gpwm_stop,
    .force_low = gpwm_force_low,
    .force_release = gpwm_force_release,
    .capture_init = gpwm_capture_init,
    .capture_start = gpwm_capture_start,
    .capture_stop = gpwm_capture_stop,
    .capture_poll = gpwm_capture_poll,
};

void hpm_gpwm_driver_register(void)
{
    intf_gpwm_register(&gpwm_ops);
}
