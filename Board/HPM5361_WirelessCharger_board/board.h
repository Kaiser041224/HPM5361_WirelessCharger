/*
 * Copyright (c) 2024 HPMicro
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
 */

#ifndef _HPM_BOARD_H
#define _HPM_BOARD_H

#include "hpm_soc.h"

#define BOARD_NAME          "HPM5361_WirelessCharger_board"
#define BOARD_UF2_SIGNATURE (0x0A4D5048UL)

/* HRPWM pins configured in pinmux.c:
 * PA24 -> PWM0_P_0, PA25 -> PWM0_P_1
 * PA26 -> PWM0_P_2, PA27 -> PWM0_P_3
 * PA28 -> PWM1_P_4, PA29 -> PWM1_P_5
 * PA30 -> PWM1_P_6, PA31 -> PWM1_P_7
 */
#define BOARD_APP_HRPWM0                 HPM_PWM0
#define BOARD_APP_HRPWM1                 HPM_PWM1
#define BOARD_APP_HRPWM_CLOCK_NAME       clock_mot0
#define BOARD_APP_HRPWM_PWM0_PAIR0_OUT   (0U)
#define BOARD_APP_HRPWM_PWM0_PAIR1_OUT   (2U)
#define BOARD_APP_HRPWM_PWM1_PAIR0_OUT   (4U)
#define BOARD_APP_HRPWM_PWM1_PAIR1_OUT   (6U)

#define SEC_CORE_IMG_START ILM_LOCAL_BASE

#ifndef BOARD_RUNNING_CORE
#define BOARD_RUNNING_CORE HPM_CORE0
#endif


#if defined(__cplusplus)
extern "C" {
#endif /* __cplusplus */

void board_init(void);
void board_init_core1(void);

#if defined(__cplusplus)
}
#endif /* __cplusplus */
#endif /* _HPM_BOARD_H */
