#include "ctrl_lcc.h"

#include <stddef.h>
#include <string.h>

#ifndef ATTR_RAMFUNC
#define ATTR_RAMFUNC __attribute__((section(".fast")))
#endif

#define LCC_FREQ_DEFAULT_HZ         CTRL_LCC_FREQ_DEFAULT_HZ
#define LCC_FREQ_MIN_DEFAULT_HZ     CTRL_LCC_FREQ_MIN_HZ
#define LCC_FREQ_MAX_DEFAULT_HZ     CTRL_LCC_FREQ_MAX_HZ
#define LCC_PHASE_MAX_DEFAULT_DEG   CTRL_LCC_PHASE_MAX_DEG
#define LCC_PHASE_DEFAULT_DEG       CTRL_LCC_PHASE_DEFAULT_DEG
#define LCC_DUTY_DEFAULT            CTRL_LCC_DUTY_DEFAULT
#define LCC_I_COIL_LIMIT_DEFAULT_A  CTRL_LCC_I_COIL_LIMIT_A

ATTR_RAMFUNC
static inline bool lcc_finite(float x)
{
    union {
        float f;
        uint32_t u;
    } v = { .f = x };
    return (v.u & 0x7F800000u) != 0x7F800000u;
}

static inline float lcc_clampf(float x, float lo, float hi)
{
    if (!lcc_finite(x)) {
        return lo;
    }
    if (x < lo) {
        return lo;
    }
    if (x > hi) {
        return hi;
    }
    return x;
}

static inline float lcc_absf(float x)
{
    return (x < 0.0f) ? -x : x;
}

ATTR_RAMFUNC
static inline void lcc_phase_samples_reset(ctrl_lcc_t *ctrl)
{
    ctrl->state.sample_phase_index = CTRL_LCC_PHASE_SAMPLE_0_DEG;
    ctrl->state.phase_samples = (ctrl_lcc_phase_samples_t){0};
}

int ctrl_lcc_init(ctrl_lcc_t *ctrl)
{
    if (ctrl == NULL) {
        return -1;
    }

    memset(ctrl, 0, sizeof(*ctrl));
    ctrl->params.phase_max_deg = LCC_PHASE_MAX_DEFAULT_DEG;
    ctrl->params.freq_min_hz = LCC_FREQ_MIN_DEFAULT_HZ;
    ctrl->params.freq_max_hz = LCC_FREQ_MAX_DEFAULT_HZ;
    ctrl->params.i_coil_limit_a = LCC_I_COIL_LIMIT_DEFAULT_A;

    ctrl->state.mode = LCC_MODE_IDLE;
    ctrl->state.frequency_hz = LCC_FREQ_DEFAULT_HZ;
    ctrl->state.phase_deg = LCC_PHASE_DEFAULT_DEG;
    ctrl->state.duty = LCC_DUTY_DEFAULT;
    ctrl->state.output_enabled = false;
    ctrl->state.i_coil_target_a = 0.0f;
    lcc_phase_samples_reset(ctrl);

    return 0;
}

int ctrl_lcc_enable(ctrl_lcc_t *ctrl)
{
    if (ctrl == NULL) {
        return -1;
    }
    ctrl->state.enabled = true;
    lcc_phase_samples_reset(ctrl);
    return 0;
}

void ctrl_lcc_disable(ctrl_lcc_t *ctrl)
{
    if (ctrl == NULL) {
        return;
    }
    ctrl->state.enabled = false;
    ctrl->state.output_enabled = false;
    ctrl->state.current_integral = 0.0f;
    ctrl->state.pll_integral = 0.0f;
}

void ctrl_lcc_emergency_stop(ctrl_lcc_t *ctrl)
{
    if (ctrl == NULL) {
        return;
    }
    ctrl_lcc_disable(ctrl);
    lcc_phase_samples_reset(ctrl);
}

ATTR_RAMFUNC
void ctrl_lcc_push_sample(ctrl_lcc_t *ctrl, float i_coil, float i_lf)
{
    uint8_t p = ctrl->state.sample_phase_index;
    ctrl->state.phase_samples.i_coil_a[p] = i_coil;
    ctrl->state.phase_samples.i_lf_a[p] = i_lf;
    p = (p + 1U) & 0x03U;
    ctrl->state.sample_phase_index = p;
    if (p == 0U) {
        ctrl->state.phase_samples.frame_ready = true;
        ctrl->state.phase_samples.frame_id++;
    }
}

ATTR_RAMFUNC
void ctrl_lcc_step(ctrl_lcc_t *ctrl)
{
    if (!ctrl->state.enabled || ctrl->state.mode == LCC_MODE_IDLE) {
        ctrl->state.output_enabled = false;
        return;
    }
    ctrl->state.output_enabled = true;
}

void ctrl_lcc_set_mode(ctrl_lcc_t *ctrl, ctrl_lcc_mode_t mode)
{
    if (ctrl == NULL) {
        return;
    }
    ctrl->state.mode = mode;
}

void ctrl_lcc_set_frequency(ctrl_lcc_t *ctrl, float freq_hz)
{
    if (ctrl == NULL || !lcc_finite(freq_hz)) {
        return;
    }
    ctrl->state.frequency_hz = lcc_clampf(freq_hz, ctrl->params.freq_min_hz, ctrl->params.freq_max_hz);
}

void ctrl_lcc_set_phase(ctrl_lcc_t *ctrl, float phase_deg)
{
    if (ctrl == NULL || !lcc_finite(phase_deg)) {
        return;
    }
    float max_phase = (ctrl->params.phase_max_deg > 0.0f) ? ctrl->params.phase_max_deg
                                                          : LCC_PHASE_MAX_DEFAULT_DEG;
    ctrl->state.phase_deg = lcc_clampf(phase_deg, 0.0f, max_phase);
}

void ctrl_lcc_set_i_coil_target(ctrl_lcc_t *ctrl, float target_a)
{
    if (ctrl == NULL || !lcc_finite(target_a)) {
        return;
    }
    float limit = (ctrl->params.i_coil_limit_a > 0.0f) ? ctrl->params.i_coil_limit_a
                                                       : LCC_I_COIL_LIMIT_DEFAULT_A;
    ctrl->state.i_coil_target_a = lcc_clampf(target_a, -limit, limit);
}

void ctrl_lcc_set_duty(ctrl_lcc_t *ctrl, float duty)
{
    if (ctrl == NULL || !lcc_finite(duty)) {
        return;
    }
    ctrl->state.duty = lcc_clampf(duty, 0.0f, 1.0f);
}

void ctrl_lcc_set_params(ctrl_lcc_t *ctrl, const ctrl_lcc_params_t *params)
{
    if (ctrl == NULL || params == NULL) {
        return;
    }
    ctrl->params = *params;
    ctrl->params.phase_max_deg = lcc_clampf(ctrl->params.phase_max_deg, 0.0f, 180.0f);
    ctrl->params.freq_min_hz = (ctrl->params.freq_min_hz > 0.0f) ? ctrl->params.freq_min_hz
                                                                 : LCC_FREQ_MIN_DEFAULT_HZ;
    ctrl->params.freq_max_hz = (ctrl->params.freq_max_hz > ctrl->params.freq_min_hz)
                                 ? ctrl->params.freq_max_hz
                                 : LCC_FREQ_MAX_DEFAULT_HZ;
    ctrl->params.i_coil_limit_a = lcc_absf(ctrl->params.i_coil_limit_a);
    if (ctrl->params.i_coil_limit_a <= 0.0f) {
        ctrl->params.i_coil_limit_a = LCC_I_COIL_LIMIT_DEFAULT_A;
    }
    ctrl_lcc_set_frequency(ctrl, ctrl->state.frequency_hz);
    ctrl_lcc_set_phase(ctrl, ctrl->state.phase_deg);
    ctrl_lcc_set_i_coil_target(ctrl, ctrl->state.i_coil_target_a);
}

float ctrl_lcc_get_frequency(const ctrl_lcc_t *ctrl)
{
    return (ctrl != NULL) ? ctrl->state.frequency_hz : 0.0f;
}

float ctrl_lcc_get_phase(const ctrl_lcc_t *ctrl)
{
    return (ctrl != NULL) ? ctrl->state.phase_deg : 0.0f;
}

bool ctrl_lcc_is_pll_locked(const ctrl_lcc_t *ctrl)
{
    return (ctrl != NULL) ? ctrl->state.pll_locked : false;
}

bool ctrl_lcc_is_enabled(const ctrl_lcc_t *ctrl)
{
    return (ctrl != NULL) ? ctrl->state.enabled : false;
}

int ctrl_lcc_get_phase_samples(const ctrl_lcc_t *ctrl, ctrl_lcc_phase_samples_t *samples)
{
    if (ctrl == NULL || samples == NULL) {
        return -1;
    }
    *samples = ctrl->state.phase_samples;
    return 0;
}

ATTR_RAMFUNC
void ctrl_lcc_get_cmd(const ctrl_lcc_t *ctrl, ctrl_lcc_cmd_t *cmd)
{
    if (cmd == NULL) {
        return;
    }
    if (ctrl == NULL) {
        *cmd = (ctrl_lcc_cmd_t){0};
        return;
    }
    cmd->frequency_hz = ctrl->state.frequency_hz;
    cmd->phase_deg = ctrl->state.phase_deg;
    cmd->duty = ctrl->state.duty;
    cmd->output_enabled = ctrl->state.output_enabled;
}
