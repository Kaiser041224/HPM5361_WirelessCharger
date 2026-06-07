/*
 * Control Common Types
 *
 * Shared types for closed-loop control modules.
 *
 * Copyright (c) 2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef CTRL_TYPES_H
#define CTRL_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 故障码位掩码。
 *
 * 每位对应一种故障，支持多故障同时存在。
 */
typedef enum {
    FAULT_NONE          = 0,
    FAULT_OV_VIN        = (1 << 0),  /**< V_IN 过压 */
    FAULT_UV_VIN        = (1 << 1),  /**< V_IN 欠压 */
    FAULT_OC_IIN        = (1 << 2),  /**< I_IN 过流 */
    FAULT_OC_IL         = (1 << 3),  /**< I_L 过流 */
    FAULT_OV_VLINK      = (1 << 4),  /**< V_LINK 过压 */
    FAULT_UV_VLINK      = (1 << 5),  /**< V_LINK 欠压 */
    FAULT_OC_ICOIL      = (1 << 6),  /**< I_COIL 过流 */
    FAULT_OC_ILF        = (1 << 7),  /**< I_LF 过流 */
    FAULT_OT            = (1 << 8),  /**< 过温 */
    FAULT_ADC           = (1 << 9),  /**< ADC 数据异常 */
    FAULT_PWM           = (1 << 10), /**< PWM 故障 */
    FAULT_CAN_BUSOFF    = (1 << 11), /**< CAN 总线关闭 */
    FAULT_DRVPWR        = (1 << 12), /**< 驱动电源异常 */
    FAULT_HARDWARE      = (1 << 13), /**< 外部硬件故障输入 */
    FAULT_TIMEOUT       = (1 << 14), /**< 通讯超时 */
} fault_code_t;

/**
 * @brief 通用 PID 参数。
 *
 * 被 Buck-Boost、LCC 等控制器共用。
 */
typedef struct {
    float kp;
    float ki;
    float kd;
    float integral_max;
    float output_max;
    float output_min;
} ctrl_pid_params_t;

#ifdef __cplusplus
}
#endif

#endif /* CTRL_TYPES_H */
