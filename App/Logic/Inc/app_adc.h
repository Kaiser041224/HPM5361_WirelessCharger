/*
 * ADC App Layer
 *
 * HPM5361 引脚→ADC 通道映射 (依据数据手册):
 *   PB08 → ch11   PB10 → ch2    PB11 → ch3
 *   PB12 → ch4    PB13 → ch5    PB14 → ch6
 *
 * 拓扑: Buck-Boost → V_LINK → 全桥LCC (级联)
 *   PB11 = V_LINK (Buck-Boost输出 / LCC全桥输入)
 *
 * Copyright (c) 2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef APP_ADC_H
#define APP_ADC_H

#include <stdint.h>
#include "intf_adc.h"

typedef enum {
    ADC_CH_V_IN   = 0,   /* PB14, ch6:  Buck-Boost输入电压  (ADC0) */
    ADC_CH_I_IN   = 1,   /* PB08, ch11: Buck-Boost输入母线电流 */
    ADC_CH_I_L    = 2,   /* PB10, ch2:  电感电流 (电流内环)    */
    ADC_CH_V_LINK = 3,   /* PB11, ch3:  V_LINK 级联电压        */
    ADC_CH_I_COIL = 4,   /* PB12, ch4:  线圈电流        (ADC1) */
    ADC_CH_I_LF   = 5,   /* PB13, ch5:  LCC谐振电流            */
    ADC_CH_COUNT,
} adc_channel_t;

void     app_adc_init(void);
uint16_t app_adc_read_raw(adc_channel_t ch);
void     app_adc_read_all(uint16_t values[ADC_CH_COUNT]);
void     app_adc_set_vref(float mv);
void     app_adc_calibrate(void);

void app_adc_pmt_init(uint8_t pmt_trig,
                      const adc_channel_t *ch_list, uint8_t count,
                      intf_adc_pmt_cb_t cb, void *user_data);
void app_adc_pmt_start(void);
void app_adc_pmt_stop(void);

void app_adc_wdog_init(adc_channel_t ch, uint16_t thshd_high, uint16_t thshd_low,
                       intf_adc_wdog_cb_t cb, void *user_data);
void app_adc_wdog_reenable(adc_channel_t ch);

void app_adc_pwm_trig_init(
    intf_adc_pmt_cb_t cb0, void *user0,
    intf_adc_pmt_cb_t cb1, void *user1);

#endif /* APP_ADC_H */
