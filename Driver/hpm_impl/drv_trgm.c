/*
 * TRGM Driver - HPM Trigger Mux implementation
 *
 * Copyright (c) 2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include "intf_trgm.h"
#include "hpm_trgm_drv.h"
#include "hpm_trgmmux_src.h"

int intf_trgm_connect(intf_trgm_src_t src, intf_trgm_dst_t dst)
{
    static const uint32_t src_map[] = {
        [INTF_TRGM_SRC_PWM0_CH8REF]  = HPM_TRGM0_INPUT_SRC_PWM0_CH8REF,
        [INTF_TRGM_SRC_PWM0_CH9REF]  = HPM_TRGM0_INPUT_SRC_PWM0_CH9REF,
        [INTF_TRGM_SRC_PWM0_CH10REF] = HPM_TRGM0_INPUT_SRC_PWM0_CH10REF,
        [INTF_TRGM_SRC_PWM0_CH11REF] = HPM_TRGM0_INPUT_SRC_PWM0_CH11REF,
        [INTF_TRGM_SRC_PWM1_CH8REF]  = HPM_TRGM0_INPUT_SRC_PWM1_CH8REF,
        [INTF_TRGM_SRC_PWM1_CH9REF]  = HPM_TRGM0_INPUT_SRC_PWM1_CH9REF,
        [INTF_TRGM_SRC_PWM1_CH10REF] = HPM_TRGM0_INPUT_SRC_PWM1_CH10REF,
        [INTF_TRGM_SRC_PWM1_CH11REF] = HPM_TRGM0_INPUT_SRC_PWM1_CH11REF,
    };

    static const uint32_t dst_map[] = {
        [INTF_TRGM_DST_ADC_PTRGI0A] = HPM_TRGM0_OUTPUT_SRC_ADCX_PTRGI0A,
        [INTF_TRGM_DST_ADC_PTRGI0B] = HPM_TRGM0_OUTPUT_SRC_ADCX_PTRGI0B,
        [INTF_TRGM_DST_ADC_PTRGI0C] = HPM_TRGM0_OUTPUT_SRC_ADCX_PTRGI0C,
        [INTF_TRGM_DST_ADC_PTRGI1A] = HPM_TRGM0_OUTPUT_SRC_ADCX_PTRGI1A,
        [INTF_TRGM_DST_ADC_PTRGI1B] = HPM_TRGM0_OUTPUT_SRC_ADCX_PTRGI1B,
        [INTF_TRGM_DST_ADC_PTRGI1C] = HPM_TRGM0_OUTPUT_SRC_ADCX_PTRGI1C,
    };

    if (src >= sizeof(src_map) / sizeof(src_map[0]) ||
        dst >= sizeof(dst_map) / sizeof(dst_map[0])) return -1;

    trgm_output_t cfg;
    cfg.invert = false;
    cfg.type   = trgm_output_same_as_input;
    cfg.input  = src_map[src];

    trgm_output_config(HPM_TRGM0, dst_map[dst], &cfg);
    return 0;
}
