/*
 * TRGM Interface - Trigger Mux signal routing
 *
 * Copyright (c) 2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef INTF_TRGM_H
#define INTF_TRGM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    INTF_TRGM_SRC_PWM0_CH8REF = 0,
    INTF_TRGM_SRC_PWM0_CH9REF,
    INTF_TRGM_SRC_PWM0_CH10REF,
    INTF_TRGM_SRC_PWM0_CH11REF,
    INTF_TRGM_SRC_PWM1_CH8REF,
    INTF_TRGM_SRC_PWM1_CH9REF,
    INTF_TRGM_SRC_PWM1_CH10REF,
    INTF_TRGM_SRC_PWM1_CH11REF,
} intf_trgm_src_t;

typedef enum {
    INTF_TRGM_DST_ADC_PTRGI0A = 0,   /* → ADC TRG0A (pmt_trig_ch=0) */
    INTF_TRGM_DST_ADC_PTRGI0B,       /* → ADC TRG0B (pmt_trig_ch=1) */
    INTF_TRGM_DST_ADC_PTRGI0C,       /* → ADC TRG0C (pmt_trig_ch=2) */
    INTF_TRGM_DST_ADC_PTRGI1A,       /* → ADC TRG1A (pmt_trig_ch=3) */
    INTF_TRGM_DST_ADC_PTRGI1B,       /* → ADC TRG1B (pmt_trig_ch=4) */
    INTF_TRGM_DST_ADC_PTRGI1C,       /* → ADC TRG1C (pmt_trig_ch=5) */
} intf_trgm_dst_t;

int intf_trgm_connect(intf_trgm_src_t src, intf_trgm_dst_t dst);

#ifdef __cplusplus
}
#endif
#endif
