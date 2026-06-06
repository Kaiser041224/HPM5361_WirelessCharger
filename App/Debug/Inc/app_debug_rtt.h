/*
 * Debug RTT - SEGGER RTT wrapper for debug output
 *
 * Copyright (c) 2026 HPMicro
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef APP_DEBUG_RTT_H
#define APP_DEBUG_RTT_H

#include <stdint.h>

int app_debug_printf(const char *fmt, ...);

#endif /* APP_DEBUG_RTT_H */
