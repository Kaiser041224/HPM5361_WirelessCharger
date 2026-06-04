/*
 * Sampling Sync App Module
 *
 * Copyright (c) 2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "app_sampling_sync.h"

#include "intf_hrpwm.h"
#include "intf_trgm.h"

#include <stddef.h>

extern void hpm_adc_driver_register(void);

void app_sampling_sync_get_default_config(app_sampling_sync_cfg_t *cfg)
{
    if (cfg == NULL) return;

    *cfg = (app_sampling_sync_cfg_t) {
        .trigger_position_ratio = 0.5f,
        .adc0_dma_en = false,
        .adc0_dma_buff = NULL,
        .adc0_dma_buff_len = 0U,
        .adc1_dma_en = false,
        .adc1_dma_buff = NULL,
        .adc1_dma_buff_len = 0U,
        .adc0_cb = NULL,
        .adc0_user_data = NULL,
        .adc1_cb = NULL,
        .adc1_user_data = NULL,
    };
}

static int app_sampling_sync_init_adc0(const app_sampling_sync_cfg_t *cfg)
{
    intf_adc_cfg_t adc0_cfg = {
        .resolution = INTF_ADC_RES_DEFAULT,
        .mode = INTF_ADC_MODE_PMT,
        .sample_cycle = INTF_ADC_DEFAULT_SAMPLE_CYCLE,
        .clock_div = INTF_ADC_DEFAULT_CLOCK_DIV,
        .vref_mv = INTF_ADC_DEFAULT_VREF_MV,
        .dma_en = cfg->adc0_dma_en,
        .dma_buff = cfg->adc0_dma_buff,
        .dma_buff_len = cfg->adc0_dma_buff_len,
        .pmt_trig_ch = APP_SAMPLING_SYNC_ADC0_TRIG_CH,
        .pmt_ch_count = 4U,
        .pmt_cb = cfg->adc0_cb,
        .pmt_cb_user_data = cfg->adc0_user_data,
    };

    adc0_cfg.pmt_ch_list[0] = 6U;
    adc0_cfg.pmt_ch_list[1] = 11U;
    adc0_cfg.pmt_ch_list[2] = 2U;
    adc0_cfg.pmt_ch_list[3] = 3U;

    return intf_adc_init(INTF_ADC_CH(APP_ADC_INST_0, 0), &adc0_cfg);
}

static int app_sampling_sync_init_adc1(const app_sampling_sync_cfg_t *cfg)
{
    intf_adc_cfg_t adc1_cfg = {
        .resolution = INTF_ADC_RES_DEFAULT,
        .mode = INTF_ADC_MODE_PMT,
        .sample_cycle = INTF_ADC_DEFAULT_SAMPLE_CYCLE,
        .clock_div = INTF_ADC_DEFAULT_CLOCK_DIV,
        .vref_mv = INTF_ADC_DEFAULT_VREF_MV,
        .dma_en = cfg->adc1_dma_en,
        .dma_buff = cfg->adc1_dma_buff,
        .dma_buff_len = cfg->adc1_dma_buff_len,
        .pmt_trig_ch = APP_SAMPLING_SYNC_ADC1_TRIG_CH,
        .pmt_ch_count = 2U,
        .pmt_cb = cfg->adc1_cb,
        .pmt_cb_user_data = cfg->adc1_user_data,
    };

    adc1_cfg.pmt_ch_list[0] = 4U;
    adc1_cfg.pmt_ch_list[1] = 5U;

    return intf_adc_init(INTF_ADC_CH(APP_ADC_INST_1, 0), &adc1_cfg);
}

int app_sampling_sync_init(const app_sampling_sync_cfg_t *cfg)
{
    app_sampling_sync_cfg_t default_cfg;
    const app_sampling_sync_cfg_t *active_cfg = cfg;

    if (active_cfg == NULL) {
        app_sampling_sync_get_default_config(&default_cfg);
        active_cfg = &default_cfg;
    }

    hpm_adc_driver_register();

    if (intf_hrpwm_config_trigger_cmp(PWM_INST_0,
                                      APP_SAMPLING_SYNC_TRIGGER_CMP_INDEX,
                                      active_cfg->trigger_position_ratio) != 0) {
        return -1;
    }

    if (intf_hrpwm_config_trigger_cmp(PWM_INST_1,
                                      APP_SAMPLING_SYNC_TRIGGER_CMP_INDEX,
                                      active_cfg->trigger_position_ratio) != 0) {
        return -1;
    }

    if (intf_trgm_connect(INTF_TRGM_SRC_PWM0_CH8REF, INTF_TRGM_DST_ADC_PTRGI0A) != 0) {
        return -1;
    }

    if (intf_trgm_connect(INTF_TRGM_SRC_PWM1_CH8REF, INTF_TRGM_DST_ADC_PTRGI1A) != 0) {
        return -1;
    }

    if (app_sampling_sync_init_adc0(active_cfg) != 0) {
        return -1;
    }

    if (app_sampling_sync_init_adc1(active_cfg) != 0) {
        return -1;
    }

    return 0;
}

void app_sampling_sync_start(void)
{
    app_adc_pmt_start_inst(APP_ADC_INST_0);
    app_adc_pmt_start_inst(APP_ADC_INST_1);
}

void app_sampling_sync_stop(void)
{
    app_adc_pmt_stop_inst(APP_ADC_INST_0);
    app_adc_pmt_stop_inst(APP_ADC_INST_1);
}
