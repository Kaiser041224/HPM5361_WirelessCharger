/*
 * MCAN Driver Register Declaration
 *
 * Copyright (c) 2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef DRV_MCAN_H
#define DRV_MCAN_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void drv_can_register(void);
uint32_t drv_can_enter_critical(void);
void drv_can_exit_critical(uint32_t irq_state);

#ifdef __cplusplus
}
#endif

#endif /* DRV_MCAN_H */
