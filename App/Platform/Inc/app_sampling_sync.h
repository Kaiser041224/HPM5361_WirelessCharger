/*
 * Sampling Sync App Module
 *
 * Encapsulates PWM compare trigger + TRGM routing + dual-ADC PMT setup.
 *
 * Copyright (c) 2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef APP_SAMPLING_SYNC_H
#define APP_SAMPLING_SYNC_H

#include <stdbool.h>
#include <stdint.h>

#include "app_adc.h"
#include "app_hrpwm.h"
#include "intf_adc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define APP_SAMPLING_SYNC_TRIGGER_CMP_INDEX      (8U)
#define APP_SAMPLING_SYNC_ADC0_TRIG_CH           (0U)
#define APP_SAMPLING_SYNC_ADC1_TRIG_CH           (3U)

typedef struct {
    float trigger_position_ratio;

    bool adc0_dma_en;
    uint32_t *adc0_dma_buff;
    uint32_t adc0_dma_buff_len;
    bool adc1_dma_en;
    uint32_t *adc1_dma_buff;
    uint32_t adc1_dma_buff_len;

    intf_adc_pmt_cb_t adc0_cb;
    void *adc0_user_data;
    intf_adc_pmt_cb_t adc1_cb;
    void *adc1_user_data;
} app_sampling_sync_cfg_t;

void app_sampling_sync_get_default_config(app_sampling_sync_cfg_t *cfg);
int  app_sampling_sync_init(const app_sampling_sync_cfg_t *cfg);
void app_sampling_sync_start(void);
void app_sampling_sync_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* APP_SAMPLING_SYNC_H */
