#include "ctrl_lcc.h"

int ctrl_lcc_init(ctrl_lcc_t *ctrl)
{
    (void)ctrl;
    return 0;
}

int ctrl_lcc_enable(ctrl_lcc_t *ctrl) { (void)ctrl; return 0; }
void ctrl_lcc_disable(ctrl_lcc_t *ctrl) { (void)ctrl; }
void ctrl_lcc_emergency_stop(ctrl_lcc_t *ctrl) { (void)ctrl; }
void ctrl_lcc_step(ctrl_lcc_t *ctrl, float i_coil, float i_lf) { (void)ctrl; (void)i_coil; (void)i_lf; }
void ctrl_lcc_set_mode(ctrl_lcc_t *ctrl, ctrl_lcc_mode_t mode) { (void)ctrl; (void)mode; }
void ctrl_lcc_set_frequency(ctrl_lcc_t *ctrl, float freq_hz) { (void)ctrl; (void)freq_hz; }
void ctrl_lcc_set_phase(ctrl_lcc_t *ctrl, float phase_deg) { (void)ctrl; (void)phase_deg; }
void ctrl_lcc_set_i_coil_target(ctrl_lcc_t *ctrl, float target_a) { (void)ctrl; (void)target_a; }
void ctrl_lcc_set_params(ctrl_lcc_t *ctrl, const ctrl_lcc_params_t *params) { (void)ctrl; (void)params; }
float ctrl_lcc_get_frequency(const ctrl_lcc_t *ctrl) { (void)ctrl; return 0.0f; }
float ctrl_lcc_get_phase(const ctrl_lcc_t *ctrl) { (void)ctrl; return 0.0f; }
bool ctrl_lcc_is_pll_locked(const ctrl_lcc_t *ctrl) { (void)ctrl; return false; }
bool ctrl_lcc_is_enabled(const ctrl_lcc_t *ctrl) { (void)ctrl; return false; }
