#ifndef APP_DEBUG_ADC_H
#define APP_DEBUG_ADC_H

/**
 * @brief 打印当前 ADC 各逻辑通道的原始值与换算结果。
 *
 * 输出内容包含原始采样值、ADC 电压、感测电压和物理量换算值，
 * 适合用于静态校准、通道映射确认和实时观测。
 */
void app_debug_adc_dump_channels(void);

/**
 * @brief 执行 ADC 单次采样测试。
 *
 * 该测试会对全部 ADC 逻辑通道进行一次轮询读取，并打印结果。
 * 适用于验证基本采样链路、通道连通性和量程范围是否正常。
 */
void app_debug_adc_run_tests(void);

#endif /* APP_DEBUG_ADC_H */
