/*
 * Debug RTT - SEGGER RTT wrapper for debug output
 *
 * Copyright (c) 2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef APP_DEBUG_RTT_H
#define APP_DEBUG_RTT_H

#include <stdint.h>

void app_debug_init(void);
void app_debug_write(const char *str);
int app_debug_printf(const char *fmt, ...);

/* Debug test: print HRPWM compare register snapshot */
void app_debug_dump_hrpwm_cmp(void);

#endif /* APP_DEBUG_RTT_H */
