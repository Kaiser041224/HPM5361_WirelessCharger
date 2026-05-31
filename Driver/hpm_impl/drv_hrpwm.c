/*
 * HRPWM Driver - HPM PWM hardware implementation
 *
 * Copyright (c) 2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "intf_hrpwm.h"

#include "board.h"
#include "hpm_clock_drv.h"
#include "hpm_pwm_drv.h"

#include <assert.h>
#include <stddef.h>

#define HRPWM_INSTANCE_COUNT    (2U)
#define HRPWM_OUTPUTS_PER_INST  (PWM_SOC_PWM_MAX_COUNT)
#define HRPWM_CHANNEL_COUNT     (8U)

#define HRPWM_CMP_START_INDEX(pwm_index) ((uint8_t)((pwm_index) * 2U))
#define HRPWM_RELOAD_MAX_VALUE          (PWM_RLD_RLD_GET(PWM_RLD_RLD_MASK))

static_assert((HRPWM_CMP_START_INDEX(PWM_SOC_PWM_MAX_COUNT - 1U) + 1U) < PWM_SOC_CMP_MAX_COUNT,
              "HRPWM compare mapping exceeds PWM compare resource count");

typedef struct {
    intf_hrpwm_ch_t channel;
    uint8_t instance;
    uint8_t pwm_index;
    uint8_t cmp_start_index;
} hrpwm_channel_map_t;

typedef struct {
    bool configured;
    float duty;
    uint32_t reload;
    uint8_t jitter_cmp;
    intf_hrpwm_align_t align;
} hrpwm_channel_state_t;

typedef struct {
    PWM_Type *base;
    clock_name_t clock_name;
    uint32_t frequency_hz;
    uint32_t reload;
    bool fault_configured;
    uint8_t force_mask;
    hrpwm_channel_state_t channels[HRPWM_OUTPUTS_PER_INST];
} hrpwm_instance_state_t;

static hrpwm_instance_state_t hrpwm_instances[HRPWM_INSTANCE_COUNT];

typedef struct {
    uint32_t cmp_begin;
    uint32_t cmp_end;
} hrpwm_cmp_pair_t;

static const hrpwm_channel_map_t hrpwm_channel_maps[] = {
    {
        .channel = BOARD_APP_HRPWM_PWM0_PAIR0_OUT,
        .instance = 0,
        .pwm_index = BOARD_APP_HRPWM_PWM0_PAIR0_OUT,
        .cmp_start_index = HRPWM_CMP_START_INDEX(BOARD_APP_HRPWM_PWM0_PAIR0_OUT),
    },
    {
        .channel = BOARD_APP_HRPWM_PWM0_PAIR1_OUT,
        .instance = 0,
        .pwm_index = BOARD_APP_HRPWM_PWM0_PAIR1_OUT,
        .cmp_start_index = HRPWM_CMP_START_INDEX(BOARD_APP_HRPWM_PWM0_PAIR1_OUT),
    },
    {
        .channel = BOARD_APP_HRPWM_PWM1_PAIR0_OUT,
        .instance = 1,
        .pwm_index = BOARD_APP_HRPWM_PWM1_PAIR0_OUT,
        .cmp_start_index = HRPWM_CMP_START_INDEX(BOARD_APP_HRPWM_PWM1_PAIR0_OUT),
    },
    {
        .channel = BOARD_APP_HRPWM_PWM1_PAIR1_OUT,
        .instance = 1,
        .pwm_index = BOARD_APP_HRPWM_PWM1_PAIR1_OUT,
        .cmp_start_index = HRPWM_CMP_START_INDEX(BOARD_APP_HRPWM_PWM1_PAIR1_OUT),
    },
};

static void hrpwm_init_instances(void)
{
    hrpwm_instances[0].base = BOARD_APP_HRPWM0;
    hrpwm_instances[0].clock_name = BOARD_APP_HRPWM_CLOCK_NAME;
    hrpwm_instances[1].base = BOARD_APP_HRPWM1;
    hrpwm_instances[1].clock_name = BOARD_APP_HRPWM_CLOCK_NAME;
}

static PWM_Type *hrpwm_get_base(uint8_t inst)
{
    return (inst < HRPWM_INSTANCE_COUNT) ? hrpwm_instances[inst].base : NULL;
}

static const hrpwm_channel_map_t *hrpwm_get_channel_map(intf_hrpwm_ch_t ch)
{
    intf_hrpwm_ch_t pair_channel = (intf_hrpwm_ch_t)(ch & (uint8_t)~1U);

    for (size_t i = 0; i < sizeof(hrpwm_channel_maps) / sizeof(hrpwm_channel_maps[0]); i++) {
        if (hrpwm_channel_maps[i].channel == pair_channel) {
            return &hrpwm_channel_maps[i];
        }
    }
    return NULL;
}

static bool hrpwm_is_valid_duty(float duty)
{
    return (duty == duty) && (duty >= 0.0f) && (duty <= 1.0f);
}

static bool hrpwm_is_valid_align(intf_hrpwm_align_t align)
{
    return (align == INTF_HRPWM_ALIGN_EDGE) || (align == INTF_HRPWM_ALIGN_CENTER);
}

static uint32_t hrpwm_duty_to_cmp_count(uint32_t reload, float duty)
{
    return (uint32_t)((float)reload * duty);
}

static hrpwm_cmp_pair_t hrpwm_calc_center_aligned_cmp(uint32_t reload, float duty)
{
    hrpwm_cmp_pair_t cmp;

    /* 100% duty cycle: output stays high permanently.
     * Set both CMP values beyond reload so Counter never matches.
     * In center-aligned mode, Counter counts 0 -> reload -> 0.
     * CMP = reload + 1 will never be reached. */
    if (duty >= 1.0f) {
        cmp.cmp_begin = reload + 1U;
        cmp.cmp_end = reload + 1U;
        return cmp;
    }

    /* 0% duty cycle: output stays low permanently.
     * Set both CMP values to 0 so Counter matches immediately at zero.
     * This forces output low for the entire period. */
    if (duty <= 0.0f) {
        cmp.cmp_begin = 0U;
        cmp.cmp_end = 0U;
        return cmp;
    }

    /* Normal duty cycle: calculate symmetric compare values */
    uint32_t target_cmp = hrpwm_duty_to_cmp_count(reload, duty);
    cmp.cmp_begin = (reload - target_cmp) >> 1;
    cmp.cmp_end = (reload + target_cmp) >> 1;

    /* Ensure CMP values are not zero to avoid edge case at Counter = 0.
     * Using 1 instead of 0 prevents unintended Compare Match at period start. */
    if (cmp.cmp_begin == 0U) {
        cmp.cmp_begin = 1U;
    }
    if (cmp.cmp_end == 0U) {
        cmp.cmp_end = 1U;
    }

    /* Ensure begin <= end (swap if needed) */
    if (cmp.cmp_begin > cmp.cmp_end) {
        uint32_t cmp_swap = cmp.cmp_begin;
        cmp.cmp_begin = cmp.cmp_end;
        cmp.cmp_end = cmp_swap;
    }

    return cmp;
}

static hrpwm_cmp_pair_t hrpwm_calc_edge_aligned_cmp(uint32_t reload, float duty)
{
    hrpwm_cmp_pair_t cmp;

    /* 100% duty cycle: output stays high permanently.
     * Set both CMP values beyond reload so Counter never matches. */
    if (duty >= 1.0f) {
        cmp.cmp_begin = reload + 1U;
        cmp.cmp_end = reload + 1U;
        return cmp;
    }

    /* 0% duty cycle: output stays low permanently.
     * Set both CMP values to 0 so Counter matches immediately. */
    if (duty <= 0.0f) {
        cmp.cmp_begin = 0U;
        cmp.cmp_end = 0U;
        return cmp;
    }

    /* Normal duty cycle */
    uint32_t target_cmp = hrpwm_duty_to_cmp_count(reload, duty);
    cmp.cmp_begin = reload - target_cmp;
    cmp.cmp_end = reload;

    /* Ensure CMP value is not zero to avoid edge case at Counter = 0 */
    if (cmp.cmp_begin == 0U) {
        cmp.cmp_begin = 1U;
    }

    return cmp;
}

static hrpwm_cmp_pair_t hrpwm_calc_cmp_pair(uint32_t reload, float duty, intf_hrpwm_align_t align)
{
    if (align == INTF_HRPWM_ALIGN_CENTER) {
        return hrpwm_calc_center_aligned_cmp(reload, duty);
    }

    return hrpwm_calc_edge_aligned_cmp(reload, duty);
}

static void hrpwm_write_cmp_pair(PWM_Type *base, uint8_t cmp_start_index, const hrpwm_cmp_pair_t *cmp)
{
    pwm_shadow_register_unlock(base);
    pwm_cmp_update_cmp_value(base, cmp_start_index, cmp->cmp_begin, 0);
    pwm_cmp_update_cmp_value(base, cmp_start_index + 1U, cmp->cmp_end, 0);
}

static int hrpwm_apply_duty(const hrpwm_channel_map_t *map)
{
    PWM_Type *base;
    const hrpwm_channel_state_t *channel;
    hrpwm_cmp_pair_t cmp;

    if (map == NULL) {
        return -1;
    }

    base = hrpwm_get_base(map->instance);
    if (base == NULL) {
        return -1;
    }

    channel = &hrpwm_instances[map->instance].channels[map->pwm_index];
    cmp = hrpwm_calc_cmp_pair(hrpwm_instances[map->instance].reload, channel->duty, channel->align);
    hrpwm_write_cmp_pair(base, map->cmp_start_index, &cmp);

    return 0;
}

static int hrpwm_apply_frequency(uint8_t inst, uint32_t frequency_hz)
{
    uint32_t clock_hz;
    uint32_t reload;
    PWM_Type *base = hrpwm_get_base(inst);
    clock_name_t clock_name;

    if ((inst >= HRPWM_INSTANCE_COUNT) || (base == NULL) || (frequency_hz == 0U)) {
        return -1;
    }

    clock_name = hrpwm_instances[inst].clock_name;
    clock_add_to_group(clock_name, 0);
    clock_hz = clock_get_frequency(clock_name);
    if (clock_hz <= frequency_hz) {
        return -1;
    }

    reload = (clock_hz / frequency_hz) - 1U;
    if (reload >= HRPWM_RELOAD_MAX_VALUE) {
        return -1;
    }

    hrpwm_instances[inst].frequency_hz = frequency_hz;
    hrpwm_instances[inst].reload = reload;

    pwm_set_reload(base, 0, hrpwm_instances[inst].reload);
    pwm_set_start_count(base, 0, 0);

    for (size_t i = 0; i < sizeof(hrpwm_channel_maps) / sizeof(hrpwm_channel_maps[0]); i++) {
        const hrpwm_channel_map_t *map = &hrpwm_channel_maps[i];
        if ((map->instance == inst) && hrpwm_instances[inst].channels[map->pwm_index].configured) {
            hrpwm_instances[inst].channels[map->pwm_index].reload = hrpwm_instances[inst].reload;
            if (hrpwm_apply_duty(map) != 0) {
                return -1;
            }
        }
    }

    return 0;
}

static uint32_t hrpwm_ns_to_deadtime_cycles(uint8_t inst, uint32_t deadtime_ns)
{
    uint32_t clock_hz = clock_get_frequency(hrpwm_instances[inst].clock_name);
    uint32_t clock_period_ns = 1000000000U / clock_hz;
    return (deadtime_ns + clock_period_ns / 2) / clock_period_ns;
}

static int hrpwm_init_pair(intf_hrpwm_ch_t ch, const intf_hrpwm_pair_cfg_t *cfg)
{
    const hrpwm_channel_map_t *map;
    uint8_t inst;
    PWM_Type *base;
    pwm_pair_config_t pair_config = {0};
    pwm_cmp_config_t cmp_config[2] = {0};

    map = hrpwm_get_channel_map(ch);
    if ((cfg == NULL) || (map == NULL) || !hrpwm_is_valid_duty(cfg->duty) || !hrpwm_is_valid_align(cfg->align)) {
        return -1;
    }

    inst = map->instance;
    base = hrpwm_get_base(inst);
    if (base == NULL) {
        return -1;
    }

    if (hrpwm_apply_frequency(inst, cfg->frequency_hz) != 0) {
        return -1;
    }

    pwm_get_default_pwm_pair_config(base, &pair_config);

    pair_config.pwm[0].enable_output = false;
    pair_config.pwm[0].invert_output = cfg->invert_high_side;
    pair_config.pwm[0].dead_zone_in_half_cycle = hrpwm_ns_to_deadtime_cycles(inst, cfg->deadtime_ns);

    pair_config.pwm[1].enable_output = false;
    pair_config.pwm[1].invert_output = cfg->invert_low_side;
    pair_config.pwm[1].dead_zone_in_half_cycle = hrpwm_ns_to_deadtime_cycles(inst, cfg->deadtime_ns);

    cmp_config[0].mode = pwm_cmp_mode_output_compare;
    cmp_config[0].cmp = hrpwm_instances[inst].reload + 1;
    cmp_config[0].jitter_cmp = cfg->jitter_cmp;
    cmp_config[0].update_trigger = pwm_shadow_register_update_on_modify;

    cmp_config[1].mode = pwm_cmp_mode_output_compare;
    cmp_config[1].cmp = hrpwm_instances[inst].reload;
    cmp_config[1].jitter_cmp = cfg->jitter_cmp;
    cmp_config[1].update_trigger = pwm_shadow_register_update_on_modify;

    if (pwm_setup_waveform_in_pair(base, map->pwm_index, &pair_config, map->cmp_start_index, cmp_config, 2) != status_success) {
        return -1;
    }

    hrpwm_instances[inst].channels[map->pwm_index].configured = true;
    hrpwm_instances[inst].channels[map->pwm_index].duty = cfg->duty;
    hrpwm_instances[inst].channels[map->pwm_index].reload = hrpwm_instances[inst].reload;
    hrpwm_instances[inst].channels[map->pwm_index].jitter_cmp = cfg->jitter_cmp;
    hrpwm_instances[inst].channels[map->pwm_index].align = cfg->align;

    return hrpwm_apply_duty(map);
}

static int hrpwm_set_duty(intf_hrpwm_ch_t ch, float duty)
{
    const hrpwm_channel_map_t *map;

    map = hrpwm_get_channel_map(ch);
    if ((map == NULL) || !hrpwm_is_valid_duty(duty)) {
        return -1;
    }

    if (!hrpwm_instances[map->instance].channels[map->pwm_index].configured) {
        return -1;
    }

    hrpwm_instances[map->instance].channels[map->pwm_index].duty = duty;
    return hrpwm_apply_duty(map);
}

static int hrpwm_set_frequency_pwm0(uint32_t frequency_hz)
{
    return hrpwm_apply_frequency(0, frequency_hz);
}

static int hrpwm_set_frequency_pwm1(uint32_t frequency_hz)
{
    return hrpwm_apply_frequency(1, frequency_hz);
}

static int hrpwm_set_jitter(intf_hrpwm_ch_t ch, uint8_t jitter_cmp)
{
    const hrpwm_channel_map_t *map;
    PWM_Type *base;

    map = hrpwm_get_channel_map(ch);
    if (map == NULL) {
        return -1;
    }

    base = hrpwm_get_base(map->instance);
    if (base == NULL) {
        return -1;
    }

    if (!hrpwm_instances[map->instance].channels[map->pwm_index].configured) {
        return -1;
    }

    pwm_shadow_register_unlock(base);
    pwm_cmp_update_jitter_value(base, map->cmp_start_index, jitter_cmp);
    pwm_cmp_update_jitter_value(base, map->cmp_start_index + 1U, jitter_cmp);
    hrpwm_instances[map->instance].channels[map->pwm_index].jitter_cmp = jitter_cmp;

    return hrpwm_apply_duty(map);
}

static int hrpwm_start(intf_hrpwm_ch_t ch)
{
    const hrpwm_channel_map_t *map;
    PWM_Type *base;

    map = hrpwm_get_channel_map(ch);
    if (map == NULL) {
        return -1;
    }

    base = hrpwm_get_base(map->instance);
    if (base == NULL) {
        return -1;
    }

    if (!hrpwm_instances[map->instance].channels[map->pwm_index].configured) {
        return -1;
    }

    pwm_enable_output(base, map->pwm_index);
    pwm_enable_output(base, map->pwm_index + 1U);
    pwm_start_counter(base);
    pwm_issue_shadow_register_lock_event(base);
    return 0;
}

static int hrpwm_stop(intf_hrpwm_ch_t ch)
{
    const hrpwm_channel_map_t *map;
    PWM_Type *base;

    map = hrpwm_get_channel_map(ch);
    if (map == NULL) {
        return -1;
    }

    base = hrpwm_get_base(map->instance);
    if (base == NULL) {
        return -1;
    }

    if (!hrpwm_instances[map->instance].channels[map->pwm_index].configured) {
        return -1;
    }

    pwm_disable_output(base, map->pwm_index);
    pwm_disable_output(base, map->pwm_index + 1U);
    return 0;
}

static int hrpwm_force_low(intf_hrpwm_ch_t ch)
{
    const hrpwm_channel_map_t *map;
    PWM_Type *base;

    map = hrpwm_get_channel_map(ch);
    if (map == NULL) {
        return -1;
    }

    base = hrpwm_get_base(map->instance);
    if (base == NULL) {
        return -1;
    }

    if (!hrpwm_instances[map->instance].channels[map->pwm_index].configured) {
        return -1;
    }

    pwm_config_force_cmd_timing(base, pwm_force_immediately);
    pwm_enable_pwm_sw_force_output(base, map->pwm_index);
    pwm_enable_pwm_sw_force_output(base, map->pwm_index + 1U);
    pwm_set_force_output(base,
                         PWM_FORCE_OUTPUT(map->pwm_index, pwm_output_0)
                             | PWM_FORCE_OUTPUT((map->pwm_index + 1U), pwm_output_0));
    hrpwm_instances[map->instance].force_mask |= (uint8_t)((1U << map->pwm_index) | (1U << (map->pwm_index + 1U)));
    pwm_enable_sw_force(base);
    return 0;
}

static int hrpwm_force_release(intf_hrpwm_ch_t ch)
{
    const hrpwm_channel_map_t *map;
    PWM_Type *base;

    map = hrpwm_get_channel_map(ch);
    if (map == NULL) {
        return -1;
    }

    base = hrpwm_get_base(map->instance);
    if (base == NULL) {
        return -1;
    }

    pwm_disable_pwm_sw_force_output(base, map->pwm_index);
    pwm_disable_pwm_sw_force_output(base, map->pwm_index + 1U);
    hrpwm_instances[map->instance].force_mask &= (uint8_t)~((1U << map->pwm_index) | (1U << (map->pwm_index + 1U)));
    if (hrpwm_instances[map->instance].force_mask == 0U) {
        pwm_disable_sw_force(base);
    }
    return 0;
}

static int hrpwm_config_fault(const intf_hrpwm_fault_cfg_t *cfg)
{
    pwm_fault_source_config_t fault_config = {0};
    pwm_fault_mode_t fault_mode;
    pwm_fault_recovery_trigger_t fault_recovery;

    if (cfg == NULL) {
        return -1;
    }

    switch (cfg->mode) {
    case INTF_HRPWM_FAULT_MODE_FORCE_LOW:
        fault_mode = pwm_fault_mode_force_output_0;
        break;
    case INTF_HRPWM_FAULT_MODE_FORCE_HIGH:
        fault_mode = pwm_fault_mode_force_output_1;
        break;
    case INTF_HRPWM_FAULT_MODE_HIGH_Z:
        fault_mode = pwm_fault_mode_force_output_highz;
        break;
    default:
        return -1;
    }

    switch (cfg->recovery) {
    case INTF_HRPWM_FAULT_RECOVERY_IMMEDIATELY:
        fault_recovery = pwm_fault_recovery_immediately;
        break;
    case INTF_HRPWM_FAULT_RECOVERY_ON_RELOAD:
        fault_recovery = pwm_fault_recovery_on_reload;
        break;
    case INTF_HRPWM_FAULT_RECOVERY_ON_HW_EVENT:
        fault_recovery = pwm_fault_recovery_on_hw_event;
        break;
    case INTF_HRPWM_FAULT_RECOVERY_ON_FAULT_CLEAR:
        fault_recovery = pwm_fault_recovery_on_fault_clear;
        break;
    default:
        return -1;
    }

    fault_config.fault_output_recovery_trigger = fault_recovery;
    switch (cfg->source) {
    case INTF_HRPWM_FAULT_SRC_INTERNAL_0:
        fault_config.source_mask = pwm_fault_source_internal_0;
        break;
    case INTF_HRPWM_FAULT_SRC_INTERNAL_1:
        fault_config.source_mask = pwm_fault_source_internal_1;
        break;
    case INTF_HRPWM_FAULT_SRC_INTERNAL_2:
        fault_config.source_mask = pwm_fault_source_internal_2;
        break;
    case INTF_HRPWM_FAULT_SRC_INTERNAL_3:
        fault_config.source_mask = pwm_fault_source_internal_3;
        break;
    case INTF_HRPWM_FAULT_SRC_EXTERNAL_0:
        fault_config.source_mask = pwm_fault_source_external_0;
        fault_config.fault_external_0_active_low = cfg->active_low;
        break;
    case INTF_HRPWM_FAULT_SRC_EXTERNAL_1:
        fault_config.source_mask = pwm_fault_source_external_1;
        fault_config.fault_external_1_active_low = cfg->active_low;
        break;
    case INTF_HRPWM_FAULT_SRC_DEBUG:
        fault_config.source_mask = pwm_fault_source_debug;
        break;
    default:
        return -1;
    }

    for (uint8_t inst = 0; inst < HRPWM_INSTANCE_COUNT; inst++) {
        PWM_Type *base = hrpwm_get_base(inst);

        if (base == NULL) {
            return -1;
        }

        for (uint8_t pwm_index = 0; pwm_index < PWM_SOC_PWM_MAX_COUNT; pwm_index++) {
            base->PWMCFG[pwm_index] = (base->PWMCFG[pwm_index] & ~(PWM_PWMCFG_FAULTMODE_MASK | PWM_PWMCFG_FAULTRECTIME_MASK))
                                      | PWM_PWMCFG_FAULTMODE_SET(fault_mode)
                                      | PWM_PWMCFG_FAULTRECTIME_SET(fault_recovery);
        }

        pwm_config_fault_source(base, &fault_config);
        hrpwm_instances[inst].fault_configured = true;
    }

    return 0;
}

static int hrpwm_clear_fault(void)
{
    for (uint8_t inst = 0; inst < HRPWM_INSTANCE_COUNT; inst++) {
        PWM_Type *base = hrpwm_get_base(inst);
        pwm_clear_fault(base);
        pwm_clear_status(base, pwm_get_status(base));
    }
    return 0;
}

static const intf_hrpwm_t hrpwm_ops_pwm0 = {
    .instance_id = 0,
    .init_pair = hrpwm_init_pair,
    .set_duty = hrpwm_set_duty,
    .set_frequency = hrpwm_set_frequency_pwm0,
    .set_jitter = hrpwm_set_jitter,
    .start = hrpwm_start,
    .stop = hrpwm_stop,
    .force_low = hrpwm_force_low,
    .force_release = hrpwm_force_release,
    .config_fault = hrpwm_config_fault,
    .clear_fault = hrpwm_clear_fault,
};

static const intf_hrpwm_t hrpwm_ops_pwm1 = {
    .instance_id = 1,
    .init_pair = hrpwm_init_pair,
    .set_duty = hrpwm_set_duty,
    .set_frequency = hrpwm_set_frequency_pwm1,
    .set_jitter = hrpwm_set_jitter,
    .start = hrpwm_start,
    .stop = hrpwm_stop,
    .force_low = hrpwm_force_low,
    .force_release = hrpwm_force_release,
    .config_fault = hrpwm_config_fault,
    .clear_fault = hrpwm_clear_fault,
};

void hpm_hrpwm_driver_register(void)
{
    hrpwm_init_instances();
    intf_hrpwm_register(&hrpwm_ops_pwm0);
    intf_hrpwm_register(&hrpwm_ops_pwm1);
}
