/*
 * Control - Fault & Protection
 *
 * 保护阈值管理 + 故障检测 + 锁存 + 清除。
 *
 * Copyright (c) 2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef CTRL_FAULT_H
#define CTRL_FAULT_H

#include "ctrl_types.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * 保护阈值
 * ============================================================================ */

typedef struct {
    float v_in_ov_mv;
    float v_in_uv_mv;
    float i_in_oc_ma;
    float i_l_oc_ma;
    float v_link_ov_mv;
    float v_link_uv_mv;
    float i_coil_oc_ma;
    float i_lf_oc_ma;
    float temp_ot_c;
    uint32_t timeout_ms;
} ctrl_fault_thresholds_t;

/* ============================================================================
 * 公开接口
 * ============================================================================ */

void     ctrl_fault_init(const ctrl_fault_thresholds_t *thresholds);
void     ctrl_fault_set_thresholds(const ctrl_fault_thresholds_t *thresholds);

uint32_t ctrl_fault_check(void);
uint32_t ctrl_fault_get_active(void);

int      ctrl_fault_clear(uint32_t code);
int      ctrl_fault_clear_all(void);
bool     ctrl_fault_is_hardware(void);

void     ctrl_fault_set_callback(void (*cb)(uint32_t active_faults));

#ifdef __cplusplus
}
#endif

#endif /* CTRL_FAULT_H */
