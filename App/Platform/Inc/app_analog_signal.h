/*
 * Analog Signal Conditioning Module
 *
 * Converts ADC readings into physical measurements for controllers,
 * protection logic, and communication services.
 */

#ifndef APP_ANALOG_SIGNAL_H
#define APP_ANALOG_SIGNAL_H

#include "app_adc.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    APP_ANALOG_SIGNAL_ITEM_V_IN = 0,
    APP_ANALOG_SIGNAL_ITEM_I_IN,
    APP_ANALOG_SIGNAL_ITEM_I_L,
    APP_ANALOG_SIGNAL_ITEM_V_LINK,
    APP_ANALOG_SIGNAL_ITEM_I_COIL,
    APP_ANALOG_SIGNAL_ITEM_I_LF,
    APP_ANALOG_SIGNAL_ITEM_COUNT,
} app_analog_signal_item_t;

/**
 * @brief 读取结果模式。
 *
 * - `APP_ANALOG_SIGNAL_VALUE_RAW`：返回未经滤波的真实物理量。
 * - `APP_ANALOG_SIGNAL_VALUE_FILTERED`：返回经过当前配置滤波器处理后的物理量。
 */
typedef enum {
    APP_ANALOG_SIGNAL_VALUE_RAW = 0,
    APP_ANALOG_SIGNAL_VALUE_FILTERED = 1,
} app_analog_signal_value_mode_t;

/**
 * @brief 全量模拟量读取结果。
 *
 * 所有字段均为已经完成 ADC 换算后的真实物理量；
 * 是否经过滤波，由 `app_analog_signal_get_measurements()` 调用时传入的
 * `app_analog_signal_value_mode_t` 决定。
 */
typedef struct {
    float v_in_v;
    float i_in_a;
    float i_l_a;
    float v_link_v;
    float i_coil_a;
    float i_lf_a;
} app_analog_signal_measurements_t;

/**
 * @brief 初始化模拟量调理模块。
 *
 * 完成 ADC 初始化、校准、默认标定参数装载，以及每路滤波器初始化。
 */
void app_analog_signal_init(void);

/**
 * @brief 装载默认 ADC 标定参数。
 */
void app_analog_signal_load_default_calibration(void);

/**
 * @brief 更新指定通道的最新原始 ADC 结果。
 *
 * 用于将 PWM/TRGM/PMT 等外部采样链得到的原始值推送进模拟量模块，
 * 供后续物理量换算和滤波读取使用。
 *
 * @param ch  目标 ADC 逻辑通道。
 * @param raw 原始 16-bit ADC 结果。
 */
void app_analog_signal_update_raw(adc_channel_t ch, uint16_t raw);

int app_analog_signal_get_cached_raw(adc_channel_t ch, uint16_t *raw);

/**
 * @brief 读取单个目标模拟量。
 *
 * @param item  目标信号项。
 * @param mode  读取模式：原始值或滤波值。
 * @param value 输出物理量。
 * @return 0 成功，-1 失败。
 */
int app_analog_signal_read_item(app_analog_signal_item_t item, app_analog_signal_value_mode_t mode, float *value);

/**
 * @brief 一次读取全部目标模拟量。
 *
 * @param measurements 输出测量结构体。
 * @param mode         读取模式：原始值或滤波值。
 * @return 0 成功，-1 失败。
 */
int app_analog_signal_get_measurements(app_analog_signal_measurements_t *measurements,
                                       app_analog_signal_value_mode_t mode);

/**
 * @brief 设置指定 ADC 通道的标定参数。
 *
 * @param ch          ADC 通道。
 * @param calibration 标定参数。
 * @return 0 成功，-1 失败。
 */
int app_analog_signal_set_channel_calibration(adc_channel_t ch, const app_adc_calibration_t *calibration);

/**
 * @brief 获取指定 ADC 通道的当前标定参数。
 *
 * @param ch          ADC 通道。
 * @param calibration 输出标定参数。
 * @return 0 成功，-1 失败。
 */
int app_analog_signal_get_channel_calibration(adc_channel_t ch, app_adc_calibration_t *calibration);

#ifdef __cplusplus
}
#endif

#endif /* APP_ANALOG_SIGNAL_H */
