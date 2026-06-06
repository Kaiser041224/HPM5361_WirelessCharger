/*
 * HRPWM Driver Macros
 *
 * Copyright (c) 2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef DRV_HRPWM_H
#define DRV_HRPWM_H

#include "hpm_soc.h"

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

#ifdef __cplusplus
extern "C" {
#endif

void hpm_hrpwm_driver_register(void);

#ifdef __cplusplus
}
#endif

#endif /* DRV_HRPWM_H */
