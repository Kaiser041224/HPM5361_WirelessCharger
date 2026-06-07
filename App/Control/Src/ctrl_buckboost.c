#include "ctrl_buckboost.h"

int ctrl_buckboost_init(ctrl_buckboost_t *ctrl)
{
    (void)ctrl;
    return 0;
}

int  ctrl_buckboost_enable(ctrl_buckboost_t *c)       { (void)c; return 0; }
void ctrl_buckboost_disable(ctrl_buckboost_t *c)      { (void)c; }
void ctrl_buckboost_emergency_stop(ctrl_buckboost_t *c) { (void)c; }
void ctrl_buckboost_step(ctrl_buckboost_t *c, float vi, float vo, float il)
{
    (void)c; (void)vi; (void)vo; (void)il;
}
void ctrl_buckboost_set_vout_target(ctrl_buckboost_t *c, float tv)  { (void)c; (void)tv; }
void ctrl_buckboost_set_il_target(ctrl_buckboost_t *c, float ta)    { (void)c; (void)ta; }
void ctrl_buckboost_set_target_type(ctrl_buckboost_t *c, ctrl_bb_target_t t) { (void)c; (void)t; }
void ctrl_buckboost_set_params(ctrl_buckboost_t *c, const ctrl_bb_params_t *p) { (void)c; (void)p; }
void ctrl_buckboost_soft_start(ctrl_buckboost_t *c)     { (void)c; }
float ctrl_buckboost_get_duty(const ctrl_buckboost_t *c) { (void)c; return 0.0f; }
ctrl_bb_mode_t ctrl_buckboost_get_mode(const ctrl_buckboost_t *c) { (void)c; return BB_MODE_IDLE; }
bool ctrl_buckboost_is_enabled(const ctrl_buckboost_t *c) { (void)c; return false; }
